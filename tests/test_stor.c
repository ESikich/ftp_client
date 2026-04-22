/* test_stor.c - integration test for FTP STOR over PASV. */

#include "ftp_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
    char received[128];
    size_t total;
    ssize_t n;

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
    if (strcmp(line, "STOR upload.txt") != 0)
        _exit(16);
    if (write_all(ctrl_fd, "150 Opening data connection\r\n", 29) < 0)
        _exit(17);

    data_fd = accept(data_listen_fd, NULL, NULL);
    if (data_fd < 0)
        _exit(18);

    total = 0;
    for (;;) {
        n = read(data_fd, received + total, sizeof(received) - 1 - total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            _exit(19);
        }
        if (n == 0)
            break;
        total += (size_t)n;
    }
    received[total] = '\0';
    close(data_fd);
    close(data_listen_fd);

    if (strcmp(received, "hello from stor\n") != 0)
        _exit(20);

    if (write_all(ctrl_fd, "226 Transfer complete\r\n", 23) < 0)
        _exit(21);

    if (read_line(ctrl_fd, line, sizeof(line)) < 0)
        _exit(22);
    if (strcmp(line, "QUIT") != 0)
        _exit(23);
    if (write_all(ctrl_fd, "221 Bye\r\n", 9) < 0)
        _exit(24);

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
    char template[] = "/tmp/ftp_storXXXXXX";
    int tmp_fd;
    const char payload[] = "hello from stor\n";

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

    tmp_fd = mkstemp(template);
    if (tmp_fd < 0) {
        perror("mkstemp");
        close(ctrl_listen_fd);
        return 1;
    }
    unlink(template);
    if (write_all(tmp_fd, payload, sizeof(payload) - 1) < 0) {
        perror("write");
        close(tmp_fd);
        close(ctrl_listen_fd);
        return 1;
    }
    if (lseek(tmp_fd, 0, SEEK_SET) < 0) {
        perror("lseek");
        close(tmp_fd);
        close(ctrl_listen_fd);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        close(tmp_fd);
        close(ctrl_listen_fd);
        return 1;
    }

    if (pid == 0) {
        close(tmp_fd);
        serve_session(ctrl_listen_fd);
    }

    close(ctrl_listen_fd);

    ftp_session_init(&session);
    rc = ftp_session_open(&session, "127.0.0.1", ctrl_port, &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        ftp_session_close(&session);
        close(tmp_fd);
        return 1;
    }

    rc = ftp_session_login(&session, (slice_t){ "demo", 4 },
        (slice_t){ "secret", 6 }, &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "login failed: %s\n", strerror(errno));
        ftp_session_close(&session);
        close(tmp_fd);
        return 1;
    }

    rc = ftp_session_stor(&session, (slice_t){ "upload.txt", 10 },
        tmp_fd, &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "stor failed: %s\n", strerror(errno));
        ftp_session_close(&session);
        close(tmp_fd);
        return 1;
    }

    if (reply.code != 226) {
        fprintf(stderr, "unexpected stor reply: %d\n", reply.code);
        ftp_session_close(&session);
        close(tmp_fd);
        return 1;
    }

    rc = ftp_session_quit(&session, &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "quit failed: %s\n", strerror(errno));
        ftp_session_close(&session);
        close(tmp_fd);
        return 1;
    }

    ftp_session_close(&session);
    close(tmp_fd);

    if (waitpid(pid, &rc, 0) < 0) {
        perror("waitpid");
        return 1;
    }

    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0) {
        fprintf(stderr, "child exited abnormally\n");
        return 1;
    }

    return 0;
}
