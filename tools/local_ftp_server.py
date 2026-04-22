#!/usr/bin/env python3
"""Small local FTP test server for exercising the client.

Supports:
- USER / PASS
- PWD / CWD / MKD
- TYPE / QUIT
- PASV
- LIST / NLST
- RETR / STOR

This is intentionally minimal and single-session oriented. It is meant for
local validation of the client against a writable FTP endpoint.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import grp
import pwd
import socket
import stat
import time
import sys
from dataclasses import dataclass


def send_line(f, text: str) -> None:
    f.write((text + "\r\n").encode("utf-8"))
    f.flush()


def read_line(f) -> str | None:
    raw = f.readline()
    if not raw:
        return None
    return raw.decode("utf-8", "replace").rstrip("\r\n")


def rel_parts(root: pathlib.Path, cwd: pathlib.Path) -> str:
    rel = cwd.relative_to(root)
    if rel == pathlib.Path("."):
        return "/"
    return "/" + rel.as_posix()


def safe_join(root: pathlib.Path, cwd: pathlib.Path, arg: str) -> pathlib.Path | None:
    if arg.startswith("/"):
        candidate = (root / arg.lstrip("/")).resolve()
    else:
        candidate = (cwd / arg).resolve()
    try:
        candidate.relative_to(root)
    except ValueError:
        return None
    return candidate


@dataclass
class DataListener:
    sock: socket.socket
    addr: tuple[str, int]


def open_pasv_listener() -> DataListener:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", 0))
    sock.listen(1)
    return DataListener(sock=sock, addr=sock.getsockname())


def format_pasv(addr: tuple[str, int]) -> str:
    host, port = addr
    parts = host.split(".")
    p1, p2 = divmod(port, 256)
    return f"227 Entering Passive Mode ({parts[0]},{parts[1]},{parts[2]},{parts[3]},{p1},{p2})"


def dir_listing(target: pathlib.Path, names_only: bool) -> str:
    entries = []
    for entry in sorted(target.iterdir(), key=lambda p: p.name):
        if names_only:
            entries.append(entry.name)
            continue
        st = entry.stat()
        mode = stat.filemode(st.st_mode)
        nlink = 1
        owner = pwd.getpwuid(st.st_uid).pw_name
        group = grp.getgrgid(st.st_gid).gr_name
        size = st.st_size
        mtime = time.strftime("%b %d %H:%M", time.localtime(st.st_mtime))
        entries.append(f"{mode} {nlink} {owner} {group} {size:>8} {mtime} {entry.name}")
    return "\r\n".join(entries) + ("\r\n" if entries else "")


def handle_client(conn: socket.socket, root: pathlib.Path) -> None:
    cwd = root
    authed = False
    pasv = None

    rf = conn.makefile("rb")
    wf = conn.makefile("wb")
    send_line(wf, "220 Local FTP ready")

    while True:
        line = read_line(rf)
        if line is None:
            break
        if not line:
            continue

        parts = line.split(" ", 1)
        verb = parts[0].upper()
        arg = parts[1] if len(parts) > 1 else ""

        if verb == "USER":
            send_line(wf, "331 Password required")
        elif verb == "PASS":
            authed = True
            send_line(wf, "230 Logged in")
        elif verb == "TYPE":
            send_line(wf, "200 Type set")
        elif verb == "PWD":
            send_line(wf, f'257 "{rel_parts(root, cwd)}" is current directory')
        elif verb == "CWD":
            target = safe_join(root, cwd, arg)
            if target is None or not target.exists() or not target.is_dir():
                send_line(wf, "550 Failed to change directory")
            else:
                cwd = target
                send_line(wf, "250 Directory changed")
        elif verb == "MKD":
            target = safe_join(root, cwd, arg)
            if target is None:
                send_line(wf, "550 Permission denied")
            else:
                try:
                    target.mkdir(parents=False, exist_ok=False)
                    send_line(wf, "257 Directory created")
                except FileExistsError:
                    send_line(wf, "550 Directory already exists")
        elif verb == "PASV":
            if pasv is not None:
                pasv.sock.close()
            pasv = open_pasv_listener()
            send_line(wf, format_pasv(("127.0.0.1", pasv.addr[1])))
        elif verb in {"LIST", "NLST", "RETR", "STOR"}:
            if pasv is None:
                send_line(wf, "425 Use PASV first")
                continue
            send_line(wf, "150 Opening data connection")
            data_conn, _ = pasv.sock.accept()
            pasv.sock.close()
            pasv = None

            if verb in {"LIST", "NLST"}:
                names_only = verb == "NLST"
                target = cwd
                if arg:
                    resolved = safe_join(root, cwd, arg)
                    if resolved is not None:
                        target = resolved
                payload = dir_listing(target, names_only)
                data_conn.sendall(payload.encode("utf-8"))
            elif verb == "RETR":
                target = safe_join(root, cwd, arg)
                if target is not None and target.exists() and target.is_file():
                    data_conn.sendall(target.read_bytes())
            elif verb == "STOR":
                target = safe_join(root, cwd, arg)
                if target is not None:
                    with target.open("wb") as f:
                        while True:
                            chunk = data_conn.recv(4096)
                            if not chunk:
                                break
                            f.write(chunk)

            data_conn.close()
            send_line(wf, "226 Transfer complete")
        elif verb == "QUIT":
            send_line(wf, "221 Bye")
            break
        else:
            send_line(wf, "502 Command not implemented")

    if pasv is not None:
        pasv.sock.close()
    rf.close()
    wf.close()
    conn.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2121)
    parser.add_argument("--root", default=None)
    args = parser.parse_args()

    root = pathlib.Path(args.root or pathlib.Path.cwd() / "ftp-test-root").resolve()
    root.mkdir(parents=True, exist_ok=True)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((args.host, args.port))
        srv.listen(5)
        print(f"FTP server listening on {args.host}:{args.port} root={root}", flush=True)
        while True:
            conn, _ = srv.accept()
            try:
                handle_client(conn, root)
            except Exception as exc:  # noqa: BLE001
                try:
                    conn.close()
                except OSError:
                    pass
                print(f"client error: {exc}", file=sys.stderr, flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
