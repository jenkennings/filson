CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L
LDFLAGS = 

# Targets
MAIN_EXEC = Filson
TEST_EXEC = test_builtins
TEST_ENV_EXEC = test_environment_variables
TEST_SEC_EXEC = test_security
TEST_PIPE_EXEC = test_pipelines

# Source files
MAIN_SRC = filson.c history.c jobcontrol.c pipelines.c filson_main.c
TEST_SRC = test_builtins.c
TEST_ENV_SRC = test_environment_variables.c
TEST_SEC_SRC = test_security.c
TEST_PIPE_SRC = test_pipelines.c

# Object files
MAIN_OBJ = filson.o
TEST_OBJ = test_builtins.o
TEST_ENV_OBJ = test_environment_variables.o

# Set default target
.DEFAULT_GOAL := $(MAIN_EXEC)

# Default target
.PHONY: all
all: $(MAIN_EXEC)

# Build main executable
$(MAIN_EXEC): $(MAIN_SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(MAIN_EXEC) $(MAIN_SRC)
	@echo "✓ Built $(MAIN_EXEC)"

# Build test_builtins
$(TEST_EXEC): $(TEST_SRC) filson.c history.c jobcontrol.c pipelines.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TEST_EXEC) $(TEST_SRC) filson.c history.c jobcontrol.c pipelines.c
	@echo "✓ Built $(TEST_EXEC)"

# Build test_environment_variables
$(TEST_ENV_EXEC): $(TEST_ENV_SRC) filson.c history.c jobcontrol.c pipelines.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TEST_ENV_EXEC) $(TEST_ENV_SRC) filson.c history.c jobcontrol.c pipelines.c
	@echo "✓ Built $(TEST_ENV_EXEC)"

# Build test_security
$(TEST_SEC_EXEC): $(TEST_SEC_SRC) filson.c history.c jobcontrol.c pipelines.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TEST_SEC_EXEC) $(TEST_SEC_SRC) filson.c history.c jobcontrol.c pipelines.c
	@echo "✓ Built $(TEST_SEC_EXEC)"

# Build test_pipelines
$(TEST_PIPE_EXEC): $(TEST_PIPE_SRC) filson.c history.c jobcontrol.c pipelines.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TEST_PIPE_EXEC) $(TEST_PIPE_SRC) filson.c history.c jobcontrol.c pipelines.c
	@echo "✓ Built $(TEST_PIPE_EXEC)"

# Run tests
.PHONY: test
test: $(TEST_EXEC) $(TEST_ENV_EXEC) $(TEST_SEC_EXEC) $(TEST_PIPE_EXEC)
	@echo "\n=== Running Built-in Tests ==="
	./$(TEST_EXEC)
	@echo "\n=== Running Environment Variable Tests ==="
	./$(TEST_ENV_EXEC)
	@echo "\n=== Running Security Tests ==="
	./$(TEST_SEC_EXEC)
	@echo "\n=== Running Pipelines Tests ==="
	./$(TEST_PIPE_EXEC)

# Run only built-in tests
.PHONY: test-builtins
test-builtins: $(TEST_EXEC)
	@echo "\n=== Running Built-in Tests ==="
	./$(TEST_EXEC)

# Run only environment variable tests
.PHONY: test-env
test-env: $(TEST_ENV_EXEC)
	@echo "\n=== Running Environment Variable Tests ==="
	./$(TEST_ENV_EXEC)

# Run only security tests
.PHONY: test-security
test-security: $(TEST_SEC_EXEC)
	@echo "\n=== Running Security Tests ==="
	./$(TEST_SEC_EXEC)

# Run only pipelines tests
.PHONY: test-pipelines
test-pipelines: $(TEST_PIPE_EXEC)
	@echo "\n=== Running Pipelines Tests ==="
	./$(TEST_PIPE_EXEC)

# Build and run main shell
.PHONY: run
run: $(MAIN_EXEC)
	@echo "Starting Filson Shell..."
	./$(MAIN_EXEC)

# Clean build artifacts
.PHONY: clean
clean:
	rm -f $(MAIN_EXEC) $(TEST_EXEC) $(TEST_ENV_EXEC) $(TEST_SEC_EXEC) $(TEST_PIPE_EXEC)
	rm -f *.o *.a *.so
	rm -f /tmp/echo_output.txt /tmp/help_output.txt
	@echo "✓ Cleaned build artifacts"

# Build everything
.PHONY: build
build: $(MAIN_EXEC) $(TEST_EXEC) $(TEST_ENV_EXEC)
	@echo "✓ All builds complete"

# Help target
.PHONY: help
help:
	@echo "Filson Shell - Makefile Targets"
	@echo "==============================="
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Build main executable (default)"
	@echo "  build            - Build all executables (main + tests)"
	@echo "  run              - Build and run Filson shell"
	@echo "  test             - Run all tests"
	@echo "  test-builtins    - Run built-in command tests only"
	@echo "  test-env         - Run environment variable tests only"
	@echo "  clean            - Remove all build artifacts"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make              - Build Filson"
	@echo "  make test         - Run all test suites"
	@echo "  make run          - Start the shell"
	@echo "  make clean test   - Clean then run tests"

# Phony targets that don't represent files
.PHONY: all test run build clean help test-builtins test-env test-security test-pipelines
