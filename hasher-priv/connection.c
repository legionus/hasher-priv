#include <sys/socket.h>
#include <sys/un.h>
#include <sys/param.h>
#include <sys/wait.h>

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>

#include "tasks.h"
#include "xmalloc.h"
#include "logging.h"
#include "sockets.h"
#include "connection.h"
#include "priv.h"

struct nettask {
	uint64_t type;

	unsigned num;

	uid_t uid;
	gid_t gid;

	int stdin;
	int stdout;
	int stderr;

	const char **argv;
	char **env;

	/* conatainer for client args */
	int argc;
	uint64_t argslen;
	char *args;

	/* conatainer for client envs */
	int envc;
	uint64_t envslen;
	char *envs;
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
recv_task_hdr(int conn, struct nettask *task)
{
	struct msghdr msg = {};
	struct iovec iov  = {};
	char *recv_buf;
	int required_args = 0, more_args = 0;

	ssize_t n;

	int rc = -1;

	task->stdin  = -1;
	task->stdout = -1;
	task->stderr = -1;

	recv_buf = xcalloc(1UL, sizeof(struct taskhdr));

	iov.iov_base = recv_buf;
	iov.iov_len  = sizeof(struct taskhdr);

	msg.msg_iov        = &iov;
	msg.msg_iovlen     = 1;
	msg.msg_controllen = CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(sizeof(int64_t) * 2);

	msg.msg_control = xcalloc(1UL, msg.msg_controllen);

	if ((n = TEMP_FAILURE_RETRY(recvmsg(conn, &msg, 0))) != (ssize_t) iov.iov_len) {
		if (n < 0) {
			err("recvmsg: %m");
		} else {
			if (n)
				err("recvmsg: expected size %u, got %u", (unsigned) iov.iov_len, (unsigned) n);
			else
				err("recvmsg: unexpected EOF");
		}
		goto out;
	}

	if (recv_credentials(&msg, NULL, &task->uid, &task->gid) < 0)
		goto out;

	if (recv_iostreams(&msg, &task->stdin, &task->stdout, &task->stderr) < 0)
		goto out;

	task->type    = ((struct taskhdr *)recv_buf)->type;
	task->argc    = ((struct taskhdr *)recv_buf)->argc;
	task->argslen = ((struct taskhdr *)recv_buf)->argslen;
	task->envc    = ((struct taskhdr *)recv_buf)->envc;
	task->envslen = ((struct taskhdr *)recv_buf)->envslen;
	task->num     = ((struct taskhdr *)recv_buf)->caller_num;

	switch (task->type) {
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
			err("unknown task type: %lu", task->type);
			goto out;
	}

	if (task->argc < 0)
		err("number of arguments must be a positive");

	else if (task->envc < 0)
		err("number of environment variables must be a positive");

	else if (task->argc < required_args)
		err("%s task requires at least %u arguments", task2str(task->type), required_args);

	else if (task->argc > required_args && !more_args)
		err("too many arguments for %s task", task2str(task->type));

	else if ((task->argslen + task->envslen) > _POSIX_ARG_MAX)
		err("too many arguments and environment variables");

	else
		rc = 0;
out:
	free(msg.msg_control);
	free(recv_buf);

	return rc;
}

static int
recv_data(int conn, uid_t want_uid, gid_t want_gid, char **data, uint64_t len)
{
	struct msghdr msg = {};
	struct iovec iov = {};
	uid_t uid = 0;
	gid_t gid = 0;

	ssize_t n;

	*data = xcalloc(1UL, len);

	iov.iov_base = *data;
	iov.iov_len  = len;

	msg.msg_name       = NULL;
	msg.msg_iov        = &iov;
	msg.msg_iovlen     = 1;
	msg.msg_controllen = CMSG_SPACE(sizeof(struct ucred));
	msg.msg_control    = xcalloc(1UL, msg.msg_controllen);

	if ((n = TEMP_FAILURE_RETRY(recvmsg(conn, &msg, 0))) != (ssize_t) iov.iov_len) {
		if (n < 0) {
			err("recvmsg: %m");
		} else {
			if (n)
				err("recvmsg: expected size %u, got %u", (unsigned) iov.iov_len, (unsigned) n);
			else
				err("recvmsg: unexpected EOF");
		}
		free(msg.msg_control);
		return -1;
	}

	if (recv_credentials(&msg, NULL, &uid, &gid) < 0) {
		free(msg.msg_control);
		return -1;
	}

	free(msg.msg_control);

	if (want_uid != uid || want_gid != gid) {
		err("task and arguments do not belong to same user");
		return -1;
	}

	return 0;
}

static int
recv_task_args(int conn, struct nettask *task)
{
	int i;
	uint64_t n = 0;

	if (recv_data(conn, task->uid, task->gid, &task->args, task->argslen) < 0)
		return -1;

	task->argv = xcalloc((size_t) task->argc + 1, sizeof(char *));

	for (i = 0; i < task->argc; i++) {
		if (task->argslen < n) {
			err("wrong arguments length. Not null-terminated string?");
			return -1;
		}
		task->argv[i] = task->args + n;
		n += strnlen(task->args + n, task->argslen - n + 1) + 1;
	}

	return 0;
}

static int
recv_task_envs(int conn, struct nettask *task)
{
	int i;
	uint64_t n = 0;

	if (recv_data(conn, task->uid, task->gid, &task->envs, task->envslen) < 0)
		return -1;

	task->env = xcalloc((size_t) task->envc + 1, sizeof(char *));

	for (i = 0; i < task->envc; i++) {
		if (task->envslen < n) {
			err("wrong environment variables length. Not null-terminated string?");
			return -1;
		}
		task->env[i] = task->envs + n;
		n += strnlen(task->envs + n, task->envslen - n + 1) + 1;
	}

	return 0;
}

static int
process_task(struct nettask *task)
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
	parse_task_args(task->type, task->argv);

	caller_num = task->num;

	if (chroot_path && *chroot_path != '/')
		fatal("%s: invalid chroot path", chroot_path);

	/* Third, initialize data related to caller. */
	init_caller_data(task->uid, task->gid);

	/* Fourth, parse environment for config options. */
	parse_env();

	/* We don't need environment variables any longer. */
	if (clearenv() != 0)
		fatal("clearenv: %m");

	/* Load config according to caller information. */
	configure();

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
handle_connection(int conn)
{
	int rc = EXIT_FAILURE;
	struct nettask task = {};
	pid_t pid, cpid;

	if ((pid = fork()) != 0) {
		if (pid < 0) {
			err("fork: %m");
			return -1;
		}
		return 0;
	}

	if (recv_task_hdr(conn, &task) < 0)
		goto end;

	if (task.argc > 0 && recv_task_args(conn, &task) < 0)
		goto end;

	if (task.envc > 0 && recv_task_envs(conn, &task) < 0)
		goto end;

	if ((cpid = process_task(&task)) < 0)
		goto end;

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
end:
	free(task.env);
	free(task.argv);
	free(task.args);
	free(task.envs);

	/* Notify client about result */
	send_answer(conn, rc);

	exit(rc);
}
