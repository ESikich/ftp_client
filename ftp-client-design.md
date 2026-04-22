# FTP Client — Design Document

Minimal, correct FTP client implementation in C23  
Target protocol: RFC 959 (FTP)

---

## 1. Goals and Non-Goals

### Goals

- Correct implementation of core FTP (RFC 959)
- Conservative, predictable behavior
- Clear separation of control and data channels
- No hidden allocations in hot paths
- Easy to reason about state machines
- Suitable as a reusable library or CLI backend

### Non-Goals

- FTPS / TLS support (explicit or implicit)
- FXP, proxying, or server-to-server transfers
- GUI or curses interface
- Performance tuning beyond basic correctness
- Windows portability

---

## 2. Supported FTP Features (v1)

- USER / PASS authentication
- TYPE A / TYPE I
- PWD, CWD
- LIST, NLST
- RETR (download)
- STOR (upload)
- PASV (passive mode, IPv4 only)
- QUIT

Unsupported commands must fail explicitly and safely.

---

## 3. Architecture Overview

Logical layers:

- Transport (TCP sockets)
- Control Channel (commands and replies)
- Reply Parser (incremental, state driven)
- Data Channel (passive-mode transfers)
- Session Logic (login, sequencing)
- CLI / Frontend (optional)

Each layer communicates via explicit structs. No hidden global state
except logging configuration.

---

## 4. Control Connection Model

- One TCP socket per server session
- Blocking I/O (v1)
- Reads may return partial or multiple replies
- Writes are synchronous and complete before reads

Timeouts enforced via `poll()` or socket options.

---

## 5. FTP Reply Parsing

- Incremental byte-fed parser
- No dynamic allocation
- Handles single- and multi-line replies

Multi-line replies follow RFC 959 rules strictly.

---

## 6. Command Dispatch Model

- Commands are sent sequentially
- Expected reply classes enforced
- No command pipelining in v1

---

## 7. Data Connection Model

- Passive mode only (PASV)
- Separate socket per transfer
- Data socket lifecycle strictly ordered

---

## 8. File Transfers

### Download (RETR)

- Binary safe
- Stream to file descriptor
- Abort safely on errors

### Upload (STOR)

- Stream from file descriptor
- Handle early aborts

---

## 9. Error Handling Strategy

- Protocol violations are fatal to the session
- Connection-level errors are logged and cleaned up
- Data errors do not corrupt client state

---

## 10. Phasing Plan

1. TCP connect and greeting
2. FTP reply parser
3. Command framework
4. Authentication
5. Passive-mode data connections
6. Directory listing
7. File download
8. File upload
9. CLI wrapper
10. Robustness and cleanup

---

## 11. Assumptions

- IPv4 only
- RFC 959 semantics dominate
- Conservative timeouts

All assumptions must be documented in code where relied upon.

---

## 12. Future Extensions (Out of Scope)

- EPSV / IPv6
- Non-blocking I/O
- FTPS
- Resume/progress indicators
