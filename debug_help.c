#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int filson_help(char **args);

int main() {
    char *args[] = {"help", NULL};
    int stdout_backup, temp_file, result;
    FILE *f;
    char buffer[1024];
    int found_header, found_cd;

    stdout_backup = dup(1);
    temp_file = open("/tmp/help_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(temp_file, 1);
    result = filson_help(args);
    dup2(stdout_backup, 1);
    close(temp_file);
    close(stdout_backup);
    
    f = fopen("/tmp/help_output.txt", "r");
    found_header = 0;
    found_cd = 0;
    printf("File content:\n");
    printf("==============\n");
    while (fgets(buffer, sizeof(buffer), f)) {
        printf("%s", buffer);
        if (strstr(buffer, "Filson") != NULL)
            found_header = 1;
        if (strstr(buffer, "cd") != NULL)
            found_cd = 1;
    }
    printf("==============\n");
    fclose(f);
    
    printf("Result: %d\n", result);
    printf("Found 'Filson': %s\n", found_header ? "YES" : "NO");
    printf("Found 'cd': %s\n", found_cd ? "YES" : "NO");
    printf("Test passes: %s\n", (result == 1 && found_header && found_cd) ? "YES" : "NO");
    
    return 0;
}
