#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int filson_echo(char **args);

int main() {
    char *args[] = {"echo", "Hello", "$USER_NAME", "welcome", NULL};
    int stdout_backup, temp_file;
    FILE *f;
    char buffer[256];
    
    setenv("USER_NAME", "testuser", 1);
    stdout_backup = dup(1);
    temp_file = open("/tmp/echo_test.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(temp_file, 1);
    
    filson_echo(args);
    
    dup2(stdout_backup, 1);
    close(temp_file);
    close(stdout_backup);
    
    f = fopen("/tmp/echo_test.txt", "r");
    if (f) {
        while (fgets(buffer, sizeof(buffer), f)) {
            printf("File content: [%s]", buffer);
        }
        fclose(f);
    }
    
    return 0;
}
