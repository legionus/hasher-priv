
/*
  Copyright (C) 2003-2006  Dmitry V. Levin <ldv@altlinux.org>

  The makedev action for the hasher-priv program.

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

#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/sysmacros.h>

#include "priv.h"

static void
xmknod(const char *name, const char *devpath, mode_t mode, unsigned major,
       unsigned minor)
{
	if (!link(devpath, name))
		return;

	if (mknod(name, mode, makedev(major, minor)))
		error(EXIT_FAILURE, errno, "mknod: %s", name);
}

int
do_makedev(void)
{
	gid_t   saved_gid = (gid_t) - 1;
	mode_t  m;

	chdiruid(chroot_path);
	chdiruid("dev");

	ch_gid(0, &saved_gid);
	m = umask(0);

	xmknod("null", "/dev/null", S_IFCHR | 0666, 1, 3);
	xmknod("zero", "/dev/zero", S_IFCHR | 0666, 1, 5);
	xmknod("full", "/dev/full", S_IFCHR | 0666, 1, 7);
	xmknod("urandom", "/dev/urandom", S_IFCHR | 0644, 1, 9);
	xmknod("random", "/dev/urandom", S_IFCHR | 0644, 1, 9);	/* pseudo random. */

	umask(m);
	ch_gid(saved_gid, 0);

	return 0;
}

int
do_makeconsole(void)
{
	gid_t   saved_gid = (gid_t) - 1;
	mode_t  m;

	chdiruid(chroot_path);
	chdiruid("dev");

	ch_gid(0, &saved_gid);
	m = umask(0);

	xmknod("console", "/dev/console", S_IFCHR | 0600, 5, 1);
	xmknod("tty0", "/dev/tty0", S_IFCHR | 0600, 4, 0);
	xmknod("fb0", "/dev/fb0", S_IFCHR | 0600, 29, 0);

	umask(m);
	ch_gid(saved_gid, 0);

	return 0;
}

int
do_maketty(void)
{
	gid_t   saved_gid = (gid_t) - 1;
	mode_t  m;

	if (!allow_tty_devices)
		error(EXIT_FAILURE, 0,
		      "maketty: creating tty devices not allowed");

	chdiruid(chroot_path);
	chdiruid("dev");

	ch_gid(0, &saved_gid);
	m = umask(0);

	xmknod("tty", "/dev/tty", S_IFCHR | 0666, 5, 0);
	xmknod("ptmx", "/dev/ptmx", S_IFCHR | 0666, 5, 2);

	umask(m);
	ch_gid(saved_gid, 0);

	return 0;
}
