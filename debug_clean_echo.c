#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

int filson_echo(char **args);

int main() {
    char *args[] = {"echo", "hello", "world", NULL};
    FILE *f;
    char buffer[256];
    
    int pipefd[2];
    pipe(pipefd);
    
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], 1);
        close(pipefd[1]);
        filson_echo(args);
        exit(0);
    } else {
        close(pipefd[1]);
        memset(buffer, 0, sizeof(buffer));
        read(pipefd[0], buffer, sizeof(buffer) - 1);
        close(pipefd[0]);
        wait(NULL);
        printf("Output: [%s]\n", buffer);
        printf("Contains 'hello world': %s\n", strstr(buffer, "hello world") ? "YES" : "NO");
    }
    
    return 0;
}
