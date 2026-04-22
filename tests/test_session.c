/* test_session.c - local integration tests for FTP command sequencing. */

#include "ftp_client.h"

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
    if (strcmp(line, "TYPE I") != 0)
        _exit(11);
    if (write_all(fd, "200 Type set to I\r\n", 19) < 0)
        _exit(12);

    if (read_line(fd, line, sizeof(line)) < 0)
        _exit(13);
    if (strcmp(line, "PWD") != 0)
        _exit(14);
    if (write_all(fd, "257 \"/home/demo\"\r\n", 18) < 0)
        _exit(15);

    if (read_line(fd, line, sizeof(line)) < 0)
        _exit(16);
    if (strcmp(line, "CWD /tmp") != 0)
        _exit(17);
    if (write_all(fd, "250 Directory changed\r\n", 23) < 0)
        _exit(18);

    if (read_line(fd, line, sizeof(line)) < 0)
        _exit(19);
    if (strcmp(line, "QUIT") != 0)
        _exit(20);
    if (write_all(fd, "221 Bye\r\n", 9) < 0)
        _exit(21);

    close(fd);
    _exit(0);
}

int
main(void)
{
    ftp_session_t session;
    ftp_reply_t reply;
    char port[16];
    uint16_t port_num;
    int listen_fd;
    pid_t pid;
    int rc;

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

    if (pid == 0)
        serve_session(listen_fd);

    close(listen_fd);

    ftp_session_init(&session);
    rc = ftp_session_open(&session, "127.0.0.1", port, &reply, 5000);
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

    rc = ftp_session_type(&session, (slice_t){ "I", 1 }, &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "type failed: %s\n", strerror(errno));
        ftp_session_close(&session);
        return 1;
    }

    rc = ftp_session_pwd(&session, &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "pwd failed: %s\n", strerror(errno));
        ftp_session_close(&session);
        return 1;
    }

    rc = ftp_session_cwd(&session, (slice_t){ "/tmp", 4 }, &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "cwd failed: %s\n", strerror(errno));
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
