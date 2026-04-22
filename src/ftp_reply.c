/* ftp_reply.c - incremental parser for FTP control replies. */

#include "ftp_client.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

static void
reply_reset(ftp_reply_t *reply)
{
    reply->code = 0;
    reply->multiline = false;
    reply->text_len = 0;
    reply->text[0] = '\0';
}

static int
reply_append(ftp_reply_t *reply, const char *buf, size_t len)
{
    size_t need;

    need = reply->text_len + len;
    if (need >= sizeof(reply->text)) {
        errno = EMSGSIZE;
        return -1;
    }

    if (len > 0) {
        memcpy(reply->text + reply->text_len, buf, len);
        reply->text_len = need;
        reply->text[reply->text_len] = '\0';
    }

    return 0;
}

static int
reply_append_line(ftp_reply_t *reply, const char *line, size_t len)
{
    if (reply->text_len > 0) {
        if (reply_append(reply, "\n", 1) < 0)
            return -1;
    }

    return reply_append(reply, line, len);
}

static int
finish_line(ftp_reply_parser_t *parser, ftp_reply_t *reply);

static int
parse_code(const char *line, size_t len, int *code, char *sep)
{
    int value;

    if (len < 4) {
        errno = EPROTO;
        return -1;
    }

    if (!isdigit((unsigned char)line[0]) ||
        !isdigit((unsigned char)line[1]) ||
        !isdigit((unsigned char)line[2])) {
        errno = EPROTO;
        return -1;
    }

    if (line[3] != ' ' && line[3] != '-') {
        errno = EPROTO;
        return -1;
    }

    value = (line[0] - '0') * 100;
    value += (line[1] - '0') * 10;
    value += (line[2] - '0');

    *code = value;
    *sep = line[3];
    return 0;
}

static int
finish_line(ftp_reply_parser_t *parser, ftp_reply_t *reply)
{
    const char *line;
    size_t len;
    int code;
    char sep;

    line = parser->line;
    len = parser->line_len;

    if (len > 0 && line[len - 1] == '\r')
        len--;

    if (!parser->active) {
        const char *text;
        size_t text_len;

        if (parse_code(line, len, &code, &sep) < 0)
            return -1;

        parser->code = code;
        parser->multiline = (sep == '-');
        parser->active = true;

        text = line + 4;
        text_len = len - 4;

        reply_reset(reply);
        reply->code = code;
        reply->multiline = parser->multiline;

        if (!parser->multiline) {
            if (reply_append_line(reply, text, text_len) < 0)
                return -1;

            return 1;
        }

        return reply_append_line(reply, text, text_len);
    }

    if (len >= 4 && isdigit((unsigned char)line[0]) &&
        isdigit((unsigned char)line[1]) &&
        isdigit((unsigned char)line[2])) {
        if (line[0] - '0' != parser->code / 100 ||
            line[1] - '0' != (parser->code / 10) % 10 ||
            line[2] - '0' != parser->code % 10) {
            errno = EPROTO;
            return -1;
        }

        if (line[3] == ' ') {
            const char *text;
            size_t text_len;

            text = line + 4;
            text_len = len - 4;
            if (reply_append_line(reply, text, text_len) < 0)
                return -1;

            parser->active = false;
            return 1;
        }
    }

    if (reply_append_line(reply, line, len) < 0)
        return -1;

    return 0;
}

void
ftp_reply_parser_init(ftp_reply_parser_t *parser)
{
    parser->active = false;
    parser->multiline = false;
    parser->saw_cr = false;
    parser->code = 0;
    parser->line_len = 0;
    reply_reset(&parser->reply);
}

int
ftp_reply_parser_feed(ftp_reply_parser_t *parser, const char *buf,
    size_t len, size_t *consumed, ftp_reply_t *reply)
{
    size_t i;
    int rc;

    i = 0;
    rc = 0;
    while (i < len) {
        unsigned char ch;

        ch = (unsigned char)buf[i];
        i++;

        if (parser->saw_cr) {
            parser->saw_cr = false;
            if (ch == '\n')
                continue;
        }

        if (ch == '\r') {
            rc = finish_line(parser, reply);
            parser->line_len = 0;
            parser->saw_cr = true;
            if (rc != 0) {
                if (rc > 0 && i < len && buf[i] == '\n') {
                    i++;
                    parser->saw_cr = false;
                } else if (rc > 0 && i < len) {
                    i--;
                }
                break;
            }
            continue;
        }

        if (ch == '\n') {
            rc = finish_line(parser, reply);
            parser->line_len = 0;
            if (rc != 0)
                break;
            continue;
        }

        if (parser->line_len >= sizeof(parser->line) - 1) {
            errno = EMSGSIZE;
            return -1;
        }

        parser->line[parser->line_len++] = (char)ch;
    }

    *consumed = i;

    if (rc == 1)
        return 1;
    if (rc < 0)
        return -1;
    return 0;
}
