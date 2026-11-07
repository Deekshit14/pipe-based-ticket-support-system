#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEXT_SIZE 256
#define WORKERS 3

typedef struct { char text[TEXT_SIZE]; } Message;
typedef struct { sem_t request_ready; sem_t response_ready; Message message; } SharedChat;

static int read_full(int fd, void *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, (char *)buf + done, len - done);
        if (n == 0) return 0;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        done += (size_t)n;
    }
    return 1;
}
static int write_full(int fd, const void *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, (const char *)buf + done, len - done);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        done += (size_t)n;
    }
    return 0;
}
static void answer(Message *m, const char *who) {
    char input[TEXT_SIZE];
    snprintf(input, sizeof(input), "%s", m->text);
    snprintf(m->text, sizeof(m->text), "%s handled: %.220s", who, input);
}
static int get_message(Message *m) {
    printf("Enter a message: "); fflush(stdout);
    if (!fgets(m->text, sizeof(m->text), stdin)) return 0;
    m->text[strcspn(m->text, "\r\n")] = '\0';
    return 1;
}

static int demo_fifo(void) {
    char in_path[96], out_path[96];
    snprintf(in_path, sizeof(in_path), "/tmp/ticket-chat-%ld-in", (long)getpid());
    snprintf(out_path, sizeof(out_path), "/tmp/ticket-chat-%ld-out", (long)getpid());
    if (mkfifo(in_path, 0600) < 0 || mkfifo(out_path, 0600) < 0) { perror("mkfifo"); unlink(in_path); unlink(out_path); return 1; }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); unlink(in_path); unlink(out_path); return 1; }
    if (pid == 0) {
        int r = open(in_path, O_RDONLY), w = open(out_path, O_WRONLY);
        Message m;
        if (r < 0 || w < 0 || read_full(r, &m, sizeof(m)) != 1) _exit(1);
        answer(&m, "FIFO support child");
        int ok = write_full(w, &m, sizeof(m)); close(r); close(w); _exit(ok ? 1 : 0);
    }
    int w = open(in_path, O_WRONLY), r = open(out_path, O_RDONLY), status = 0;
    Message m = {{0}};
    if (w < 0 || r < 0 || !get_message(&m) || write_full(w, &m, sizeof(m)) < 0 || read_full(r, &m, sizeof(m)) != 1) { perror("FIFO exchange"); status = 1; }
    else printf("Reply: %s\n", m.text);
    if (w >= 0) close(w);
    if (r >= 0) close(r);
    waitpid(pid, NULL, 0); unlink(in_path); unlink(out_path); return status;
}

static int demo_mq(void) {
    char request_name[64], response_name[64];
    snprintf(request_name, sizeof(request_name), "/ticket-req-%ld", (long)getpid());
    snprintf(response_name, sizeof(response_name), "/ticket-res-%ld", (long)getpid());
    struct mq_attr attr = {0}; attr.mq_maxmsg = 4; attr.mq_msgsize = sizeof(Message);
    mqd_t req = mq_open(request_name, O_CREAT | O_EXCL | O_RDWR, 0600, &attr);
    mqd_t res = mq_open(response_name, O_CREAT | O_EXCL | O_RDWR, 0600, &attr);
    if (req == (mqd_t)-1 || res == (mqd_t)-1) { perror("mq_open"); if (req != (mqd_t)-1) mq_close(req); if (res != (mqd_t)-1) mq_close(res); mq_unlink(request_name); mq_unlink(response_name); return 1; }
    pid_t pid = fork();
    if (pid == 0) { Message m; if (mq_receive(req, m.text, sizeof(m.text), NULL) < 0) _exit(1); m.text[TEXT_SIZE-1]='\0'; answer(&m, "Message-queue child"); _exit(mq_send(res, m.text, sizeof(m.text), 0) == 0 ? 0 : 1); }
    if (pid < 0) { perror("fork"); mq_close(req); mq_close(res); mq_unlink(request_name); mq_unlink(response_name); return 1; }
    Message m = {{0}}; int status = 0;
    if (!get_message(&m) || mq_send(req, m.text, sizeof(m.text), 0) < 0 || mq_receive(res, m.text, sizeof(m.text), NULL) < 0) { perror("message queue exchange"); status = 1; }
    else { m.text[TEXT_SIZE-1]='\0'; printf("Reply: %s\n", m.text); }
    waitpid(pid, NULL, 0); mq_close(req); mq_close(res); mq_unlink(request_name); mq_unlink(response_name); return status;
}

