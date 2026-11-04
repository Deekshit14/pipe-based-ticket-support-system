# Pipe-Based Customer Support Ticket System

Educational C project for Linux/POSIX, based on the assignment sheet. The reception parent and support child exchange commands and replies through two anonymous pipes. Ticket records are kept in a fixed-size array; a deliberately small circular FIFO queue stores only four pending ticket IDs. **No linked list is used.**

## Core functionality

- One-way and bidirectional parent/child pipe communication; interactive command/reply chat.
- Ticket creation with customer, issue, and priority (1 normal, 2 urgent, 3 critical).
- FIFO ticket processing, status updates (critical tickets are escalated), wait-list handling when the small queue fills, ticket listing, and statistics.
- Timestamped audit logging to `ticket_log.txt`; the log is created owner-readable/writable (0600) and appended across runs.
- Clean pipe closure, child reaping, and Ctrl+C shutdown handling.
- Fixed-size arrays and small queue only. No linked list.

## Build and run

Requires GCC and a Linux/POSIX environment. On Windows, use WSL. Build both programs with:

```sh
make
```

Run the ticket application:

```sh
./ticket_system
```

Commands:

```text
/new CUSTOMER|ISSUE|PRIORITY
/process
/queue
/list
/stats
/help
/quit
```

For example, `/new Alice|Cannot log in|2` creates a ticket. The queue holds up to four tickets at a time; further tickets remain in the ticket array with `Waiting` status. Use `/process` to make room and `/queue` to move waiting tickets into available slots.

## Additional IPC demonstrations

Build `ipc_extensions` (also built by `make`) and run one of these small demos:

```sh
./ipc_extensions fifo     # two named FIFOs, bidirectional request/reply
./ipc_extensions mq       # POSIX message queues
./ipc_extensions shm      # shared memory, synchronized with semaphores
./ipc_extensions signal   # SIGUSR1 notification plus pipe payload
./ipc_extensions workers  # child-process ticket workers
```

The core `/stats` command is the ticket statistics dashboard. These add-on demos illustrate the assignment's advanced IPC topics; they are learning examples, not production services. `make clean` removes generated executables. The implementation uses arrays and a small queue throughout, not linked lists.
