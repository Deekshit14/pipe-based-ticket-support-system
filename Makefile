CC = gcc
CXX = g++
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -O2
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2
LDLIBS_IPC ?= -pthread -lrt
LDLIBS_CRYPTO ?= -lcrypto
GTEST_CFLAGS ?= $(shell pkg-config --cflags gtest_main 2>/dev/null)
GTEST_LIBS ?= $(shell pkg-config --libs gtest_main 2>/dev/null || echo '-lgtest_main -lgtest -pthread')
BUILD_DIR := build
COVERAGE_DIR := $(BUILD_DIR)/coverage

.PHONY: all clean run extensions test unit integration advanced-integration gtest cppcheck misra \
        coverage function_coverage branch_coverage valgrind helgrind

all: ticket_system ipc_extensions ipc_advanced

ticket_system: main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

ipc_extensions: ipc_extensions.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDLIBS_IPC)

ipc_advanced: ipc_advanced.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDLIBS_CRYPTO)

extensions: ipc_extensions ipc_advanced

run: ticket_system
	./ticket_system

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/test_ticket: tests/test_ticket.c main.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_ticket.c

$(BUILD_DIR)/integration_test: tests/integration_test.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/integration_test.c

unit: $(BUILD_DIR)/test_ticket
	(cd $(BUILD_DIR) && ./test_ticket)

integration: ticket_system $(BUILD_DIR)/integration_test
	./$(BUILD_DIR)/integration_test $(CURDIR)/ticket_system

advanced-integration: integration

# Requires GoogleTest development headers and libraries. pkg-config is used when available.
gtest: ticket_system | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(GTEST_CFLAGS) tests/test_gtest.cpp -o $(BUILD_DIR)/test_gtest $(GTEST_LIBS)
	TICKET_SYSTEM_BIN=$(CURDIR)/ticket_system ./$(BUILD_DIR)/test_gtest

# Run unit, integration, and GoogleTest suites. Coverage and Valgrind targets are separate.
test:
	$(MAKE) unit
	$(MAKE) integration
	$(MAKE) gtest

cppcheck:
	cppcheck --error-exitcode=1 --enable=warning,style,performance,portability \
		--std=c11 main.c ipc_extensions.c ipc_advanced.c tests/test_ticket.c tests/integration_test.c 2> cppcheck_report.txt

# Provide a cppcheck MISRA addon/configuration, e.g. MISRA_ADDON=misra.json.
# This is a rule check, not certification of MISRA compliance.
MISRA_ADDON ?=
misra:
	@test -n "$(MISRA_ADDON)" || (echo "Set MISRA_ADDON to a cppcheck MISRA addon/configuration" >&2; exit 2)
	cppcheck --error-exitcode=1 --enable=all --std=c11 --addon=$(MISRA_ADDON) \
		main.c ipc_extensions.c ipc_advanced.c tests/test_ticket.c tests/integration_test.c 2> misra_report.txt

# Instrument main.c and enforce the project's configured line/branch/function thresholds.
coverage: ticket_system $(BUILD_DIR)/test_ticket $(BUILD_DIR)/integration_test | $(COVERAGE_DIR)
	rm -f $(COVERAGE_DIR)/*.gcda $(COVERAGE_DIR)/*.gcno
	$(CC) $(CPPFLAGS) $(CFLAGS) -O0 -g --coverage -o $(COVERAGE_DIR)/ticket_system main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -O0 -g --coverage -o $(COVERAGE_DIR)/test_ticket tests/test_ticket.c
	(cd $(COVERAGE_DIR) && ./test_ticket)
	./$(BUILD_DIR)/integration_test $(CURDIR)/$(COVERAGE_DIR)/ticket_system
	@if command -v gcovr >/dev/null 2>&1; then \
		GCOVR=gcovr; \
	elif python3 -m gcovr --version >/dev/null 2>&1; then \
		GCOVR="python3 -m gcovr"; \
	else \
		echo "gcovr is required. Install it with: python3 -m pip install --user gcovr" >&2; exit 127; \
	fi; \
	$$GCOVR --root $(CURDIR) --object-directory $(CURDIR)/$(COVERAGE_DIR) \
		--filter '.*main[.]c$$' --txt --output $(COVERAGE_DIR)/coverage_report.txt \
		--fail-under-line 95 --fail-under-branch 95 --fail-under-function 95

function_coverage branch_coverage: coverage

$(COVERAGE_DIR): | $(BUILD_DIR)
	mkdir -p $@

valgrind: ticket_system
	@command -v valgrind >/dev/null 2>&1 || { echo "valgrind is required for this target." >&2; exit 127; }
	@tmp=$$(mktemp -d); \
	(cd "$$tmp" && printf '%s\n' '/new Valgrind|memory check|1' '/process' '/quit' | \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(CURDIR)/ticket_system); \
	status=$$?; rm -rf "$$tmp"; exit $$status

# Helgrind instruments the process-based pipe workflow; it is a useful diagnostic, not a thread-race proof.
helgrind: ticket_system
	@command -v valgrind >/dev/null 2>&1 || { echo "Valgrind/Helgrind is required for this target." >&2; exit 127; }
	@tmp=$$(mktemp -d); \
	(cd "$$tmp" && printf '%s\n' '/new Helgrind|IPC check|1' '/process' '/quit' | \
		valgrind --tool=helgrind --error-exitcode=1 $(CURDIR)/ticket_system); \
	status=$$?; rm -rf "$$tmp"; exit $$status

clean:
	rm -f ticket_system ipc_extensions ipc_advanced cppcheck_report.txt misra_report.txt
	rm -f *.gcda *.gcno *.gcov ticket_log.txt chat_log.txt
	rm -rf $(BUILD_DIR)
