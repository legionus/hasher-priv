
/*
  Copyright (C) 2003-2013  Dmitry V. Levin <ldv@altlinux.org>

  The chdir-with-validation module for the hasher-priv program.

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

#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "priv.h"
#include "xmalloc.h"

/* This function may be executed with root privileges. */
static const char *
is_changed(struct stat *st1, struct stat *st2)
{
	if (st1->st_dev != st2->st_dev)
		return "device number";
	if (st1->st_ino != st2->st_ino)
		return "inode number";
	if (st1->st_rdev != st2->st_rdev)
		return "device type";
	if (st1->st_mode != st2->st_mode)
		return "protection";
	if (st1->st_uid != st2->st_uid || st1->st_gid != st2->st_gid)
		return "ownership";

	return 0;
}

/*
 * Change the current working directory
 * using lstat+validate+chdir+lstat+compare technique.
 */

/* This function may be executed with root privileges. */
static void
safe_chdir_simple(const char *name, VALIDATE_FPTR validator)
{
	struct stat st1, st2;
	const char *what;

	if (lstat(name, &st1) < 0)
		error(EXIT_FAILURE, errno, "lstat: %s", name);

	if (!S_ISDIR(st1.st_mode))
		error(EXIT_FAILURE, ENOTDIR, "%s", name);

	validator(&st1, name);

	if (chdir(name) < 0)
		error(EXIT_FAILURE, errno, "chdir: %s", name);

	if (lstat(".", &st2) < 0)
		error(EXIT_FAILURE, errno, "lstat: %s", name);

	if ((what = is_changed(&st1, &st2)))
		error(EXIT_FAILURE, 0, "%s: %s changed during execution",
		      name, what);
}

/*
 * Change the current working directory
 * using lstat+validate+chdir+lstat+compare technique.
 * If the path is relative, chdir to each path element sequentially.
 */

/* This function may be executed with root privileges. */
void
safe_chdir(const char *path, VALIDATE_FPTR validator)
{
	if (path[0] == '/' || !strchr(path, '/'))
		safe_chdir_simple(path, validator);
	else
	{
		char   *elem, *p = xstrdup(path);

		for (elem = strtok(p, "/"); elem; elem = strtok(0, "/"))
			safe_chdir_simple(elem, validator);
		free(p);
	}
}

/*
 * Ensure that owner group is change_gid1,
 * no world writable permissions, and group writable
 * bit is set if and only if sticky bit is also set.
 */

/* This function may be executed with caller privileges. */
static void
stat_group1_ok_validator(struct stat *st, const char *name)
{
	if (st->st_gid != change_gid1)
		error(EXIT_FAILURE, 0,
		      "%s: expected group %u, found group %u",
		      name, change_gid1, st->st_gid);

	if ((st->st_mode & S_IWOTH)
	    || ((st->st_mode & S_IWGRP) && !(st->st_mode & S_ISVTX)))
		error(EXIT_FAILURE, 0,
		      "%s: bad perms: %o", name, st->st_mode & 07777);
}

/*
 * Ensure that owner is caller_uid:change_gid1,
 * no world writable permissions, and group writable
 * bit is set if and only if sticky bit is also set.
 */

/* This function may be executed with caller privileges. */
void
stat_caller_ok_validator(struct stat *st, const char *name)
{
	if (st->st_uid != caller_uid)
		error(EXIT_FAILURE, 0,
		      "%s: expected owner %u, found owner %u",
		      name, caller_uid, st->st_uid);

	stat_group1_ok_validator(st, name);
}

/*
 * Ensure that owner is either caller_uid:change_gid1
 * or change_uid1:change_gid1,
 * no world writable permissions,
 * and group writable bit is set only when sticky bit is also set.
 */

/*
 * This function is only called via chrootuid() -> unshare_mount() ->
 * setup_mountpoints() -> xmount() -> chdiruid() -> chdiruid_simple() ->
 * safe_chdir() -> safe_chdir_simple() -> stat_private_mount_ok_validator()
 * chain.
 */

/* This function may be executed with caller privileges. */
void
stat_private_mount_ok_validator(struct stat *st, const char *name)
{
	if (st->st_uid != caller_uid && st->st_uid != change_uid1)
		error(EXIT_FAILURE, 0,
		      "%s: expected owner %u or %u, found owner %u",
		      name, caller_uid, change_uid1, st->st_uid);

	stat_group1_ok_validator(st, name);
}

/*
 * Ensure that owner is root and permissions contain no
 * group or world writable bits set.
 */

/* This function may be executed with root privileges. */
void
stat_root_ok_validator(struct stat *st, const char *name)
{
	if (st->st_uid)
		error(EXIT_FAILURE, 0, "%s: bad owner: %u", name, st->st_uid);

	if (st->st_mode & (S_IWGRP | S_IWOTH))
		error(EXIT_FAILURE, 0, "%s: bad perms: %o", name,
		      st->st_mode & 07777);
}

/* This function may be executed with root privileges. */
void
stat_any_ok_validator( __attribute__ ((unused))
		     struct stat *st, __attribute__ ((unused))
		     const char *name)
{
}
