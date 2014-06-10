
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

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

static
void
usage(const char *progname, int code)
{
    printf("Usage:\t%1$s file command [args]\n"
           "\t%1$s -h\n"
           "\t%1$s --help\n", progname);
    exit(code);
}

int
main(int argc, char **argv)
{
    const char *what;
    const char *fname;
    char *tmp_fname = NULL;
    /* XXX Add int pip_fds[2] for use in detecting pre-exec child failure */
    int fd = -1;
    int tmp_fd = -1;
    pid_t pid;
    int status;
#ifdef HAVE_POSIX_SPAWN
    posix_spawn_file_actions_t actions;
#endif

    if (argc == 1)
        usage(argv[0], 0);
    if (argc < 3)
        usage(argv[0], 1);
    if (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0)
        usage(argv[0], 0);

    argc--;
    argv++;
    fname = argv[0];
    what = "asprintf";
    if (asprintf(&tmp_fname, "%s-XXXXXX", fname) == -1)
        goto fail;

    what = "mkstemp";
    tmp_fd = mkstemp(tmp_fname);
    if (tmp_fd == -1)
        goto fail;

    what = "open";
    fd = open(fname, O_RDONLY);
    if (fd == -1)
        goto fail;

    what = "dup2";
    if (dup2(fd, STDIN_FILENO) == -1)
        goto fail;

    argc--;
    argv++;

    /* XXX Call pipe()/pipe2() and write side of pipe_fds[] to be O_CLOEXEC */

#ifdef HAVE_POSIX_SPAWN
    what = "posix_spawn_file_actions_init";
    errno = posix_spawn_file_actions_init(&actions);
    if (errno != 0)
        goto fail;
    what = "posix_spawn_file_actions_adddup2";
    errno = posix_spawn_file_actions_adddup2(&actions, fd, STDIN_FILENO);
    if (errno != 0)
        goto fail;
    errno = posix_spawn_file_actions_adddup2(&actions, tmp_fd, STDOUT_FILENO);
    if (errno != 0)
        goto fail;
    if (argv[0][0] == '/') {
        what = "posix_spawn";
        errno = posix_spawn(&pid, argv[0], &actions, NULL, &argv[0], NULL);
        if (errno != 0)
            goto fail;
    } else {
        what = "posix_spawnp";
        errno = posix_spawnp(&pid, argv[0], &actions, NULL, &argv[0], NULL);
        if (errno != 0)
            goto fail;
    }
#else /* HAVE_POSIX_SPAWN */
    what = "fork";
    pid = fork();
    if (pid == -1)
        goto fail;
    if (pid == 0) {
        /* child */
        if (dup2(tmp_fd, STDOUT_FILENO) == -1) {
            perror("dup2");
            /* XXX Write to pipe */
            exit(125);
        }
        (void) close(tmp_fd);
        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2");
            /* XXX Write to pipe */
            exit(125);
        }
        (void) close(fd);
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

    if (WIFSIGNALED(status)) {
        (void) kill(getpid(), WTERMSIG(status));
        (void) unlink(tmp_fname);
        exit(2);
    }
    if (!WIFEXITED(status)) {
        (void) unlink(tmp_fname);
        exit(2);
    }

    status = WEXITSTATUS(status);
    if (status == 0)
        rename(tmp_fname, fname);
    exit(status);

fail:
    perror(what);
    
    if (tmp_fname != NULL) {
        (void) unlink(tmp_fname);
        free(tmp_fname);
    }
}
