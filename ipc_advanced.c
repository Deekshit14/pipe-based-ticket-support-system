#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEXT_SIZE 256
#define CHAT_WORKERS 3
#define KEY_SIZE 32
#define SALT_SIZE 16
#define NONCE_SIZE 12
#define TAG_SIZE 16
#define PBKDF2_ROUNDS 200000

typedef struct { char text[TEXT_SIZE]; } Message;

static int read_full(int fd, void *buffer, size_t length)
{
    size_t done = 0;
    while (done < length) {
        ssize_t count = read(fd, (char *)buffer + done, length - done);
        if (count == 0) return 0;
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)count;
    }
    return 1;
}

static int write_full(int fd, const void *buffer, size_t length)
{
    size_t done = 0;
    while (done < length) {
        ssize_t count = write(fd, (const char *)buffer + done, length - done);
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (count == 0) return -1;
        done += (size_t)count;
    }
    return 0;
}

static int read_line(char *buffer, size_t capacity)
{
    if (fgets(buffer, (int)capacity, stdin) == NULL) return 0;
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return 1;
}

/* Interactive parent/child conversation over two named FIFOs. */
static int demo_rooms(void)
{
    char parent_to_child[128], child_to_parent[128];
    (void)snprintf(parent_to_child, sizeof(parent_to_child), "/tmp/ticket-room-%ld-in", (long)getpid());
    (void)snprintf(child_to_parent, sizeof(child_to_parent), "/tmp/ticket-room-%ld-out", (long)getpid());
    if (mkfifo(parent_to_child, 0600) < 0 || mkfifo(child_to_parent, 0600) < 0) {
        perror("mkfifo");
        (void)unlink(parent_to_child);
        (void)unlink(child_to_parent);
        return 1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        (void)unlink(parent_to_child);
        (void)unlink(child_to_parent);
        return 1;
    }
    if (pid == 0) {
        int input_fd = open(parent_to_child, O_RDONLY);
        int output_fd = open(child_to_parent, O_WRONLY);
        if (input_fd < 0 || output_fd < 0) _exit(EXIT_FAILURE);
        for (;;) {
            Message request = {{0}}, reply = {{0}};
            if (read_full(input_fd, &request, sizeof(request)) != 1) break;
            request.text[TEXT_SIZE - 1] = '\0';
            (void)snprintf(reply.text, sizeof(reply.text), "support room received: %.220s", request.text);
            if (write_full(output_fd, &reply, sizeof(reply)) < 0 || strcmp(request.text, "/quit") == 0) break;
        }
        (void)close(input_fd);
        (void)close(output_fd);
        _exit(EXIT_SUCCESS);
    }

    int output_fd = open(parent_to_child, O_WRONLY);
    int input_fd = open(child_to_parent, O_RDONLY);
    int result = 0;
    if (output_fd < 0 || input_fd < 0) {
        perror("open room FIFO");
        result = 1;
    } else {
        char line[TEXT_SIZE];
        puts("Named-FIFO chat room. Type /quit to leave.");
        for (;;) {
            Message request = {{0}}, reply = {{0}};
            fputs("you> ", stdout);
            (void)fflush(stdout);
            if (!read_line(line, sizeof(line))) break;
            (void)snprintf(request.text, sizeof(request.text), "%s", line);
            if (write_full(output_fd, &request, sizeof(request)) < 0 || read_full(input_fd, &reply, sizeof(reply)) != 1) {
                perror("FIFO chat exchange");
                result = 1;
                break;
            }
            puts(reply.text);
            if (strcmp(line, "/quit") == 0) break;
        }
    }
    if (output_fd >= 0) (void)close(output_fd);
    if (input_fd >= 0) (void)close(input_fd);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) { }
    (void)unlink(parent_to_child);
    (void)unlink(child_to_parent);
    return result;
}

static void worker_loop(int request_fd, int response_fd, unsigned int worker_id)
{
    for (;;) {
        Message request = {{0}}, reply = {{0}};
        if (read_full(request_fd, &request, sizeof(request)) != 1) break;
        request.text[TEXT_SIZE - 1] = '\0';
        (void)snprintf(reply.text, sizeof(reply.text), "agent-%u: %.240s", worker_id, request.text);
        if (write_full(response_fd, &reply, sizeof(reply)) < 0 || strcmp(request.text, "/quit") == 0) break;
    }
    (void)close(request_fd);
    (void)close(response_fd);
    _exit(EXIT_SUCCESS);
}

