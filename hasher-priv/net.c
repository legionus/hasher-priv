#include <errno.h>
#include <error.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include <net/if.h>
#include <linux/rtnetlink.h>

#include "priv.h"

void
unshare_network(void)
{
	int     rtnetlink_sk;
	struct
	{
		struct nlmsghdr n;
		struct ifinfomsg i;
	} req;

	if (unshare(CLONE_NEWNET) < 0)
	{
		if (errno == ENOSYS || errno == EINVAL) {
			error(share_network ? EXIT_SUCCESS : EXIT_FAILURE, 0,
			      "network isolation is not supported by the kernel");
			return;
		}
		error(EXIT_FAILURE, errno, "unshare CLONE_NEWNET");
	}

	/* Setup loopback interface */

	rtnetlink_sk = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	if (rtnetlink_sk < 0)
		error(EXIT_FAILURE, errno, "socket AF_NETLINK");

	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));

	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.n.nlmsg_type = RTM_NEWLINK;
	req.i.ifi_family = AF_UNSPEC;

	req.i.ifi_index = (int) if_nametoindex("lo");
	req.i.ifi_flags = IFF_UP;
	req.i.ifi_change = IFF_UP;

	if (send(rtnetlink_sk, &req, req.n.nlmsg_len, 0) < 0)
		error(EXIT_FAILURE, errno, "send AF_NETLINK");

	if (close(rtnetlink_sk) < 0)
		error(EXIT_FAILURE, errno, "close AF_NETLINK");
}