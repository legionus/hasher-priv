#ifndef _SOCKETS_H_
#define _SOCKETS_H_

int unix_listen(const char *, const char *);
int unix_connect(const char *, const char *);

int set_passcred(int);

#include <sys/socket.h>

int recv_iostreams(struct msghdr *, int *, int *, int *);
int recv_credentials(struct msghdr *, pid_t *, uid_t *, gid_t *);

int send_hdr(int, task_t, unsigned, const char **, const char **);
int send_args(int, const char **);

int recv_answer(int, int *);
int send_answer(int, int);

#endif /* _SOCKETS_H_ */
