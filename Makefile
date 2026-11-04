# Pipe-Based Ticket Support System

A small Linux/POSIX C project that simulates a customer-support ticket system using a parent process and a child support process connected by pipes. The project uses fixed-size arrays and a small circular queue for pending tickets. No linked list is used.

## Features

- Parent-child communication using anonymous pipes
- Fixed-size ticket database using arrays
- Small queue for pending ticket IDs
- Ticket logging to `ticket_log.txt`
- Commands: `/new`, `/process`, `/queue`, `/list`, `/stats`, `/help`, `/quit`
- Linux/POSIX compatibility

## Build and run

```bash
make
./ticket_system
```

Example session:

```text
Customer Support Ticket System
Commands: /new CUSTOMER|ISSUE|PRIORITY, /process, /queue, /list, /stats, /help, /quit

reception> /new Alice|Login issue|2
support> Created ticket #1 for Alice and added to queue.
reception> /process
support> Processed ticket #1 for Alice. Status: Resolved
reception> /quit
support> Support service shutting down.
```

## Additional IPC examples

This repo also contains `ipc_extensions.c`, which demonstrates extra Linux IPC ideas in a small educational style:

- named FIFOs
- message queue
- shared memory
- multiple child workers
- signal demo

To build the extension examples:

```bash
make ipc_extensions
./ipc_extensions fifo
./ipc_extensions mq
./ipc_extensions shm
./ipc_extensions workers
./ipc_extensions signal
```

## Notes

- The core project is intentionally simple and array-based.
- The queue is intentionally small, as required by the assignment.
- The program is designed for Linux or WSL, not native Windows.

