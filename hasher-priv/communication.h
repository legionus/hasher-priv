#ifndef _PROTO_H_
#define _PROTO_H_

#include <stdint.h>

typedef enum {
	CMD_NONE = 0,

	/* Master commands */
	CMD_OPEN_SESSION,
	CMD_CLOSE_SESSION,

	/* Session commands */
	CMD_TASK_BEGIN,
	CMD_TASK_FDS,
	CMD_TASK_ARGUMENTS,
	CMD_TASK_ENVIRON,
	CMD_TASK_RUN,

} cmd_t;

typedef enum {
	CMD_STATUS_DONE = 0,
	CMD_STATUS_FAILED,
} cmd_status_t;

struct cmd {
	cmd_t    type;
	uint64_t datalen;
};

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

struct taskhdr {
	task_t type;
	unsigned caller_num;
};

char *task2str(task_t type);
task_t str2task(char *s);

int recv_fds(int conn, uint64_t datalen, int *fds);
int recv_list(int conn, uint64_t datalen, char ***argv);

long int send_command_response(int conn, int retcode, const char *fmt, ...);

int server_command(int conn, cmd_t cmd, const char **args);
int server_open_session(const char *dir_name, const char *file_name);
int server_close_session(const char *dir_name, const char *file_name);

int server_task(int conn, task_t task, unsigned caller_num);
int server_task_fds(int conn);

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#endif /* _PROTO_H_ */
