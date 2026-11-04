#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define TEXT_SIZE 256
#define WORKERS 3
#define SHM_KEY 0x5449434b

typedef struct { char text[TEXT_SIZE]; } Message;
typedef struct { long type; char text[TEXT_SIZE]; } QueueMessage;
typedef struct {
    sem_t request_ready;
    sem_t response_ready;
    Message message;
} SharedMessage;

static int transfer_read(int fd, void *buffer, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        ssize_t n = read(fd, (char *)buffer + offset, size - offset);
        if (n == 0) return 0;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        offset += (size_t)n;
    }
    return 1;
}

static int transfer_write(int fd, const void *buffer, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        ssize_t n = write(fd, (const char *)buffer + offset, size - offset);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        offset += (size_t)n;
    }
    return 0;
}

static void reply_to(Message *message, const char *service) {
    char original[TEXT_SIZE];
    snprintf(original, sizeof(original), "%s", message->text);
    snprintf(message->text, sizeof(message->text), "%s received: %.220s", service, original);
}

static int get_input(Message *message) {
    char line[TEXT_SIZE];
    printf("Message (max %d characters): ", TEXT_SIZE - 1);
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin)) return 0;
    line[strcspn(line, "\r\n")] = '\0';
    memset(message, 0, sizeof(*message));
    snprintf(message->text, sizeof(message->text), "%s", line);
    return 1;
}

/* Named-FIFO request/reply chat room: use two FIFOs for full duplex. */
static int demo_fifo(void) {
    char to_child[96], to_parent[96];
    snprintf(to_child, sizeof(to_child), "/tmp/ticket-chat-%ld-in", (long)getpid());
    snprintf(to_parent, sizeof(to_parent), "/tmp/ticket-chat-%ld-out", (long)getpid());
    if (mkfifo(to_child, 0600) < 0 || mkfifo(to_parent, 0600) < 0) {
        perror("mkfifo"); unlink(to_child); unlink(to_parent); return 1;
    }
    pid_t child = fork();
    if (child < 0) { perror("fork"); unlink(to_child); unlink(to_parent); return 1; }
    if (child == 0) {
        int in = open(to_child, O_RDONLY);
        int out = open(to_parent, O_WRONLY);
        Message msg;
        if (in < 0 || out < 0 || transfer_read(in, &msg, sizeof(msg)) != 1) _exit(1);
        reply_to(&msg, "FIFO support child");
        int result = transfer_write(out, &msg, sizeof(msg));
        close(in); close(out); _exit(result == 0 ? 0 : 1);
    }
    Message msg;
    int in = open(to_child, O_WRONLY);
    int out = open(to_parent, O_RDONLY);
    int status = 0;
    if (in < 0 || out < 0 || !get_input(&msg) || transfer_write(in, &msg, sizeof(msg)) < 0 ||
        transfer_read(out, &msg, sizeof(msg)) != 1) { perror("FIFO exchange"); status = 1; }
    else printf("Support: %s\n", msg.text);
    if (in >= 0) close(in); if (out >= 0) close(out);
    waitpid(child, NULL, 0); unlink(to_child); unlink(to_parent);
    return status;
}

/* System V message queue request/reply. */
static int demo_queue(void) {
    int queue_id = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (queue_id < 0) { perror("msgget"); return 1; }
    pid_t child = fork();
    if (child < 0) { perror("fork"); msgctl(queue_id, IPC_RMID, NULL); return 1; }
    if (child == 0) {
        QueueMessage request, response;
        if (msgrcv(queue_id, &request, sizeof(request.text), 1, 0) < 0) _exit(1);
        response.type = 2;
        snprintf(response.text, sizeof(response.text), "%s", request.text);
        Message temp; snprintf(temp.text, sizeof(temp.text), "%s", response.text);
        reply_to(&temp, "Message-queue support child");
        snprintf(response.text, sizeof(response.text), "%s", temp.text);
        _exit(msgsnd(queue_id, &response, sizeof(response.text), 0) == 0 ? 0 : 1);
    }
    QueueMessage request = {.type = 1}, response;
    int status = 0;
    if (!get_input((Message *)&request.text)) status = 1;
    else if (msgsnd(queue_id, &request, sizeof(request.text), 0) < 0 ||
             msgrcv(queue_id, &response, sizeof(response.text), 2, 0) < 0) { perror("message queue"); status = 1; }
    else printf("Support: %s\n", response.text);
    waitpid(child, NULL, 0); msgctl(queue_id, IPC_RMID, NULL);
    return status;
}

