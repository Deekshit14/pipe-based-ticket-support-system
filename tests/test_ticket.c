#include <stdio.h>
#include <string.h>

#define main ticket_system_program_main
#include "../main.c"
#undef main

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void test_queue_fifo_and_capacity(void)
{
    SupportDesk desk = {0};
    int id = -1;

    CHECK(dequeue(&desk, &id) == 0);
    CHECK(enqueue(&desk, 11) == 1);
    CHECK(enqueue(&desk, 12) == 1);
    CHECK(enqueue(&desk, 13) == 1);
    CHECK(enqueue(&desk, 14) == 1);
    CHECK(enqueue(&desk, 15) == 0);
    CHECK(dequeue(&desk, &id) == 1 && id == 11);
    CHECK(enqueue(&desk, 15) == 1);
    CHECK(dequeue(&desk, &id) == 1 && id == 12);
    CHECK(dequeue(&desk, &id) == 1 && id == 13);
    CHECK(dequeue(&desk, &id) == 1 && id == 14);
    CHECK(dequeue(&desk, &id) == 1 && id == 15);
    CHECK(dequeue(&desk, &id) == 0);
}

static void test_commands_and_ticket_workflow(void)
{
    SupportDesk desk = {0};
    char out[MESSAGE_SIZE];
    desk.next_id = 1;

    handle_command(&desk, "/help", out, sizeof(out));
    CHECK(strstr(out, "/new CUSTOMER") != NULL);
    handle_command(&desk, "/new invalid", out, sizeof(out));
    CHECK(strstr(out, "Usage:") != NULL);
    CHECK(desk.count == 0);

    handle_command(&desk, "/new Alice|Cannot log in|3", out, sizeof(out));
    CHECK(strstr(out, "Created and queued ticket #1") != NULL);
    CHECK(desk.count == 1 && desk.queued == 1);
    handle_command(&desk, "/stats", out, sizeof(out));
    CHECK(strstr(out, "total=1 queued=1/4") != NULL);
    handle_command(&desk, "/process", out, sizeof(out));
    CHECK(strstr(out, "status: Escalated") != NULL);

    handle_command(&desk, "/list", out, sizeof(out));
    CHECK(strstr(out, "Alice") != NULL && strstr(out, "Escalated") != NULL);
    handle_command(&desk, "/queue", out, sizeof(out));
    CHECK(strstr(out, "Moved 0 waiting") != NULL);
    handle_command(&desk, "/process", out, sizeof(out));
    CHECK(strstr(out, "Queue is empty") != NULL);
    handle_command(&desk, "/quit", out, sizeof(out));
    CHECK(strstr(out, "shutting down") != NULL);
    handle_command(&desk, "/unknown", out, sizeof(out));
    CHECK(strstr(out, "Unknown command") != NULL);
}

