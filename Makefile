CC = gcc
CFLAGS = -Wall -Wextra -std=c11
TARGET = ticket_system

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

clean:
	rm -f $(TARGET) ticket_log.txt

.PHONY: all clean
