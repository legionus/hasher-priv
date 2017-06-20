#ifndef _EPOLL_H_
#define _EPOLL_H_

#include <sys/epoll.h>

int epollin_add(int fd_ep, int fd);
void epollin_remove(int fd_ep, int fd);

#endif /* _EPOLL_H_ */