static void test_command_boundaries(void)
{
    SupportDesk desk = {0};
    char out[MESSAGE_SIZE];
    char small[16] = {0};
    char tiny[4] = {0};
    desk.next_id = 1;

    handle_command(&desk, "/list", out, sizeof(out));
    CHECK(strstr(out, "No tickets yet.") != NULL);
    desk.count = 1;
    ticket_list(&desk, small, sizeof(small));
    CHECK(small[0] == 'T');
    ticket_list(&desk, tiny, sizeof(tiny));
    CHECK(tiny[0] == 'T');
    desk.count = 0;
    ticket_list(&desk, tiny, sizeof(tiny));
    CHECK(tiny[0] == 'T');

    handle_command(&desk, "/new |Issue|1", out, sizeof(out));
    CHECK(strstr(out, "Usage:") != NULL);
    handle_command(&desk, "/new Alice||1", out, sizeof(out));
    CHECK(strstr(out, "Usage:") != NULL);
    handle_command(&desk, "/new Alice|Issue|0", out, sizeof(out));
    CHECK(strstr(out, "Usage:") != NULL);
    handle_command(&desk, "/new Alice|Issue|4", out, sizeof(out));
    CHECK(strstr(out, "Usage:") != NULL);
    handle_command(&desk, "/new Alice|Issue|1 extra", out, sizeof(out));
    CHECK(strstr(out, "Usage:") != NULL);

    desk.count = MAX_TICKETS;
    handle_command(&desk, "/new Alice|Issue|1", out, sizeof(out));
    CHECK(strstr(out, "Ticket array is full") != NULL);

    memset(&desk, 0, sizeof(desk));
    desk.next_id = 1;
    handle_command(&desk, "/new Alice|Issue|1", out, sizeof(out));
    handle_command(&desk, "/new Bob|Issue|1", out, sizeof(out));
    handle_command(&desk, "/new Cara|Issue|1", out, sizeof(out));
    handle_command(&desk, "/new Dan|Issue|1", out, sizeof(out));
    handle_command(&desk, "/new Eve|Issue|1", out, sizeof(out));
    CHECK(desk.queued == QUEUE_CAPACITY);
    handle_command(&desk, "/queue", out, sizeof(out));
    CHECK(strstr(out, "Moved 0 waiting") != NULL);
    handle_command(&desk, "/process", out, sizeof(out));
    handle_command(&desk, "/queue", out, sizeof(out));
    CHECK(strstr(out, "Moved 1 waiting") != NULL);

    memset(&desk, 0, sizeof(desk));
    desk.queued = 1;
    desk.ids[0] = 99;
    handle_command(&desk, "/process", out, sizeof(out));
    CHECK(strstr(out, "was not found") != NULL);

    memset(&desk, 0, sizeof(desk));
    desk.count = 3;
    (void)snprintf(desk.tickets[0].status, sizeof(desk.tickets[0].status), "Resolved");
    (void)snprintf(desk.tickets[1].status, sizeof(desk.tickets[1].status), "Escalated");
    (void)snprintf(desk.tickets[2].status, sizeof(desk.tickets[2].status), "Waiting");
    handle_command(&desk, "/stats", out, sizeof(out));
    CHECK(strstr(out, "resolved=1 escalated=1 waiting=1") != NULL);
}

static void test_io_boundaries(void)
{
    int channel[2];
    char byte;
    if (pipe(channel) < 0) {
        CHECK(0);
        return;
    }
    (void)close(channel[1]);
    CHECK(read_full(channel[0], &byte, sizeof(byte)) == 0);
    (void)close(channel[0]);
    CHECK(read_full(-1, &byte, sizeof(byte)) == -1);
    CHECK(write_full(-1, &byte, sizeof(byte)) == -1);
}

static void test_child_response_failure(void)
{
    int request_pipe[2] = {-1, -1}, response_pipe[2] = {-1, -1};
    struct sigaction ignore_sigpipe, old_sigpipe;
    Message request = {{0}};
    int status = 0;
    if (pipe(request_pipe) < 0) {
        CHECK(0);
        return;
    }
    if (pipe(response_pipe) < 0) {
        CHECK(0);
        (void)close(request_pipe[0]);
        (void)close(request_pipe[1]);
        return;
    }

    memset(&ignore_sigpipe, 0, sizeof(ignore_sigpipe));
    ignore_sigpipe.sa_handler = SIG_IGN;
    sigemptyset(&ignore_sigpipe.sa_mask);
    if (sigaction(SIGPIPE, &ignore_sigpipe, &old_sigpipe) < 0) {
        CHECK(0);
        (void)close(request_pipe[0]);
        (void)close(request_pipe[1]);
        (void)close(response_pipe[0]);
        (void)close(response_pipe[1]);
        return;
    }
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        (void)close(request_pipe[1]);
        (void)close(response_pipe[0]);
        child_service(request_pipe[0], response_pipe[1]);
        _exit(EXIT_FAILURE);
    }
    if (child < 0) {
        (void)close(request_pipe[0]);
        (void)close(request_pipe[1]);
        (void)close(response_pipe[0]);
        (void)close(response_pipe[1]);
        (void)sigaction(SIGPIPE, &old_sigpipe, NULL);
        return;
    }

    (void)close(request_pipe[0]);
    (void)close(response_pipe[0]);
    (void)close(response_pipe[1]);
    (void)snprintf(request.text, sizeof(request.text), "/help");
    CHECK(write_full(request_pipe[1], &request, sizeof(request)) == 0);
    (void)close(request_pipe[1]);
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(sigaction(SIGPIPE, &old_sigpipe, NULL) == 0);
}

