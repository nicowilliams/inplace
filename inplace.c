
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_POSIX_SPAWN
#include <spawn.h>
#endif

static const char *prog = "inplace";

static
void
usage(const char *progname, int code)
{
    printf("Usage:\t%1$s [options] FILE [COMMAND [ARGUMENTS]]\n"
           "\t%1$s -h\n"
           "\t%1$s --help\n\n"
           "\tThis program runs the COMMAND, if given, with stdin from FILE,\n"
           "\tand saves the output of COMMAND to FILE when the COMMAND\n"
           "\tcompletes.  Any ARGUMENTS are passed to the COMMAND.\n"
           "\tIf a COMMAND is not given then the stdin will be saved to FILE\n"
           "\twhen end-of-file (EOF) is reached on stdin.\n\n"
           "\tOptions:\n"
           "\t -bEXT, -b EXT, --backup EXT -> keep a backup named FILE.EXT\n"
           "\t -w, --write -> re-write FILE to keep file identity the same,\n"
           "\t                do not rename into place\n\n"
           "\tBy default %1$s renames the new FILE into place; use the\n"
           "\t-w option to have %1$s rewrite the FILE..\n", progname);
    exit(code);
}

static
int
copy_file(int from_fd, int to_fd)
{
    char buf[4096];
    ssize_t bytes, wbytes;

    while ((bytes = read(from_fd, buf, sizeof(buf))) != 0) {
        if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fprintf(stderr,
                    "%s: Error: non-blocking stdin not supported\n", prog);
            return 0;
        }
        if (bytes == -1 && errno != EINTR) {
            fprintf(stderr,"%s: Error: %s while reading stdin\n", prog,
                    strerror(errno));
            return 0;
        }
        while (bytes > 0 && (wbytes = write(to_fd, buf, bytes)) != bytes) {
            if (wbytes == -1 && errno != EINTR) {
                fprintf(stderr,"%s: Error: %s while writing\n", prog,
                        strerror(errno));
                return 0;
            }
            bytes -= wbytes;
        }
    }
    return 1;
}

static
int
run_cmd(const char **what, int argc, char **argv)
{
    /* XXX Add int pip_fds[2] for use in detecting pre-exec child failure */
    pid_t pid, tmp_fd;
    int status;
    /* XXX Call pipe()/pipe2() and write side of pipe_fds[] to be O_CLOEXEC */

#ifdef HAVE_POSIX_SPAWN
    if (argv[0][0] == '/') {
        *what = "posix_spawn";
        errno = posix_spawn(&pid, argv[0], &actions, NULL, &argv[0], NULL);
        if (errno != 0)
            return -1;
    } else {
        *what = "posix_spawnp";
        errno = posix_spawnp(&pid, argv[0], &actions, NULL, &argv[0], NULL);
        if (errno != 0)
            return -1;
    }
#else /* HAVE_POSIX_SPAWN */
    *what = "fork";
    pid = fork();
    if (pid == -1)
        return -1;
    if (pid == 0) {
        /* child */
        if (argv[0][1] == '/') {
            (void) execv(argv[0], &argv[0]);
            perror("execv");
        } else {
            (void) execvp(argv[0], &argv[0]);
            perror("execvp");
        }
        /* XXX Write to pipe */
        exit(125);
    }
#endif /* HAVE_POSIX_SPAWN */

    /* parent */
    while (wait(&status) != pid)
        ;

    return status;
}

int
main(int argc, char **argv)
{
    const char *what;
    const char *fname;
    const char *bkp = NULL;
    char *tmp_fname = NULL;
    int fd = -1;
    int tmp_fd = -1;
    int rename_into_place = 1;

    assert(argc > 0);
    prog = argv[0];

    /* No getopt; too simple */
    for (argc--, argv++; argc && argv[0][0] == '-'; argc--, argv++) {
        if (strcmp(argv[0], "--") == 0) {
            argc--;
            argv++;
            break;
        }
        if (strncmp(argv[0], "-b", sizeof("-b") - 1) == 0) {
            bkp = argv[0] + sizeof("-b") - 1;
            if (bkp[0] == '\0') {
                if (argv[1] == NULL)
                    usage(prog, 1);
                bkp = argv[1];
                argc--;
                argv++;
            }
            continue;
        }
        if (strcmp(argv[0], "-w") == 0 || strcmp(argv[0], "--write") == 0)
            rename_into_place = 0;
        if (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0)
            usage(prog, 0);
    }

    if (argc < 1)
        usage(prog, 0);

    fname = argv[0];
    if (bkp == NULL) {
        what = "asprintf";
        if (asprintf(&tmp_fname, "%s-XXXXXX", fname) == -1)
            goto fail;

        what = "mkstemp";
        tmp_fd = mkstemp(tmp_fname);
    } else {
        what = "asprintf";
        if (asprintf(&tmp_fname, "%s.%s", fname, bkp) == -1)
            goto fail;

        what = "open (bkp file)";
        tmp_fd = open(tmp_fname, O_CREAT | O_EXCL | O_WRONLY, 0600);
    }

    if (tmp_fd == -1)
        goto fail;
    what = "dup2";
    if (dup2(tmp_fd, STDOUT_FILENO) == -1)
        goto fail;
    (void) close(tmp_fd);
    tmp_fd = -1;

    /* At this point stdout is set to write to tmp_fname */

    argc--;
    argv++;

    if (argc == 0) {
        /* stdin has the input we want; write it to stdout */
        if (!copy_file(STDIN_FILENO, STDOUT_FILENO)) {
            (void) unlink(tmp_fname);
            exit(2);
        }
    } else {
        int status;

        /* Redirect stdin to be from fname */
        what = "open";
        fd = open(fname, O_RDONLY);
        if (fd == -1)
            goto fail;

        what = "dup2";
        if (dup2(fd, STDIN_FILENO) == -1)
            goto fail;
        (void) close(fd);
        fd = -1;

        status = run_cmd(&what, argc, argv);

        if (status == -1)
            goto fail;

        if (WIFSIGNALED(status)) {
            (void) unlink(tmp_fname);
            (void) kill(getpid(), WTERMSIG(status)); /* exit with same signal */
            /* NOTREACHED */
            exit(2);
        }
        if (!WIFEXITED(status)) {
            (void) unlink(tmp_fname);
            exit(2);
        }

        status = WEXITSTATUS(status);
        if (status != 0) {
            (void) unlink(tmp_fname);
            exit(status);
        }
    }

    if (rename_into_place) {
        what = "rename";
        if (rename(tmp_fname, fname) == -1)
            goto fail;
        return 0;
    }

    what = "open (file)";
    fd = open(fname, O_TRUNC | O_WRONLY, 0600);
    if (fd == -1)
        goto fail;
    tmp_fd = open(tmp_fname, O_RDONLY);
    what = "open (bkp file)";
    if (tmp_fd == -1)
        goto fail;
    if (!copy_file(tmp_fd, fd)) {
        fprintf(stderr, "%s: Error: Re-write failed, %s is truncated!\n",
                prog, fname);
        exit(2);
    }

    return 0;

fail:
    perror(what);
    
    if (tmp_fname != NULL) {
        (void) unlink(tmp_fname);
        free(tmp_fname);
    }
}
