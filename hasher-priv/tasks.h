#ifndef _TASKS_H_
#define _TASKS_H_

typedef enum {
	TASK_NONE = 0,
	TASK_GETCONF,
	TASK_KILLUID,
	TASK_GETUGID1,
	TASK_CHROOTUID1,
	TASK_GETUGID2,
	TASK_CHROOTUID2,
	TASK_MAKEDEV,
	TASK_MAKETTY,
	TASK_MAKECONSOLE,
	TASK_MOUNT,
	TASK_UMOUNT
} task_t;

#include <stdint.h>

struct taskhdr {
	task_t type;
	unsigned caller_num;

	uint64_t argc;
	uint64_t argslen;

	uint64_t envc;
	uint64_t envslen;
};

char *task2str(task_t type);
task_t str2task(char *s);

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#endif /* _TASKS_H_ */
