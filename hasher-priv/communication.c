#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "priv.h"
#include "logging.h"
#include "xmalloc.h"
#include "sockets.h"
#include "communication.h"

struct cmd_resp {
	int     status;
	ssize_t msglen;
};

#define taskbuflen 20
static char taskbuf[taskbuflen];

static const struct cm {
	const char *name;
	task_t value;
} taskmap[] = {
	{ "none",        TASK_NONE },
	{ "getconf",     TASK_GETCONF },
	{ "killuid",     TASK_KILLUID },
	{ "getugid1",    TASK_GETUGID1 },
	{ "getugid2",    TASK_GETUGID2 },
	{ "chrootuid1",  TASK_CHROOTUID1 },
	{ "chrootuid2",  TASK_CHROOTUID2 },
	{ "makedev",     TASK_MAKEDEV },
	{ "maketty",     TASK_MAKETTY },
	{ "makeconsole", TASK_MAKECONSOLE },
	{ "mount",       TASK_MOUNT },
	{ "umount",      TASK_UMOUNT }
};

static const size_t taskmap_size = ARRAY_SIZE(taskmap);

char *
task2str(task_t type)
{
	size_t i;

	for (i = 0; i < taskmap_size; i++) {
		if (taskmap[i].value == type)
			return strncpy(taskbuf, taskmap[i].name, taskbuflen);
	}
	return NULL;
}

task_t
str2task(char *s)
{
	size_t i;

	for (i = 0; i < taskmap_size; i++) {
		if (!strcmp(taskmap[i].name, s))
			return taskmap[i].value;
	}
	return TASK_NONE;
}

static int
send_list(int conn, cmd_t cmd, const char **argv)
{
	int i = 0;
	struct cmd hdr = {};

	hdr.type = cmd;

	while (argv && argv[i])
		hdr.datalen += strlen(argv[i++]) + 1;

	if (xsendmsg(conn, &hdr, sizeof(hdr)) < 0)
		return -1;

	i = 0;
	while (argv && argv[i]) {
		if (xsendmsg(conn, (char *) argv[i], strlen(argv[i]) + 1) < 0)
			return -1;
		i++;
	}

	return 0;
}

int
recv_list(int conn, uint64_t datalen, char ***argv)
{
	char *args = xcalloc(1UL, datalen);

	if (xrecvmsg(conn, args, datalen) < 0) {
		free(args);
		return -1;
	}

	if (!argv) {
		free(args);
		return 0;
	}

	size_t i = 0;
	uint64_t n = 0;
	char **av = NULL;

	while (args && n < datalen) {
		av = xrealloc(av, i + 1, sizeof(char *));
		av[i++] = args + n;
		n += strnlen(args + n, datalen - n) + 1;
	}

	av = xrealloc(av, (i + 1), sizeof(char *));
	av[i] = NULL;

	*argv = av;

	return 0;
}

int
recv_fds(int conn, uint64_t datalen, int *fds)
{
	ssize_t n;
	struct cmsghdr *cmsg;

	char dummy;
	struct msghdr msg = {};
	struct iovec iov  = {};

	union {
		struct cmsghdr cmh;
		char   control[CMSG_SPACE(datalen)];
	} control_un;

	iov.iov_base = &dummy;
	iov.iov_len  = sizeof(dummy);

	control_un.cmh.cmsg_len   = CMSG_LEN(datalen);
	control_un.cmh.cmsg_level = SOL_SOCKET;
	control_un.cmh.cmsg_type  = SCM_RIGHTS;

	msg.msg_name   = NULL;
	msg.msg_iov    = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control_un.control;
	msg.msg_controllen = sizeof(control_un.control);

	if ((n = TEMP_FAILURE_RETRY(recvmsg(conn, &msg, 0))) != (ssize_t) iov.iov_len) {
		if (n < 0)
			err("recvmsg: %m");
		else if (n)
			err("recvmsg: expected size %u, got %u", (unsigned) iov.iov_len, (unsigned) n);
		else
			err("recvmsg: unexpected EOF");
		return -1;
	}

	if (!msg.msg_controllen) {
		err("ancillary data not specified");
		return -1;
	}

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
			continue;

		if (cmsg->cmsg_len - CMSG_LEN(0) != datalen) {
			err("expected fd payload");
			return -1;
		}

		memcpy(fds, CMSG_DATA(cmsg), datalen);
		break;
	}

	return 0;
}

