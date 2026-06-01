#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int filson_echo(char **args);

int main() {
    char *args[] = {"echo", "hello", "world", NULL};
    int stdout_backup, temp_file;
    FILE *f;
    char buffer[256];
    
    printf("TEST 1: Direct echo\n");
    filson_echo(args);
    
    printf("\nTEST 2: Redirected echo\n");
    stdout_backup = dup(1);
    temp_file = open("/tmp/test_echo.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(temp_file, 1);
    
    filson_echo(args);
    
    fflush(stdout);
    dup2(stdout_backup, 1);
    close(temp_file);
    close(stdout_backup);
    
    f = fopen("/tmp/test_echo.txt", "r");
    if (f) {
        memset(buffer, 0, sizeof(buffer));
        fgets(buffer, sizeof(buffer), f);
        printf("File content: [%s]\n", buffer);
        printf("Contains 'hello world': %s\n", strstr(buffer, "hello world") ? "YES" : "NO");
        fclose(f);
    }
    
    return 0;
}
