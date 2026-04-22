/* ftp_conn.c - FTP control connection helpers. */

#include "ftp_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int
set_blocking(int fd, bool blocking)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;

    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) < 0)
        return -1;

    return 0;
}

static int
wait_fd(int fd, short events, int timeout_ms)
{
    struct pollfd pfd;
    int rc;

    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;

    for (;;) {
        rc = poll(&pfd, 1, timeout_ms);
        if (rc < 0 && errno == EINTR)
            continue;
        return rc;
    }
}

static int
connect_timeout(int fd, const struct sockaddr *sa, socklen_t len,
    int timeout_ms)
{
    int rc;
    int err;
    socklen_t errlen;

    if (set_blocking(fd, false) < 0)
        return -1;

    rc = connect(fd, sa, len);
    if (rc < 0 && errno != EINPROGRESS)
        return -1;

    if (rc == 0)
        return set_blocking(fd, true);

    rc = wait_fd(fd, POLLOUT, timeout_ms);
    if (rc <= 0) {
        if (rc == 0)
            errno = ETIMEDOUT;
        return -1;
    }

    err = 0;
    errlen = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0)
        return -1;
    if (err != 0) {
        errno = err;
        return -1;
    }

    return set_blocking(fd, true);
}

static int
ctrl_read_more(ftp_conn_t *conn, int timeout_ms)
{
    int rc;

    if (conn->ctrl.len >= sizeof(conn->ctrl.data)) {
        errno = EOVERFLOW;
        return -1;
    }

    rc = wait_fd(conn->fd, POLLIN, timeout_ms);
    if (rc <= 0) {
        if (rc == 0)
            errno = ETIMEDOUT;
        return -1;
    }

    rc = recv(conn->fd, conn->ctrl.data + conn->ctrl.len,
        sizeof(conn->ctrl.data) - conn->ctrl.len, 0);
    if (rc < 0)
        return -1;
    if (rc == 0) {
        errno = ECONNRESET;
        return -1;
    }

    conn->ctrl.len += (size_t)rc;
    return 0;
}

static void
ctrl_consume(ftp_conn_t *conn, size_t n)
{
    if (n == 0)
        return;

    if (n < conn->ctrl.len)
        memmove(conn->ctrl.data, conn->ctrl.data + n, conn->ctrl.len - n);
    conn->ctrl.len -= n;
}

static int
ctrl_write_all(ftp_conn_t *conn, const char *buf, size_t len, int timeout_ms)
{
    size_t done;
    int rc;

    done = 0;
    while (done < len) {
        rc = wait_fd(conn->fd, POLLOUT, timeout_ms);
        if (rc <= 0) {
            if (rc == 0)
                errno = ETIMEDOUT;
            return -1;
        }

        rc = send(conn->fd, buf + done, len - done, 0);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return -1;
        }
        if (rc == 0) {
            errno = EPIPE;
            return -1;
        }

        done += (size_t)rc;
    }

    return 0;
}

void
ftp_conn_init(ftp_conn_t *conn)
{
    conn->fd = -1;
    conn->ctrl.len = 0;
}

void
ftp_conn_close(ftp_conn_t *conn)
{
    if (conn->fd >= 0)
        close(conn->fd);
    conn->fd = -1;
    conn->ctrl.len = 0;
}

int
ftp_conn_connect(ftp_conn_t *conn, const char *host, const char *port,
    int timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *ai;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        errno = EINVAL;
        return -1;
    }

    for (ai = res; ai != NULL; ai = ai->ai_next) {
        conn->fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (conn->fd < 0)
            continue;

        rc = connect_timeout(conn->fd, ai->ai_addr, ai->ai_addrlen,
            timeout_ms);
        if (rc == 0)
            break;

        close(conn->fd);
        conn->fd = -1;
    }

    freeaddrinfo(res);

    if (conn->fd < 0)
        return -1;

    return 0;
}

int
ftp_conn_read_reply(ftp_conn_t *conn, ftp_reply_t *reply, int timeout_ms)
{
    ftp_reply_parser_t parser;
    size_t consumed;
    int rc;

    ftp_reply_parser_init(&parser);

    for (;;) {
        if (conn->ctrl.len > 0) {
            consumed = 0;
            rc = ftp_reply_parser_feed(&parser, conn->ctrl.data,
                conn->ctrl.len, &consumed, reply);
            ctrl_consume(conn, consumed);
            if (rc != 0)
                return (rc > 0) ? 0 : -1;
        }

        rc = ctrl_read_more(conn, timeout_ms);
        if (rc < 0)
            return -1;
    }
}

int
ftp_conn_send_command(ftp_conn_t *conn, const char *verb, slice_t arg,
    int timeout_ms)
{
    char line[FTP_CTRL_BUF_SIZE];
    size_t verb_len;
    size_t pos;
    size_t i;

    verb_len = strlen(verb);
    if (verb_len == 0) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < arg.len; i++) {
        if (arg.ptr[i] == '\r' || arg.ptr[i] == '\n') {
            errno = EINVAL;
            return -1;
        }
    }

    pos = 0;
    if (verb_len + 2 >= sizeof(line)) {
        errno = EMSGSIZE;
        return -1;
    }

    memcpy(line + pos, verb, verb_len);
    pos += verb_len;

    if (arg.len > 0) {
        if (pos + 1 + arg.len + 2 > sizeof(line)) {
            errno = EMSGSIZE;
            return -1;
        }

        line[pos++] = ' ';
        memcpy(line + pos, arg.ptr, arg.len);
        pos += arg.len;
    }

    line[pos++] = '\r';
    line[pos++] = '\n';

    return ctrl_write_all(conn, line, pos, timeout_ms);
}
