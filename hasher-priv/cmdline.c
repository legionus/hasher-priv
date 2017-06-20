
/*
  Copyright (C) 2003-2012  Dmitry V. Levin <ldv@altlinux.org>

  Command line parser for the hasher-priv program.

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

#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>

#include "priv.h"

static void __attribute__ ((noreturn, format(printf, 1, 2)))
show_usage(const char *fmt, ...)
{
	fprintf(stderr, "%s: ", program_invocation_short_name);

	va_list arg;

	va_start(arg, fmt);
	vfprintf(stderr, fmt, arg);
	va_end(arg);

	fprintf(stderr, "\nTry `%s --help' for more information.\n",
		program_invocation_short_name);
	exit(EXIT_FAILURE);
}

static void __attribute__ ((noreturn))
print_help(void)
{
	printf("Privileged helper for the hasher project.\n"
	       "\nUsage: %s [options] <args>\n"
	       "\nValid options are:\n"
	       "  -<number>:\n"
	       "       subconfig identifier;\n"
	       "  --version:\n"
	       "       print program version and exit.\n"
	       "  -h or --help:\n"
	       "       print this help text and exit.\n"
	       "\nValid args are any of:\n\n"
	       "getconf:\n"
	       "       print config file name;\n"
	       "killuid:\n"
	       "       kill all processes of user1 and user2;\n"
	       "getugid1:\n"
	       "       print uid:gid pair for user1;\n"
	       "chrootuid1 <chroot path> <program> [program args]:\n"
	       "       execute program in given chroot with credentials of user1;\n"
	       "getugid2:\n"
	       "       print uid:gid pair for user2;\n"
	       "chrootuid2 <chroot path> <program> [program args]:\n"
	       "       execute program in given chroot with credentials of user2;\n"
	       "makedev <chroot path>:\n"
	       "       make essential devices in given chroot;\n"
	       "maketty <chroot path>:\n"
	       "       make tty devices in given chroot;\n"
	       "makeconsole <chroot path>:\n"
	       "       make console devices in given chroot;\n"
	       "mount <chroot path> <mount point>:\n"
	       "       mount appropriate file system to the given mount point;\n"
	       "umount <chroot path>:\n"
	       "       umount all previously mounted file systems.\n",
	       program_invocation_short_name);
	exit(EXIT_SUCCESS);
}

static void __attribute__ ((noreturn))
print_version(void)
{
	printf("hasher-priv version %s\n"
	       "\nCopyright (C) 2003-2013  Dmitry V. Levin <ldv@altlinux.org>\n"
	       "\nThis is free software; see the source for copying conditions.\n"
	       "There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
	       "\nWritten by Dmitry V. Levin <ldv@altlinux.org> et al.\n",
	       PROJECT_VERSION);
	exit(EXIT_SUCCESS);
}

const char *chroot_path;
const char *single_mountpoint;
const char **chroot_argv;
unsigned caller_num;

const char **task_args;

static unsigned
get_caller_num(const char *str)
{
	char   *p = 0;
	unsigned long n;

	if (!*str)
		show_usage("-%s: invalid option", str);

	n = strtoul(str, &p, 10);
	if (!p || *p || n > INT_MAX)
		show_usage("-%s: invalid option", str);

	return (unsigned) n;
}

/* Parse command line arguments. */
task_t
parse_cmdline(int argc, const char *argv[])
{
	int     ac;
	const char **av;

	if (argc < 2)
		show_usage("insufficient arguments");

	ac = argc - 1;
	av = argv + 1;

	if (av[0][0] == '-')
	{
		/* option */
		if (!strcmp("-h", av[0]) || !strcmp("--help", av[0]))
			print_help();

		if (!strcmp("--version", av[0]))
			print_version();

		caller_num = get_caller_num(&av[0][1]);
		--ac;
		++av;
	}

	if (ac < 1)
		show_usage("insufficient arguments");

	task_args = NULL;

	if (!strcmp("getconf", av[0]))
	{
		if (ac != 1)
			show_usage("%s: invalid usage", av[0]);
		return TASK_GETCONF;
	} else if (!strcmp("killuid", av[0]))
	{
		if (ac != 1)
			show_usage("%s: invalid usage", av[0]);
		return TASK_KILLUID;
	} else if (!strcmp("getugid1", av[0]))
	{
		if (ac != 1)
			show_usage("%s: invalid usage", av[0]);
		return TASK_GETUGID1;
	} else if (!strcmp("chrootuid1", av[0]))
	{
		if (ac < 3)
			show_usage("%s: invalid usage", av[0]);
		task_args = av + 1;
		return TASK_CHROOTUID1;
	} else if (!strcmp("getugid2", av[0]))
	{
		if (ac != 1)
			show_usage("%s: invalid usage", av[0]);
		return TASK_GETUGID2;
	} else if (!strcmp("chrootuid2", av[0]))
	{
		if (ac < 3)
			show_usage("%s: invalid usage", av[0]);
		task_args = av + 1;
		return TASK_CHROOTUID2;
	} else if (!strcmp("makedev", av[0]))
	{
		if (ac != 2)
			show_usage("%s: invalid usage", av[0]);
		task_args = av + 1;
		return TASK_MAKEDEV;
	} else if (!strcmp("maketty", av[0]))
	{
		if (ac != 2)
			show_usage("%s: invalid usage", av[0]);
		task_args = av + 1;
		return TASK_MAKETTY;
	} else if (!strcmp("makeconsole", av[0]))
	{
		if (ac != 2)
			show_usage("%s: invalid usage", av[0]);
		task_args = av + 1;
		return TASK_MAKECONSOLE;
	} else if (!strcmp("mount", av[0]))
	{
		if (ac != 3)
			show_usage("%s: invalid usage", av[0]);
		task_args = av + 1;
		return TASK_MOUNT;
	} else if (!strcmp("umount", av[0]))
	{
		if (ac != 2)
			show_usage("%s: invalid usage", av[0]);
		task_args = av + 1;
		return TASK_UMOUNT;
	} else
		show_usage("%s: invalid argument", av[0]);
}

void
parse_task_args(task_t task, const char *argv[])
{
	switch (task) {
		case TASK_MAKEDEV:
		case TASK_MAKETTY:
		case TASK_MAKECONSOLE:
		case TASK_UMOUNT:
			chroot_path = argv[0];
			break;
		case TASK_CHROOTUID1:
		case TASK_CHROOTUID2:
			chroot_path = argv[0];
			chroot_argv = (const char **)argv + 1;
		case TASK_MOUNT:
			chroot_path       = argv[0];
			single_mountpoint = argv[1];
			break;
		default:
			break;
	}
}
