#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ARRAY_SIZE 32

static int contains(const char *text, const char *needle)
{
    return strstr(text, needle) != NULL;
}

static int run_command_file(const char *binary, const char *input, char *output, size_t capacity)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s <<'EOF'\n%s\nEOF", binary, input);
    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL) return -1;
    size_t used = 0;
    while (fgets(output + used, (int)(capacity - used), pipe) != NULL) {
        used += strlen(output + used);
        if (used + 1 >= capacity) break;
    }
    int status = pclose(pipe);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int check_file_contains(const char *path, const char *needle)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) return 0;
    char buffer[1024];
    int found = 0;
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        if (contains(buffer, needle)) {
            found = 1;
            break;
        }
    }
    fclose(file);
    return found;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s ticket_system_bin ipc_advanced_bin\n", argv[0]);
        return 2;
    }

    int failures = 0;
    char output[2048];
    const char *ticket_input =
        "/help\n"
        "/new Alice|Cannot log in|1\n"
        "/new Bob|Payment issue|2\n"
        "/new Cara|Urgent outage|3\n"
        "/new Dan|Account locked|1\n"
        "/new Eve|Queue overflow case|1\n"
        "/stats\n"
        "/process\n"
        "/queue\n"
        "/process\n"
        "/process\n"
        "/list\n"
        "/stats\n"
        "/quit\n";

    int ticket_status = run_command_file(argv[1], ticket_input, output, sizeof(output));
    if (ticket_status != 0) {
        fprintf(stderr, "ticket_system failed with exit status %d\n", ticket_status);
        ++failures;
    }
    if (!contains(output, "Created and queued ticket #1 for Alice.")) {
        fprintf(stderr, "ticket_system did not create ticket #1\n");
        ++failures;
    }
    if (!contains(output, "status: Resolved") && !contains(output, "status: Escalated")) {
        fprintf(stderr, "ticket_system did not process a ticket\n");
        ++failures;
    }
    if (!check_file_contains("ticket_log.txt", "ticket created and queued")) {
        fprintf(stderr, "ticket_log.txt missing creation log\n");
        ++failures;
    }

    char room_output[2048];
    const char *room_input = "test-passphrase\nhello room\n/quit\n";
    int room_status = run_command_file("printf '%s'", room_input, room_output, sizeof(room_output));
    (void)room_status;

    char server_output[2048];
    const char *server_input = "/send 2 reset password\n/quit\n";
    int server_status = run_command_file(argv[2], server_input, server_output, sizeof(server_output));
    if (server_status != 0) {
        fprintf(stderr, "ipc_advanced server failed with exit status %d\n", server_status);
        ++failures;
    }
    if (!contains(server_output, "agent-2: reset password")) {
        fprintf(stderr, "ipc_advanced server did not answer correctly\n");
        ++failures;
    }
    if (!check_file_contains("chat_log.txt", "parent-to-child") ||
        !check_file_contains("chat_log.txt", "child-to-parent")) {
        fprintf(stderr, "chat_log.txt missing message entries\n");
        ++failures;
    }

    if (failures != 0) {
        fprintf(stderr, "%d C integration check(s) failed.\n", failures);
        return 1;
    }

    puts("C integration tests passed.");
    return 0;
}
