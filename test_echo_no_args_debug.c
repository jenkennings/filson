#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int filson_echo(char **args);

int main() {
    char *args[] = {"echo", NULL};
    int stdout_backup, temp_file, result;
    FILE *f;
    char buffer[256];

    memset(buffer, 0, sizeof(buffer));
    stdout_backup = dup(1);
    temp_file = open("/tmp/echo_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(temp_file, 1);
    result = filson_echo(args);
    dup2(stdout_backup, 1);
    close(temp_file);
    close(stdout_backup);
    
    f = fopen("/tmp/echo_output.txt", "r");
    int chars_read = fgets(buffer, sizeof(buffer), f) != NULL;
    fclose(f);
    
    printf("Result: %d\n", result);
    printf("Chars read: %d\n", chars_read);
    printf("Buffer[0]: %d (0x%02x)\n", buffer[0], (unsigned char)buffer[0]);
    printf("Buffer[1]: %d (0x%02x)\n", buffer[1], (unsigned char)buffer[1]);
    printf("strlen(buffer): %lu\n", strlen(buffer));
    printf("Test condition (buffer[0]=='\\n'): %s\n", (buffer[0] == '\n') ? "TRUE" : "FALSE");
    printf("Test passes: %s\n", (result == 1 && (buffer[0] == '\n' || buffer[0] == '\0')) ? "YES" : "NO");
    
    return 0;
}
