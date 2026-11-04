#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_TICKETS 20
#define QUEUE_CAPACITY 4
#define NAME_SIZE 32
#define ISSUE_SIZE 120
#define STATUS_SIZE 20
#define MESSAGE_SIZE 256

#define LOG_FILE "ticket_log.txt"

typedef struct {
    int id;
    char customer[NAME_SIZE];
    char issue[ISSUE_SIZE];
    int priority;
    char status[STATUS_SIZE];
} Ticket;

typedef struct {
    Ticket tickets[MAX_TICKETS];
    int count;
    int next_id;
    int queue[QUEUE_CAPACITY];
    int front;
    int rear;
    int queued;
} SupportDesk;

typedef struct {
    char text[MESSAGE_SIZE];
} Message;

static void init_desk(SupportDesk *desk) {
    memset(desk, 0, sizeof(*desk));
    desk->next_id = 1;
    desk->front = 0;
    desk->rear = 0;
    desk->queued = 0;
}

static int read_exact(int fd, void *buffer, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = read(fd, (char *)buffer + total, len - total);
        if (n == 0) {
            return 0;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)n;
    }
    return 1;
}

static int write_exact(int fd, const void *buffer, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, (const char *)buffer + total, len - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)n;
    }
    return 1;
}

static int enqueue_id(SupportDesk *desk, int ticket_id) {
    if (desk->queued >= QUEUE_CAPACITY) {
        return 0;
    }
    desk->queue[desk->rear] = ticket_id;
    desk->rear = (desk->rear + 1) % QUEUE_CAPACITY;
    desk->queued++;
    return 1;
}

static int dequeue_id(SupportDesk *desk, int *ticket_id) {
    if (desk->queued == 0) {
        return 0;
    }
    *ticket_id = desk->queue[desk->front];
    desk->front = (desk->front + 1) % QUEUE_CAPACITY;
    desk->queued--;
    return 1;
}

static void log_event(const Ticket *ticket, const char *event) {
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file == NULL) {
        perror("Unable to open ticket log");
        return;
    }

    time_t now = time(NULL);
    struct tm local_time;
    char time_text[32];

    localtime_r(&now, &local_time);
    strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", &local_time);

    fprintf(log_file,
            "[%s] Ticket #%d | customer=%s | priority=%d | status=%s | event=%s\n",
            time_text,
            ticket->id,
            ticket->customer,
            ticket->priority,
            ticket->status,
            event);
    fclose(log_file);
}

static void print_ticket(const Ticket *ticket) {
    printf("Ticket #%d | Customer: %-15s | Priority: %d | Status: %-12s | Issue: %s\n",
           ticket->id,
           ticket->customer,
           ticket->priority,
           ticket->status,
           ticket->issue);
}

static void update_ticket_status(Ticket *ticket) {
    if (ticket->priority == 3) {
        snprintf(ticket->status, sizeof(ticket->status), "Escalated");
    } else if (ticket->priority == 2) {
        snprintf(ticket->status, sizeof(ticket->status), "Resolved");
    } else {
        snprintf(ticket->status, sizeof(ticket->status), "Resolved");
    }
}

static void list_tickets(const SupportDesk *desk, char *reply, size_t reply_size) {
    size_t used = 0;
    used += snprintf(reply + used, reply_size - used, "Total tickets: %d\n", desk->count);
    for (int i = 0; i < desk->count && used < reply_size; i++) {
        used += snprintf(reply + used,
                         reply_size - used,
                         "#%d | %s | priority %d | %s | %s\n",
                         desk->tickets[i].id,
                         desk->tickets[i].customer,
                         desk->tickets[i].priority,
                         desk->tickets[i].status,
                         desk->tickets[i].issue);
    }
}