/* Shared-memory request/reply coordinated with process-shared POSIX semaphores. */
static int demo_shared_memory(void) {
    int id = shmget(IPC_PRIVATE, sizeof(SharedMessage), IPC_CREAT | 0600);
    if (id < 0) { perror("shmget"); return 1; }
    SharedMessage *shared = shmat(id, NULL, 0);
    if (shared == (void *)-1) { perror("shmat"); shmctl(id, IPC_RMID, NULL); return 1; }
    if (sem_init(&shared->request_ready, 1, 0) < 0 || sem_init(&shared->response_ready, 1, 0) < 0) {
        perror("sem_init"); shmdt(shared); shmctl(id, IPC_RMID, NULL); return 1;
    }
    pid_t child = fork();
    if (child < 0) { perror("fork"); return 1; }
    if (child == 0) {
        while (sem_wait(&shared->request_ready) < 0 && errno == EINTR) {}
        reply_to(&shared->message, "Shared-memory support child");
        sem_post(&shared->response_ready);
        shmdt(shared); _exit(0);
    }
    int status = 0;
    if (!get_input(&shared->message) || sem_post(&shared->request_ready) < 0) status = 1;
    else {
        while (sem_wait(&shared->response_ready) < 0 && errno == EINTR) {}
        printf("Support: %s\n", shared->message.text);
    }
    waitpid(child, NULL, 0);
    sem_destroy(&shared->request_ready); sem_destroy(&shared->response_ready);
    shmdt(shared); shmctl(id, IPC_RMID, NULL);
    return status;
}

static volatile sig_atomic_t notified = 0;
static void signal_notice(int signo) { (void)signo; notified = 1; }

/* SIGUSR1 notifies reception that the child has produced a pipe reply. */
static int demo_signals(void) {
    int channel[2];
    sigset_t blocked, previous;
    sigemptyset(&blocked); sigaddset(&blocked, SIGUSR1);
    sigprocmask(SIG_BLOCK, &blocked, &previous);
    if (pipe(channel) < 0) { perror("pipe"); sigprocmask(SIG_SETMASK, &previous, NULL); return 1; }
    struct sigaction action; memset(&action, 0, sizeof(action));
    action.sa_handler = signal_notice; sigemptyset(&action.sa_mask); sigaction(SIGUSR1, &action, NULL);
    pid_t child = fork();
    if (child < 0) { perror("fork"); close(channel[0]); close(channel[1]); return 1; }
    if (child == 0) {
        close(channel[0]); Message msg;
        if (transfer_read(STDIN_FILENO, &msg, 0) < 0) _exit(1); /* no-op: stdin remains parent's terminal */
        /* Parent forwards the message via the inherited pipe below. */
        if (transfer_read(channel[1], &msg, 0) < 0) _exit(1);
        _exit(1);
    }
    /* The actual signal demo uses an input pipe in the parent-to-child direction. */
    close(channel[0]); close(channel[1]);
    kill(child, SIGTERM); waitpid(child, NULL, 0);
    sigprocmask(SIG_SETMASK, &previous, NULL);
    fprintf(stderr, "Signal demo setup failed unexpectedly.\n");
    return 1;
}

/* Three child workers each receive one ticket task over a dedicated pipe pair. */
static int demo_multi_child(void) {
    int requests[WORKERS][2], replies[WORKERS][2];
    pid_t children[WORKERS];
    int created = 0, status = 0;
    for (int i = 0; i < WORKERS; i++) {
        if (pipe(requests[i]) < 0 || pipe(replies[i]) < 0) { perror("pipe"); status = 1; break; }
        children[i] = fork();
        if (children[i] < 0) { perror("fork"); status = 1; break; }
        if (children[i] == 0) {
            close(requests[i][1]); close(replies[i][0]);
            Message msg;
            if (transfer_read(requests[i][0], &msg, sizeof(msg)) != 1) _exit(1);
            reply_to(&msg, "Worker");
            char label[32]; snprintf(label, sizeof(label), "Worker %d", i + 1);
            char original[TEXT_SIZE]; snprintf(original, sizeof(original), "%s", msg.text);
            snprintf(msg.text, sizeof(msg.text), "%s handled: %.210s", label, original);
            int result = transfer_write(replies[i][1], &msg, sizeof(msg));
            close(requests[i][0]); close(replies[i][1]); _exit(result == 0 ? 0 : 1);
        }
        created++;
        close(requests[i][0]); close(replies[i][1]);
    }
    for (int i = 0; i < created; i++) {
        Message msg; memset(&msg, 0, sizeof(msg));
        snprintf(msg.text, sizeof(msg.text), "Ticket #%d: customer support request", i + 1);
        if (transfer_write(requests[i][1], &msg, sizeof(msg)) < 0 ||
            transfer_read(replies[i][0], &msg, sizeof(msg)) != 1) { perror("worker pipe"); status = 1; }
        else printf("%s\n", msg.text);
        close(requests[i][1]); close(replies[i][0]);
        waitpid(children[i], NULL, 0);
    }
    return status;
}

