/* main.c - minimal FTP client starter CLI. */

#include "ftp_client.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

enum {
    CMD_NONE,
    CMD_PWD,
    CMD_CWD,
    CMD_LIST,
    CMD_NLST,
    CMD_RETR,
    CMD_STOR,
    CMD_DELE,
    CMD_PUT
};

enum {
    SHELL_LINE_MAX = 1024,
    SHELL_MAX_ARGS = 8
};

typedef struct {
    ftp_session_t session;
    bool have_pending_user;
    char pending_user[128];
} shell_state_t;

static void
print_reply(const ftp_reply_t *reply)
{
    dprintf(STDOUT_FILENO, "%d%s\n", reply->code,
        reply->multiline ? " (multiline)" : "");
    if (reply->text_len > 0)
        dprintf(STDOUT_FILENO, "%s\n", reply->text);
}

static int
parse_command(const char *name)
{
    if (strcmp(name, "list") == 0)
        return CMD_LIST;
    if (strcmp(name, "nlst") == 0)
        return CMD_NLST;
    if (strcmp(name, "pwd") == 0)
        return CMD_PWD;
    if (strcmp(name, "cwd") == 0)
        return CMD_CWD;
    if (strcmp(name, "retr") == 0)
        return CMD_RETR;
    if (strcmp(name, "stor") == 0)
        return CMD_STOR;
    if (strcmp(name, "dele") == 0)
        return CMD_DELE;
    if (strcmp(name, "put") == 0)
        return CMD_PUT;

    return CMD_NONE;
}

static int
reply_class_local(const ftp_reply_t *reply)
{
    if (reply->code < 100 || reply->code > 599)
        return 0;

    return reply->code / 100;
}

static void
set_auth_errno_local(const ftp_reply_t *reply)
{
    switch (reply_class_local(reply)) {
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

static bool
is_help_arg(const char *arg)
{
    return strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0;
}

static void
print_usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [host [port] [user [pass [pwd|cwd path|list|nlst|retr|stor|dele [path [local]]|put dir remote local]]]]\n",
        prog);
    fprintf(stderr,
        "       %s (no args for interactive shell)\n",
        prog);
    fprintf(stderr,
        "       %s -h | --help\n",
        prog);
}

static void
print_shell_help(void)
{
    printf("commands:\n");
    printf("  open HOST [PORT]\n");
    printf("  user NAME [PASS]\n");
    printf("  pass PASSWORD\n");
    printf("  pwd\n");
    printf("  cwd PATH\n");
    printf("  list [PATH]\n");
    printf("  nlst [PATH]\n");
    printf("  retr REMOTE [LOCAL]\n");
    printf("  stor REMOTE LOCAL\n");
    printf("  dele REMOTE\n");
    printf("  put DIR REMOTE LOCAL\n");
    printf("  quit\n");
    printf("  help\n");
}

static void
shell_state_init(shell_state_t *shell)
{
    ftp_session_init(&shell->session);
    shell->have_pending_user = false;
    shell->pending_user[0] = '\0';
}

static void
shell_state_close(shell_state_t *shell)
{
    ftp_session_close(&shell->session);
    shell->have_pending_user = false;
    shell->pending_user[0] = '\0';
}

