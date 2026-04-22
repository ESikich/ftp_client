## Role

You are implementing a minimal, correct FTP client according to the accompanying design document (`ftp-client-design.md`). Read the design doc fully before writing any code. When the FTP specification or the design is silent on a detail, choose the most conservative behavior and leave a comment documenting the assumption.

---

## Language & Standard

- Language: **C23** (`-std=c23`; GCC ≥ 13, Clang ≥ 17)
- Use C23 features only where they improve correctness or clarity (`[[nodiscard]]`, `constexpr`, `typeof`).
- Prefer older C idioms if they are clearer.
- Platform: **POSIX / Linux**
- `_GNU_SOURCE` is defined project-wide and must not be defined per-file.

---

## Naming Conventions

- Functions: `ftp_verb_noun()` (`ftp_cmd_send`, `ftp_reply_parse`)
- Types: `noun_t` (`ftp_conn_t`, `ftp_reply_t`, `slice_t`)
- Enums: `NOUN_STATE` (`REPLY_MULTILINE`, `DATA_CONNECTING`)
- Constants: `ALL_CAPS` (`CTRL_BUF_SIZE`, `MAX_REPLY_LINES`)
- Parameters: short and explicit (`conn`, `reply`, `buf`, `len`)
- Locals: brief (`i`, `n`, `rc`, `fd` are acceptable)

No Hungarian notation. No redundant prefixes. Names must be readable without a glossary.

---

## Style

- Indentation: 4 spaces, no tabs
- Braces: K&R (opening brace on the same line)
- Line length: 79 columns (hard limit)
- One blank line between logical blocks; never more than one
- Function definitions put the return type on its own line
- Pointer style: `char *p`, not `char* p`

---

## Comments

- File header: one block comment with filename and one-line purpose
- Function comments only for non-obvious contracts or invariants
- Do not narrate what the code already makes clear

---

## Error Handling

- Every system call result is checked
- No chained assignment-and-check expressions
- Prefer early returns to keep the happy path unindented
- Fatal errors call `ftp_fatal()` (logs to stderr and exits)
- Protocol or connection errors return error codes and allow cleanup

---

## Memory Rules

- `malloc()` / `free()` allowed only during startup and shutdown
- No heap allocation in steady-state command or transfer paths
- Parsers operate directly on caller-provided buffers
- Slices (`slice_t`) never own memory and are short-lived

---

## Protocol Discipline

- Never assume read boundaries match protocol boundaries
- All parsing is incremental and state-machine driven
- FTP multi-line replies must be handled correctly
- Control and data connections are strictly separated

---

## Phasing

Work strictly phase-by-phase as defined in the design document.

A phase is complete only when:
- Code compiles cleanly with `-Wall -Wextra -Wpedantic -Werror`
- Tests and manual verification for the phase succeed
- No TODOs remain for that phase
- Design deviations are documented and reflected in the design doc

---

## What Not To Do

- Do not ignore protocol violations
- Do not invent abstractions not described in the design
- Do not block indefinitely without timeouts
- Do not mix control-channel and data-channel logic
- Do not optimize prematurely
