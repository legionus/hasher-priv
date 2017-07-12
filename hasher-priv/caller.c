
/*
  Copyright (C) 2003-2005  Dmitry V. Levin <ldv@altlinux.org>

  The caller data initialization module for the hasher-priv program.

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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>

#include "priv.h"
#include "xmalloc.h"
#include "logging.h"

const char *caller_user, *caller_home;
uid_t   caller_uid;
gid_t   caller_gid;

/*
 * Initialize caller_user, caller_uid, caller_gid and caller_home.
 */
int
init_caller_data(uid_t uid, gid_t gid)
{
	struct passwd *pw = 0;

	caller_uid = uid;
	if (caller_uid < MIN_CHANGE_UID) {
		err("caller has invalid uid: %u", caller_uid);
		return -1;
	}

	caller_gid = gid;
	if (caller_gid < MIN_CHANGE_GID) {
		err("caller has invalid gid: %u", caller_gid);
		return -1;
	}

	pw = getpwuid(caller_uid);

	if (!pw || !pw->pw_name) {
		err("caller lookup failure");
		return -1;
	}

	caller_user = xstrdup(pw->pw_name);

	if (caller_uid != pw->pw_uid) {
		err("caller %s: uid mismatch", caller_user);
		return -1;
	}

	if (caller_gid != pw->pw_gid) {
		err("caller %s: gid mismatch", caller_user);
		return -1;
	}

	errno = 0;
	if (pw->pw_dir && *pw->pw_dir)
		caller_home = canonicalize_file_name(pw->pw_dir);

	if (!caller_home || !*caller_home) {
		err("caller %s: invalid home: %m", caller_user);
		return -1;
	}

	return 0;
}
