#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

std::string shell_quote(const std::string &value)
{
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

std::string run_application(const std::string &commands)
{
    const char *binary_env = std::getenv("TICKET_SYSTEM_BIN");
    if (binary_env == nullptr) {
        ADD_FAILURE() << "TICKET_SYSTEM_BIN was not set";
        return {};
    }

    static unsigned int test_number = 0;
    const auto temp_dir = std::filesystem::temp_directory_path() /
        ("ticket-system-gtest-" + std::to_string(static_cast<long long>(getpid())) +
         "-" + std::to_string(test_number++));
    std::filesystem::create_directories(temp_dir);
    const auto input_path = temp_dir / "input.txt";
    const auto output_path = temp_dir / "output.txt";
    {
        std::ofstream input(input_path);
        input << commands;
        if (!input) {
            ADD_FAILURE() << "Could not write test input";
            std::filesystem::remove_all(temp_dir);
            return {};
        }
    }

    const std::string command = "cd " + shell_quote(temp_dir.string()) + " && " +
        shell_quote(binary_env) + " < " + shell_quote(input_path.string()) +
        " > " + shell_quote(output_path.string()) + " 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream output_file(output_path);
    const std::string output((std::istreambuf_iterator<char>(output_file)),
                             std::istreambuf_iterator<char>());
    std::filesystem::remove_all(temp_dir);
    EXPECT_EQ(status, 0) << output;
    return output;
}

TEST(TicketSystemIntegration, CreatesAndProcessesTickets)
{
    const std::string output = run_application(
        "/new GTest customer|Test issue|1\n/process\n/quit\n");
    EXPECT_NE(output.find("Created and queued ticket #1"), std::string::npos);
    EXPECT_NE(output.find("status: Resolved"), std::string::npos);
}

TEST(TicketSystemIntegration, ReportsInvalidCommandAndHelp)
{
    const std::string output = run_application("/help\n/not-a-command\n/quit\n");
    EXPECT_NE(output.find("Commands: /new CUSTOMER"), std::string::npos);
    EXPECT_NE(output.find("Unknown command"), std::string::npos);
}

} // namespace