static int
shell_store_pending_user(shell_state_t *shell, slice_t user)
{
    if (user.len >= sizeof(shell->pending_user)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(shell->pending_user, user.ptr, user.len);
    shell->pending_user[user.len] = '\0';
    shell->have_pending_user = true;
    return 0;
}

static int
shell_open(shell_state_t *shell, const char *host, const char *port)
{
    ftp_reply_t reply;
    int rc;

    if (shell->session.conn.fd >= 0)
        ftp_session_close(&shell->session);

    rc = ftp_session_open(&shell->session, host, port, &reply, 10000);
    if (rc < 0)
        return -1;

    shell->have_pending_user = false;
    shell->pending_user[0] = '\0';
    print_reply(&reply);
    return 0;
}

static int
shell_user(shell_state_t *shell, const char *name, const char *pass,
    bool has_pass)
{
    ftp_reply_t reply;
    slice_t user;
    slice_t pass_slice;
    int rc;

    user = (slice_t){ name, strlen(name) };
    if (has_pass) {
        pass_slice = (slice_t){ pass, strlen(pass) };
        rc = ftp_session_login(&shell->session, user, pass_slice, &reply,
            10000);
        if (rc < 0)
            return -1;
        shell->have_pending_user = false;
        shell->pending_user[0] = '\0';
        print_reply(&reply);
        return 0;
    }

    rc = ftp_conn_send_command(&shell->session.conn, "USER", user, 10000);
    if (rc < 0)
        return -1;

    rc = ftp_conn_read_reply(&shell->session.conn, &reply, 10000);
    if (rc < 0)
        return -1;

    print_reply(&reply);
    switch (reply_class_local(&reply)) {
    case 2:
        shell->have_pending_user = false;
        shell->pending_user[0] = '\0';
        shell->session.logged_in = true;
        return 0;
    case 3:
        return shell_store_pending_user(shell, user);
    default:
        set_auth_errno_local(&reply);
        return -1;
    }
}

static int
shell_pass(shell_state_t *shell, const char *pass)
{
    ftp_reply_t reply;
    slice_t pass_slice;
    int rc;

    if (!shell->have_pending_user) {
        errno = EINVAL;
        return -1;
    }

    pass_slice = (slice_t){ pass, strlen(pass) };
    rc = ftp_conn_send_command(&shell->session.conn, "PASS", pass_slice,
        10000);
    if (rc < 0)
        return -1;

    rc = ftp_conn_read_reply(&shell->session.conn, &reply, 10000);
    if (rc < 0)
        return -1;

    print_reply(&reply);
    if (reply_class_local(&reply) == 2) {
        shell->session.logged_in = true;
        shell->have_pending_user = false;
        shell->pending_user[0] = '\0';
        return 0;
    }

    set_auth_errno_local(&reply);
    return -1;
}

static int
shell_pwd(shell_state_t *shell)
{
    ftp_reply_t reply;

    if (ftp_session_pwd(&shell->session, &reply, 10000) < 0)
        return -1;
    print_reply(&reply);
    return 0;
}

static int
shell_cwd(shell_state_t *shell, const char *path)
{
    ftp_reply_t reply;

    if (ftp_session_cwd(&shell->session,
            (slice_t){ path, strlen(path) }, &reply, 10000) < 0)
        return -1;
    print_reply(&reply);
    return 0;
}

static int
shell_list_like(shell_state_t *shell, const char *verb, const char *path)
{
    ftp_reply_t reply;
    slice_t arg;

    arg = (slice_t){ path, strlen(path) };
    if (strcmp(verb, "LIST") == 0) {
        if (ftp_session_list(&shell->session, arg, STDOUT_FILENO, &reply,
                10000) < 0)
            return -1;
    } else {
        if (ftp_session_nlst(&shell->session, arg, STDOUT_FILENO, &reply,
                10000) < 0)
            return -1;
    }
    print_reply(&reply);
    return 0;
}

static int
shell_retr(shell_state_t *shell, const char *remote, const char *local)
{
    ftp_reply_t reply;
    int fd;
    int rc;

    if (local == NULL) {
        rc = ftp_session_retr(&shell->session,
            (slice_t){ remote, strlen(remote) }, STDOUT_FILENO, &reply,
            10000);
        if (rc < 0)
            return -1;
        print_reply(&reply);
        return 0;
    }

    fd = open(local, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return -1;

    rc = ftp_session_retr(&shell->session,
        (slice_t){ remote, strlen(remote) }, fd, &reply, 10000);
    close(fd);
    if (rc < 0)
        return -1;

    print_reply(&reply);
    return 0;
}

static int
shell_stor(shell_state_t *shell, const char *remote, const char *local)
{
    ftp_reply_t reply;
    int fd;
    int rc;

    fd = open(local, O_RDONLY);
    if (fd < 0)
        return -1;

    rc = ftp_session_stor(&shell->session,
        (slice_t){ remote, strlen(remote) }, fd, &reply, 10000);
    close(fd);
    if (rc < 0)
        return -1;

    print_reply(&reply);
    return 0;
}

static int
shell_put(shell_state_t *shell, const char *dir, const char *remote,
    const char *local)
{
    ftp_reply_t reply;
    int fd;
    int rc;

    rc = ftp_session_mkd(&shell->session, (slice_t){ dir, strlen(dir) },
        &reply, 10000);
    if (rc == 0) {
        print_reply(&reply);
    } else if (errno != EACCES) {
        return -1;
    }

    rc = ftp_session_cwd(&shell->session, (slice_t){ dir, strlen(dir) },
        &reply, 10000);
    if (rc < 0)
        return -1;
    print_reply(&reply);

    fd = open(local, O_RDONLY);
    if (fd < 0)
        return -1;

    rc = ftp_session_stor(&shell->session,
        (slice_t){ remote, strlen(remote) }, fd, &reply, 10000);
    close(fd);
    if (rc < 0)
        return -1;

    print_reply(&reply);
    return 0;
}

static int
run_shell(void)
{
    shell_state_t shell;
    char line[SHELL_LINE_MAX];
    char *args[SHELL_MAX_ARGS];
    char *save;
    size_t argc;
    bool tty;
    int rc;

    shell_state_init(&shell);
    tty = isatty(STDIN_FILENO);

    for (;;) {
        if (tty) {
            if (dprintf(STDOUT_FILENO, "ftp> ") < 0)
                break;
        }

        if (fgets(line, sizeof(line), stdin) == NULL)
            break;

        line[strcspn(line, "\r\n")] = '\0';
        argc = 0;
        save = NULL;
        for (char *tok = strtok_r(line, " \t", &save);
            tok != NULL && argc < SHELL_MAX_ARGS;
            tok = strtok_r(NULL, " \t", &save)) {
            args[argc++] = tok;
        }

        if (argc == 0)
            continue;

        if (strcmp(args[0], "help") == 0) {
            print_shell_help();
            continue;
        }
        if (strcmp(args[0], "quit") == 0 || strcmp(args[0], "exit") == 0) {
            if (shell.session.conn.fd >= 0 && shell.session.logged_in) {
                ftp_reply_t reply;

                if (ftp_session_quit(&shell.session, &reply, 10000) == 0)
                    print_reply(&reply);
            }
            break;
        }
        if (strcmp(args[0], "open") == 0) {
            const char *port = (argc >= 3) ? args[2] : "21";

            if (argc < 2) {
                fprintf(stderr, "usage: open HOST [PORT]\n");
                continue;
            }

            rc = shell_open(&shell, args[1], port);
            if (rc < 0)
                fprintf(stderr, "open failed: %s\n", strerror(errno));
            continue;
        }
        if (strcmp(args[0], "user") == 0) {
            if (argc < 2) {
                fprintf(stderr, "usage: user NAME [PASS]\n");
                continue;
            }

            rc = shell_user(&shell, args[1], (argc >= 3) ? args[2] : "",
                argc >= 3);
            if (rc < 0)
                fprintf(stderr, "user failed: %s\n", strerror(errno));
            continue;
        }
        if (strcmp(args[0], "pass") == 0) {
            if (argc < 2) {
                fprintf(stderr, "usage: pass PASSWORD\n");
                continue;
            }

            rc = shell_pass(&shell, args[1]);
            if (rc < 0)
                fprintf(stderr, "pass failed: %s\n", strerror(errno));
            continue;
        }
        if (strcmp(args[0], "pwd") == 0) {
            rc = shell_pwd(&shell);
            if (rc < 0)
                fprintf(stderr, "pwd failed: %s\n", strerror(errno));
            continue;
        }
        if (strcmp(args[0], "cwd") == 0) {
            if (argc < 2) {
                fprintf(stderr, "usage: cwd PATH\n");
                continue;
            }

            rc = shell_cwd(&shell, args[1]);
            if (rc < 0)
                fprintf(stderr, "cwd failed: %s\n", strerror(errno));
            continue;
        }
        if (strcmp(args[0], "list") == 0 || strcmp(args[0], "nlst") == 0) {
            rc = shell_list_like(&shell, args[0], (argc >= 2) ? args[1] : "");
            if (rc < 0)
                fprintf(stderr, "%s failed: %s\n", args[0], strerror(errno));
            continue;
        }
        if (strcmp(args[0], "retr") == 0) {
            if (argc < 2) {
                fprintf(stderr, "usage: retr REMOTE [LOCAL]\n");
                continue;
            }

            rc = shell_retr(&shell, args[1], (argc >= 3) ? args[2] : NULL);
            if (rc < 0)
                fprintf(stderr, "retr failed: %s\n", strerror(errno));
            continue;
        }
        if (strcmp(args[0], "dele") == 0) {
            ftp_reply_t reply;

            if (argc < 2) {
                fprintf(stderr, "usage: dele REMOTE\n");
                continue;
            }

            rc = ftp_session_dele(&shell.session,
                (slice_t){ args[1], strlen(args[1]) }, &reply, 10000);
            if (rc < 0) {
                fprintf(stderr, "dele failed: %s\n", strerror(errno));
            } else {
                print_reply(&reply);
            }
            continue;
        }
        if (strcmp(args[0], "stor") == 0) {
            if (argc < 3) {
                fprintf(stderr, "usage: stor REMOTE LOCAL\n");
                continue;
            }

            rc = shell_stor(&shell, args[1], args[2]);
            if (rc < 0)
                fprintf(stderr, "stor failed: %s\n", strerror(errno));
            continue;
        }
        if (strcmp(args[0], "put") == 0) {
            if (argc < 4) {
                fprintf(stderr, "usage: put DIR REMOTE LOCAL\n");
                continue;
            }

            rc = shell_put(&shell, args[1], args[2], args[3]);
            if (rc < 0)
                fprintf(stderr, "put failed: %s\n", strerror(errno));
            continue;
        }

        fprintf(stderr, "unknown command: %s\n", args[0]);
    }

    shell_state_close(&shell);
    return 0;
}

int
main(int argc, char **argv)
{
    ftp_conn_t conn;
    ftp_session_t session;
    ftp_reply_t reply;
    const char *host;
    const char *port;
    const char *user;
    const char *pass;
    const char *cmd_arg;
    const char *cmd_arg2;
    const char *cmd_arg3;
    int cmd;
    int rc;

    if (argc == 1)
        return run_shell();

    if (argc >= 2 && is_help_arg(argv[1])) {
        print_usage(argv[0]);
        return 0;
    }

    if (argc < 2 || argc > 9) {
        print_usage(argv[0]);
        return 2;
    }

    host = argv[1];
    port = (argc >= 3) ? argv[2] : "21";
    user = (argc >= 4) ? argv[3] : NULL;
    pass = (argc >= 5) ? argv[4] : "";
    cmd = CMD_NONE;
    cmd_arg = "";
    cmd_arg2 = "";
    cmd_arg3 = "";

    if (argc >= 6) {
        cmd = parse_command(argv[5]);
        if (cmd == CMD_NONE) {
            print_usage(argv[0]);
            return 2;
        }

        cmd_arg = (argc >= 7) ? argv[6] : "";
        cmd_arg2 = (argc >= 8) ? argv[7] : "";
        cmd_arg3 = (argc >= 9) ? argv[8] : "";
    }

    ftp_conn_init(&conn);
    rc = ftp_conn_connect(&conn, host, port, 10000);
    if (rc < 0) {
        fprintf(stderr, "connect failed: %s\n", strerror(errno));
        return 1;
    }

    rc = ftp_conn_read_reply(&conn, &reply, 10000);
    if (rc < 0) {
        fprintf(stderr, "greeting failed: %s\n", strerror(errno));
        ftp_conn_close(&conn);
        return 1;
    }

    print_reply(&reply);

    ftp_session_init(&session);
    session.conn = conn;
    conn.fd = -1;

    if (user != NULL) {
        rc = ftp_session_login(&session, (slice_t){ user, strlen(user) },
            (slice_t){ pass, strlen(pass) },
            &reply, 10000);
        if (rc < 0) {
            fprintf(stderr, "login failed: %s\n", strerror(errno));
            ftp_session_close(&session);
            return 1;
        }

        print_reply(&reply);
    }

    if (cmd == CMD_PUT) {
        int fd;

        if (argc < 9) {
            print_usage(argv[0]);
            ftp_session_close(&session);
            return 2;
        }

        rc = ftp_session_mkd(&session,
            (slice_t){ cmd_arg, strlen(cmd_arg) }, &reply, 10000);
        if (rc == 0) {
            print_reply(&reply);
        } else if (errno != EACCES) {
            fprintf(stderr, "mkdir failed: %s\n", strerror(errno));
            ftp_session_close(&session);
            return 1;
        } else {
            fprintf(stderr, "mkdir skipped: %s\n", strerror(errno));
        }

        rc = ftp_session_cwd(&session,
            (slice_t){ cmd_arg, strlen(cmd_arg) }, &reply, 10000);
        if (rc < 0) {
            fprintf(stderr, "cwd failed: %s\n", strerror(errno));
            ftp_session_close(&session);
            return 1;
        }
        print_reply(&reply);

        fd = open(cmd_arg3, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "open failed: %s\n", strerror(errno));
            ftp_session_close(&session);
            return 1;
        }

        rc = ftp_session_stor(&session,
            (slice_t){ cmd_arg2, strlen(cmd_arg2) }, fd, &reply, 10000);
        close(fd);
        if (rc < 0) {
            fprintf(stderr, "transfer failed: %s\n", strerror(errno));
            ftp_session_close(&session);
            return 1;
        }

        print_reply(&reply);
    } else if (cmd == CMD_PWD) {
        rc = ftp_session_pwd(&session, &reply, 10000);
        if (rc < 0) {
            fprintf(stderr, "pwd failed: %s\n", strerror(errno));
            ftp_session_close(&session);
            return 1;
        }

        print_reply(&reply);
    } else if (cmd == CMD_CWD) {
        if (argc < 7) {
            fprintf(stderr,
                "usage: %s host [port] [user [pass [cwd path]]]\n",
                argv[0]);
            ftp_session_close(&session);
            return 2;
        }

        rc = ftp_session_cwd(&session,
            (slice_t){ cmd_arg, strlen(cmd_arg) }, &reply, 10000);
        if (rc < 0) {
            fprintf(stderr, "cwd failed: %s\n", strerror(errno));
            ftp_session_close(&session);
            return 1;
        }

        print_reply(&reply);
    } else if (cmd == CMD_LIST || cmd == CMD_NLST || cmd == CMD_RETR ||
        cmd == CMD_STOR || cmd == CMD_DELE) {
        if (cmd == CMD_LIST) {
            rc = ftp_session_list(&session,
                (slice_t){ cmd_arg, strlen(cmd_arg) }, STDOUT_FILENO,
                &reply, 10000);
        } else if (cmd == CMD_NLST) {
            rc = ftp_session_nlst(&session,
                (slice_t){ cmd_arg, strlen(cmd_arg) }, STDOUT_FILENO,
                &reply, 10000);
        } else if (cmd == CMD_RETR) {
            rc = ftp_session_retr(&session,
                (slice_t){ cmd_arg, strlen(cmd_arg) }, STDOUT_FILENO,
                &reply, 10000);
        } else if (cmd == CMD_DELE) {
            if (argc < 7) {
                print_usage(argv[0]);
                ftp_session_close(&session);
                return 2;
            }

            rc = ftp_session_dele(&session,
                (slice_t){ cmd_arg, strlen(cmd_arg) }, &reply, 10000);
        } else {
            int fd;

            if (argc < 8) {
                print_usage(argv[0]);
                ftp_session_close(&session);
                return 2;
            }

            fd = open(argv[7], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "open failed: %s\n", strerror(errno));
                ftp_session_close(&session);
                return 1;
            }

            rc = ftp_session_stor(&session,
                (slice_t){ cmd_arg, strlen(cmd_arg) }, fd, &reply, 10000);
            close(fd);
        }
        if (rc < 0) {
            fprintf(stderr, "transfer failed: %s\n", strerror(errno));
            ftp_session_close(&session);
            return 1;
        }

        print_reply(&reply);
    }

    ftp_session_close(&session);
    return 0;
}
