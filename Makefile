# Makefile - build the FTP client starter.

CC ?= cc
CFLAGS ?= -std=c2x -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS ?= -D_GNU_SOURCE -Iinclude -Isrc
LDFLAGS ?=
LDLIBS ?=
PYTHON ?= python3

BUILD_DIR := build
FTP_SERVER_HOST := 127.0.0.1
FTP_SERVER_PORT := 2121
FTP_SERVER_ROOT := /tmp/ftp-local-root
BIN := $(BUILD_DIR)/ftp-client
TEST_BIN := $(BUILD_DIR)/test_greeting
TEST_REPLY_BIN := $(BUILD_DIR)/test_reply_parser
TEST_SESSION_BIN := $(BUILD_DIR)/test_session
TEST_AUTH_FAIL_BIN := $(BUILD_DIR)/test_auth_fail
TEST_RETR_BIN := $(BUILD_DIR)/test_retr
TEST_STOR_BIN := $(BUILD_DIR)/test_stor
TEST_PUT_BIN := $(BUILD_DIR)/test_put
TEST_PUT_EXISTING_BIN := $(BUILD_DIR)/test_put_existing
TEST_STOR_ABORT_BIN := $(BUILD_DIR)/test_stor_abort
TEST_CLI_NAV_BIN := $(BUILD_DIR)/test_cli_nav
TEST_CLI_HELP_BIN := $(BUILD_DIR)/test_cli_help
TEST_CLI_SHELL_BIN := $(BUILD_DIR)/test_cli_shell
TEST_DELE_BIN := $(BUILD_DIR)/test_dele
APP_SRC := \
    src/main.c \
    src/ftp_conn.c \
    src/ftp_data.c \
    src/ftp_reply.c \
    src/ftp_session.c

LIB_SRC := \
    src/ftp_conn.c \
    src/ftp_data.c \
    src/ftp_reply.c \
    src/ftp_session.c

APP_OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(APP_SRC))
LIB_OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(LIB_SRC))
TEST_GREET_SRC := tests/test_greeting.c
TEST_REPLY_SRC := tests/test_reply_parser.c
TEST_SESSION_SRC := tests/test_session.c
TEST_AUTH_FAIL_SRC := tests/test_auth_fail.c
TEST_RETR_SRC := tests/test_retr.c
TEST_STOR_SRC := tests/test_stor.c
TEST_PUT_SRC := tests/test_put.c
TEST_PUT_EXISTING_SRC := tests/test_put_existing.c
TEST_STOR_ABORT_SRC := tests/test_stor_abort.c
TEST_CLI_NAV_SRC := tests/test_cli_nav.c
TEST_CLI_HELP_SRC := tests/test_cli_help.c
TEST_CLI_SHELL_SRC := tests/test_cli_shell.c
TEST_DELE_SRC := tests/test_dele.c

COMPILE_COMMANDS_SRCS := \
    $(APP_SRC) \
    $(TEST_GREET_SRC) \
    $(TEST_REPLY_SRC) \
    $(TEST_SESSION_SRC) \
    $(TEST_AUTH_FAIL_SRC) \
    $(TEST_RETR_SRC) \
    $(TEST_STOR_SRC) \
    $(TEST_PUT_SRC) \
    $(TEST_PUT_EXISTING_SRC) \
    $(TEST_STOR_ABORT_SRC) \
    $(TEST_CLI_NAV_SRC) \
    $(TEST_CLI_HELP_SRC) \
    $(TEST_CLI_SHELL_SRC)

COMPILE_COMMANDS_CPPFLAGS := $(CPPFLAGS)
COMPILE_COMMANDS_CFLAGS := $(CFLAGS)
COMPILE_COMMANDS_CC := $(CC)

.PHONY: all clean test ftp-server compile-commands

all: $(BIN)

test: $(BIN) $(TEST_BIN) $(TEST_REPLY_BIN) $(TEST_SESSION_BIN) \
    $(TEST_AUTH_FAIL_BIN) $(TEST_RETR_BIN) $(TEST_STOR_BIN) \
    $(TEST_PUT_BIN) $(TEST_PUT_EXISTING_BIN) $(TEST_STOR_ABORT_BIN) \
    $(TEST_CLI_NAV_BIN) $(TEST_CLI_HELP_BIN) $(TEST_CLI_SHELL_BIN) \
    $(TEST_DELE_BIN)
	./$(TEST_REPLY_BIN)
	./$(TEST_BIN)
	./$(TEST_SESSION_BIN)
	./$(TEST_AUTH_FAIL_BIN)
	./$(TEST_RETR_BIN)
	./$(TEST_STOR_BIN)
	./$(TEST_PUT_BIN)
	./$(TEST_PUT_EXISTING_BIN)
	./$(TEST_STOR_ABORT_BIN)
	./$(TEST_CLI_NAV_BIN)
	./$(TEST_CLI_HELP_BIN)
	./$(TEST_CLI_SHELL_BIN)
	./$(TEST_DELE_BIN)

$(BIN): $(APP_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_BIN): $(BUILD_DIR)/test_greeting.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_REPLY_BIN): $(BUILD_DIR)/test_reply_parser.o $(BUILD_DIR)/ftp_reply.o
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_SESSION_BIN): $(BUILD_DIR)/test_session.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_AUTH_FAIL_BIN): $(BUILD_DIR)/test_auth_fail.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_RETR_BIN): $(BUILD_DIR)/test_retr.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_STOR_BIN): $(BUILD_DIR)/test_stor.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_PUT_BIN): $(BUILD_DIR)/test_put.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_PUT_EXISTING_BIN): $(BUILD_DIR)/test_put_existing.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_STOR_ABORT_BIN): $(BUILD_DIR)/test_stor_abort.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_CLI_NAV_BIN): $(BUILD_DIR)/test_cli_nav.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_CLI_HELP_BIN): $(BUILD_DIR)/test_cli_help.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_CLI_SHELL_BIN): $(BUILD_DIR)/test_cli_shell.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_DELE_BIN): $(BUILD_DIR)/test_dele.o $(LIB_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c include/ftp_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_greeting.o: tests/test_greeting.c include/ftp_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_reply_parser.o: tests/test_reply_parser.c include/ftp_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_session.o: tests/test_session.c include/ftp_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_auth_fail.o: tests/test_auth_fail.c include/ftp_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_retr.o: tests/test_retr.c include/ftp_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_stor.o: tests/test_stor.c include/ftp_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_put.o: tests/test_put.c include/ftp_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_put_existing.o: tests/test_put_existing.c include/ftp_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_stor_abort.o: tests/test_stor_abort.c include/ftp_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_cli_nav.o: tests/test_cli_nav.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_cli_help.o: tests/test_cli_help.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_cli_shell.o: tests/test_cli_shell.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_dele.o: tests/test_dele.c include/ftp_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) ftp-client src/*.o

ftp-server:
	python3 tools/local_ftp_server.py --host $(FTP_SERVER_HOST) \
	    --port $(FTP_SERVER_PORT) --root $(FTP_SERVER_ROOT)

compile-commands:
	$(PYTHON) tools/gen_compile_commands.py \
	    --cc $(COMPILE_COMMANDS_CC) \
	    --cppflags "$(COMPILE_COMMANDS_CPPFLAGS)" \
	    --cflags "$(COMPILE_COMMANDS_CFLAGS)" \
	    --build-dir $(BUILD_DIR) \
	    --out compile_commands.json \
	    $(COMPILE_COMMANDS_SRCS)
