#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include "shell_session.h"

int
main(int argc, char **argv)
{
	int fd;

	if (argc > 2) {
		fprintf(stderr, "usage: filson [script_file]\n");
		return EXIT_FAILURE;
	}
	if (argc == 2) {
		fd = open(argv[1], O_RDONLY);
		if (fd < 0) {
			err(1, "%s", argv[1]);
		}
		if (dup2(fd, STDIN_FILENO) < 0) {
			err(1, "dup2");
		}
		close(fd);
	}
	filson_loop();
	return EXIT_SUCCESS;
}
