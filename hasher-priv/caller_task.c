#include <sys/socket.h>
#include <sys/un.h>
#include <sys/param.h>
#include <sys/wait.h>

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>

#include "communication.h"
#include "xmalloc.h"
#include "logging.h"
#include "sockets.h"
#include "priv.h"

struct task {
	uint64_t type;

	unsigned num;

	int stdin;
	int stdout;
	int stderr;

	char **argv;
	char **env;
};

static int
reopen_fd(int oldfd, int newfd)
{
	if (oldfd < 0)
		return 0;

	close(newfd);

	if (dup2(oldfd, newfd) < 0) {
		err("dup2: %m");
		return -1;
	}

	close(oldfd);

	return 0;
}

static int
reopen_iostreams(int stdin, int stdout, int stderr)
{
	return (reopen_fd(stdin, STDIN_FILENO) < 0 ||
	    reopen_fd(stdout, STDOUT_FILENO) < 0 ||
	    reopen_fd(stderr, STDERR_FILENO) < 0) ? -1 : 0;
}

static int
validate_arguments(task_t task, char **argv)
{
	int required_args = 0, more_args = 0;
	int rc = -1;
	int argc = 0;

	while (argv && argv[argc])
		argc++;

	switch (task) {
		case TASK_GETCONF:
		case TASK_KILLUID:
		case TASK_GETUGID1:
		case TASK_GETUGID2:
			required_args = 0;
			break;
		case TASK_MAKEDEV:
		case TASK_MAKETTY:
		case TASK_MAKECONSOLE:
		case TASK_UMOUNT:
			required_args = 1;
			break;
		case TASK_CHROOTUID1:
		case TASK_CHROOTUID2:
			more_args = 1;
		case TASK_MOUNT:
			required_args = 2;
			break;
		default:
			err("unknown task type: %lu", task);
			return rc;
	}

	if (argc < 0)
		err("number of arguments must be a positive");

	else if (argc < required_args)
		err("%s task requires at least %u arguments", task2str(task), required_args);

	else if (argc > required_args && !more_args)
		err("too many arguments for %s task", task2str(task));

	else
		rc = 0;

	return rc;
}

static int
process_task(struct task *task)
{
	int rc = EXIT_FAILURE;
	int i = 0;
	pid_t pid;

	if ((pid = fork()) != 0) {
		if (pid < 0) {
			err("fork: %m");
			return -1;
		}
		return pid;
	}

	if ((rc = reopen_iostreams(task->stdin, task->stdout, task->stderr)) < 0)
		exit(rc);

	/* cleanup environment to avoid side effects. */
	if (clearenv() != 0)
		fatal("clearenv: %m");

	while (task->env && task->env[i]) {
		if (putenv(task->env[i++]) != 0)
			fatal("putenv: %m");
	}

	/* First, check and sanitize file descriptors. */
	sanitize_fds();

	/* Second, parse task arguments. */
	parse_task_args(task->type, (const char **) task->argv);

	if (chroot_path && *chroot_path != '/')
		fatal("%s: invalid chroot path", chroot_path);

	/* Fourth, parse environment for config options. */
	parse_env();

	/* We don't need environment variables any longer. */
	if (clearenv() != 0)
		fatal("clearenv: %m");

	/* Finally, execute choosen task. */
	switch (task->type) {
		case TASK_GETCONF:
			rc = do_getconf();
			break;
		case TASK_KILLUID:
			rc = do_killuid();
			break;
		case TASK_GETUGID1:
			rc = do_getugid1();
			break;
		case TASK_CHROOTUID1:
			rc = do_chrootuid1();
			break;
		case TASK_GETUGID2:
			rc = do_getugid2();
			break;
		case TASK_CHROOTUID2:
			rc = do_chrootuid2();
			break;
		case TASK_MAKEDEV:
			rc = do_makedev();
			break;
		case TASK_MAKETTY:
			rc = do_maketty();
			break;
		case TASK_MAKECONSOLE:
			rc = do_makeconsole();
			break;
		case TASK_MOUNT:
			rc = do_mount();
			break;
		case TASK_UMOUNT:
			rc = do_umount();
			break;
		default:
			fatal("unknown task %d", task->type);
	}

	/* Write of all user-space buffered data */
	fflush(stdout);
	fflush(stderr);

	exit(rc);
}

int
caller_task(int conn)
{
	int fds[3];
	int rc = EXIT_FAILURE;
	struct task task = {};
	pid_t pid, cpid;

	if ((pid = fork()) != 0) {
		if (pid < 0) {
			err("fork: %m");
			return -1;
		}
		return 0;
	}

	while (1) {
		task_t type = TASK_NONE;
		struct cmd hdr = {};

		if ((rc = xrecvmsg(conn, &hdr, sizeof(hdr))) < 0)
			goto answer;

		switch (hdr.type) {
			case CMD_TASK_BEGIN:
				if (hdr.datalen != sizeof(type))
					goto answer;

				if ((rc = xrecvmsg(conn, &type, hdr.datalen)) < 0)
					goto answer;

				task.type = type;

				break;

			case CMD_TASK_FDS:
				if (hdr.datalen != sizeof(int) * 3)
					goto answer;

				if (task.stdin)
					close(task.stdin);

				if (task.stdout)
					close(task.stdout);

				if (task.stderr)
					close(task.stderr);

				if ((rc = recv_fds(conn, hdr.datalen, fds)) < 0)
					goto answer;

				task.stdin  = fds[0];
				task.stdout = fds[1];
				task.stderr = fds[2];

				break;

			case CMD_TASK_ARGUMENTS:
				if (task.argv) {
					free(task.argv[0]);
					free(task.argv);
				}

				if ((rc = recv_list(conn, hdr.datalen, &task.argv)) < 0)
					goto answer;

				if (validate_arguments(task.type, task.argv) < 0)
					goto answer;

				break;

			case CMD_TASK_ENVIRON:
				if (task.env) {
					free(task.env[0]);
					free(task.env);
				}

				if ((rc = recv_list(conn, hdr.datalen, &task.env)) < 0)
					goto answer;

				break;

			case CMD_TASK_RUN:
				if ((cpid = process_task(&task)) < 0)
					goto answer;

				goto wait;

			default:
				err("unsupported command: %d", hdr.type);
		}

		send_command_response(conn, CMD_STATUS_DONE, NULL);
	}
wait:
	while (1) {
		pid_t w;
		int wstatus;

		if ((w = waitpid(cpid, &wstatus, WUNTRACED | WCONTINUED)) < 0) {
			err("waitpid: %m");
			break;
		}

		if (WIFEXITED(wstatus)) {
			rc = WEXITSTATUS(wstatus);
			info("%s: process %d exited, status=%d", task2str(task.type), cpid, WEXITSTATUS(wstatus));
			break;
		}

		if (WIFSIGNALED(wstatus)) {
			info("%s: process %d killed by signal %d", task2str(task.type), cpid, WTERMSIG(wstatus));
			break;
		}
	}
answer:
	if (task.env) {
		free(task.env[0]);
		free(task.env);
	}

	if (task.argv) {
		free(task.argv[0]);
		free(task.argv);
	}

	/* Notify client about result */
	(rc == EXIT_FAILURE)
		? send_command_response(conn, CMD_STATUS_FAILED, "command failed")
		: send_command_response(conn, CMD_STATUS_DONE, NULL);

	exit(rc);
}
