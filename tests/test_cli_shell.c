/* test_cli_shell.c - smoke test for the interactive shell mode. */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static int
write_all(int fd, const char *buf, size_t len)
{
    size_t done;
    ssize_t rc;

    done = 0;
    while (done < len) {
        rc = write(fd, buf + done, len - done);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        done += (size_t)rc;
    }

    return 0;
}

static ssize_t
read_line(int fd, char *buf, size_t len)
{
    size_t pos;
    char ch;
    ssize_t rc;

    pos = 0;
    while (pos + 1 < len) {
        rc = read(fd, &ch, 1);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (rc == 0)
            break;

        if (ch == '\n')
            break;
        if (ch == '\r')
            continue;

        buf[pos++] = ch;
    }

    buf[pos] = '\0';
    return (ssize_t)pos;
}

static int
create_listener(uint16_t *port_out)
{
    int fd;
    struct sockaddr_in addr;
    socklen_t len;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        return -1;

    if (listen(fd, 1) < 0)
        return -1;

    len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &len) < 0)
        return -1;

    *port_out = ntohs(addr.sin_port);
    return fd;
}

static void
serve_session(int listen_fd)
{
    int fd;
    char line[128];

    fd = accept(listen_fd, NULL, NULL);
    if (fd < 0)
        _exit(2);

    if (write_all(fd, "220 Mock ready\r\n", 16) < 0)
        _exit(3);

    if (read_line(fd, line, sizeof(line)) < 0)
        _exit(4);
    if (strcmp(line, "USER demo") != 0)
        _exit(5);
    if (write_all(fd, "331 Password required\r\n", 23) < 0)
        _exit(6);

    if (read_line(fd, line, sizeof(line)) < 0)
        _exit(7);
    if (strcmp(line, "PASS secret") != 0)
        _exit(8);
    if (write_all(fd, "230 Logged in\r\n", 15) < 0)
        _exit(9);

    if (read_line(fd, line, sizeof(line)) < 0)
        _exit(10);
    if (strcmp(line, "PWD") != 0)
        _exit(11);
    if (write_all(fd, "257 \"/home/demo\" is current directory\r\n",
            39) < 0)
        _exit(12);

    if (read_line(fd, line, sizeof(line)) < 0)
        _exit(13);
    if (strcmp(line, "CWD pub") != 0)
        _exit(14);
    if (write_all(fd, "250 Directory changed\r\n", 23) < 0)
        _exit(15);

    if (read_line(fd, line, sizeof(line)) < 0)
        _exit(16);
    if (strcmp(line, "QUIT") != 0)
        _exit(17);
    if (write_all(fd, "221 Bye\r\n", 9) < 0)
        _exit(18);

    close(fd);
    _exit(0);
}

static int
capture_shell(const char *input, char *buf, size_t buflen)
{
    int in_pipe[2];
    int out_pipe[2];
    pid_t pid;
    ssize_t n;
    size_t total;
    int status;

    if (pipe(in_pipe) < 0)
        return -1;
    if (pipe(out_pipe) < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        if (dup2(in_pipe[0], STDIN_FILENO) < 0)
            _exit(90);
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0)
            _exit(91);
        if (dup2(out_pipe[1], STDERR_FILENO) < 0)
            _exit(92);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        execl("./build/ftp-client", "ftp-client", (char *)NULL);
        _exit(93);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    if (write_all(in_pipe[1], input, strlen(input)) < 0)
        return -1;
    close(in_pipe[1]);

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
    uint16_t port_num;
    int listen_fd;
    pid_t pid;
    int status;
    char port[16];
    char out[4096];

    listen_fd = create_listener(&port_num);
    if (listen_fd < 0) {
        perror("listener");
        return 1;
    }

    snprintf(port, sizeof(port), "%u", port_num);

    pid = fork();
    if (pid < 0) {
        perror("fork");
        close(listen_fd);
        return 1;
    }

    if (pid == 0) {
        serve_session(listen_fd);
    }

    close(listen_fd);

    {
        char input[512];

        snprintf(input, sizeof(input),
            "open 127.0.0.1 %s\nuser demo\npass secret\npwd\ncwd pub\nquit\n",
            port);

        if (capture_shell(input, out, sizeof(out)) < 0) {
            fprintf(stderr, "shell run failed\n");
            return 1;
        }
    }

    if (strstr(out, "220") == NULL || strstr(out, "230") == NULL ||
        strstr(out, "257") == NULL || strstr(out, "250") == NULL ||
        strstr(out, "221") == NULL) {
        fprintf(stderr, "unexpected shell output:\n%s", out);
        return 1;
    }

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "server exited abnormally\n");
        return 1;
    }

    return 0;
}
