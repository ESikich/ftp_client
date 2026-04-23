# FTP Client

Minimal FTP client in C23, following the RFC 959 control/data flow from the design doc.

Use `./build/ftp-client` with no arguments for the interactive shell, or pass
`host ...` arguments for one-shot batch mode.

## What it does

- Connects to an FTP server over TCP
- Reads and parses FTP replies
- Supports `USER` / `PASS`
- Supports `PWD`, `CWD`, `LIST`, `NLST`, `RETR`, `STOR`, `MKD`, and `DELE`
- Uses passive mode (`PASV`) for data transfers
- Provides both batch mode and an interactive shell

## Build

```bash
make
```

The binary is written to:

```bash
./build/ftp-client
```

For a smaller release build, use:

```bash
make size
```

For the smallest binary we found on this machine, use:

```bash
make size-musl
```

To regenerate `compile_commands.json` from the Makefile settings:

```bash
make compile-commands
```

## Test

```bash
make test
```

This runs the parser, session, transfer, DELE, CLI, and shell smoke tests.

## Usage

### Interactive shell

Start the client with no arguments:

```bash
./build/ftp-client
```

Available shell commands:

- `open HOST [PORT]`
- `user NAME [PASS]`
- `pass PASSWORD`
- `pwd`
- `cwd PATH`
- `list [PATH]`
- `nlst [PATH]`
- `retr REMOTE [LOCAL]`
- `stor REMOTE LOCAL`
- `dele REMOTE`
- `put DIR REMOTE LOCAL`
- `quit`
- `help`

### Command Reference

- `open HOST [PORT]`: connect to an FTP server without leaving the shell
- `user NAME [PASS]`: start or complete login
- `pass PASSWORD`: send the password after a `331` reply
- `pwd`: print the current remote working directory
- `cwd PATH`: change the current remote working directory
- `list [PATH]`: show a detailed directory listing
- `nlst [PATH]`: show a plain list of names
- `retr REMOTE [LOCAL]`: download a remote file to stdout or a local file
- `stor REMOTE LOCAL`: upload a local file to a remote path
- `dele REMOTE`: delete a remote file
- `put DIR REMOTE LOCAL`: create or enter a directory, then upload a file
- `quit`: end the session and close the connection
- `help`: print the shell command list

### Batch mode

You can still run one command at a time from the command line:

```bash
./build/ftp-client host [port] [user [pass [pwd|cwd path|list|nlst|retr|stor|dele [path [local]]|put dir remote local]]]
```

Examples:

```bash
./build/ftp-client test.rebex.net 21 demo password pwd
./build/ftp-client test.rebex.net 21 demo password list
./build/ftp-client test.rebex.net 21 demo password retr readme.txt
./build/ftp-client test.rebex.net 21 demo password put pub upload.txt Makefile
```

## Local FTP server

For writable local testing, start the bundled server:

```bash
make ftp-server
```

It listens on `127.0.0.1:2121` and uses `/tmp/ftp-local-root` as its root.

## Notes

- The implementation is IPv4-only.
- FTPS/TLS is out of scope.
- The project includes a `compile_commands.json` file for editor integration.
