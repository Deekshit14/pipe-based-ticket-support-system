#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_TICKETS 32
#define QUEUE_CAPACITY 4 /* Deliberately small FIFO queue; tickets themselves use an array. */
#define NAME_SIZE 48
#define ISSUE_SIZE 160
#define STATUS_SIZE 20
#define LINE_SIZE 256
#define LOG_FILE "ticket_log.txt"

typedef struct {
    int id;
    int priority;
    char customer[NAME_SIZE];
    char issue[ISSUE_SIZE];
    char status[STATUS_SIZE];
} Ticket;

typedef struct {
    Ticket tickets[MAX_TICKETS];
    int count;
    int next_id;
    int queued_ids[QUEUE_CAPACITY];
    int front;
    int rear;
    int queued;
} SupportDesk;

typedef struct {
    char text[LINE_SIZE];
} Message;

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

/* Pipe reads/writes may be partial, so transfer the complete fixed-size message. */
static int read_full(int fd, void *buffer, size_t length) {
    size_t done = 0;
    while (done < length) {
        ssize_t result = read(fd, (char *)buffer + done, length - done);
        if (result == 0) return 0;
        if (result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)result;
    }
    return 1;
}

static int write_full(int fd, const void *buffer, size_t length) {
    size_t done = 0;
    while (done < length) {
        ssize_t result = write(fd, (const char *)buffer + done, length - done);
        if (result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)result;
    }
    return 0;
}

static int enqueue(SupportDesk *desk, int ticket_id) {
    if (desk->queued == QUEUE_CAPACITY) return 0;
    desk->queued_ids[desk->rear] = ticket_id;
    desk->rear = (desk->rear + 1) % QUEUE_CAPACITY;
    desk->queued++;
    return 1;
}

static int dequeue(SupportDesk *desk, int *ticket_id) {
    if (desk->queued == 0) return 0;
    *ticket_id = desk->queued_ids[desk->front];
    desk->front = (desk->front + 1) % QUEUE_CAPACITY;
    desk->queued--;
    return 1;
}

static void audit_log(const Ticket *ticket, const char *event) {
    FILE *file = fopen(LOG_FILE, "a");
    if (file == NULL) {
        perror("Could not append audit log");
        return;
    }
    (void)fchmod(fileno(file), S_IRUSR | S_IWUSR); /* Keep the audit log private (0600). */
    time_t now = time(NULL);
    struct tm stamp;
    char time_text[32] = "unknown-time";
    if (localtime_r(&now, &stamp) != NULL)
        strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", &stamp);
    fprintf(file, "[%s] ticket=%d customer=\"%s\" priority=%d status=%s event=%s\n",
            time_text, ticket->id, ticket->customer, ticket->priority,
            ticket->status, event);
    fclose(file);
}

static void send_reply(int response_fd, const char *text) {
    Message reply;
    memset(&reply, 0, sizeof(reply));
    snprintf(reply.text, sizeof(reply.text), "%s", text);
    if (write_full(response_fd, &reply, sizeof(reply)) < 0) {
        perror("write to parent");
        _exit(EXIT_FAILURE);
    }
}

static void list_tickets(const SupportDesk *desk, char *output, size_t output_size) {
    size_t used = (size_t)snprintf(output, output_size, "Tickets (%d total):\n", desk->count);
    if (desk->count == 0) {
        snprintf(output + used, output_size - used, "  No tickets yet.");
        return;
    }
    for (int i = 0; i < desk->count && used < output_size; i++) {
        const Ticket *ticket = &desk->tickets[i];
        int written = snprintf(output + used, output_size - used,
            "  #%d | %-15s | priority %d | %-10s | %s\n",
            ticket->id, ticket->customer, ticket->priority, ticket->status, ticket->issue);
        if (written < 0) break;
        used += (size_t)written;
    }
}