static int sem_wait_retry(sem_t *s) { while (sem_wait(s) < 0) { if (errno != EINTR) return -1; } return 0; }
static int demo_shm(void) {
    char name[64]; snprintf(name, sizeof(name), "/ticket-shm-%ld", (long)getpid());
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 || ftruncate(fd, sizeof(SharedChat)) < 0) { perror("shared memory setup"); if (fd >= 0) close(fd); shm_unlink(name); return 1; }
    SharedChat *shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) { perror("mmap"); close(fd); shm_unlink(name); return 1; }
    if (sem_init(&shared->request_ready, 1, 0) < 0 || sem_init(&shared->response_ready, 1, 0) < 0) { perror("sem_init"); munmap(shared, sizeof(*shared)); close(fd); shm_unlink(name); return 1; }
    pid_t pid = fork();
    if (pid == 0) { if (sem_wait_retry(&shared->request_ready) < 0) _exit(1); answer(&shared->message, "Shared-memory child"); sem_post(&shared->response_ready); _exit(0); }
    if (pid < 0) { perror("fork"); sem_destroy(&shared->request_ready); sem_destroy(&shared->response_ready); munmap(shared, sizeof(*shared)); close(fd); shm_unlink(name); return 1; }
    int status = 0;
    if (!get_message(&shared->message) || sem_post(&shared->request_ready) < 0 || sem_wait_retry(&shared->response_ready) < 0) { perror("shared-memory exchange"); status = 1; }
    else printf("Reply: %s\n", shared->message.text);
    waitpid(pid, NULL, 0); sem_destroy(&shared->request_ready); sem_destroy(&shared->response_ready); munmap(shared, sizeof(*shared)); close(fd); shm_unlink(name); return status;
}

static int demo_signal(void) {
    int p[2]; sigset_t set, old;
    sigemptyset(&set); sigaddset(&set, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &set, &old) < 0 || pipe(p) < 0) { perror("signal/pipe setup"); return 1; }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); close(p[0]); close(p[1]); sigprocmask(SIG_SETMASK, &old, NULL); return 1; }
    if (pid == 0) {
        close(p[0]); Message m = {{0}}; snprintf(m.text, sizeof(m.text), "Ticket status updated by child");
        int ok = write_full(p[1], &m, sizeof(m)); close(p[1]);
        if (ok == 0) kill(getppid(), SIGUSR1);
        _exit(ok == 0 ? 0 : 1);
    }
    close(p[1]);
    int sig = 0, status = 0;
    if (sigwait(&set, &sig) != 0) { fprintf(stderr, "sigwait failed\n"); status = 1; }
    else { Message m; if (read_full(p[0], &m, sizeof(m)) != 1) { perror("signal pipe read"); status = 1; } else printf("Signal notified reception: %s\n", m.text); }
    close(p[0]); waitpid(pid, NULL, 0); sigprocmask(SIG_SETMASK, &old, NULL); return status;
}

static int demo_workers(void) {
    for (int i = 0; i < WORKERS; i++) {
        int request[2], response[2];
        if (pipe(request) < 0 || pipe(response) < 0) { perror("worker pipe"); return 1; }
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); close(request[0]); close(request[1]); close(response[0]); close(response[1]); return 1; }
        if (pid == 0) {
            close(request[1]); close(response[0]); Message m;
            int ok = read_full(request[0], &m, sizeof(m));
            if (ok == 1) { char label[32]; snprintf(label, sizeof(label), "Worker %d", i + 1); answer(&m, label); ok = write_full(response[1], &m, sizeof(m)) == 0 ? 1 : -1; }
            close(request[0]); close(response[1]); _exit(ok == 1 ? 0 : 1);
        }
        close(request[0]); close(response[1]); Message m = {{0}}, reply;
        snprintf(m.text, sizeof(m.text), "Customer support ticket #%d", i + 1);
        int status = write_full(request[1], &m, sizeof(m)); close(request[1]);
        if (status == 0 && read_full(response[0], &reply, sizeof(reply)) == 1) printf("%s\n", reply.text); else status = -1;
        close(response[0]); waitpid(pid, NULL, 0); if (status < 0) return 1;
    }
    return 0;
}

static void usage(const char *p) {
    printf("Usage: %s fifo|mq|shm|signal|workers\n", p);
    printf("Demonstrations: named FIFO chat, POSIX message queues, shared memory + semaphores, signal notification, child workers.\n");
}
int main(int argc, char **argv) {
    if (argc != 2) { usage(argv[0]); return 1; }
    if (!strcmp(argv[1], "fifo")) return demo_fifo();
    if (!strcmp(argv[1], "mq")) return demo_mq();
    if (!strcmp(argv[1], "shm")) return demo_shm();
    if (!strcmp(argv[1], "signal")) return demo_signal();
    if (!strcmp(argv[1], "workers")) return demo_workers();
    usage(argv[0]); return 1;
}
