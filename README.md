# Pipe-Based Customer Support Ticket System

Educational Linux/POSIX C project demonstrating a ticket-support workflow with parent/child process communication. Ticket records use fixed-size arrays and a bounded FIFO queue; **no linked lists are used**.

## Core application

The reception parent and support child exchange commands and replies over two anonymous pipes. The interactive application supports ticket creation, FIFO processing, status changes, a waiting list when the small queue fills, listing, statistics, timestamped audit logging, clean shutdown and Ctrl+C handling. The log file is restricted to owner read/write permissions (0600).

Build and run:

```sh
make
./ticket_system
```

Commands: `/new CUSTOMER|ISSUE|PRIORITY`, `/process`, `/queue`, `/list`, `/stats`, `/help`, `/quit`.

## IPC demos

`make` builds `ipc_extensions` and `ipc_advanced`. Existing examples in `ipc_extensions` demonstrate one-message named-FIFO exchange, POSIX message queues, shared memory with semaphores, SIGUSR1 notification, and child workers. The interactive demos in `ipc_advanced` are:

```sh
./ipc_advanced rooms       # persistent parent-child chat over two named FIFOs
./ipc_advanced server      # three persistent child agents; use /send ID MESSAGE
./ipc_advanced signal-chat # two anonymous pipes; SIGUSR1 signals each response
./ipc_advanced encrypt     # AES-256-GCM with PBKDF2-derived key (OpenSSL)
```

The encryption demo prompts for a passphrase visibly, encrypts and authenticates a message, prints the payload components in hex, then verifies/decrypts it. It is an educational demonstration, not a production key-management system.

## Tests and quality tools

`make test` runs native C unit tests, the core parent/child integration test, the advanced IPC/encryption integration smoke tests, and GoogleTest integration tests in sequence. Install GoogleTest development files and OpenSSL development headers/libraries first. Other optional targets are `make cppcheck`, `make misra MISRA_ADDON=...`, `make valgrind`, `make helgrind`, and `make coverage`. Coverage target thresholds are configured to 95% line/branch/function coverage of `main.c`; the percentage is only established when the target is actually run successfully.

See `TESTING.md` for dependencies and limitations. The application uses Linux/POSIX APIs and is intended for Linux (or a POSIX-compatible environment such as WSL), not native Windows.
