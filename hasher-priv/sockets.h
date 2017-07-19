#ifndef _SOCKETS_H_
#define _SOCKETS_H_

int unix_listen(const char *, const char *);
int unix_connect(const char *, const char *);

int get_peercred(int, pid_t *, uid_t *, gid_t *);

int set_recv_timeout(int fd, int secs);

#include <stdint.h>

int xsendmsg(int conn, void *data, uint64_t len);
int xrecvmsg(int conn, void *data, uint64_t len);

#endif /* _SOCKETS_H_ */
