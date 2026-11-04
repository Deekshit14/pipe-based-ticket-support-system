#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MESSAGE_SIZE 256
#define WORKER_COUNT 3

typedef struct {
    char text[MESSAGE_SIZE];
} Message;

static int read_exact(int fd, void *buffer, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = read(fd, (char *)buffer + offset, length - offset);
        if (n == 0) {
            return 0;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        offset += (size_t)n;
    }
    return 1;
}

static int write_exact(int fd, const void *buffer, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = write(fd, (const char *)buffer + offset, length - offset);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        offset += (size_t)n;
    }
    return 1;
}

static void reply_to(Message *message, const char *service_name) {
    char original[MESSAGE_SIZE];
    snprintf(original, sizeof(original), "%s", message->text);
    snprintf(message->text, sizeof(message->text), "%s handled: %s", service_name, original);
}

static int demo_fifo(void) {
    const char *in_path = "/tmp/ticket_fifo_in";
    const char *out_path = "/tmp/ticket_fifo_out";

    unlink(in_path);
    unlink(out_path);

    if (mkfifo(in_path, 0600) < 0 || mkfifo(out_path, 0600) < 0) {
        perror("mkfifo");
        unlink(in_path);
        unlink(out_path);
        return 1;
    }

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        unlink(in_path);
        unlink(out_path);
        return 1;
    }

    if (child == 0) {
        int in_fd = open(in_path, O_RDONLY);
        int out_fd = open(out_path, O_WRONLY);
        if (in_fd < 0 || out_fd < 0) {
            _exit(1);
        }

        Message msg;
        memset(&msg, 0, sizeof(msg));
        if (read_exact(in_fd, &msg, sizeof(msg)) <= 0) {
            _exit(1);
        }

        reply_to(&msg, "FIFO child");
        if (write_exact(out_fd, &msg, sizeof(msg)) < 0) {
            _exit(1);
        }

        close(in_fd);
        close(out_fd);
        _exit(0);
    }

    int in_fd = open(in_path, O_WRONLY);
    int out_fd = open(out_path, O_RDONLY);
    Message send_msg;
    Message reply_msg;
    memset(&send_msg, 0, sizeof(send_msg));
    snprintf(send_msg.text, sizeof(send_msg.text), "Customer login problem");

    if (in_fd < 0 || out_fd < 0) {
        fprintf(stderr, "Failed to open FIFO pipes.\n");
        waitpid(child, NULL, 0);
        unlink(in_path);
        unlink(out_path);
        return 1;
    }

    if (write_exact(in_fd, &send_msg, sizeof(send_msg)) < 0) {
        perror("write_exact fifo");
    }
    if (read_exact(out_fd, &reply_msg, sizeof(reply_msg)) <= 0) {
        perror("read_exact fifo");
    }

    printf("FIFO reply: %s\n", reply_msg.text);

    close(in_fd);
    close(out_fd);
    waitpid(child, NULL, 0);
    unlink(in_path);
    unlink(out_path);
    return 0;
}

static int demo_message_queue(void) {
    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(Message);

    mq_unlink("/ticket_support_mq");
    mqd_t mq = mq_open("/ticket_support_mq", O_CREAT | O_RDWR, 0600, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        return 1;
    }

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        mq_close(mq);
        mq_unlink("/ticket_support_mq");
        return 1;
    }

    if (child == 0) {
        Message msg;
        memset(&msg, 0, sizeof(msg));
        if (mq_receive(mq, (char *)&msg, sizeof(msg), NULL) < 0) {
            _exit(1);
        }
        reply_to(&msg, "MQ child");
        if (mq_send(mq, (const char *)&msg, sizeof(msg), 1) < 0) {
            _exit(1);
        }
        _exit(0);
    }

    Message send_msg;
    Message reply_msg;
    memset(&send_msg, 0, sizeof(send_msg));
    snprintf(send_msg.text, sizeof(send_msg.text), "Billing issue");

    if (mq_send(mq, (const char *)&send_msg, sizeof(send_msg), 1) < 0) {
        perror("mq_send");
        waitpid(child, NULL, 0);
        mq_close(mq);
        mq_unlink("/ticket_support_mq");
        return 1;
    }

    if (mq_receive(mq, (char *)&reply_msg, sizeof(reply_msg), NULL) < 0) {
        perror("mq_receive");
        waitpid(child, NULL, 0);
        mq_close(mq);
        mq_unlink("/ticket_support_mq");
        return 1;
    }

    printf("Message queue reply: %s\n", reply_msg.text);
    waitpid(child, NULL, 0);
    mq_close(mq);
    mq_unlink("/ticket_support_mq");
    return 0;
}

