#include "client.h"

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

#include <linux/errqueue.h>
#include <linux/uio.h>
#define __iovec_defined

#include <netinet/in.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/poll.h>

#include "memory.h"
#include "socket.h"
#include "ncdevmem.h"

#include "logger.h"

#define ERR(...) do {		\
	log(__VA_ARGS__);	\
	exit(EXIT_FAILURE);	\
} while (true)

static size_t writelen, total;
static const int waittime_ms = 500;

void client_tcp_start(char *address, int port, size_t buffer_size)
{
	int retval;
	size_t sendlen;

	socket_connect(address, port);

	total = 0;
	for (int i = 0; i < 128; i++) {
		sendlen = 0;
		while (sendlen < buffer_size) {
			retval = send(
				sockfd, buffer + sendlen,
				buffer_size - sendlen, 0
			);

			if (retval == -1)
				ERR(PERRN, "failed to send(): ");

			log(INFO, "send: %zu", retval);

			sendlen += retval;
		}

		total += sendlen;
	}

	log(INFO, "total: %zu", total);
}

static uint64_t gettimeofday_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return (tv.tv_sec * 1000ULL) + (tv.tv_usec / 1000ULL);
}

static int do_poll(int fd)
{
	struct pollfd pfd;
	int ret;

	pfd.revents = 0;
	pfd.fd = fd;

	ret = poll(&pfd, 1, waittime_ms);
	if (ret == -1)
		ERR(PERRN, "failed to poll(): ");

	return ret && (pfd.revents & POLLERR);
}

static void wait_compl(int fd)
{
	int64_t tstop = gettimeofday_ms() + waittime_ms;
	char control[CMSG_SPACE(100)] = {};
	struct sock_extended_err *serr;
	struct msghdr msg = {};
	struct cmsghdr *cm;
	__u32 hi, lo;
	int ret;

	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	while (gettimeofday_ms() < tstop) {
		if (!do_poll(fd))
			continue;

		ret = recvmsg(fd, &msg, MSG_ERRQUEUE);
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;

			ERR(PERRN, "failed to recvmsg(MSG_ERRQUEUE): ");
		}

		if (msg.msg_flags & MSG_CTRUNC)
			ERR(ERRN, "recvmsg() flag set MSG_CTRUNC");

		for (cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
			if (cm->cmsg_level != SOL_IP &&
			    cm->cmsg_level != SOL_IPV6)
				continue;
			if (cm->cmsg_level == SOL_IP &&
			    cm->cmsg_type != IP_RECVERR)
				continue;
			if (cm->cmsg_level == SOL_IPV6 &&
			    cm->cmsg_type != IPV6_RECVERR)
				continue;

			serr = (void *) CMSG_DATA(cm);
			if (serr->ee_origin != SO_EE_ORIGIN_ZEROCOPY)
				ERR(ERRN, "wrong origin %u", serr->ee_origin);
			if (serr->ee_errno != 0)
				ERR(PERRN, "wrong errno %d", serr->ee_errno);

			hi = serr->ee_data;
			lo = serr->ee_info;
			return;
		}
	}

	ERR(ERRN, "did not receive tx completion");
}

void client_dma_start(char *address, int port, size_t buffer_size, char *ifname)
{
	char ctrl_data[CMSG_SPACE(sizeof(uint32_t))];
	struct iovec iovec;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	uint32_t ddmabuf;

	int opt = 1, ret;

	ret = setsockopt(
		sockfd, SOL_SOCKET, SO_BINDTODEVICE,
		ifname, strlen(ifname) + 1
	);
	if (ret == -1)
		log(PERRN, "failed to setsockopt(): ");

	ret = setsockopt(sockfd, SOL_SOCKET, SO_ZEROCOPY, &opt, sizeof(opt));
	if (ret == -1)
		log(PERRN, "failed to setsockopt(): ");

	socket_connect(address, port);

	total = 0;
	while (true) {
		iovec.iov_base = 0;
		iovec.iov_len = buffer_size;

		amdgpu_dmabuf_provider.memcpy_to(
			dmabuf, buffer, 0, buffer_size
		);

		msg.msg_iov = &iovec;

		msg.msg_control = ctrl_data;
		msg.msg_controllen = sizeof(ctrl_data);

		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_DEVMEM_DMABUF;
		cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));

		ddmabuf = ncdevmem_get_dmabuf_id(ncdevmem);

		*((uint32_t *) CMSG_DATA(cmsg)) = ddmabuf;

		ret = sendmsg(sockfd, &msg, MSG_ZEROCOPY);
		if (ret < 0)
			ERR(PERRN, "failed to sendmsg(): ");

		if (ret != buffer_size)
			ERR(ERRN, "did not send all bytes %d (expected: %zd)",
       				  ret, buffer_size);

		wait_compl(sockfd);
	}
}

void client_start(char *address, int port, bool is_dma, size_t buffer_size, char *ifname)
{
	if (is_dma)	client_dma_start(address, port, buffer_size, ifname);
	else		client_tcp_start(address, port, buffer_size);
}
