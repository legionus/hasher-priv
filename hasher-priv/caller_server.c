#include <sys/param.h> /* MAXPATHLEN */
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/wait.h>

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <grp.h>

#include "priv.h"
#include "xmalloc.h"
#include "sockets.h"
#include "logging.h"
#include "epoll.h"

static int finish_server = 0;
static char socketpath[MAXPATHLEN];

static int
handle_signal(uint32_t signo)
{
	int status;

	switch (signo) {
		case SIGINT:
		case SIGTERM:
			finish_server = 1;
			break;

		case SIGCHLD:
			if (waitpid(-1, &status, 0) < 0) {
				err("waitpid: %m");
				return -1;
			}
			break;

		case SIGHUP:
			break;
	}
	return 0;
}

static int
caller_server(int cl_conn, uid_t uid, gid_t gid)
{
	int i;
	unsigned long nsec;
	sigset_t mask;
	char *sockname;

	int fd_ep     = -1;
	int fd_signal = -1;
	int fd_conn   = -1;

	if (init_caller_data(uid, gid) < 0)
		return -1;

	info("%s(%d): start session server", caller_user, caller_uid);

	umask(077);

	xasprintf(&sockname, "hasher-priv-%d", caller_uid);

	if ((fd_conn = unix_listen(SOCKETDIR, sockname)) < 0)
		return -1;

	snprintf(socketpath, sizeof(socketpath), "%s/%s", SOCKETDIR, sockname);

	if (chown(socketpath, caller_uid, caller_gid)) {
		err("fchown: %s: %m", socketpath);
		return -1;
	}
	free(sockname);

	sigfillset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	if ((fd_signal = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC)) < 0)
		fatal("signalfd: %m");

	if ((fd_ep = epoll_create1(EPOLL_CLOEXEC)) < 0) {
		err("epoll_create1: %m");
		return -1;
	}

	if (epollin_add(fd_ep, fd_signal) < 0 || epollin_add(fd_ep, fd_conn) < 0) {
		err("epollin_add: failed");
		return -1;
	}

	/* Tell client that caller server is ready */
	send_answer(cl_conn, 0);
	close(cl_conn);

	nsec = 0;
	while (!finish_server) {
		struct epoll_event ev[42];
		int fdcount;

		errno = 0;
		if ((fdcount = epoll_wait(fd_ep, ev, ARRAY_SIZE(ev), 1000)) < 0) {
			if (errno == EINTR)
				continue;
			err("epoll_wait: %m");
			break;
		}

		if (fdcount == 0) {
			nsec++;

			if (nsec >= server_session_timeout)
				break;

		} else for (i = 0; i < fdcount; i++) {
			if (!(ev[i].events & EPOLLIN)) {
				continue;

			} else if (ev[i].data.fd == fd_signal) {
				struct signalfd_siginfo fdsi;
				ssize_t size;

				size = TEMP_FAILURE_RETRY(read(fd_signal, &fdsi, sizeof(struct signalfd_siginfo)));
				if (size != sizeof(struct signalfd_siginfo)) {
					err("unable to read signal info");
					continue;
				}

				handle_signal(fdsi.ssi_signo);

			} else if (ev[i].data.fd == fd_conn) {
				int conn;

				if ((conn = accept4(fd_conn, NULL, 0, SOCK_CLOEXEC)) < 0) {
					err("accept4: %m");
					continue;
				}

				caller_task(conn);

				close(conn);

				/* reset timer */
				nsec = 0;
			}
		}
	}

	if (fd_ep >= 0) {
		epollin_remove(fd_ep, fd_signal);
		epollin_remove(fd_ep, fd_conn);
		close(fd_ep);
	}

	info("%s(%d): finish session server", caller_user, caller_uid);

	return 0;
}

pid_t
fork_server(int cl_conn, uid_t uid, gid_t gid)
{
	int rc;
	pid_t pid;

	if ((pid = fork()) != 0) {
		if (pid < 0) {
			err("fork: %m");
			return -1;
		}
		return pid;
	}

	if ((rc = caller_server(cl_conn, uid, gid)) < 0) {
		send_answer(cl_conn, rc);
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}