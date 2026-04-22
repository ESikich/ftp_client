/* test_greeting.c - local integration test for FTP greeting handling. */

#include "ftp_client.h"

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
serve_once(int listen_fd)
{
    int fd;
    const char *greeting;
    char buf[128];

    fd = accept(listen_fd, NULL, NULL);
    if (fd < 0)
        _exit(2);

    greeting = "220 Mock FTP ready\r\n";
    if (write(fd, greeting, strlen(greeting)) < 0)
        _exit(3);

    if (read(fd, buf, sizeof(buf)) < 0)
        _exit(4);
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

    if (pid == 0) {
        serve_once(listen_fd);
    }

    close(listen_fd);

    ftp_session_init(&session);
    rc = ftp_session_open(&session, "127.0.0.1", port, &reply, 5000);
    if (rc < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        ftp_session_close(&session);
        return 1;
    }

    if (reply.code != 220) {
        fprintf(stderr, "unexpected reply code: %d\n", reply.code);
        ftp_session_close(&session);
        return 1;
    }

    if (strcmp(reply.text, "Mock FTP ready") != 0) {
        fprintf(stderr, "unexpected reply text: %s\n", reply.text);
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
