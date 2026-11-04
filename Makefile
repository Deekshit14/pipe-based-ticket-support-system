CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
LDLIBS_IPC ?= -pthread -lrt

.PHONY: all clean run extensions
all: ticket_system ipc_extensions

ticket_system: main.c
	$(CC) $(CFLAGS) -o $@ $<

ipc_extensions: ipc_extensions.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS_IPC)

extensions: ipc_extensions

run: ticket_system
	./ticket_system

clean:
	rm -f ticket_system ipc_extensions
