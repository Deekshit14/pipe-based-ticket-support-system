# Linux C testing and code-quality targets

The project targets Linux/POSIX. Ticket data uses fixed-size arrays and a bounded FIFO queue—**no linked lists are used**. Production programs and unit/integration tests are written in C; the GoogleTest integration suite is C++.

## Build and demos

`make` builds the ticket program and IPC demos. Install OpenSSL development headers/libraries for `ipc_advanced` (linked with `-lcrypto`).

- `./ipc_advanced rooms ROOM_NAME` — named-FIFO parent/child room using AES-256-GCM authenticated messages.
- `./ipc_advanced server` — three persistent child agents, addressed with `/send ID MESSAGE`.
- `./ipc_advanced signal-chat` — pipe chat with SIGUSR1 response notifications.
- `./ipc_advanced encrypt` — standalone AES-256-GCM round-trip demonstration.

The chat demos append timestamped sent/received message text to `chat_log.txt`, with owner-only permissions. This log contains plaintext; keep it private and remove it when no longer needed.

## Required validation targets

Run each target from the repository root:

```sh
make clean
make
make unit
make integration
make gtest
make coverage
make valgrind
make helgrind
```

- `make test` runs the unit, C integration, and GoogleTest suites.
- `make gtest` compiles and executes the C++ GoogleTest tests. It requires the GoogleTest development headers and libraries; the Makefile uses `pkg-config` when available and otherwise links the conventional `gtest_main`/`gtest` libraries.
- `make coverage` runs the instrumented C tests and produces `build/coverage/coverage_report.txt`. It enforces at least 95% line, branch, and function coverage for `main.c`. Branches that require injected OS/resource failures or `EINTR` are excluded from the branch metric; their lines and functions remain covered. Install `gcovr` if it is not available on the command path (`python3 -m pip install --user gcovr` is one option).
- `make valgrind` checks the core ticket process for memory errors/leaks.
- `make helgrind` runs Valgrind's Helgrind tool on the process/pipe workflow. Since this program uses processes rather than shared-memory threads, Helgrind is an additional diagnostic, not proof of race-free behavior.

These validation tools depend on local Linux packages: GoogleTest, OpenSSL, gcovr, and Valgrind. A target being implemented does not mean it has been run or passed on a particular machine.

## Other Make targets

- `make cppcheck` — static analysis.
- `make misra MISRA_ADDON=...` — run a supplied cppcheck MISRA addon/configuration; this is not certification.

GoogleTest, OpenSSL, cppcheck, Valgrind, gcovr, and a MISRA addon are external Linux dependencies.
