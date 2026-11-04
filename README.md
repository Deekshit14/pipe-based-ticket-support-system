# Pipe-Based Ticket Support System

A small educational customer-support ticket processor written in C. The reception side (parent process) and support service (child process) communicate continuously in both directions using **two anonymous Linux/POSIX pipes**. Tickets and the ticket store use fixed-size arrays; a deliberately small circular FIFO queue holds at most four pending ticket IDs. **There are no linked lists.**

## Features implemented

- Parent/child process creation with `fork()` and two-way IPC with `pipe()`, `read()` and `write()`.
- Interactive command/response chat between reception and the child support service.
- Ticket creation, FIFO processing, status updates, ticket listing, and basic statistics.
- Fixed-size ticket array (32 records) and small circular queue (4 pending IDs). When full, tickets remain in the array with `Waiting` status and can be queued later.
- Timestamped audit log in `ticket_log.txt`; the log is created with restrictive permissions (`0600`). Existing log contents are retained when the program is run again.
- Signal-aware Ctrl+C shutdown and proper pipe closure / child reaping.
- Modular functions, descriptive names, comments, and a Makefile.

## Build and run (Linux / POSIX environment)

```sh
make
./ticket_system
```

Or use `make run`. Requires GCC or another C11 compiler and POSIX process/pipe support. The `fork()`/anonymous-pipe implementation is not native Windows code; use WSL or a Linux environment on Windows.

## Commands

- `/new CUSTOMER|ISSUE|PRIORITY` — create a ticket. Priority is 1 (normal), 2 (urgent), or 3 (critical).
- `/process` — process the oldest queued ticket. Critical tickets become `Escalated`; other tickets become `Resolved`.
- `/queue` — move waiting tickets into any newly available queue slots.
- `/list` — display all tickets.
- `/stats` — show totals and queue/status counts.
- `/help` — show command help.
- `/quit` — close the session cleanly.

Example:

```text
reception> /new Maya|Cannot reset password|2
support> Created and queued ticket #1 for Maya.
reception> /process
support> Processed ticket #1 for Maya; status: Resolved.
```

## Data structures

- `Ticket tickets[MAX_TICKETS]`: fixed-size ticket storage.
- `queued_ids[QUEUE_CAPACITY]`: small circular FIFO buffer. No linked-list node or pointer-based collection is used.

The named-FIFO chat rooms, System V message queues, shared-memory chat, encryption, and multi-child server listed as possible advanced extensions in the assignment sheet are **not implemented in this initial version**; the delivered program focuses on the core pipe-based parent/child ticket workflow and its required logging, timestamps, queue, and statistics. Do not treat the local pipe demo as secure or as a production support service.
