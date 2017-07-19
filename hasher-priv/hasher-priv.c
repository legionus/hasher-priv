
/*
  Copyright (C) 2003-2007  Dmitry V. Levin <ldv@altlinux.org>

  The entry function for the hasher-priv program.

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

/* Code in this file may be executed with root privileges. */

#include <sys/param.h> /* MAXPATHLEN */

#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "priv.h"
#include "logging.h"
#include "sockets.h"
#include "communication.h"

static void
my_error_print_progname(void)
{
	fprintf(stderr, "%s: ", program_invocation_short_name);
}

int
main(int ac, const char *av[], const char *ev[])
{
	int conn;
	task_t  task;
	char socketname[MAXPATHLEN];

	error_print_progname = my_error_print_progname;

	/* First, check and sanitize file descriptors. */
	sanitize_fds();

	/* Second, parse command line arguments. */
	task = parse_cmdline(ac, av);

	/* Connect to remote server and open session. */
	if (server_open_session(SOCKETDIR, PROJECT) < 0)
		return EXIT_FAILURE;

	/* Open user session */
	snprintf(socketname, sizeof(socketname), "hasher-priv-%d", geteuid());

	if ((conn = unix_connect(SOCKETDIR, socketname)) < 0)
		return EXIT_FAILURE;

	if (server_task(conn, task, caller_num) < 0)
		return EXIT_FAILURE;

	if (server_task_fds(conn) < 0)
		return EXIT_FAILURE;

	if (server_command(conn, CMD_TASK_ARGUMENTS, task_args) < 0)
		return EXIT_FAILURE;

	if (server_command(conn, CMD_TASK_ENVIRON, ev) < 0)
		return EXIT_FAILURE;

	if (server_command(conn, CMD_TASK_RUN, NULL) < 0)
		return EXIT_FAILURE;

	/* Close session socket. */
	close(conn);

	return EXIT_SUCCESS;
}
