#include <sys/epoll.h>

#include <string.h>
#include <unistd.h>

#include "epoll.h"
#include "logging.h"

int epollin_add(int fd_ep, int fd)
{
	struct epoll_event ev;

	memset(&ev, 0, sizeof(struct epoll_event));
	ev.events  = EPOLLIN;
	ev.data.fd = fd;

	if (epoll_ctl(fd_ep, EPOLL_CTL_ADD, fd, &ev) < 0) {
		err("epoll_ctl: %m");
		return -1;
	}

	return 0;
}

void epollin_remove(int fd_ep, int fd)
{
	if (fd < 0)
		return;
	epoll_ctl(fd_ep, EPOLL_CTL_DEL, fd, NULL);
	close(fd);
}