long int __attribute__((format(printf, 3, 4)))
send_command_response(int conn, int retcode, const char *fmt, ...)
{
	va_list ap;

	long int rc = 0;
	struct msghdr msg  = {};
	struct iovec iov   = {};
	struct cmd_resp rs = {};

	rs.status = retcode;

	if (fmt && *fmt) {
		va_start(ap, fmt);

		if ((rs.msglen = vsnprintf(NULL, 0, fmt, ap)) < 0) {
			err("unable to calculate message size");
			va_end(ap);
			return -1;
		}

		rs.msglen++;
		va_end(ap);
	}

	iov.iov_base = &rs;
	iov.iov_len  = sizeof(rs);

	msg.msg_iov    = &iov;
	msg.msg_iovlen = 1;

	errno = 0;
	if (TEMP_FAILURE_RETRY(sendmsg(conn, &msg, MSG_NOSIGNAL)) < 0) {
		/* The client left without waiting for an answer */
		if (errno == EPIPE)
			return 0;

		err("sendmsg: %m");
		return -1;
	}

	if (!rs.msglen)
		return 0;

	iov.iov_base = xmalloc((size_t) rs.msglen);
	iov.iov_len  = (size_t) rs.msglen;

	va_start(ap, fmt);
	vsnprintf(iov.iov_base, iov.iov_len, fmt, ap);
	va_end(ap);

	errno = 0;
	if (TEMP_FAILURE_RETRY(sendmsg(conn, &msg, MSG_NOSIGNAL)) < 0 && errno != EPIPE) {
		err("sendmsg: %m");
		rc = -1;
	}

	free(iov.iov_base);

	return rc;
}

static int
recv_command_response(int conn, cmd_status_t *retcode, char **m)
{
	struct cmd_resp rs = {};

	if (xrecvmsg(conn, &rs, sizeof(rs)) < 0)
		return -1;

	if (retcode)
		*retcode = rs.status;

	if (!m || rs.msglen <= 0)
		return 0;

	char *x = xcalloc(1UL, (size_t) rs.msglen);
	if (xrecvmsg(conn, x, (uint64_t) rs.msglen) < 0)
		return -1;

	*m = x;
	return 0;
}

int
server_command(int conn, cmd_t cmd, const char **args)
{
	cmd_status_t status;
	char *msg = NULL;

	if (send_list(conn, cmd, args) < 0)
		return -1;

	if (recv_command_response(conn, &status, &msg) < 0) {
		free(msg);
		return -1;
	}

	if (msg && *msg) {
		err("%s", msg);
		free(msg);
	}

	return status == CMD_STATUS_FAILED ? -1 : 0;
}

int
server_open_session(const char *dir_name, const char *file_name)
{
	int conn, rc;

	if ((conn = unix_connect(dir_name, file_name)) < 0)
		return -1;

	rc = server_command(conn, CMD_OPEN_SESSION, NULL);
	close(conn);

	return rc;
}

int
server_close_session(const char *dir_name, const char *file_name)
{
	int conn, rc;

	if ((conn = unix_connect(dir_name, file_name)) < 0)
		return -1;

	rc = server_command(conn, CMD_CLOSE_SESSION, NULL);
	close(conn);

	return rc;
}

int
server_task(int conn, task_t type, unsigned num)
{
	cmd_status_t status;
	char *msg = NULL;

	struct cmd hdr = {};
	struct taskhdr taskhdr = {};

	hdr.type    = CMD_TASK_BEGIN;
	hdr.datalen = sizeof(taskhdr);

	taskhdr.type = type;
	taskhdr.caller_num = num;

	if (xsendmsg(conn, &hdr, sizeof(hdr)) < 0)
		return -1;

	if (xsendmsg(conn, &taskhdr, hdr.datalen) < 0)
		return -1;

	if (recv_command_response(conn, &status, &msg) < 0) {
		free(msg);
		return -1;
	}

	if (msg && *msg) {
		err("%s", msg);
		free(msg);
	}

	return status == CMD_STATUS_FAILED ? -1 : 0;
}

int
server_task_fds(int conn)
{
	cmd_status_t status;
	char *msg = NULL;

	int myfds[3];
	struct cmd hdr = {};

	hdr.type    = CMD_TASK_FDS;
	hdr.datalen = sizeof(myfds);

	myfds[0] = STDIN_FILENO;
	myfds[1] = STDOUT_FILENO;
	myfds[2] = STDERR_FILENO;

	if (xsendmsg(conn, &hdr, sizeof(hdr)) < 0)
		return -1;

	if (fds_send(conn, myfds, 3) < 0)
		return -1;

	if (recv_command_response(conn, &status, &msg) < 0) {
		free(msg);
		return -1;
	}

	if (msg && *msg) {
		err("%s", msg);
		free(msg);
	}

	return status == CMD_STATUS_FAILED ? -1 : 0;
}
