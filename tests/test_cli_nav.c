/* test_cli_nav.c - CLI smoke test for PWD and CWD. */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
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

    if (listen(fd, 2) < 0)
        return -1;

    len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &len) < 0)
        return -1;

    *port_out = ntohs(addr.sin_port);
    return fd;
}

static void
serve_nav_session(int listen_fd, const char *expected_cmd, const char *reply)
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
    if (strcmp(line, expected_cmd) != 0)
        _exit(11);
    if (write_all(fd, reply, strlen(reply)) < 0)
        _exit(12);

    close(fd);
}

static int
capture_client(char *const argv[], char *buf, size_t buflen)
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
    int listen_fd;
    uint16_t port_num;
    char port[16];
    pid_t pid;
    int status;
    char out[1024];
    char *const pwd_argv[] = {
        "ftp-client",
        "127.0.0.1",
        port,
        "demo",
        "secret",
        "pwd",
        NULL
    };
    char *const cwd_argv[] = {
        "ftp-client",
        "127.0.0.1",
        port,
        "demo",
        "secret",
        "cwd",
        "pub",
        NULL
    };

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
        serve_nav_session(listen_fd, "PWD", "257 \"/home/demo\" is current directory\r\n");
        serve_nav_session(listen_fd, "CWD pub", "250 Directory changed\r\n");
        close(listen_fd);
        _exit(0);
    }

    close(listen_fd);

    if (capture_client(pwd_argv, out, sizeof(out)) < 0) {
        fprintf(stderr, "pwd cli failed\n");
        return 1;
    }
    if (strstr(out, "257") == NULL || strstr(out, "/home/demo") == NULL) {
        fprintf(stderr, "unexpected pwd cli output:\n%s", out);
        return 1;
    }

    if (capture_client(cwd_argv, out, sizeof(out)) < 0) {
        fprintf(stderr, "cwd cli failed\n");
        return 1;
    }
    if (strstr(out, "250") == NULL || strstr(out, "Directory changed") == NULL) {
        fprintf(stderr, "unexpected cwd cli output:\n%s", out);
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