static void handle_command(SupportDesk *desk, const char *command, char *reply, size_t reply_size) {
    if (strncmp(command, "/new ", 5) == 0) {
        char customer[NAME_SIZE], issue[ISSUE_SIZE], extra;
        int priority;
        if (sscanf(command + 5, "%47[^|]|%159[^|]|%d %c", customer, issue, &priority, &extra) != 3 ||
            customer[0] == '\0' || issue[0] == '\0' || priority < 1 || priority > 3) {
            snprintf(reply, reply_size,
                "Usage: /new CUSTOMER|ISSUE|PRIORITY (priority must be 1-3).");
            return;
        }
        if (desk->count == MAX_TICKETS) {
            snprintf(reply, reply_size, "Ticket array is full (%d tickets).", MAX_TICKETS);
            return;
        }
        Ticket ticket;
        memset(&ticket, 0, sizeof(ticket));
        ticket.id = desk->next_id++;
        ticket.priority = priority;
        snprintf(ticket.customer, sizeof(ticket.customer), "%s", customer);
        snprintf(ticket.issue, sizeof(ticket.issue), "%s", issue);
        snprintf(ticket.status, sizeof(ticket.status), "Queued");
        desk->tickets[desk->count++] = ticket;
        if (!enqueue(desk, ticket.id)) {
            snprintf(ticket.status, sizeof(ticket.status), "Waiting");
            desk->tickets[desk->count - 1] = ticket;
            audit_log(&ticket, "queue full; ticket stored in array as Waiting");
            snprintf(reply, reply_size,
                "Created ticket #%d, but the small queue is full; status is Waiting. Process queued tickets, then use /queue to enqueue waiting tickets.",
                ticket.id);
        } else {
            audit_log(&ticket, "ticket created and enqueued");
            snprintf(reply, reply_size, "Created and queued ticket #%d for %s.", ticket.id, ticket.customer);
        }
        return;
    }

    if (strcmp(command, "/process") == 0) {
        int ticket_id;
        if (!dequeue(desk, &ticket_id)) {
            snprintf(reply, reply_size, "Queue is empty. Add a ticket with /new.");
            return;
        }
        for (int i = 0; i < desk->count; i++) {
            Ticket *ticket = &desk->tickets[i];
            if (ticket->id == ticket_id) {
                snprintf(ticket->status, sizeof(ticket->status),
                         ticket->priority == 3 ? "Escalated" : "Resolved");
                audit_log(ticket, "ticket processed by child service");
                snprintf(reply, reply_size, "Processed ticket #%d for %s; status: %s.",
                         ticket->id, ticket->customer, ticket->status);
                return;
            }
        }
        snprintf(reply, reply_size, "Queue contained an unknown ticket ID.");
        return;
    }

    if (strcmp(command, "/queue") == 0) {
        int waiting = 0;
        for (int i = 0; i < desk->count; i++) {
            if (strcmp(desk->tickets[i].status, "Waiting") == 0) {
                if (enqueue(desk, desk->tickets[i].id)) {
                    snprintf(desk->tickets[i].status, sizeof(desk->tickets[i].status), "Queued");
                    audit_log(&desk->tickets[i], "waiting ticket added to queue");
                    waiting++;
                } else break;
            }
        }
        snprintf(reply, reply_size, "Moved %d waiting ticket(s) into the queue (%d/%d slots used).",
                 waiting, desk->queued, QUEUE_CAPACITY);
        return;
    }

    if (strcmp(command, "/list") == 0) {
        list_tickets(desk, reply, reply_size);
        return;
    }

    if (strcmp(command, "/stats") == 0) {
        int resolved = 0, escalated = 0, waiting = 0;
        for (int i = 0; i < desk->count; i++) {
            resolved += strcmp(desk->tickets[i].status, "Resolved") == 0;
            escalated += strcmp(desk->tickets[i].status, "Escalated") == 0;
            waiting += strcmp(desk->tickets[i].status, "Waiting") == 0;
        }
        snprintf(reply, reply_size,
            "Statistics: total=%d, in queue=%d/%d, resolved=%d, escalated=%d, waiting=%d.",
            desk->count, desk->queued, QUEUE_CAPACITY, resolved, escalated, waiting);
        return;
    }

    if (strcmp(command, "/help") == 0) {
        snprintf(reply, reply_size,
            "Commands: /new CUSTOMER|ISSUE|PRIORITY, /process, /queue, /list, /stats, /help, /quit. Priority: 1=normal, 2=urgent, 3=critical.");
        return;
    }

    snprintf(reply, reply_size, "Unknown command. Type /help for commands.");
}

