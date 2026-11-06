# Linux testing and code-quality targets

The project targets Linux/POSIX. Ticket data uses fixed-size arrays and a bounded FIFO queue—**no linked lists are used**. The production application is C; GoogleTest tests the executable from a separate C++ test file.

## Build and demos

`make` builds the ticket program and IPC demos. Install OpenSSL development headers/libraries for `ipc_advanced` (linked with `-lcrypto`). Run:

- `./ipc_advanced rooms ROOM_NAME` — named-FIFO parent/child room. Room names isolate FIFO paths; messages in both directions are encrypted and authenticated with AES-256-GCM. Enter the same room passphrase for the demo peers (the forked child inherits the derived key). The passphrase input is visible. This is a local educational demo, not a network chat or production key exchange.
- `./ipc_advanced server` — three persistent child agents, addressed with `/send ID MESSAGE`.
- `./ipc_advanced signal-chat` — pipe chat with SIGUSR1 response notifications.
- `./ipc_advanced encrypt` — standalone AES-256-GCM round-trip demonstration.

## Make targets

- `make test` — run native C unit tests, core process integration tests, advanced IPC/encryption smoke tests, and GoogleTest integration tests in order. Install GoogleTest development files and OpenSSL development files first.
- `make cppcheck` — static analysis.
- `make misra MISRA_ADDON=...` — run a supplied cppcheck MISRA addon/configuration; this is not certification.
- `make valgrind` / `make helgrind` — run memory/synchronization tooling on the core ticket process.
- `make coverage` — measure core `main.c` coverage and enforce at least 95% line, branch, and function coverage. This remains a target threshold, not a measured result until run.

GoogleTest, OpenSSL, cppcheck, Valgrind, gcovr, and a MISRA addon are external Linux dependencies. Helgrind concerns thread synchronization; these examples communicate with processes, so Helgrind does not verify general IPC correctness.
