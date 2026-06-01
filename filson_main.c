#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

void filson_loop(void);

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
			perror("filson");
			return EXIT_FAILURE;
		}
		if (dup2(fd, STDIN_FILENO) < 0) {
			perror("filson");
			close(fd);
			return EXIT_FAILURE;
		}
		close(fd);
	}
	filson_loop();
	return EXIT_SUCCESS;
}
