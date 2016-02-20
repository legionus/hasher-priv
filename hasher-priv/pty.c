/*
  Copyright (C) 2016  Dmitry V. Levin <ldv@altlinux.org>

  The pty opener for the hasher-priv program.

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

/* Code in this file is executed with root privileges. */

#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "priv.h"
#include "xmalloc.h"

int open_pty(int *slave_fd, const int chrooted, const int verbose_error)
{
	const char *dev_ptmx = "/dev/ptmx";
	const char *pts_fmt = "/dev/pts/%u";
	const int dev_open_flags = O_RDWR | O_NOCTTY;
	unsigned int num = 0;
	int     ptmx = -1;
	int     slave = -1;
	gid_t   saved_gid = (gid_t) - 1;
	uid_t   saved_uid = (uid_t) - 1;
	char   *ptsname = NULL;

	ch_gid(caller_gid, &saved_gid);
	ch_uid(caller_uid, &saved_uid);

	if (chrooted)
	{
		static const char dev_pts_ptmx[] = "pts/ptmx";
		const mode_t rwdev = S_IFCHR | 0666;
		struct stat st;

		safe_chdir("dev", stat_caller_ok_validator);
		dev_ptmx = "ptmx";
		pts_fmt = "pts/%u";

		if (stat(dev_pts_ptmx, &st) ||
		    (st.st_mode & rwdev) != rwdev)
			goto err;

		if (unlink(dev_ptmx) && ENOENT != errno)
			error(EXIT_FAILURE, errno, "unlink: %s", dev_ptmx);
		if (symlink(dev_pts_ptmx, dev_ptmx))
			error(EXIT_FAILURE, errno, "symlink: %s", dev_ptmx);
	}

	ptmx = open(dev_ptmx, dev_open_flags);
	if (ptmx < 0)
	{
		if (verbose_error)
			error(EXIT_SUCCESS, errno, "open: %s", dev_ptmx);
		goto err;
	}

	if (ioctl(ptmx, TIOCGPTN, &num))
	{
		if (verbose_error)
			error(EXIT_SUCCESS, errno,
			      "ioctl: %s: TIOCGPTN", dev_ptmx);
		goto err;
	}

	xasprintf(&ptsname, pts_fmt, num);

#ifdef TIOCSPTLCK
	num = 0;
	if (ioctl(ptmx, TIOCSPTLCK, &num))
	{
		if (verbose_error)
			error(EXIT_SUCCESS, errno,
			      "ioctl: %s: TIOCSPTLCK", dev_ptmx);
		goto err;
	}
#endif

	slave = open(ptsname, dev_open_flags);
	if (slave < 0)
	{
		if (verbose_error)
			error(EXIT_SUCCESS, errno, "open: %s", ptsname);
		goto err;
	}

	goto out;

err:
	close(slave), slave = -1;
	close(ptmx), ptmx = -1;

out:
	free(ptsname);
	if (chrooted && chdir("/"))
		error(EXIT_FAILURE, errno, "chdir: %s", "/");
	ch_uid(saved_uid, 0);
	ch_gid(saved_gid, 0);
	*slave_fd = slave;
	return ptmx;
}
