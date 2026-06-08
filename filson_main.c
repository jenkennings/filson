#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include "shell_session.h"
#include "pipelines.h"

int
main(int argc, char **argv)
{
	int fd;
	char *sh_path;

	sh_path = argv[0];
	if (sh_path != NULL && sh_path[0] != '\0') {
		setenv("SH", sh_path, 1);
		setenv("LINENO", "0", 0);
	}
	if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
		filson_execute_and_chain(argv[2]);
		return 0;
	}
	if (argc > 2) {
		fprintf(stderr, "usage: filson [-c command] [script_file]\n");
		return EXIT_FAILURE;
	}
	if (argc == 2) {
		setenv("FILSON_SCRIPT_PATH", argv[1], 1);
		fd = open(argv[1], O_RDONLY);
		if (fd < 0) {
			err(1, "%s", argv[1]);
		}
		if (dup2(fd, STDIN_FILENO) < 0) {
			err(1, "dup2");
		}
		close(fd);
	}
	return filson_loop();
}