/* Persistent child agents addressed with /send WORKER_ID MESSAGE. */
static int demo_server(void)
{
    int requests[CHAT_WORKERS][2], responses[CHAT_WORKERS][2];
    pid_t children[CHAT_WORKERS];
    for (int i = 0; i < CHAT_WORKERS; ++i) {
        if (pipe(requests[i]) < 0 || pipe(responses[i]) < 0) {
            perror("worker pipe");
            return 1;
        }
    }
    for (int i = 0; i < CHAT_WORKERS; ++i) {
        children[i] = fork();
        if (children[i] < 0) {
            perror("fork worker");
            return 1;
        }
        if (children[i] == 0) {
            for (int j = 0; j < CHAT_WORKERS; ++j) {
                (void)close(requests[j][1]);
                (void)close(responses[j][0]);
                if (j != i) {
                    (void)close(requests[j][0]);
                    (void)close(responses[j][1]);
                }
            }
            worker_loop(requests[i][0], responses[i][1], (unsigned int)i + 1U);
        }
    }
    for (int i = 0; i < CHAT_WORKERS; ++i) {
        (void)close(requests[i][0]);
        (void)close(responses[i][1]);
    }

    puts("Multi-child support chat. Use /send 1..3 MESSAGE or /quit.");
    int result = 0;
    char line[TEXT_SIZE];
    for (;;) {
        fputs("server> ", stdout);
        (void)fflush(stdout);
        if (!read_line(line, sizeof(line))) break;
        if (strcmp(line, "/quit") == 0) break;
        int worker = 0;
        char text[TEXT_SIZE] = {0};
        if (sscanf(line, "/send %d %255[^\n]", &worker, text) != 2 || worker < 1 || worker > CHAT_WORKERS) {
            fprintf(stderr, "Usage: /send WORKER_ID MESSAGE (worker IDs 1-%d)\n", CHAT_WORKERS);
            continue;
        }
        Message request = {{0}}, reply = {{0}};
        (void)snprintf(request.text, sizeof(request.text), "%s", text);
        if (write_full(requests[worker - 1][1], &request, sizeof(request)) < 0 ||
            read_full(responses[worker - 1][0], &reply, sizeof(reply)) != 1) {
            perror("worker communication");
            result = 1;
            break;
        }
        puts(reply.text);
    }
    for (int i = 0; i < CHAT_WORKERS; ++i) {
        Message stop = {{0}}, reply = {{0}};
        (void)snprintf(stop.text, sizeof(stop.text), "/quit");
        if (write_full(requests[i][1], &stop, sizeof(stop)) == 0) (void)read_full(responses[i][0], &reply, sizeof(reply));
        (void)close(requests[i][1]);
        (void)close(responses[i][0]);
    }
    for (int i = 0; i < CHAT_WORKERS; ++i) {
        while (waitpid(children[i], NULL, 0) < 0 && errno == EINTR) { }
    }
    return result;
}

static sigset_t chat_signal_set;

/* SIGUSR1 notifies the parent that a child response is available on its pipe. */
static int demo_signal_chat(void)
{
    int request_pipe[2], response_pipe[2];
    if (sigemptyset(&chat_signal_set) < 0 || sigaddset(&chat_signal_set, SIGUSR1) < 0 ||
        sigprocmask(SIG_BLOCK, &chat_signal_set, NULL) < 0 || pipe(request_pipe) < 0 || pipe(response_pipe) < 0) {
        perror("signal chat setup");
        return 1;
    }
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }
    if (child == 0) {
        (void)close(request_pipe[1]);
        (void)close(response_pipe[0]);
        for (;;) {
            Message request = {{0}}, reply = {{0}};
            if (read_full(request_pipe[0], &request, sizeof(request)) != 1) break;
            request.text[TEXT_SIZE - 1] = '\0';
            (void)snprintf(reply.text, sizeof(reply.text), "signal child received: %.220s", request.text);
            if (write_full(response_pipe[1], &reply, sizeof(reply)) < 0) break;
            (void)kill(getppid(), SIGUSR1);
            if (strcmp(request.text, "/quit") == 0) break;
        }
        _exit(EXIT_SUCCESS);
    }
    (void)close(request_pipe[0]);
    (void)close(response_pipe[1]);
    puts("Signal-controlled pipe chat. Type /quit to leave.");
    int result = 0;
    char line[TEXT_SIZE];
    for (;;) {
        fputs("you> ", stdout);
        (void)fflush(stdout);
        if (!read_line(line, sizeof(line))) break;
        Message request = {{0}}, reply = {{0}};
        (void)snprintf(request.text, sizeof(request.text), "%s", line);
        if (write_full(request_pipe[1], &request, sizeof(request)) < 0) {
            result = 1;
            break;
        }
        int received_signal = 0;
        if (sigwait(&chat_signal_set, &received_signal) != 0 || received_signal != SIGUSR1 ||
            read_full(response_pipe[0], &reply, sizeof(reply)) != 1) {
            fprintf(stderr, "signal chat response failed\n");
            result = 1;
            break;
        }
        puts(reply.text);
        if (strcmp(line, "/quit") == 0) break;
    }
    (void)close(request_pipe[1]);
    (void)close(response_pipe[0]);
    while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { }
    return result;
}

