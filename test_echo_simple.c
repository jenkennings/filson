#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int filson_echo(char **args);

int main() {
    char *args[] = {"echo", "Hello", "$USER_NAME", "welcome", NULL};
    setenv("USER_NAME", "testuser", 1);
    
    printf("Before echo:\n");
    filson_echo(args);
    printf("\nTest complete\n");
    
    return 0;
}
