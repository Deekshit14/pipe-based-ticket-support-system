# Pipe-Based Ticket Support System

An educational C customer-support ticket processing project demonstrating Linux/POSIX IPC. The main program implements reception and support as parent and child processes communicating over two anonymous pipes. Ticket records are stored in fixed-size arrays; a deliberately small circular FIFO queue holds only four pending IDs. **No linked lists are used.**

## Core application

```sh
make ticket_system
./ticket_system
```

Interactive commands:
- `/new CUSTOMER|ISSUE|PRIORITY` (priority 1=normal, 2=urgent, 3=critical)
- `/process` process the oldest queued ticket
- `/queue` move waiting tickets into available queue slots
- `/list`, `/stats`, `/help`, `/quit`

The parent/child exchange is bidirectional, handles complete pipe transfers, closes descriptors, and reaps the child. Audit events are timestamped in `ticket_log.txt`, created with owner-only permissions (0600), and preserved across runs.

## IPC and extension demonstrations

Build the extension executable with `make ipc_extensions`. It requires GCC, POSIX/Linux, OpenSSL development headers/library (libcrypto), and pthread/semaphore support. Run one demonstration at a time:

```sh
./ipc_extensions fifo     # two named FIFOs, full-duplex request/reply
./ipc_extensions mq       # System V message queue
./ipc_extensions shm      # shared memory synchronized by process-shared semaphores
./ipc_extensions signal   # signal notification plus pipe-based payload (planned)
./ipc_extensions multi    # three child ticket workers
./ipc_extensions encrypt  # AES-256-GCM authenticated-encryption demonstration
```

`/stats` in the core program provides the ticket statistics dashboard. The extension demonstrations are focused, one-exchange educational examples, not production chat servers. AES-GCM uses a fresh ephemeral random key, which is not persisted; the demo proves encryption/authentication behavior, not durable key management. Do not use the IPC demos as a secure multi-user service.

Build both executables with `make`, and remove generated binaries with `make clean`. The POSIX process and anonymous-pipe core requires Linux, macOS, or a suitable POSIX environment (for example WSL); it is not native Windows code.
