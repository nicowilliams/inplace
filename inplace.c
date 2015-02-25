
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
    printf("Usage:\t%1$s [options] FILE [COMMAND [ARGUMENTS]]\n\n"
           "\tThis program runs the COMMAND, if given, with stdin from FILE,\n"
           "\tand saves the output of COMMAND to FILE when the COMMAND\n"
           "\tcompletes.  Any ARGUMENTS are passed to the COMMAND.\n\n"
           "\tIf a COMMAND is not given then the stdin will be saved to FILE.\n\n"
           "\tOptions:\n"
           "\t -h\n"
           "\t --help          -> this message;\n"
           "\t -b SUFFIX,\n"
           "\t --backup SUFFIX -> keep a backup named $FILE$SUFFIX;\n"
           "\t -w,\n"
           "\t --write         -> re-write FILE to keep file identity\n"
           "\t                    the same, do not rename into place.\n\n"
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
        if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;
        if (bytes == -1 && errno != EINTR)
            return 0;
        while (bytes > 0 && (wbytes = write(to_fd, buf, bytes)) != bytes) {
            if (wbytes == -1 && errno != EINTR)
                return 0;
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

static
int
fix_it(const char *dst_fname, const char *src_fname, int rename_into_place)
{
    if (src_fname == NULL)
        return 1;

    if (rename_into_place) {
        if (rename(src_fname, dst_fname) == -1) {
            fprintf(stderr,
                    "%s: Error: could not restore file %s after "
                    "failed stream edit: %s\n", prog, dst_fname, strerror(errno));
            return 0;
        }
    } else {
        int src, dst;

        if ((src = open(src_fname, O_RDONLY)) == -1 ||
            (dst = open(dst_fname, O_WRONLY | O_TRUNC)) == -1 ||
            !copy_file(src, dst)) {
            fprintf(stderr,
                    "%s: Error: could not restore file %s after "
                    "failed stream edit: %s\n", prog, dst_fname, strerror(errno));
            if (src != -1)
                (void) close(src);
            if (dst != -1)
                (void) close(dst);
            return 0;
        }
    }

    return 1;
}

int
main(int argc, char **argv)
{
    const char *what;
    const char *fname;
    const char *bkp = NULL;
    char *tmp_fname = NULL;
    char *bkp_fname = NULL;
    int tmp_fd = -1;
    int bkp_fd = -1;
    int rename_into_place = 1;

    assert(argc > 0);
    prog = argv[0];

    /* This program is too simple for getopt and friends */
    for (argc--, argv++; argc && argv[0][0] == '-'; argc--, argv++) {
        if (argv[0][1] == '-') {
            if (strcmp(argv[0], "--help") == 0)
                usage(prog, 0);
            if (strcmp(argv[0], "--write") == 0) {
                rename_into_place = 0;
            } else if (strcmp(argv[0], "--backup") == 0) {
                bkp = argv[1];
                argc--;
                argv++;
            } else if (strcmp(argv[0], "--") == 0) {
                argc--;
                argv++;
                break;
            } else {
                usage(prog, 1);
            }
        } else {
            char *p;
            for (p = argv[0] + 1; *p != '\0'; p++) {
                if (p[0] == 'h')
                    usage(prog, 0);
                if (p[0] == 'b') {
                    bkp = argv[1];
                    argc--;
                    argv++;
                } else if (p[0] == 'w') {
                    rename_into_place = 0;
                } else if (p[0] == 'h') {
                    usage(prog, 0);
                } else {
                    usage(prog, 1);
                }
            }
        }
    }
    if (argc < 1)
        usage(prog, 0);

    fname = argv[0];
    argc--;
    argv++;

    /* Prep temp file or bkp file */
    what = "asprintf";
    if (asprintf(&tmp_fname, "%s-XXXXXX", fname) == -1)
        goto fail;

    what = "mkstemp";
    tmp_fd = mkstemp(tmp_fname);
    if (tmp_fd == -1)
        goto fail;

    if (bkp != NULL) {
        /* Make backup */
        what = "asprintf";
        if (asprintf(&bkp_fname, "%s%s", fname, bkp) == -1)
            goto fail;

        if (rename_into_place) {
            what = "link";
            (void) unlink(bkp_fname);
            if (link(fname, bkp_fname) == -1) {
                if (errno == EEXIST) {
                    fprintf(stderr, "%s: Error: racing with another %s to "
                            "update %s? %s\n", prog, prog, fname,
                            strerror(errno));
                    exit(2);
                }
                goto fail;
            }
        } else {
            int fd;

            what = "open (bkp file)";
            (void) unlink(bkp_fname);
            bkp_fd = open(bkp_fname, O_CREAT | O_EXCL | O_WRONLY, 0600);
            if (bkp_fd == -1) {
                if (errno == EEXIST) {
                    fprintf(stderr, "%s: Error: racing with another %s to "
                            "update %s? %s\n", prog, prog, fname,
                            strerror(errno));
                    exit(2);
                }
                goto fail;
            }

            what = "open (target file)";
            fd = open(fname, O_RDONLY);
            if (fd == -1)
                goto fail;

            if (!copy_file(fd, bkp_fd) ||
                close(bkp_fd) == -1) {
                (void) close(fd);
                fprintf(stderr, "%s: I/O error creating backup\n", prog);
                exit(2);
            }
            (void) close(fd);
        }
    }

    /* Redirect stdout to go to tmp file */
    what = "dup2";
    if (dup2(tmp_fd, STDOUT_FILENO) == -1)
        goto fail;
    (void) close(tmp_fd);
    tmp_fd = -1;

    if (argc == 0) {
        /* stdin has the input we want; write it to stdout */
        // XXX Better info in copy_file for error handling?
        if (!copy_file(STDIN_FILENO, STDOUT_FILENO)) {
            fprintf(stderr, "%s: Error: I/O error copying stdin\n", prog);
            (void) unlink(tmp_fname);
            (void) fix_it(fname, bkp_fname, rename_into_place);
            exit(2);
        }
    } else {
        int fd, status;

        /* Redirect stdin to be from fname */
        what = "open";
        fd = open(fname, O_RDONLY);
        if (fd == -1)
            goto fail;
        what = "dup2";
        if (dup2(fd, STDIN_FILENO) == -1)
            goto fail;
        (void) close(fd);

        status = run_cmd(&what, argc, argv);

        if (status == -1)
            goto fail;

        if (WIFSIGNALED(status)) {
            (void) unlink(tmp_fname);
            (void) fix_it(fname, bkp_fname, rename_into_place);
            (void) kill(getpid(), WTERMSIG(status)); /* exit with same signal */
            /* NOTREACHED */
            exit(2);
        }
        if (!WIFEXITED(status)) {
            (void) unlink(tmp_fname);
            (void) fix_it(fname, bkp_fname, rename_into_place);
            exit(2);
        }

        status = WEXITSTATUS(status);
        if (status != 0) {
            (void) unlink(tmp_fname);
            (void) fix_it(fname, bkp_fname, rename_into_place);
            exit(status);
        }
    }

    /* Rename or write into place */
    if (!fix_it(fname, tmp_fname, rename_into_place)) {
        (void) fix_it(fname, bkp_fname, rename_into_place);
        exit(2);
    }

    if (!rename_into_place)
        (void) unlink(tmp_fname);

    /* Leave backup file around */

    return 0;

fail:
    perror(what);
    
    if (tmp_fname != NULL) {
        (void) unlink(tmp_fname);
        free(tmp_fname);
    }
}
