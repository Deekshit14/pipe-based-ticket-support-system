# pipe-based-ticket-support-system

A C-based customer support ticket processing system that demonstrates the use of:

- parent-child process communication via Unix pipes
- fixed-size arrays instead of linked lists
- a simple circular queue for ticket handling
- ticket logging and status updates

## Project idea

This project simulates a support center where a parent process receives tickets and forwards them to a child process through a pipe. The child process acts as the support worker, updates ticket status, logs the event, and sends the processed result back to the parent.

## Requirements covered

- Pipe-based communication between parent and child processes
- Ticket processing using arrays and a queue
- No linked list usage
- Simple FIFO ticket queue with circular buffer logic
- Support for multiple customer tickets
- File logging and status tracking
- C implementation suitable for Linux/Unix systems

## Files

- `main.c` — complete implementation
- `Makefile` — build commands
- `README.md` — project overview
- `.gitignore` — build artifacts

## Build and run

```bash
make
./ticket_system
```

## Example behavior

The program simulates ticket processing for several customers such as:

- login issue
- billing problem
- server timeout
- password reset

Each ticket is inserted into a queue, sent through a pipe to the child process, processed, and logged with a timestamp.