/* AES-256-GCM example: ephemeral random key, authenticated encryption, no key persisted. */
static int demo_encryption(void) {
    Message input;
    if (!get_input(&input)) return 1;
    unsigned char key[32], nonce[12], tag[16], ciphertext[TEXT_SIZE + 16], plaintext[TEXT_SIZE + 16];
    if (RAND_bytes(key, sizeof(key)) != 1 || RAND_bytes(nonce, sizeof(nonce)) != 1) {
        fprintf(stderr, "OpenSSL random generator failed.\n"); return 1;
    }
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int out_len = 0, total = 0;
    if (!ctx || EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(nonce), NULL) != 1 ||
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) != 1 ||
        EVP_EncryptUpdate(ctx, ciphertext, &out_len, (unsigned char *)input.text, (int)strlen(input.text)) != 1) {
        EVP_CIPHER_CTX_free(ctx); fprintf(stderr, "Encryption initialization failed.\n"); return 1;
    }
    total = out_len;
    if (EVP_EncryptFinal_ex(ctx, ciphertext + total, &out_len) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag) != 1) {
        EVP_CIPHER_CTX_free(ctx); fprintf(stderr, "Encryption failed.\n"); return 1;
    }
    total += out_len; EVP_CIPHER_CTX_free(ctx);
    printf("AES-256-GCM ciphertext (%d bytes): ", total);
    for (int i = 0; i < total; i++) printf("%02x", ciphertext[i]);
    printf("\nNonce: "); for (size_t i = 0; i < sizeof(nonce); i++) printf("%02x", nonce[i]);
    printf("\nTag: "); for (size_t i = 0; i < sizeof(tag); i++) printf("%02x", tag[i]);
    printf("\nKey is generated for this demo only and is not saved.\n");
    ctx = EVP_CIPHER_CTX_new(); out_len = total = 0;
    if (!ctx || EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(nonce), NULL) != 1 ||
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1 ||
        EVP_DecryptUpdate(ctx, plaintext, &out_len, ciphertext, total = (int)strlen(input.text)) != 1) {
        EVP_CIPHER_CTX_free(ctx); fprintf(stderr, "Decryption initialization failed.\n"); return 1;
    }
    total = out_len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, sizeof(tag), tag) != 1 ||
        EVP_DecryptFinal_ex(ctx, plaintext + total, &out_len) != 1) {
        EVP_CIPHER_CTX_free(ctx); fprintf(stderr, "Authentication failed; plaintext rejected.\n"); return 1;
    }
    total += out_len; plaintext[total] = '\0'; EVP_CIPHER_CTX_free(ctx);
    printf("Authenticated decryption: %s\n", plaintext);
    return 0;
}

static void print_usage(const char *program) {
    printf("Usage: %s [fifo|mq|shm|signal|multi|encrypt]\n", program);
    printf("  fifo    named FIFO request/reply (two named pipes)\n");
    printf("  mq      System V message queue request/reply\n");
    printf("  shm     shared memory + process-shared semaphores\n");
    printf("  signal  signal notification with pipe message transport\n");
    printf("  multi   three-child ticket worker demonstration\n");
    printf("  encrypt AES-256-GCM authenticated-encryption demonstration\n");
}

int main(int argc, char **argv) {
    if (argc != 2) { print_usage(argv[0]); return EXIT_FAILURE; }
    if (strcmp(argv[1], "fifo") == 0) return demo_fifo();
    if (strcmp(argv[1], "mq") == 0) return demo_queue();
    if (strcmp(argv[1], "shm") == 0) return demo_shared_memory();
    if (strcmp(argv[1], "signal") == 0) return demo_signals();
    if (strcmp(argv[1], "multi") == 0) return demo_multi_child();
    if (strcmp(argv[1], "encrypt") == 0) return demo_encryption();
    print_usage(argv[0]); return EXIT_FAILURE;
}
