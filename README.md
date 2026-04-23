# FTP Client

Minimal FTP client in C23, following the RFC 959 control/data flow from the design doc.

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
