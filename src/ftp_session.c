/* ftp_session.c - FTP session sequencing and login helpers. */

#include "ftp_client.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int
reply_class(const ftp_reply_t *reply)
{
    if (reply->code < 100 || reply->code > 599)
        return 0;

    return reply->code / 100;
}

static int
expect_reply_class(const ftp_reply_t *reply, int expect_class)
{
    if (reply_class(reply) != expect_class) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

static void
set_auth_errno(const ftp_reply_t *reply)
{
    switch (reply_class(reply)) {
    case 4:
        errno = EAGAIN;
        break;
    case 5:
        errno = EACCES;
        break;
    default:
        errno = EPROTO;
        break;
    }
}

static int
read_preliminary_reply(ftp_conn_t *conn, ftp_reply_t *reply, int timeout_ms)
{
    int rc;

    rc = ftp_conn_read_reply(conn, reply, timeout_ms);
    if (rc < 0)
        return -1;

    switch (reply_class(reply)) {
    case 1:
        return 0;
    case 4:
        errno = EAGAIN;
        return -1;
    case 5:
        errno = EACCES;
        return -1;
    default:
        errno = EPROTO;
        return -1;
    }
}

static int
read_transfer_reply(ftp_conn_t *conn, ftp_reply_t *reply, int timeout_ms)
{
    int rc;
    int class;

    for (;;) {
        rc = ftp_conn_read_reply(conn, reply, timeout_ms);
        if (rc < 0)
            return -1;

        class = reply_class(reply);
        if (class == 1)
            continue;
        if (class == 2 || class == 4 || class == 5)
            return 0;

        errno = EPROTO;
        return -1;
    }
}

int
ftp_session_command(ftp_session_t *session, const char *verb, slice_t arg,
    int expect_class, ftp_reply_t *reply, int timeout_ms)
{
    int rc;

    rc = ftp_conn_send_command(&session->conn, verb, arg, timeout_ms);
    if (rc < 0)
        return -1;

    rc = ftp_conn_read_reply(&session->conn, reply, timeout_ms);
    if (rc < 0)
        return -1;

    rc = expect_reply_class(reply, expect_class);
    if (rc < 0)
        return -1;

    return 0;
}

static int
parse_pasv_reply(const ftp_reply_t *reply, char *host, size_t host_len,
    char *port, size_t port_len)
{
    const char *p;
    unsigned long parts[6];
    int count;
    char *end;
    int n;

    p = strchr(reply->text, '(');
    if (p == NULL) {
        errno = EPROTO;
        return -1;
    }

    p++;
    for (count = 0; count < 6; count++) {
        while (*p == ' ' || *p == '\t')
            p++;

        if (!isdigit((unsigned char)*p)) {
            errno = EPROTO;
            return -1;
        }

        errno = 0;
        parts[count] = strtoul(p, &end, 10);
        if (errno != 0 || end == p || parts[count] > 255) {
            errno = EPROTO;
            return -1;
        }

        p = end;
        if (count < 5) {
            if (*p != ',') {
                errno = EPROTO;
                return -1;
            }
            p++;
        }
    }

    n = snprintf(host, host_len, "%lu.%lu.%lu.%lu",
        parts[0], parts[1], parts[2], parts[3]);
    if (n < 0 || (size_t)n >= host_len) {
        errno = EINVAL;
        return -1;
    }

    n = snprintf(port, port_len, "%lu",
        parts[4] * 256UL + parts[5]);
    if (n < 0 || (size_t)n >= port_len) {
        errno = EINVAL;
        return -1;
    }

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;

    if (*p != ')') {
        errno = EPROTO;
        return -1;
    }

    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;

    if (*p != '\0') {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

void
ftp_session_init(ftp_session_t *session)
{
    ftp_conn_init(&session->conn);
    session->logged_in = false;
}

void
ftp_session_close(ftp_session_t *session)
{
    ftp_conn_close(&session->conn);
    session->logged_in = false;
}

int
ftp_session_open(ftp_session_t *session, const char *host, const char *port,
    ftp_reply_t *greeting, int timeout_ms)
{
    int rc;

    rc = ftp_conn_connect(&session->conn, host, port, timeout_ms);
    if (rc < 0)
        return -1;

    rc = ftp_conn_read_reply(&session->conn, greeting, timeout_ms);
    if (rc < 0) {
        ftp_session_close(session);
        return -1;
    }

    if (reply_class(greeting) != 2) {
        errno = EPROTO;
        ftp_session_close(session);
        return -1;
    }

    return 0;
}

int
ftp_session_login(ftp_session_t *session, slice_t user, slice_t pass,
    ftp_reply_t *reply, int timeout_ms)
{
    int rc;

    rc = ftp_conn_send_command(&session->conn, "USER", user, timeout_ms);
    if (rc < 0)
        return -1;

    rc = ftp_conn_read_reply(&session->conn, reply, timeout_ms);
    if (rc < 0)
        return -1;

    switch (reply_class(reply)) {
    case 2:
        session->logged_in = true;
        return 0;
    case 3:
        break;
    default:
        set_auth_errno(reply);
        return -1;
    }

    rc = ftp_conn_send_command(&session->conn, "PASS", pass, timeout_ms);
    if (rc < 0)
        return -1;

    rc = ftp_conn_read_reply(&session->conn, reply, timeout_ms);
    if (rc < 0)
        return -1;

    if (reply_class(reply) != 2) {
        set_auth_errno(reply);
        return -1;
    }

    session->logged_in = true;
    return 0;
}

int
ftp_session_type(ftp_session_t *session, slice_t mode, ftp_reply_t *reply,
    int timeout_ms)
{
    return ftp_session_command(session, "TYPE", mode, 2, reply,
        timeout_ms);
}

int
ftp_session_pwd(ftp_session_t *session, ftp_reply_t *reply, int timeout_ms)
{
    return ftp_session_command(session, "PWD", (slice_t){0}, 2, reply,
        timeout_ms);
}

int
ftp_session_cwd(ftp_session_t *session, slice_t path, ftp_reply_t *reply,
    int timeout_ms)
{
    return ftp_session_command(session, "CWD", path, 2, reply, timeout_ms);
}

int
ftp_session_mkd(ftp_session_t *session, slice_t path, ftp_reply_t *reply,
    int timeout_ms)
{
    int rc;

    rc = ftp_conn_send_command(&session->conn, "MKD", path, timeout_ms);
    if (rc < 0)
        return -1;

    rc = ftp_conn_read_reply(&session->conn, reply, timeout_ms);
    if (rc < 0)
        return -1;

    switch (reply_class(reply)) {
    case 2:
        return 0;
    case 4:
        errno = EAGAIN;
        return -1;
    case 5:
        errno = EACCES;
        return -1;
    default:
        errno = EPROTO;
        return -1;
    }
}

int
ftp_session_pasv(ftp_session_t *session, ftp_data_conn_t *data,
    ftp_reply_t *reply, int timeout_ms)
{
    char host[32];
    char port[8];
    int rc;

    rc = ftp_session_command(session, "PASV", (slice_t){0}, 2, reply,
        timeout_ms);
    if (rc < 0)
        return -1;

    if (reply->code != 227) {
        errno = EPROTO;
        return -1;
    }

    rc = parse_pasv_reply(reply, host, sizeof(host), port, sizeof(port));
    if (rc < 0)
        return -1;

    ftp_data_conn_close(data);
    rc = ftp_data_conn_connect(data, host, port, timeout_ms);
    if (rc < 0)
        return -1;

    return 0;
}

static int
transfer_listing(ftp_session_t *session, const char *verb, slice_t path,
    int out_fd, ftp_reply_t *reply, int timeout_ms)
{
    ftp_data_conn_t data;
    int rc;

    ftp_data_conn_init(&data);

    rc = ftp_session_pasv(session, &data, reply, timeout_ms);
    if (rc < 0)
        return -1;

    rc = ftp_conn_send_command(&session->conn, verb, path, timeout_ms);
    if (rc < 0) {
        ftp_data_conn_close(&data);
        return -1;
    }

    rc = read_preliminary_reply(&session->conn, reply, timeout_ms);
    if (rc < 0) {
        ftp_data_conn_close(&data);
        return -1;
    }

    rc = ftp_data_conn_stream_to_fd(&data, out_fd, timeout_ms);
    ftp_data_conn_close(&data);
    if (rc < 0) {
        int saved_errno;

        saved_errno = errno;
        (void)read_transfer_reply(&session->conn, reply, timeout_ms);
        errno = saved_errno;
        return -1;
    }

    rc = read_transfer_reply(&session->conn, reply, timeout_ms);
    if (rc < 0)
        return -1;

    switch (reply_class(reply)) {
    case 2:
        return 0;
    case 4:
        errno = EAGAIN;
        return -1;
    case 5:
        errno = EACCES;
        return -1;
    default:
        errno = EPROTO;
        return -1;
    }

}

static int
transfer_file(ftp_session_t *session, const char *verb, slice_t path,
    int out_fd, ftp_reply_t *reply, int timeout_ms)
{
    ftp_data_conn_t data;
    int rc;

    ftp_data_conn_init(&data);

    rc = ftp_session_pasv(session, &data, reply, timeout_ms);
    if (rc < 0)
        return -1;

    rc = ftp_conn_send_command(&session->conn, verb, path, timeout_ms);
    if (rc < 0) {
        ftp_data_conn_close(&data);
        return -1;
    }

    rc = read_preliminary_reply(&session->conn, reply, timeout_ms);
    if (rc < 0) {
        ftp_data_conn_close(&data);
        return -1;
    }

    rc = ftp_data_conn_stream_to_fd(&data, out_fd, timeout_ms);
    ftp_data_conn_close(&data);
    if (rc < 0) {
        int saved_errno;

        saved_errno = errno;
        (void)read_transfer_reply(&session->conn, reply, timeout_ms);
        errno = saved_errno;
        return -1;
    }

    rc = read_transfer_reply(&session->conn, reply, timeout_ms);
    if (rc < 0)
        return -1;

    switch (reply_class(reply)) {
    case 2:
        return 0;
    case 4:
        errno = EAGAIN;
        return -1;
    case 5:
        errno = EACCES;
        return -1;
    default:
        errno = EPROTO;
        return -1;
    }

}

int
ftp_session_list(ftp_session_t *session, slice_t path, int out_fd,
    ftp_reply_t *reply, int timeout_ms)
{
    return transfer_listing(session, "LIST", path, out_fd, reply,
        timeout_ms);
}

int
ftp_session_nlst(ftp_session_t *session, slice_t path, int out_fd,
    ftp_reply_t *reply, int timeout_ms)
{
    return transfer_listing(session, "NLST", path, out_fd, reply,
        timeout_ms);
}

int
ftp_session_retr(ftp_session_t *session, slice_t path, int out_fd,
    ftp_reply_t *reply, int timeout_ms)
{
    return transfer_file(session, "RETR", path, out_fd, reply, timeout_ms);
}

int
ftp_session_stor(ftp_session_t *session, slice_t path, int in_fd,
    ftp_reply_t *reply, int timeout_ms)
{
    ftp_data_conn_t data;
    int rc;

    ftp_data_conn_init(&data);

    rc = ftp_session_pasv(session, &data, reply, timeout_ms);
    if (rc < 0)
        return -1;

    rc = ftp_conn_send_command(&session->conn, "STOR", path, timeout_ms);
    if (rc < 0) {
        ftp_data_conn_close(&data);
        return -1;
    }

    rc = read_preliminary_reply(&session->conn, reply, timeout_ms);
    if (rc < 0) {
        ftp_data_conn_close(&data);
        return -1;
    }

    rc = ftp_data_conn_stream_from_fd(&data, in_fd, timeout_ms);
    ftp_data_conn_close(&data);
    if (rc < 0) {
        int saved_errno;

        saved_errno = errno;
        (void)read_transfer_reply(&session->conn, reply, timeout_ms);
        errno = saved_errno;
        return -1;
    }

    rc = read_transfer_reply(&session->conn, reply, timeout_ms);
    if (rc < 0)
        return -1;

    switch (reply_class(reply)) {
    case 2:
        return 0;
    case 4:
        errno = EAGAIN;
        return -1;
    case 5:
        errno = EACCES;
        return -1;
    default:
        errno = EPROTO;
        return -1;
    }

}

int
ftp_session_dele(ftp_session_t *session, slice_t path, ftp_reply_t *reply,
    int timeout_ms)
{
    int rc;

    rc = ftp_conn_send_command(&session->conn, "DELE", path, timeout_ms);
    if (rc < 0)
        return -1;

    rc = ftp_conn_read_reply(&session->conn, reply, timeout_ms);
    if (rc < 0)
        return -1;

    switch (reply_class(reply)) {
    case 2:
        return 0;
    case 4:
        errno = EAGAIN;
        return -1;
    case 5:
        errno = EACCES;
        return -1;
    default:
        errno = EPROTO;
        return -1;
    }
}

int
ftp_session_quit(ftp_session_t *session, ftp_reply_t *reply, int timeout_ms)
{
    int rc;

    rc = ftp_session_command(session, "QUIT", (slice_t){0}, 2, reply,
        timeout_ms);
    if (rc < 0)
        return -1;

    session->logged_in = false;
    return 0;
}
