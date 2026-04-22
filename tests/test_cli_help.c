/* test_cli_help.c - CLI smoke test for help output. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int
capture_help(char *const argv[], char *buf, size_t buflen)
{
    int out_pipe[2];
    pid_t pid;
    ssize_t n;
    size_t total;
    int status;

    if (pipe(out_pipe) < 0)
        return -1;

    pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0)
            _exit(90);
        if (dup2(out_pipe[1], STDERR_FILENO) < 0)
            _exit(91);
        close(out_pipe[0]);
        close(out_pipe[1]);
        execv("./build/ftp-client", argv);
        _exit(92);
    }

    close(out_pipe[1]);
    total = 0;
    while ((n = read(out_pipe[0], buf + total, buflen - 1 - total)) > 0)
        total += (size_t)n;
    buf[total] = '\0';
    close(out_pipe[0]);

    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;

    return 0;
}

int
main(void)
{
    char out[1024];
    char *const help_argv[] = { "ftp-client", "--help", NULL };
    char *const short_help_argv[] = { "ftp-client", "-h", NULL };

    if (capture_help(help_argv, out, sizeof(out)) < 0) {
        fprintf(stderr, "--help failed\n");
        return 1;
    }
    if (strstr(out, "usage:") == NULL) {
        fprintf(stderr, "help output missing usage:\n%s", out);
        return 1;
    }

    if (capture_help(short_help_argv, out, sizeof(out)) < 0) {
        fprintf(stderr, "-h failed\n");
        return 1;
    }
    if (strstr(out, "usage:") == NULL) {
        fprintf(stderr, "short help output missing usage:\n%s", out);
        return 1;
    }

    return 0;
}
