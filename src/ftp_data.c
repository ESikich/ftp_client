/* ftp_data.c - FTP passive data connection helpers. */

#include "ftp_client.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
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
write_all_fd(int fd, const char *buf, size_t len)
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
        if (rc == 0) {
            errno = EPIPE;
            return -1;
        }

        done += (size_t)rc;
    }

    return 0;
}

static int
send_all_fd(int fd, const char *buf, size_t len)
{
    size_t done;
    ssize_t rc;

    done = 0;
    while (done < len) {
        rc = send(fd, buf + done, len - done, MSG_NOSIGNAL);
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
ftp_data_conn_init(ftp_data_conn_t *conn)
{
    conn->fd = -1;
}

void
ftp_data_conn_close(ftp_data_conn_t *conn)
{
    if (conn->fd >= 0)
        close(conn->fd);
    conn->fd = -1;
}

int
ftp_data_conn_connect(ftp_data_conn_t *conn, const char *host,
    const char *port, int timeout_ms)
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
ftp_data_conn_stream_to_fd(ftp_data_conn_t *conn, int out_fd, int timeout_ms)
{
    char buf[FTP_CTRL_BUF_SIZE];
    int rc;
    ssize_t n;

    for (;;) {
        rc = wait_fd(conn->fd, POLLIN, timeout_ms);
        if (rc <= 0) {
            if (rc == 0)
                errno = ETIMEDOUT;
            return -1;
        }

        n = recv(conn->fd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return 0;

        if (write_all_fd(out_fd, buf, (size_t)n) < 0)
            return -1;
    }
}

int
ftp_data_conn_stream_from_fd(ftp_data_conn_t *conn, int in_fd, int timeout_ms)
{
    char buf[FTP_CTRL_BUF_SIZE];
    ssize_t n;

    /* v1 uses blocking I/O on the caller-provided fd. */
    (void)timeout_ms;

    for (;;) {
        n = read(in_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;

        if (send_all_fd(conn->fd, buf, (size_t)n) < 0)
            return -1;
    }

    shutdown(conn->fd, SHUT_WR);
    return 0;
}
