/* test_reply_parser.c - unit tests for FTP reply parsing. */

#include "ftp_client.h"

#include <stdio.h>
#include <string.h>

static int
check(int cond, const char *msg)
{
    if (cond)
        return 0;

    fprintf(stderr, "%s\n", msg);
    return -1;
}

static int
test_single_line(void)
{
    ftp_reply_parser_t parser;
    ftp_reply_t reply;
    const char *buf;
    size_t consumed;
    int rc;

    ftp_reply_parser_init(&parser);
    buf = "220 Service ready\r\n";
    rc = ftp_reply_parser_feed(&parser, buf, strlen(buf), &consumed, &reply);
    if (check(rc == 1, "single-line reply not completed"))
        return -1;
    if (check(consumed == strlen(buf), "single-line reply not fully consumed"))
        return -1;
    if (check(reply.code == 220, "wrong reply code"))
        return -1;
    if (check(reply.text_len == strlen("Service ready"),
            "wrong reply text length"))
        return -1;
    if (check(strcmp(reply.text, "Service ready") == 0,
            "wrong reply text"))
        return -1;

    return 0;
}

static int
test_multi_line(void)
{
    ftp_reply_parser_t parser;
    ftp_reply_t reply;
    const char *buf;
    size_t consumed;
    int rc;

    ftp_reply_parser_init(&parser);

    buf = "220-First line\r\n220 Second line\r\n";
    rc = ftp_reply_parser_feed(&parser, buf, strlen(buf), &consumed, &reply);
    if (check(rc == 1, "multi-line reply not completed"))
        return -1;
    if (check(consumed == strlen(buf), "multi-line reply not fully consumed"))
        return -1;
    if (check(reply.code == 220, "wrong multi-line reply code"))
        return -1;
    if (check(strcmp(reply.text, "First line\nSecond line") == 0,
            "wrong multi-line reply text"))
        return -1;

    return 0;
}

static int
test_cr_terminated(void)
{
    ftp_reply_parser_t parser;
    ftp_reply_t reply;
    const char *buf;
    size_t consumed;
    int rc;

    ftp_reply_parser_init(&parser);
    buf = "220 Banner with CR only\r";
    rc = ftp_reply_parser_feed(&parser, buf, strlen(buf), &consumed, &reply);
    if (check(rc == 1, "CR-terminated reply not completed"))
        return -1;
    if (check(consumed == strlen(buf), "CR-terminated reply not consumed"))
        return -1;
    if (check(reply.code == 220, "wrong CR-terminated reply code"))
        return -1;
    if (check(strcmp(reply.text, "Banner with CR only") == 0,
            "wrong CR-terminated reply text"))
        return -1;

    return 0;
}

int
main(void)
{
    if (test_single_line() < 0)
        return 1;
    if (test_multi_line() < 0)
        return 1;
    if (test_cr_terminated() < 0)
        return 1;

    return 0;
}
