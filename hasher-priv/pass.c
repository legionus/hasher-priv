
/*
  Copyright (C) 2005, 2009  Dmitry V. Levin <ldv@altlinux.org>

  The descriptor passing routines for the hasher-priv program.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Code in this file may be executed with caller or child privileges. */

#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>

#include "logging.h"
#include "priv.h"

union cmsg_data_u
{
	int    *i;
	unsigned char *c;
};

/* This function may be executed with child privileges. */

void
fd_send(int ctl, int pass, const char *data, size_t data_len)
{
	struct iovec vec;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	union cmsg_data_u cmsg_data_p;
	char    buf[CMSG_SPACE(sizeof pass)];

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof pass);

	cmsg_data_p.c = CMSG_DATA(cmsg);
	*cmsg_data_p.i = pass;

	vec.iov_base = (char *) data;
	vec.iov_len = data_len;

	ssize_t rc;

	if ((rc = TEMP_FAILURE_RETRY(sendmsg(ctl, &msg, 0))) !=
	    (ssize_t) data_len)
	{
		if (rc < 0)
		{
			error(EXIT_FAILURE, errno, "sendmsg");
		} else
		{
			if (rc)
				error(EXIT_FAILURE, 0,
				      "sendmsg: expected size %u, got %u",
				      (unsigned) data_len, (unsigned) rc);
			else
				error(EXIT_FAILURE, 0,
				      "sendmsg: unexpected EOF");
		}
	}
}

/* This function may be executed with caller privileges. */

int
fd_recv(int ctl, char *data, size_t data_len)
{
	struct iovec vec;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	union cmsg_data_u cmsg_data_p;
	char    buf[CMSG_SPACE(sizeof(int))];

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;

	vec.iov_base = data;
	vec.iov_len = data_len;

	ssize_t rc;

	if ((rc = TEMP_FAILURE_RETRY(recvmsg(ctl, &msg, 0))) !=
	    (ssize_t) data_len)
	{
		if (rc < 0)
		{
			error(EXIT_SUCCESS, errno, "recvmsg");
			fputc('\r', stderr);
		} else
		{
			if (rc)
				error(EXIT_SUCCESS, 0,
				      "recvmsg: expected size %u, got %u\r",
				      (unsigned) data_len, (unsigned) rc);
			else
				error(EXIT_SUCCESS, 0,
				      "recvmsg: unexpected EOF\r");
		}
		return -1;
	}

	if (!(cmsg = CMSG_FIRSTHDR(&msg)))
	{
		error(EXIT_SUCCESS, 0, "recvmsg: no message header\r");
		return -1;
	}

	if (cmsg->cmsg_type != SCM_RIGHTS)
	{
		error(EXIT_SUCCESS, 0, "recvmsg: expected type %u, got %u\r",
		      SCM_RIGHTS, cmsg->cmsg_type);
		return -1;
	}

	cmsg_data_p.c = CMSG_DATA(cmsg);
	return *cmsg_data_p.i;
}

int
fds_send(int conn, int *fds, size_t fds_len)
{
	struct iovec iov  = {};
	struct msghdr msg = {};
	struct cmsghdr *cmsg;
	ssize_t rc;
	size_t len = sizeof(int) * fds_len;

	union {
		char buf[CMSG_SPACE(len)];
		struct cmsghdr align;
	} u;

	/*
	 * We must send at least 1 byte of real data in
	 * order to send ancillary data
	*/
	char dummy = 'Z';

	iov.iov_base = &dummy;
	iov.iov_len  = sizeof(dummy);

	msg.msg_iov        = &iov;
	msg.msg_iovlen     = 1;
	msg.msg_control    = u.buf;
	msg.msg_controllen = sizeof(u.buf);

	cmsg             = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type  = SCM_RIGHTS;
	cmsg->cmsg_len   = CMSG_LEN(len);

	memcpy(CMSG_DATA(cmsg), fds, len);

	if ((rc = TEMP_FAILURE_RETRY(sendmsg(conn, &msg, 0))) != (ssize_t) iov.iov_len) {
		if (rc < 0)
			err("Xsendmsg: %m");
		else if (rc)
			err("Xsendmsg: expected size %u, got %u", (unsigned) iov.iov_len, (unsigned) rc);
		else
			err("Xsendmsg: unexpected EOF");
		return -1;
	}

	return 0;
}
