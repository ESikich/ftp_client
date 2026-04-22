/* test_retr.c - integration test for FTP RETR over PASV. */

#include "ftp_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
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
create_listener(void)
{
    int fd;
    struct sockaddr_in addr;

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
    return fd;
}

static void
serve_session(int ctrl_listen_fd)
{
    int ctrl_fd;
    int data_listen_fd;
    int data_fd;
    char line[128];
    char pasv_reply[128];

    ctrl_fd = accept(ctrl_listen_fd, NULL, NULL);
    if (ctrl_fd < 0)
        _exit(2);

    if (write_all(ctrl_fd, "220 Mock ready\r\n", 16) < 0)
        _exit(3);

    if (read_line(ctrl_fd, line, sizeof(line)) < 0)
        _exit(4);
    if (strcmp(line, "USER demo") != 0)
        _exit(5);
    if (write_all(ctrl_fd, "331 Password required\r\n", 23) < 0)
        _exit(6);

    if (read_line(ctrl_fd, line, sizeof(line)) < 0)
        _exit(7);
    if (strcmp(line, "PASS secret") != 0)
        _exit(8);
    if (write_all(ctrl_fd, "230 Logged in\r\n", 15) < 0)
        _exit(9);

    if (read_line(ctrl_fd, line, sizeof(line)) < 0)
        _exit(10);
    if (strcmp(line, "PASV") != 0)
        _exit(11);

    data_listen_fd = create_listener();
    if (data_listen_fd < 0)
        _exit(12);

    {
        struct sockaddr_in addr;
        socklen_t len;
        uint16_t port_num;
        uint8_t p1;
        uint8_t p2;

        len = sizeof(addr);
        if (getsockname(data_listen_fd, (struct sockaddr *)&addr, &len) < 0)
            _exit(13);
        port_num = ntohs(addr.sin_port);
        p1 = (uint8_t)(port_num / 256);
        p2 = (uint8_t)(port_num % 256);
        snprintf(pasv_reply, sizeof(pasv_reply),
            "227 Entering Passive Mode (127,0,0,1,%u,%u)\r\n",
            p1, p2);
    }

    if (write_all(ctrl_fd, pasv_reply, strlen(pasv_reply)) < 0)
        _exit(14);

    if (read_line(ctrl_fd, line, sizeof(line)) < 0)
        _exit(15);
    if (strcmp(line, "RETR readme.txt") != 0)
        _exit(16);
    if (write_all(ctrl_fd, "150 Opening data connection\r\n", 29) < 0)
        _exit(17);

    data_fd = accept(data_listen_fd, NULL, NULL);
    if (data_fd < 0)
        _exit(18);
    if (write_all(data_fd, "hello from retr\n", 16) < 0)
        _exit(19);
    close(data_fd);
    close(data_listen_fd);

    if (write_all(ctrl_fd, "226 Transfer complete\r\n", 23) < 0)
        _exit(20);

    if (read_line(ctrl_fd, line, sizeof(line)) < 0)
        _exit(21);
    if (strcmp(line, "QUIT") != 0)
        _exit(22);
    if (write_all(ctrl_fd, "221 Bye\r\n", 9) < 0)
        _exit(23);

    close(ctrl_fd);
    _exit(0);
}

int
main(void)
{
    ftp_session_t session;
    ftp_reply_t reply;
    char ctrl_port[16];
    uint16_t ctrl_port_num;
    int ctrl_listen_fd;
    pid_t pid;
    int rc;
    char buf[128];
    int out_pipe[2];
    ssize_t n;
    size_t total;

    ctrl_listen_fd = create_listener();
    if (ctrl_listen_fd < 0) {
        perror("listener");
        return 1;
    }

    {
        struct sockaddr_in addr;
        socklen_t len;

        len = sizeof(addr);
        if (getsockname(ctrl_listen_fd, (struct sockaddr *)&addr, &len) < 0) {
            perror("getsockname");
            close(ctrl_listen_fd);
            return 1;
        }
        ctrl_port_num = ntohs(addr.sin_port);
    }

    snprintf(ctrl_port, sizeof(ctrl_port), "%u", ctrl_port_num);

    if (pipe(out_pipe) < 0) {
        perror("pipe");
        close(ctrl_listen_fd);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        close(ctrl_listen_fd);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return 1;
    }

    if (pid == 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        serve_session(ctrl_listen_fd);
    }

    close(ctrl_listen_fd);

    ftp_session_init(&session);
    rc = ftp_session_open(&session, "127.0.0.1", ctrl_port, &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        ftp_session_close(&session);
        return 1;
    }

    rc = ftp_session_login(&session, (slice_t){ "demo", 4 },
        (slice_t){ "secret", 6 }, &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "login failed: %s\n", strerror(errno));
        ftp_session_close(&session);
        return 1;
    }

    rc = ftp_session_retr(&session, (slice_t){ "readme.txt", 10 },
        out_pipe[1], &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "retr failed: %s\n", strerror(errno));
        ftp_session_close(&session);
        return 1;
    }

    close(out_pipe[1]);

    total = 0;
    while ((n = read(out_pipe[0], buf + total, sizeof(buf) - 1 - total)) > 0)
        total += (size_t)n;
    if (n < 0) {
        perror("read");
        ftp_session_close(&session);
        close(out_pipe[0]);
        return 1;
    }
    buf[total] = '\0';
    close(out_pipe[0]);

    if (strcmp(buf, "hello from retr\n") != 0) {
        fprintf(stderr, "unexpected retr data: %s\n", buf);
        ftp_session_close(&session);
        return 1;
    }

    rc = ftp_session_quit(&session, &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "quit failed: %s\n", strerror(errno));
        ftp_session_close(&session);
        return 1;
    }

    ftp_session_close(&session);

    if (waitpid(pid, &rc, 0) < 0) {
        perror("waitpid");
        return 1;
    }

    return 0;
}
