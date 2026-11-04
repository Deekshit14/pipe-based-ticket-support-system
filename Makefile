CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
TARGET = ticket_system
EXTENSION_TARGET = ipc_extensions

.PHONY: all clean run extensions
all: $(TARGET) $(EXTENSION_TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $@ $<

$(EXTENSION_TARGET): ipc_extensions.c
	$(CC) $(CFLAGS) -o $@ $< -lcrypto -pthread

extensions: $(EXTENSION_TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(EXTENSION_TARGET)

