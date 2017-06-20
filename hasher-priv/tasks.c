#include <stdint.h>
#include <string.h>

#include "tasks.h"

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
