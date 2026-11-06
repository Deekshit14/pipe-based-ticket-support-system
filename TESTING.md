# Linux testing and code-quality targets

The project targets Linux/POSIX. Ticket data uses fixed-size arrays and a bounded FIFO queue—**no linked lists are used**. The production program remains C. Native C tests cover internal queue/command behavior, while the optional GoogleTest C++ suite exercises the executable as a process-level integration test.

## Make targets

- `make` — build the ticket system and IPC demonstrations.
- `make test` — run native C unit tests, Python process-level integration tests, and GoogleTest integration tests in sequence (also with `make -j`).
- `make unit` / `make integration` / `make gtest` — run one test suite. GoogleTest headers and libraries must be installed for `make gtest` and `make test`.
- `make cppcheck` — static analysis; writes diagnostics to `cppcheck_report.txt`.
- `make misra MISRA_ADDON=misra.json` — run the configured cppcheck MISRA addon and write diagnostics to `misra_report.txt`. Provide a valid addon/configuration for your cppcheck installation. This is not a claim of certified MISRA compliance.
- `make valgrind` — run a ticket create/process/quit scenario under Valgrind.
- `make helgrind` — run the same process workflow under Helgrind. Helgrind checks thread synchronization; the program's IPC is process-based, so this is not a substitute for dedicated process/IPC testing.
- `make coverage` — build instrumented tests, run unit and integration suites, produce `build/coverage/coverage_report.txt`, and fail unless `main.c` reaches at least 95% line, branch, and function coverage.
- `make function_coverage` / `make branch_coverage` — aliases for the coverage report and all three threshold checks.
- `make clean` — remove build products, reports, coverage files, and generated ticket logs.

## Linux dependencies

GCC, GNU Make, Python 3, and POSIX/Linux are required for the core application and base tests. Optional checks require `cppcheck`, Valgrind, `gcovr`, and a configured cppcheck MISRA addon. GoogleTest and a C++17 compiler are needed for the GoogleTest target. Install these using your Linux distribution's package manager or the upstream project instructions.

Coverage is a measured result, not guaranteed by the target's existence. If the threshold fails, add tests for uncovered behavior and rerun `make coverage`. No coverage, static-analysis, MISRA, Valgrind, or Helgrind pass is claimed until you run the corresponding target on your Linux server.