static void child_service(int request_fd, int response_fd) {
    SupportDesk desk;
    memset(&desk, 0, sizeof(desk));
    desk.next_id = 1;
    Message request;

    for (;;) {
        int result = read_full(request_fd, &request, sizeof(request));
        if (result <= 0) break;
        request.text[sizeof(request.text) - 1] = '\0';
        if (strcmp(request.text, "/quit") == 0) {
            send_reply(response_fd, "Support service is shutting down. Goodbye.");
            break;
        }
        char reply[LINE_SIZE];
        memset(reply, 0, sizeof(reply));
        handle_command(&desk, request.text, reply, sizeof(reply));
        send_reply(response_fd, reply);
    }
    close(request_fd);
    close(response_fd);
    _exit(EXIT_SUCCESS);
}

int main(void) {
    int parent_to_child[2], child_to_parent[2];
    if (pipe(parent_to_child) == -1 || pipe(child_to_parent) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    /* Create a private audit log before forking; preserve it on later runs. */
    mode_t previous_mask = umask(0077);
    int log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0600);
    umask(previous_mask);
    if (log_fd < 0) {
        perror("open ticket log");
        return EXIT_FAILURE;
    }
    close(log_fd);

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }
    if (child == 0) {
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        child_service(parent_to_child[0], child_to_parent[1]);
    }

    close(parent_to_child[0]);
    close(child_to_parent[1]);
    signal(SIGPIPE, SIG_IGN);
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_sigint;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);

    printf("Customer Support Ticket Chat (parent = reception, child = support service)\n");
    printf("Type /help for commands. This demo uses two anonymous pipes and a small array-backed FIFO queue.\n\n");

    char input[LINE_SIZE];
    while (!stop_requested) {
        printf("reception> ");
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin)) break;
            if (errno == EINTR) { clearerr(stdin); continue; }
            perror("fgets");
            break;
        }
        input[strcspn(input, "\r\n")] = '\0';
        Message request;
        memset(&request, 0, sizeof(request));
        snprintf(request.text, sizeof(request.text), "%s", input);
        if (write_full(parent_to_child[1], &request, sizeof(request)) < 0) {
            perror("send request");
            break;
        }
        Message reply;
        int result = read_full(child_to_parent[0], &reply, sizeof(reply));
        if (result <= 0) {
            fprintf(stderr, "Support child closed its response pipe.\n");
            break;
        }
        reply.text[sizeof(reply.text) - 1] = '\0';
        printf("support> %s\n", reply.text);
        if (strcmp(input, "/quit") == 0) break;
    }

    if (stop_requested) {
        Message request;
        memset(&request, 0, sizeof(request));
        snprintf(request.text, sizeof(request.text), "/quit");
        (void)write_full(parent_to_child[1], &request, sizeof(request));
        Message reply;
        (void)read_full(child_to_parent[0], &reply, sizeof(reply));
        printf("\nInterrupted; shutting down support service.\n");
    }
    close(parent_to_child[1]);
    close(child_to_parent[0]);
    while (waitpid(child, NULL, 0) == -1 && errno == EINTR) {}
    printf("Audit log: %s (permissions 0600)\n", LOG_FILE);
    return EXIT_SUCCESS;
}
