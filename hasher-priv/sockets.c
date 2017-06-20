#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "logging.h"
#include "tasks.h"
#include "sockets.h"

/* This function may be executed with caller or child privileges. */

int unix_listen(const char *dir_name, const char *file_name)
{
	struct sockaddr_un sun;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	snprintf(sun.sun_path, sizeof sun.sun_path, "%s/%s", dir_name, file_name);

	if (unlink(sun.sun_path) && errno != ENOENT) {
		err("unlink: %s: %m", sun.sun_path);
		return -1;
	}

	if (mkdir(dir_name, 0700) && errno != EEXIST) {
		err("mkdir: %s: %m", dir_name);
		return -1;
	}

	int fd;

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0) {
		err("socket AF_UNIX: %m");
		return -1;
	}

	if (bind(fd, (struct sockaddr *)&sun, (socklen_t)sizeof sun)) {
		err("bind: %s: %m", sun.sun_path);
		(void)close(fd);
		return -1;
	}

	if (listen(fd, 16) < 0) {
		err("listen: %s: %m", sun.sun_path);
		(void)close(fd);
		return -1;
	}

	return fd;
}

int unix_connect(const char *dir_name, const char *file_name)
{
	struct sockaddr_un sun;
	int conn;

	if ((conn = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0) {
		err("socket AF_UNIX: %m");
		return -1;
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	snprintf(sun.sun_path, sizeof sun.sun_path, "%s/%s", dir_name, file_name);

	if (connect(conn, (const struct sockaddr *)&sun, sizeof(struct sockaddr_un)) < 0) {
		err("connect: %m");
		return -1;
	}

	return conn;
}

int set_passcred(int fd)
{
	int enable = 1;

	if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &enable, sizeof(enable)) < 0) {
		err("setsockopt(SO_PASSCRED): %m");
		return -1;
	}
	return 0;
}

int recv_iostreams(struct msghdr *msg, int *stdin, int *stdout, int *stderr)
{
	struct cmsghdr *cmsg;
	int *fds;

	if (!msg->msg_controllen) {
		err("ancillary data not specified");
		return -1;
	}

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
			continue;

		if (cmsg->cmsg_len - CMSG_LEN(0) != sizeof(int) * 3) {
			err("expected fd payload");
			return -1;
		}

		fds = (int *)(CMSG_DATA(cmsg));

		if (stdin)
			*stdin = fds[0];

		if (stdout)
			*stdout = fds[1];

		if (stderr)
			*stderr = fds[2];
	}

	return 0;
}

int recv_credentials(struct msghdr *msg, pid_t *pid, uid_t *uid, gid_t *gid)
{
	struct cmsghdr *cmsg;
	struct ucred *cr;

	if (!msg->msg_controllen) {
		err("ancillary data not specified");
		return -1;
	}

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_CREDENTIALS)
			continue;

		if (cmsg->cmsg_len - CMSG_LEN(0) != sizeof(struct ucred)) {
			err("expected rights payload");
			return -1;
		}

		cr = (struct ucred *)(CMSG_DATA(cmsg));

		if (pid)
			*pid = cr->pid;

		if (uid)
			*uid = cr->uid;

		if (gid)
			*gid = cr->gid;
	}

	return 0;
}

int send_hdr(int conn, task_t task, unsigned caller_num, const char **argv, const char **env)
{
	int myfds[3];
	struct iovec iov  = {};
	struct msghdr msg = {};
	struct cmsghdr *cmsg;
	struct taskhdr hdr;
	ssize_t rc;

	char buf[CMSG_SPACE(sizeof(myfds))];

	hdr.type       = task;
	hdr.caller_num = caller_num;
	hdr.argc       = 0;
	hdr.argslen    = 0;
	hdr.envc       = 0;
	hdr.envslen    = 0;

	while (argv && argv[hdr.argc])
		hdr.argslen += strlen(argv[hdr.argc++]) + 1;

	while (env && env[hdr.envc])
		hdr.envslen += strlen(env[hdr.envc++]) + 1;

	myfds[0] = STDIN_FILENO;
	myfds[1] = STDOUT_FILENO;
	myfds[2] = STDERR_FILENO;

	iov.iov_base = &hdr;
	iov.iov_len  = sizeof(hdr);

	msg.msg_iov        = &iov;
	msg.msg_iovlen     = 1;
	msg.msg_control    = buf;
	msg.msg_controllen = sizeof(buf);

	cmsg             = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type  = SCM_RIGHTS;
	cmsg->cmsg_len   = CMSG_LEN(sizeof(myfds));

	memcpy(CMSG_DATA(cmsg), myfds, sizeof(myfds));

	if ((rc = TEMP_FAILURE_RETRY(sendmsg(conn, &msg, 0))) != (ssize_t) iov.iov_len) {
		if (rc < 0) {
			err("sendmsg: %m");
		} else {
			if (rc)
				err("sendmsg: expected size %u, got %u", (unsigned) iov.iov_len, (unsigned) rc);
			else
				err("sendmsg: unexpected EOF");
		}
		return -1;
	}

	return 0;
}

int send_args(int conn, const char **argv)
{
	int i             = 0;
	struct msghdr msg = {};
	struct iovec iov  = {};

	ssize_t rc;

	msg.msg_iov        = &iov;
	msg.msg_iovlen     = 1;
	msg.msg_control    = NULL;
	msg.msg_controllen = 0;

	while (argv && argv[i]) {
		iov.iov_base = (char *)argv[i];
		iov.iov_len  = strlen(argv[i]) + 1;

		if ((rc = TEMP_FAILURE_RETRY(sendmsg(conn, &msg, 0))) != (ssize_t) iov.iov_len) {
			if (rc < 0) {
				err("sendmsg: %m");
			} else {
				if (rc)
					err("sendmsg: expected size %u, got %u", (unsigned) iov.iov_len, (unsigned) rc);
				else
					err("sendmsg: unexpected EOF");
			}
			return -1;
		}

		i++;
	}

	return 0;
}

int recv_answer(int conn, int *retcode)
{
	struct msghdr msg = {};
	struct iovec iov  = {};

	iov.iov_base = retcode;
	iov.iov_len  = sizeof(int);

	msg.msg_iov        = &iov;
	msg.msg_iovlen     = 1;
	msg.msg_control    = NULL;
	msg.msg_controllen = 0;

	if (TEMP_FAILURE_RETRY(recvmsg(conn, &msg, 0)) < 0) {
		err("recvmsg: %m");
		return -1;
	}

	return 0;
}

int send_answer(int conn, int retcode)
{
	struct msghdr msg = {};
	struct iovec iov  = {};

	iov.iov_base = &retcode;
	iov.iov_len  = sizeof(retcode);

	msg.msg_iov        = &iov;
	msg.msg_iovlen     = 1;
	msg.msg_control    = NULL;
	msg.msg_controllen = 0;

	if (TEMP_FAILURE_RETRY(sendmsg(conn, &msg, MSG_NOSIGNAL)) < 0) {
		/* The client left without waiting for an answer */
		if (errno == EPIPE)
			return 0;

		err("sendmsg: %m");
		return -1;
	}

	return 0;
}
