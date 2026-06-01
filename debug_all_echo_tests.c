#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int filson_echo(char **args);

void test_echo(const char *name, char **args, const char *env_var, const char *env_val, const char *expected) {
    int stdout_backup, temp_file;
    FILE *f;
    char buffer[256];
    
    if (env_var && env_val) {
        setenv(env_var, env_val, 1);
    }
    
    stdout_backup = dup(1);
    temp_file = open("/tmp/test_echo.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(temp_file, 1);
    
    int result = filson_echo(args);
    
    fflush(stdout);
    dup2(stdout_backup, 1);
    close(temp_file);
    close(stdout_backup);
    
    f = fopen("/tmp/test_echo.txt", "r");
    memset(buffer, 0, sizeof(buffer));
    if (f) {
        fgets(buffer, sizeof(buffer), f);
        fclose(f);
    }
    
    printf("%s: buffer=[%s] expected=[%s] found=%s\n", 
           name, buffer, expected, strstr(buffer, expected) ? "YES" : "NO");
    unlink("/tmp/test_echo.txt");
}

int main() {
    char *args1[] = {"echo", "hello", "world", NULL};
    test_echo("literal", args1, NULL, NULL, "hello world");
    
    char *args2[] = {"echo", "$ECHO_TEST", NULL};
    test_echo("var_exp", args2, "ECHO_TEST", "expanded_value", "expanded_value");
    
    char *args3[] = {"echo", "Hello", "$USER_NAME", "welcome", NULL};
    test_echo("mixed", args3, "USER_NAME", "testuser", "Hello");
    
    char *args4[] = {"echo", NULL};
    test_echo("no_args", args4, NULL, NULL, "");
    
    return 0;
}