static void handle_command(SupportDesk *desk, const char *command, char *reply, size_t reply_size) {
    snprintf(reply, reply_size, "Unknown command. Type /help.");

    if (strcmp(command, "/help") == 0) {
        snprintf(reply, reply_size,
                 "Commands: /new CUSTOMER|ISSUE|PRIORITY, /process, /queue, /list, /stats, /help, /quit");
        return;
    }

    if (strcmp(command, "/list") == 0) {
        list_tickets(desk, reply, reply_size);
        return;
    }

    if (strcmp(command, "/stats") == 0) {
        int waiting = 0;
        int resolved = 0;
        int escalated = 0;
        for (int i = 0; i < desk->count; i++) {
            if (strcmp(desk->tickets[i].status, "Waiting") == 0) waiting++;
            if (strcmp(desk->tickets[i].status, "Resolved") == 0) resolved++;
            if (strcmp(desk->tickets[i].status, "Escalated") == 0) escalated++;
        }
        snprintf(reply, reply_size,
                 "Total=%d | queue=%d/%d | resolved=%d | escalated=%d | waiting=%d",
                 desk->count,
                 desk->queued,
                 QUEUE_CAPACITY,
                 resolved,
                 escalated,
                 waiting);
        return;
    }

    if (strncmp(command, "/new ", 5) == 0) {
        char customer[NAME_SIZE];
        char issue[ISSUE_SIZE];
        int priority = 0;
        char extra = '\0';

        if (sscanf(command + 5, "%31[^|]|%119[^|]|%d %c", customer, issue, &priority, &extra) != 3) {
            snprintf(reply, reply_size, "Usage: /new CUSTOMER|ISSUE|PRIORITY");
            return;
        }

        if (desk->count >= MAX_TICKETS) {
            snprintf(reply, reply_size, "Ticket array is full.");
            return;
        }

        Ticket ticket;
        memset(&ticket, 0, sizeof(ticket));
        ticket.id = desk->next_id++;
        snprintf(ticket.customer, sizeof(ticket.customer), "%s", customer);
        snprintf(ticket.issue, sizeof(ticket.issue), "%s", issue);
        ticket.priority = priority;
        snprintf(ticket.status, sizeof(ticket.status), "Queued");

        if (priority < 1 || priority > 3) {
            snprintf(reply, reply_size, "Priority must be 1, 2, or 3.");
            return;
        }

        desk->tickets[desk->count++] = ticket;
        if (enqueue_id(desk, ticket.id)) {
            log_event(&ticket, "ticket created and queued");
            snprintf(reply, reply_size, "Created ticket #%d for %s and added to queue.", ticket.id, ticket.customer);
        } else {
            snprintf(ticket.status, sizeof(ticket.status), "Waiting");
            log_event(&ticket, "ticket created but queue was full");
            snprintf(reply, reply_size, "Created ticket #%d but queue is full; status set to Waiting.", ticket.id);
        }
        return;
    }

    if (strcmp(command, "/queue") == 0) {
        int moved = 0;
        for (int i = 0; i < desk->count; i++) {
            if (strcmp(desk->tickets[i].status, "Waiting") == 0) {
                if (enqueue_id(desk, desk->tickets[i].id)) {
                    snprintf(desk->tickets[i].status, sizeof(desk->tickets[i].status), "Queued");
                    moved++;
                } else {
                    break;
                }
            }
        }
        snprintf(reply, reply_size, "Moved %d waiting ticket(s) into the queue.", moved);
        return;
    }

    if (strcmp(command, "/process") == 0) {
        int ticket_id = -1;
        if (!dequeue_id(desk, &ticket_id)) {
            snprintf(reply, reply_size, "Queue is empty.");
            return;
        }

        int found = 0;
        for (int i = 0; i < desk->count; i++) {
            if (desk->tickets[i].id == ticket_id) {
                update_ticket_status(&desk->tickets[i]);
                log_event(&desk->tickets[i], "ticket processed");
                snprintf(reply, reply_size,
                         "Processed ticket #%d for %s. Status: %s",
                         desk->tickets[i].id,
                         desk->tickets[i].customer,
                         desk->tickets[i].status);
                found = 1;
                break;
            }
        }

        if (!found) {
            snprintf(reply, reply_size, "Queued ticket #%d was not found in the ticket list.", ticket_id);
        }
        return;
    }

    if (strcmp(command, "/quit") == 0) {
        snprintf(reply, reply_size, "Support service shutting down.");
        return;
    }
}

static void child_service(int in_fd, int out_fd) {
    SupportDesk desk;
    init_desk(&desk);

    Message request;
    while (1) {
        memset(&request, 0, sizeof(request));
        if (read_exact(in_fd, &request, sizeof(request)) <= 0) {
            break;
        }

        char response[MESSAGE_SIZE];
        memset(response, 0, sizeof(response));
        handle_command(&desk, request.text, response, sizeof(response));
        Message reply;
        memset(&reply, 0, sizeof(reply));
        snprintf(reply.text, sizeof(reply.text), "%s", response);

        if (write_exact(out_fd, &reply, sizeof(reply)) < 0) {
            perror("write to parent");
            break;
        }

        if (strcmp(request.text, "/quit") == 0) {
            break;
        }
    }

    close(in_fd);
    close(out_fd);
    _exit(0);
}

int main(void) {
    int request_pipe[2];
    int response_pipe[2];

    if (pipe(request_pipe) == -1 || pipe(response_pipe) == -1) {
        perror("pipe");
        return 1;
    }

    FILE *log_file = fopen(LOG_FILE, "w");
    if (log_file == NULL) {
        perror("Unable to create log file");
        return 1;
    }
    fprintf(log_file, "Ticket support log started\n");
    fclose(log_file);

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }

    if (child == 0) {
        close(request_pipe[1]);
        close(response_pipe[0]);
        child_service(request_pipe[0], response_pipe[1]);
    }

    close(request_pipe[0]);
    close(response_pipe[1]);

    printf("Customer Support Ticket System\n");
    printf("Commands: /new CUSTOMER|ISSUE|PRIORITY, /process, /queue, /list, /stats, /help, /quit\n\n");

    while (1) {
        char user_input[MESSAGE_SIZE];
        printf("reception> ");
        fflush(stdout);

        if (!fgets(user_input, sizeof(user_input), stdin)) {
            break;
        }

        user_input[strcspn(user_input, "\r\n")] = '\0';
        if (user_input[0] == '\0') {
            continue;
        }

        Message send_msg;
        memset(&send_msg, 0, sizeof(send_msg));
        snprintf(send_msg.text, sizeof(send_msg.text), "%s", user_input);

        if (write_exact(request_pipe[1], &send_msg, sizeof(send_msg)) < 0) {
            perror("write to child");
            break;
        }

        Message reply_msg;
        memset(&reply_msg, 0, sizeof(reply_msg));
        if (read_exact(response_pipe[0], &reply_msg, sizeof(reply_msg)) <= 0) {
            fprintf(stderr, "Child closed the response pipe.\n");
            break;
        }

        printf("support> %s\n", reply_msg.text);

        if (strcmp(user_input, "/quit") == 0) {
            break;
        }
    }

    close(request_pipe[1]);
    close(response_pipe[0]);
    waitpid(child, NULL, 0);

    printf("\nProgram ended. Log file: %s\n", LOG_FILE);
    return 0;
}

