#include <sys/param.h> /* MAXPATHLEN */
#include <sys/socket.h> /* SOCK_CLOEXEC */
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <sys/capability.h>

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <grp.h>

#include "priv.h"
#include "xmalloc.h"
#include "sockets.h"
#include "logging.h"
#include "epoll.h"
#include "communication.h"

static int finish_server = 0;
static char socketpath[MAXPATHLEN];

static char session_caps[] = "cap_setgid,cap_setuid,cap_kill,cap_mknod,cap_sys_chroot,cap_sys_admin=ep";

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
process_task(int conn)
{
	uid_t uid;
	gid_t gid;

	if (get_peercred(conn, NULL, &uid, &gid) < 0)
		return -1;

	if (caller_uid != uid || caller_gid != gid) {
		err("user (uid=%d) don't have permission to send commands to the session of user (uid=%d)", uid, caller_uid);
		return -1;
	}

	caller_task(conn);

	return 0;
}

static int
drop_privs(void)
{
	cap_t caps;

	if (setgroups(0UL, 0) < 0) {
		err("setgroups: %m");
		return -1;
	}

	if (setgid(caller_gid) < 0) {
		err("setgid: %m");
		return -1;
	}

#ifdef ENABLE_SUPPLEMENTARY_GROUPS
	if (initgroups(caller_user, caller_gid) < 0) {
		err("initgroups: %s: %m", caller_user);
		return -1;
	}
#endif /* ENABLE_SUPPLEMENTARY_GROUPS */

	if(prctl(PR_SET_KEEPCAPS, 1) < 0) {
		err("prctl(PR_SET_KEEPCAPS): %m");
		return -1;
	}

	// Drop capabilities
	if ((caps = cap_from_text(session_caps)) == NULL) {
		err("cap_from_text: %m");
		return -1;
	}

	if (cap_set_proc(caps) < 0) {
		err("cap_set_proc: %m");
		return -1;
	}

	if (cap_free(caps) < 0) {
		err("cap_free: %m");
		return -1;
	}

	if (setreuid(caller_uid, caller_uid) < 0) {
		err("setreuid: %m");
		return -1;
	}

	if ((caps = cap_from_text(session_caps)) == NULL) {
		err("cap_from_text: %m");
		return -1;
	}

	if (cap_set_proc(caps) < 0) {
		err("cap_set_proc: %m");
		return -1;
	}

	if (cap_free(caps) < 0) {
		err("cap_free: %m");
		return -1;
	}

	if(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
		err("prctl(PR_SET_NO_NEW_PRIVS): %m");
		return -1;
	}

	return 0;
}

static void
set_rlimits(void)
{
	change_rlimit_t *p;

	for (p = change_rlimit; p->name; ++p)
	{
		struct rlimit rlim;

		if (!p->hard && !p->soft)
			continue;

		if (getrlimit(p->resource, &rlim) < 0)
			error(EXIT_FAILURE, errno, "getrlimit: %s", p->name);

		if (p->hard)
			rlim.rlim_max = *(p->hard);

		if (p->soft)
			rlim.rlim_cur = *(p->soft);

		if ((unsigned long) rlim.rlim_max <
		    (unsigned long) rlim.rlim_cur)
			rlim.rlim_cur = rlim.rlim_max;

		if (setrlimit(p->resource, &rlim) < 0)
			error(EXIT_FAILURE, errno, "setrlimit: %s", p->name);
	}
}

static int
caller_server(int cl_conn, uid_t uid, gid_t gid, unsigned num)
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

	caller_num = num;

	info("%s(%d) num=%u: start session server", caller_user, caller_uid, caller_num);

	umask(077);

	xasprintf(&sockname, "hasher-priv-%d-%d", caller_uid, caller_num);

	if ((fd_conn = unix_listen(SOCKETDIR, sockname)) < 0)
		return -1;

	snprintf(socketpath, sizeof(socketpath), "%s/%s", SOCKETDIR, sockname);

	if (chown(socketpath, caller_uid, caller_gid)) {
		err("fchown: %s: %m", socketpath);
		return -1;
	}
	free(sockname);

	if (drop_privs() < 0)
		return -1;

	/* Load config according to caller information. */
	configure();

	set_rlimits();

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
	send_command_response(cl_conn, CMD_STATUS_DONE, NULL);
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

				if (set_recv_timeout(conn, 3) < 0) {
					close(conn);
					continue;
				}

				if (!process_task(conn)) {
					/* reset timer */
					nsec = 0;
				}

				close(conn);
			}
		}
	}

	if (fd_ep >= 0) {
		epollin_remove(fd_ep, fd_signal);
		epollin_remove(fd_ep, fd_conn);
		close(fd_ep);
	}

	info("%s(%d): finish session server", caller_user, caller_uid);
	unlink(socketpath);

	return 0;
}

pid_t
fork_server(int cl_conn, uid_t uid, gid_t gid, unsigned num)
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

	if ((rc = caller_server(cl_conn, uid, gid, num)) < 0) {
		send_command_response(cl_conn, CMD_STATUS_FAILED, NULL);
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
