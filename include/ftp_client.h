/* ftp_client.h - public interface for the FTP client library. */

#ifndef FTP_CLIENT_H
#define FTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *ptr;
    size_t len;
} slice_t;

enum {
    FTP_REPLY_LINE_MAX = 1024,
    FTP_REPLY_TEXT_MAX = 8192,
    FTP_CTRL_BUF_SIZE = 4096
};

typedef struct {
    int code;
    bool multiline;
    char text[FTP_REPLY_TEXT_MAX];
    size_t text_len;
} ftp_reply_t;

typedef struct {
    bool active;
    bool multiline;
    bool saw_cr;
    int code;
    char line[FTP_REPLY_LINE_MAX];
    size_t line_len;
    ftp_reply_t reply;
} ftp_reply_parser_t;

typedef struct {
    char data[FTP_CTRL_BUF_SIZE];
    size_t len;
} ftp_ctrl_buf_t;

typedef struct {
    int fd;
    ftp_ctrl_buf_t ctrl;
} ftp_conn_t;

typedef struct {
    int fd;
} ftp_data_conn_t;

typedef struct {
    ftp_conn_t conn;
    bool logged_in;
} ftp_session_t;

void
ftp_reply_parser_init(ftp_reply_parser_t *parser);

int
ftp_reply_parser_feed(ftp_reply_parser_t *parser, const char *buf,
    size_t len, size_t *consumed, ftp_reply_t *reply);

void
ftp_conn_init(ftp_conn_t *conn);

void
ftp_conn_close(ftp_conn_t *conn);

void
ftp_data_conn_init(ftp_data_conn_t *conn);

void
ftp_data_conn_close(ftp_data_conn_t *conn);

int
ftp_data_conn_connect(ftp_data_conn_t *conn, const char *host,
    const char *port, int timeout_ms);

int
ftp_data_conn_stream_to_fd(ftp_data_conn_t *conn, int out_fd,
    int timeout_ms);

int
ftp_data_conn_stream_from_fd(ftp_data_conn_t *conn, int in_fd,
    int timeout_ms);

int
ftp_conn_connect(ftp_conn_t *conn, const char *host, const char *port,
    int timeout_ms);

int
ftp_conn_read_reply(ftp_conn_t *conn, ftp_reply_t *reply, int timeout_ms);

int
ftp_conn_send_command(ftp_conn_t *conn, const char *verb, slice_t arg,
    int timeout_ms);

void
ftp_session_init(ftp_session_t *session);

void
ftp_session_close(ftp_session_t *session);

int
ftp_session_open(ftp_session_t *session, const char *host, const char *port,
    ftp_reply_t *greeting, int timeout_ms);

int
ftp_session_command(ftp_session_t *session, const char *verb, slice_t arg,
    int expect_class, ftp_reply_t *reply, int timeout_ms);

int
ftp_session_login(ftp_session_t *session, slice_t user, slice_t pass,
    ftp_reply_t *reply, int timeout_ms);

int
ftp_session_type(ftp_session_t *session, slice_t mode, ftp_reply_t *reply,
    int timeout_ms);

int
ftp_session_pwd(ftp_session_t *session, ftp_reply_t *reply, int timeout_ms);

int
ftp_session_cwd(ftp_session_t *session, slice_t path, ftp_reply_t *reply,
    int timeout_ms);

int
ftp_session_mkd(ftp_session_t *session, slice_t path, ftp_reply_t *reply,
    int timeout_ms);

int
ftp_session_pasv(ftp_session_t *session, ftp_data_conn_t *data,
    ftp_reply_t *reply, int timeout_ms);

int
ftp_session_list(ftp_session_t *session, slice_t path, int out_fd,
    ftp_reply_t *reply, int timeout_ms);

int
ftp_session_nlst(ftp_session_t *session, slice_t path, int out_fd,
    ftp_reply_t *reply, int timeout_ms);

int
ftp_session_retr(ftp_session_t *session, slice_t path, int out_fd,
    ftp_reply_t *reply, int timeout_ms);

int
ftp_session_stor(ftp_session_t *session, slice_t path, int in_fd,
    ftp_reply_t *reply, int timeout_ms);

int
ftp_session_quit(ftp_session_t *session, ftp_reply_t *reply, int timeout_ms);

#endif