static int demo_shared_memory(void) {
    const char *name = "/ticket_support_shm";
    const char *sem_name = "/ticket_support_sem";

    shm_unlink(name);
    sem_unlink(sem_name);

    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (shm_fd < 0) {
        perror("shm_open");
        return 1;
    }

    if (ftruncate(shm_fd, sizeof(Message)) < 0) {
        perror("ftruncate");
        close(shm_fd);
        shm_unlink(name);
        return 1;
    }

    sem_t *ready = sem_open(sem_name, O_CREAT | O_RDWR, 0600, 0);
    if (ready == SEM_FAILED) {
        perror("sem_open");
        close(shm_fd);
        shm_unlink(name);
        return 1;
    }

    Message *shared = mmap(NULL, sizeof(Message), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        sem_close(ready);
        sem_unlink(sem_name);
        close(shm_fd);
        shm_unlink(name);
        return 1;
    }

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        munmap(shared, sizeof(Message));
        sem_close(ready);
        sem_unlink(sem_name);
        close(shm_fd);
        shm_unlink(name);
        return 1;
    }

    if (child == 0) {
        while (sem_wait(ready) < 0 && errno == EINTR) {}
        reply_to(&shared[0], "Shared-memory child");
        sem_post(ready);
        munmap(shared, sizeof(Message));
        sem_close(ready);
        _exit(0);
    }

    memset(shared, 0, sizeof(Message));
    snprintf(shared->text, sizeof(shared->text), "Password reset issue");
    if (sem_post(ready) < 0) {
        perror("sem_post");
    }
    while (sem_wait(ready) < 0 && errno == EINTR) {}
    printf("Shared memory reply: %s\n", shared->text);

    waitpid(child, NULL, 0);
    munmap(shared, sizeof(Message));
    sem_close(ready);
    sem_unlink(sem_name);
    close(shm_fd);
    shm_unlink(name);
    return 0;
}

static int demo_workers(void) {
    int request_fd[WORKER_COUNT][2];
    int reply_fd[WORKER_COUNT][2];
    pid_t children[WORKER_COUNT];

    for (int i = 0; i < WORKER_COUNT; i++) {
        if (pipe(request_fd[i]) < 0 || pipe(reply_fd[i]) < 0) {
            perror("pipe");
            return 1;
        }

        children[i] = fork();
        if (children[i] < 0) {
            perror("fork");
            return 1;
        }

        if (children[i] == 0) {
            close(request_fd[i][1]);
            close(reply_fd[i][0]);

            Message msg;
            memset(&msg, 0, sizeof(msg));
            if (read_exact(request_fd[i][0], &msg, sizeof(msg)) <= 0) {
                _exit(1);
            }

            char label[32];
            snprintf(label, sizeof(label), "Worker %d", i + 1);
            reply_to(&msg, label);
            if (write_exact(reply_fd[i][1], &msg, sizeof(msg)) < 0) {
                _exit(1);
            }

            close(request_fd[i][0]);
            close(reply_fd[i][1]);
            _exit(0);
        }

        close(request_fd[i][0]);
        close(reply_fd[i][1]);
    }

    for (int i = 0; i < WORKER_COUNT; i++) {
        Message send_msg;
        Message reply_msg;
        memset(&send_msg, 0, sizeof(send_msg));
        snprintf(send_msg.text, sizeof(send_msg.text), "Ticket %d request", i + 1);

        if (write_exact(request_fd[i][1], &send_msg, sizeof(send_msg)) < 0) {
            perror("worker write");
            return 1;
        }

        if (read_exact(reply_fd[i][0], &reply_msg, sizeof(reply_msg)) <= 0) {
            perror("worker read");
            return 1;
        }

        printf("Worker reply %d: %s\n", i + 1, reply_msg.text);
        close(request_fd[i][1]);
        close(reply_fd[i][0]);
    }

    for (int i = 0; i < WORKER_COUNT; i++) {
        waitpid(children[i], NULL, 0);
    }

    return 0;
}

static int demo_signal(void) {
    printf("Signal demo: SIGUSR1 is sent to the current process when a child worker completes a ticket job.\n");
    printf("This is a simplified example; the actual signal notification is not a full production IPC system.\n");
    raise(SIGUSR1);
    return 0;
}

static void usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [fifo|mq|shm|workers|signal]\n", program_name);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "fifo") == 0) {
        return demo_fifo();
    }
    if (strcmp(argv[1], "mq") == 0) {
        return demo_message_queue();
    }
    if (strcmp(argv[1], "shm") == 0) {
        return demo_shared_memory();
    }
    if (strcmp(argv[1], "workers") == 0) {
        return demo_workers();
    }
    if (strcmp(argv[1], "signal") == 0) {
        return demo_signal();
    }

    usage(argv[0]);
    return 1;
}