static void test_log_open_failure(void)
{
    char directory[] = "/tmp/ticket-log-test-XXXXXX";
    char out[MESSAGE_SIZE];
    SupportDesk desk = {0};
    int saved_directory = open(".", O_RDONLY);
    CHECK(saved_directory >= 0);
    if (saved_directory < 0 || mkdtemp(directory) == NULL) {
        if (saved_directory >= 0) (void)close(saved_directory);
        return;
    }

    CHECK(chdir(directory) == 0);
    CHECK(mkdir(LOG_FILE, 0700) == 0);
    desk.next_id = 1;
    handle_command(&desk, "/new Alice|Issue|1", out, sizeof(out));
    CHECK(strstr(out, "Created and queued") != NULL);
    CHECK(ticket_system_program_main() == EXIT_FAILURE);
    CHECK(rmdir(LOG_FILE) == 0);
    CHECK(fchdir(saved_directory) == 0);
    CHECK(rmdir(directory) == 0);
    (void)close(saved_directory);
}

static void run_ticket_system(const char *input, int send_interrupt)
{
    int input_pipe[2] = {-1, -1};
    if (pipe(input_pipe) < 0) {
        CHECK(0);
        return;
    }
    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);
    int null_fd = open("/dev/null", O_WRONLY);
    CHECK(saved_stdin >= 0 && saved_stdout >= 0 && null_fd >= 0);
    if (saved_stdin < 0 || saved_stdout < 0 || null_fd < 0) {
        if (saved_stdin >= 0) (void)close(saved_stdin);
        if (saved_stdout >= 0) (void)close(saved_stdout);
        if (null_fd >= 0) (void)close(null_fd);
        (void)close(input_pipe[0]);
        (void)close(input_pipe[1]);
        return;
    }

    if (input != NULL) {
        CHECK(write_full(input_pipe[1], input, strlen(input)) == 0);
    }
    (void)close(input_pipe[1]);
    CHECK(dup2(input_pipe[0], STDIN_FILENO) == STDIN_FILENO);
    CHECK(dup2(null_fd, STDOUT_FILENO) == STDOUT_FILENO);
    clearerr(stdin);
    clearerr(stdout);
    (void)close(input_pipe[0]);
    (void)close(null_fd);
    interrupted = 0;
    if (send_interrupt) on_sigint(SIGINT);
    CHECK(ticket_system_program_main() == EXIT_SUCCESS);
    (void)fflush(stdout);
    interrupted = 0;
    CHECK(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO);
    CHECK(dup2(saved_stdout, STDOUT_FILENO) == STDOUT_FILENO);
    (void)close(saved_stdin);
    (void)close(saved_stdout);
}

static void test_application_entry_paths(void)
{
    run_ticket_system("/help\n\n/quit\n", 0);
    run_ticket_system(NULL, 0);
    run_ticket_system(NULL, 1);
}

int main(void)
{
    test_queue_fifo_and_capacity();
    test_commands_and_ticket_workflow();
    test_command_boundaries();
    test_io_boundaries();
    test_child_response_failure();
    test_log_open_failure();
    test_application_entry_paths();
    if (failures != 0) {
        (void)fprintf(stderr, "%d unit check(s) failed\n", failures);
        return 1;
    }
    (void)puts("Native C unit tests passed.");
    return 0;
}
