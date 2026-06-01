#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int filson_echo(char **args);

#define TEST_PASS(name) printf("[PASS] %s\n", name)
#define TEST_FAIL(name, msg) printf("[FAIL] %s: %s\n", name, msg)

int main() {
    char *args[] = {"echo", NULL};
    int stdout_backup, temp_file, result;
    FILE *f;
    char buffer[256];

    stdout_backup = dup(1);
    temp_file = open("/tmp/echo_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(temp_file, 1);
    result = filson_echo(args);
    dup2(stdout_backup, 1);
    close(temp_file);
    close(stdout_backup);
    f = fopen("/tmp/echo_output.txt", "r");
    fgets(buffer, sizeof(buffer), f);
    fclose(f);
    if (result == 1 && (buffer[0] == '\n' || buffer[0] == '\0')) {
        TEST_PASS("echo_no_args");
    } else {
        TEST_FAIL("echo_no_args", "Echo with no args should print newline");
        printf("DEBUG: result=%d, buffer[0]=%d\n", result, (int)buffer[0]);
    }
    unlink("/tmp/echo_output.txt");
    
    return 0;
}
