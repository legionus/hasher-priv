
/*
  Copyright (C) 2005-2013  Dmitry V. Levin <ldv@altlinux.org>

  The X11 forwarding support_str for the hasher-priv program.

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

/* Code in this file may be executed with root, caller or child privileges. */

#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "priv.h"
#include "xmalloc.h"
#include "sockets.h"

#define X11_UNIX_DIR "/tmp/.X11-unix"

typedef int (*x11_connect_method_t) (const char *, unsigned);
static x11_connect_method_t x11_connect_method;
static const char *x11_connect_name;
static unsigned long x11_connect_port;

/* This function may be executed with caller privileges. */

int
log_listen(void)
{
	int     fd = unix_listen("/dev", "log");

	if (fd >= 0 && chmod("/dev/log", 0622))
	{
		error(EXIT_SUCCESS, errno, "chmod: %s", "/dev/log");
		(void) close(fd);
		fd = -1;
	}

	return fd;
}

/* This function may be executed with child privileges. */

int
x11_listen(void)
{
	return unix_listen(X11_UNIX_DIR, "X10");
}

static int x11_dir_fd = -1;

/* This function may be executed with caller privileges. */

void
x11_closedir(void)
{
	if (x11_dir_fd >= 0)
	{
		close(x11_dir_fd);
		x11_dir_fd = -1;
	}
}

/* This function may be executed with caller privileges. */

static int
x11_connect_unix( __attribute__ ((unused))
		 const char *name, unsigned display_number)
{
	int     fd = -1;

	for (;;)
	{
		if (fchdir(x11_dir_fd))
		{
			error(EXIT_SUCCESS, errno, "fchdir (%d)", x11_dir_fd);
			fputc('\r', stderr);
			break;
		}

		if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		{
			error(EXIT_SUCCESS, errno, "socket AF_UNIX");
			fputc('\r', stderr);
			break;
		}

		struct sockaddr_un sun;

		memset(&sun, 0, sizeof(sun));
		sun.sun_family = AF_UNIX;
		snprintf(sun.sun_path, sizeof sun.sun_path, "X%u",
			 display_number);

		if (!connect
		    (fd, (struct sockaddr *) &sun, (socklen_t) sizeof sun))
			break;

		error(EXIT_SUCCESS, errno, "connect: %s", sun.sun_path);
		fputc('\r', stderr);
		close(fd);
		fd = -1;
		break;
	}
	if (chdir("/"))
	{
		error(EXIT_SUCCESS, errno, "chdir: /");
		fputc('\r', stderr);
	}
	return fd;
}

/* This function may be executed with caller privileges. */

static int
x11_connect_inet(const char *name, unsigned display_number)
{
	int     rc, saved_errno = 0, fd = -1;
	unsigned port_num = 6000 + display_number;
	struct addrinfo hints, *ai, *ai_start;
	char    port_str[NI_MAXSERV];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(port_str, sizeof port_str, "%u", port_num);

	if ((rc = getaddrinfo(name, port_str, &hints, &ai_start)) != 0)
	{
		error(EXIT_SUCCESS, errno, "getaddrinfo: %s:%u", name,
		      port_num);
		fputc('\r', stderr);
		return -1;
	}

	for (ai = ai_start; ai; ai = ai->ai_next)
	{
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0)
			continue;

		if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0)
		{
			saved_errno = errno;
			close(fd);
			fd = -1;
			continue;
		}

		break;
	}

	freeaddrinfo(ai_start);
	if (fd < 0)
	{
		error(EXIT_SUCCESS, saved_errno, "connect: %s:%u",
		      name, port_num);
		fputc('\r', stderr);
	}

	return fd;
}

/* This function may be executed with caller privileges. */

int
x11_connect(void)
{
	return x11_connect_method
		? x11_connect_method(x11_connect_name,
				     (unsigned) x11_connect_port) : -1;
}

/* This function may be executed with caller privileges. */

int
unix_accept(int fd)
{
	struct sockaddr_un sun;
	socklen_t len = sizeof(sun);

	int     rc = accept(fd, (struct sockaddr *) &sun, &len);

	if (rc < 0)
	{
		error(EXIT_SUCCESS, errno, "accept");
		fputc('\r', stderr);
	}

	return rc;
}

/* This function may be executed with caller privileges. */

int
x11_check_listen(int fd)
{
	struct sockaddr_un sun;
	socklen_t len = sizeof(sun);

	memset(&sun, 0, sizeof sun);
	if (getsockname(fd, (struct sockaddr *) &sun, &len))
	{
		error(EXIT_SUCCESS, errno, "getsockname");
		fputc('\r', stderr);
		(void) close(fd);
		return -1;
	}

	if (sun.sun_family != AF_UNIX)
	{
		error(EXIT_SUCCESS, 0,
		      "getsockname: expected type %u, got %u\r", AF_UNIX,
		      sun.sun_family);
		return -1;
	}

	char    path[sizeof sun.sun_path];

	snprintf(path, sizeof path, "%s/%s", X11_UNIX_DIR, "X10");
	if (strcmp(path, sun.sun_path))
	{
		error(EXIT_SUCCESS, 0, "getsockname: path %s, got %*s\r",
		      path, (unsigned) sizeof path, sun.sun_path);
		return -1;
	}

	return fd;
}

/* This function may be executed with root privileges. */

int
x11_parse_display(void)
{
	static char *display;

	if (!x11_display)
		return EXIT_FAILURE;

	display = xstrdup(x11_display);

	const char *const colon = strchr(display, ':');
	char *number = strrchr(display, ':');

	if (!colon || !number)
	{
		error(EXIT_SUCCESS, 0,
		      "Unrecognized DISPLAY=%s, X11 forwarding disabled",
		      display);
		return EXIT_FAILURE;
	}

	x11_connect_name = display;
	*number = '\0';
	++number;

	char   *endp;

	x11_connect_port = strtoul(number, &endp, 10);
	if (!endp || (*endp && *endp != '.') || x11_connect_port > 100)
	{
		error(EXIT_SUCCESS, 0,
		      "Unrecognized DISPLAY=%s, X11 forwarding disabled",
		      display);
		return EXIT_FAILURE;
	}

	/* DISPLAY looks valid. */

	if (x11_connect_name[0] == '\0')
	{
		x11_connect_method = x11_connect_unix;
	} else
	{

		const char *slash = strrchr(x11_connect_name, '/');

		if (slash && !strcmp(slash + 1, "unix"))
			x11_connect_method = x11_connect_unix;
		else {
			x11_connect_method = x11_connect_inet;
			share_caller_network = 1;
		}
	}

	return EXIT_SUCCESS;
}

/* This function may be executed with root privileges. */

void
x11_drop_display(void)
{
	free((char *) x11_key);
	free((char *) x11_display);
	x11_display = x11_key = 0;
	x11_data_len = 0;
}

/* This function may be executed with root privileges. */

int
x11_prepare_connect(void)
{
	if (!x11_connect_method)
		return EXIT_FAILURE;

	if (x11_connect_method == x11_connect_unix &&
	    (x11_dir_fd = open(X11_UNIX_DIR, O_RDONLY)) < 0)
	{
		error(EXIT_SUCCESS, errno, "open: %s", X11_UNIX_DIR);
		error(EXIT_SUCCESS, 0, "X11 forwarding disabled");
		x11_drop_display();
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