static void print_hex(const char *label, const unsigned char *data, size_t length)
{
    printf("%s", label);
    for (size_t i = 0; i < length; ++i) printf("%02x", (unsigned int)data[i]);
    putchar('\n');
}

/* Authenticated AES-256-GCM demo using PBKDF2 key derivation and OpenSSL. */
static int demo_encrypt(void)
{
    char passphrase[TEXT_SIZE] = {0}, plaintext[TEXT_SIZE] = {0};
    unsigned char salt[SALT_SIZE], nonce[NONCE_SIZE], tag[TAG_SIZE];
    unsigned char key[KEY_SIZE] = {0}, ciphertext[TEXT_SIZE], recovered[TEXT_SIZE];
    int encrypted_length = 0, final_length = 0, recovered_length = 0;
    EVP_CIPHER_CTX *ctx = NULL;
    int status = 1;

    fputs("Passphrase (input is visible): ", stdout);
    (void)fflush(stdout);
    if (!read_line(passphrase, sizeof(passphrase)) || passphrase[0] == '\0') {
        fprintf(stderr, "A non-empty passphrase is required.\n");
        goto cleanup;
    }
    fputs("Message to encrypt: ", stdout);
    (void)fflush(stdout);
    if (!read_line(plaintext, sizeof(plaintext))) goto cleanup;
    if (RAND_bytes(salt, sizeof(salt)) != 1 || RAND_bytes(nonce, sizeof(nonce)) != 1 ||
        PKCS5_PBKDF2_HMAC(passphrase, (int)strlen(passphrase), salt, sizeof(salt),
                          PBKDF2_ROUNDS, EVP_sha256(), sizeof(key), key) != 1) {
        fprintf(stderr, "Random/key derivation failed.\n");
        goto cleanup;
    }
    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL || EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)sizeof(nonce), NULL) != 1 ||
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) != 1 ||
        EVP_EncryptUpdate(ctx, ciphertext, &encrypted_length, (const unsigned char *)plaintext,
                          (int)strlen(plaintext)) != 1 ||
        EVP_EncryptFinal_ex(ctx, ciphertext + encrypted_length, &final_length) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, (int)sizeof(tag), tag) != 1) {
        fprintf(stderr, "Encryption failed.\n");
        goto cleanup;
    }
    encrypted_length += final_length;
    puts("AES-256-GCM encrypted payload (hex):");
    print_hex("salt: ", salt, sizeof(salt));
    print_hex("nonce: ", nonce, sizeof(nonce));
    print_hex("ciphertext: ", ciphertext, (size_t)encrypted_length);
    print_hex("authentication tag: ", tag, sizeof(tag));

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)sizeof(nonce), NULL) != 1 ||
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1 ||
        EVP_DecryptUpdate(ctx, recovered, &recovered_length, ciphertext, encrypted_length) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, (int)sizeof(tag), tag) != 1 ||
        EVP_DecryptFinal_ex(ctx, recovered + recovered_length, &final_length) != 1) {
        fprintf(stderr, "Authentication/decryption failed.\n");
        goto cleanup;
    }
    recovered_length += final_length;
    recovered[recovered_length] = '\0';
    printf("Authenticated plaintext: %s\n", recovered);
    status = 0;

cleanup:
    OPENSSL_cleanse(key, sizeof(key));
    OPENSSL_cleanse(passphrase, sizeof(passphrase));
    EVP_CIPHER_CTX_free(ctx);
    return status;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s rooms|server|signal-chat|encrypt\n", program);
    fprintf(stderr, "  rooms       interactive chat over two named FIFOs\n");
    fprintf(stderr, "  server      multi-child support server (3 persistent agents)\n");
    fprintf(stderr, "  signal-chat pipe chat with SIGUSR1 response notifications\n");
    fprintf(stderr, "  encrypt     AES-256-GCM message encryption/decryption demo\n");
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "rooms") == 0) return demo_rooms();
    if (strcmp(argv[1], "server") == 0) return demo_server();
    if (strcmp(argv[1], "signal-chat") == 0) return demo_signal_chat();
    if (strcmp(argv[1], "encrypt") == 0) return demo_encrypt();
    usage(argv[0]);
    return EXIT_FAILURE;
}
