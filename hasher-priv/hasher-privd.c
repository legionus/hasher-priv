#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/stat.h> /* umask */
#include <sys/param.h> /* MAXPATHLEN */
#include <sys/wait.h>
#include <sys/socket.h>

#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "connection.h"
#include "epoll.h"
#include "logging.h"
#include "pidfile.h"
#include "tasks.h"
#include "sockets.h"
#include "priv.h"

unsigned caller_num;

static int
handle_signal(uint32_t signo)
{
	switch (signo) {
		case SIGINT:
		case SIGTERM:
			return 0;

		case SIGCHLD:
			while (1) {
				int status;

				errno = 0;
				if (waitpid(-1, &status, 0) < 0) {
					if (errno == ECHILD)
						break;
					err("waitpid: %m");
					return -1;
				}
			}
			break;

		case SIGHUP:
			break;
	}
	return 1;
}

int main(int argc, char **argv)
{
	int i;
	sigset_t mask;
	mode_t m;

	int ep_timeout = -1;

	int fd_ep     = -1;
	int fd_signal = -1;
	int fd_conn   = -1;

	int loglevel = -1;

	const char *pidfile = NULL;
	int daemonize = 1;

	char socketpath[MAXPATHLEN];

	struct option long_options[] = {
		{ "help", no_argument, 0, 'h' },
		{ "version", no_argument, 0, 'V' },
		{ "foreground", no_argument, 0, 'f' },
		{ "loglevel", required_argument, 0, 'l' },
		{ "pidfile", required_argument, 0, 'p' },
		{ 0, 0, 0, 0 }
	};

	while ((i = getopt_long(argc, argv, "hVfl:p:", long_options, NULL)) != -1) {
		switch (i) {
			case 'p':
				pidfile = optarg;
				break;
			case 'l':
				loglevel = logging_level(optarg);
				break;
			case 'f':
				daemonize = 0;
				break;
			case 'V':
				printf("%s %s\n", program_invocation_short_name, PROJECT_VERSION);
				return EXIT_SUCCESS;
			default:
			case 'h':
				printf("Usage: %s [options]\n"
				       " -p, --pidfile=FILE   pid file location;\n"
				       " -l, --loglevel=LVL   set logging level;\n"
				       " -f, --foreground     stay in the foreground;\n"
				       " -V, --version        print program version and exit;\n"
				       " -h, --help           show this text and exit.\n"
				       "\n",
				       program_invocation_short_name);
				return EXIT_SUCCESS;
		}
	}

	configure_server();

	if (!pidfile && server_pidfile && *server_pidfile)
		pidfile = server_pidfile;

	if (server_log_priority >= 0)
		loglevel = server_log_priority;

	if (loglevel < 0)
		loglevel = logging_level("info");

	umask(022);

	if (pidfile && check_pid(pidfile))
		error(EXIT_FAILURE, 0, "%s: already running",
		      program_invocation_short_name);

	if (daemonize && daemon(0, 0) < 0)
		error(EXIT_FAILURE, errno, "daemon");

	logging_init(loglevel);

	if (pidfile && write_pid(pidfile) == 0)
		return EXIT_FAILURE;

	sigfillset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	sigdelset(&mask, SIGABRT);
	sigdelset(&mask, SIGSEGV);

	if ((fd_ep = epoll_create1(EPOLL_CLOEXEC)) < 0)
		fatal("epoll_create1: %m");

	if ((fd_signal = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC)) < 0)
		fatal("signalfd: %m");

	m = umask(017);

	if ((fd_conn = unix_listen(SOCKETDIR, PROJECT)) < 0)
		return EXIT_FAILURE;

	umask(m);

	snprintf(socketpath, sizeof(socketpath), "%s/%s", SOCKETDIR, PROJECT);

	if (chown(socketpath, 0, server_gid))
		fatal("fchown: %s: %m", socketpath);

	if (set_passcred(fd_conn) < 0)
		return EXIT_FAILURE;

	if (epollin_add(fd_ep, fd_signal) < 0 || epollin_add(fd_ep, fd_conn) < 0)
		return EXIT_FAILURE;

	while (1) {
		struct epoll_event ev[42];
		int fdcount;
		ssize_t size;

		errno = 0;
		if ((fdcount = epoll_wait(fd_ep, ev, ARRAY_SIZE(ev), ep_timeout)) < 0) {
			if (errno == EINTR)
				continue;
			err("epoll_wait: %m");
			break;
		}

		for (i = 0; i < fdcount; i++) {
			if (!(ev[i].events & EPOLLIN)) {
				continue;

			} else if (ev[i].data.fd == fd_signal) {
				struct signalfd_siginfo fdsi;

				size = TEMP_FAILURE_RETRY(read(fd_signal, &fdsi, sizeof(struct signalfd_siginfo)));
				if (size != sizeof(struct signalfd_siginfo)) {
					err("unable to read signal info");
					continue;
				}

				if (handle_signal(fdsi.ssi_signo) < 1)
					goto out;

			} else if (ev[i].data.fd == fd_conn) {
				int conn;

				if ((conn = accept4(fd_conn, NULL, 0, SOCK_CLOEXEC)) < 0) {
					err("accept4: %m");
					continue;
				}

				handle_connection(conn);
				close(conn);
			}
		}
	}
out:
	if (fd_ep >= 0) {
		epollin_remove(fd_ep, fd_signal);
		epollin_remove(fd_ep, fd_conn);
		close(fd_ep);
	}

	if (pidfile)
		remove_pid(pidfile);

	logging_close();

	return EXIT_SUCCESS;
}
