#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#define MAX_TICKETS 10
#define MAX_NAME 50
#define MAX_ISSUE 200
#define MAX_STATUS 30

typedef struct {
    int id;
    char customer[MAX_NAME];
    char issue[MAX_ISSUE];
    int priority;
    char status[MAX_STATUS];
} Ticket;

typedef struct {
    Ticket items[MAX_TICKETS];
    int front;
    int rear;
    int count;
} TicketQueue;

void initQueue(TicketQueue *queue) {
    queue->front = 0;
    queue->rear = 0;
    queue->count = 0;
}

int isQueueEmpty(const TicketQueue *queue) {
    return queue->count == 0;
}

int isQueueFull(const TicketQueue *queue) {
    return queue->count == MAX_TICKETS;
}

int enqueue(TicketQueue *queue, Ticket ticket) {
    if (isQueueFull(queue)) {
        return 0;
    }

    queue->items[queue->rear] = ticket;
    queue->rear = (queue->rear + 1) % MAX_TICKETS;
    queue->count++;
    return 1;
}

int dequeue(TicketQueue *queue, Ticket *ticket) {
    if (isQueueEmpty(queue)) {
        return 0;
    }

    *ticket = queue->items[queue->front];
    queue->front = (queue->front + 1) % MAX_TICKETS;
    queue->count--;
    return 1;
}

void displayQueue(const TicketQueue *queue) {
    int i;
    int index = queue->front;

    printf("\nQueue contents:\n");
    if (isQueueEmpty(queue)) {
        printf("  Queue is empty\n");
        return;
    }

    for (i = 0; i < queue->count; i++) {
        printf("  Ticket #%d | Customer: %-15s | Priority: %d | Status: %s\n",
               queue->items[index].id,
               queue->items[index].customer,
               queue->items[index].priority,
               queue->items[index].status);
        index = (index + 1) % MAX_TICKETS;
    }
}

void logTicket(const Ticket *ticket, const char *message) {
    FILE *logFile = fopen("ticket_log.txt", "a");
    time_t now;
    struct tm *local;

    if (logFile == NULL) {
        perror("Unable to open ticket_log.txt");
        return;
    }

    time(&now);
    local = localtime(&now);

    fprintf(logFile,
            "[%02d-%02d-%d %02d:%02d:%02d] Ticket #%d | Customer: %-15s | Priority: %d | Status: %-15s | %s\n",
            local->tm_mday,
            local->tm_mon + 1,
            local->tm_year + 1900,
            local->tm_hour,
            local->tm_min,
            local->tm_sec,
            ticket->id,
            ticket->customer,
            ticket->priority,
            ticket->status,
            message);

    fclose(logFile);
}

void initializeLogFile(void) {
    FILE *logFile = fopen("ticket_log.txt", "w");
    if (logFile != NULL) {
        fprintf(logFile, "Support Ticket Log\n");
        fclose(logFile);
    }
}

void processTicket(Ticket *ticket) {
    snprintf(ticket->status, sizeof(ticket->status), "Processed");

    if (ticket->priority >= 3) {
        snprintf(ticket->status, sizeof(ticket->status), "High Priority");
    } else if (ticket->priority == 2) {
        snprintf(ticket->status, sizeof(ticket->status), "In Progress");
    } else {
        snprintf(ticket->status, sizeof(ticket->status), "Resolved");
    }

    printf("Child: Ticket #%d processed for %s\n", ticket->id, ticket->customer);
    logTicket(ticket, "Child process handled the ticket");
}

void childWorker(int requestPipe, int responsePipe) {
    Ticket incoming;
    ssize_t bytesRead;

    while (1) {
        bytesRead = read(requestPipe, &incoming, sizeof(Ticket));
        if (bytesRead <= 0) {
            break;
        }

        processTicket(&incoming);
        write(responsePipe, &incoming, sizeof(Ticket));
    }

    close(requestPipe);
    close(responsePipe);
    _exit(0);
}

void printTicket(const Ticket *ticket) {
    printf("Ticket #%d | Customer: %-15s | Priority: %d | Issue: %s | Status: %s\n",
           ticket->id,
           ticket->customer,
           ticket->priority,
           ticket->issue,
           ticket->status);
}

int main(void) {
    TicketQueue queue;
    Ticket sampleTickets[5] = {
        {1, "Alice", "Cannot login to account", 1, "Queued"},
        {2, "Bob",   "Billing amount incorrect", 3, "Queued"},
        {3, "Cara",  "Server timeout during report", 2, "Queued"},
        {4, "David", "Password reset not working", 3, "Queued"},
        {5, "Emma",  "Need support for API access", 1, "Queued"}
    };

    int requestPipe[2];
    int responsePipe[2];
    pid_t pid;
    int i;

    initializeLogFile();
    initQueue(&queue);

    if (pipe(requestPipe) == -1 || pipe(responsePipe) == -1) {
        perror("pipe failed");
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return 1;
    }

    if (pid == 0) {
        close(requestPipe[1]);
        close(responsePipe[0]);
        childWorker(requestPipe[0], responsePipe[1]);
        return 0;
    }

    close(requestPipe[0]);
    close(responsePipe[1]);

    printf("Parent: Sending tickets to child process using pipe communication.\n");

    for (i = 0; i < 5; i++) {
        enqueue(&queue, sampleTickets[i]);
        write(requestPipe[1], &sampleTickets[i], sizeof(sampleTickets[i]));
        printf("Parent: Sent ticket #%d to child worker\n", sampleTickets[i].id);

        Ticket processed;
        read(responsePipe[0], &processed, sizeof(processed));
        printTicket(&processed);
        logTicket(&processed, "Parent received updated ticket");
    }

    close(requestPipe[1]);
    close(responsePipe[0]);

    wait(NULL);

    printf("\nFinal queue snapshot:\n");
    displayQueue(&queue);

    printf("\nTicket log saved to ticket_log.txt\n");
    return 0;
}

                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                
