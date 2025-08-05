#include "client.h"

#include <stdio.h>	// BUFSIZ
#include <string.h>	// strerror()
#include <errno.h>	// errno
#include <stdlib.h>	// malloc()
#include <stddef.h>	// size_t
#include <stdbool.h>	// false
#include <stdint.h>	// uint32_t

#include <sys/socket.h>
#include <sys/time.h>	// gettimeofday()
#include <sys/poll.h>	// poll()

#include <linux/errqueue.h>

#include "memory_provider.h"

#include "socket.h"

#define CTRL_DATA_SIZE	CMSG_SPACE(sizeof(uint32_t))

#define MAX_IOV	1024
#define IOV_LEN	65536

#define WAITTIME_MS	100

#define ERROR(...) do {					\
	snprintf(error, BUFSIZ, __VA_ARGS__);		\
} while(false)

struct client {
	Memory context;
	char *buffer;
	size_t size;

	char *address;
	int port;
};

static char error[BUFSIZ];
static struct memory_provider *provider = &amdgpu_memory_provider;

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

	ret = poll(&pfd, 1, WAITTIME_MS);
	if (ret == -1) {
		ERROR("failed to poll(): %s", strerror(errno));
		return -1;
	}

	if (pfd.revents & POLLERR) {
		ERROR("failed to poll(): %s", strerror(errno));
		return -1;
	}

	return 0;
}

static int wait_compl(int fd)
{
	int64_t tstop = gettimeofday_ms() + WAITTIME_MS;
	char control[CMSG_SPACE(100)] = {};
	struct sock_extended_err *serr;
	struct msghdr msg = {};
	uint32_t hi, lo;
	int ret;

	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	while (gettimeofday_ms() < tstop) {
		if (!do_poll(fd))
			continue;

		ret = recvmsg(fd, &msg, MSG_ERRQUEUE);
		if (ret == -1) {
			ERROR("failed to recvmsg(): %s", strerror(errno));
			return -1;
		}

		for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
		     cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg))
		{
			serr = (void *) CMSG_DATA(cmsg);
			if (serr->ee_origin != SO_EE_ORIGIN_ZEROCOPY) {
				ERROR("wrong origin %u", serr->ee_origin);
				return -1;
			} if (serr->ee_errno != 0) {
				ERROR("wrong errno %d", serr->ee_errno);
				return -1;
			}

			hi = serr->ee_data;
			lo = serr->ee_info;

			return 0;
		}
	}

	ERROR("did not receive tx completion");

	return -1;
}


Client client_setup(Memory context, char *address, int port)
{
	Client client;
	struct sockaddr_in *sockaddr;
	int ret;

	client = malloc(sizeof(struct client));
	if (client == NULL) {
		ERROR("failed to malloc(): %s", strerror(errno));
		goto RETURN_NULL;
	}	
	
	client->context = context;
	client->size = provider->get_size(context);
	client->address = address;
	client->port = port;

	client->buffer = malloc(client->size);
	if (client->buffer == NULL) {
		ERROR("failed to malloc(): %s", strerror(errno));
		goto FREE_CLIENT;
	}
	
	return client;

FREE_CLIENT:	free(client);
RETURN_NULL:	return NULL;
}

int client_run_as_tcp(Client client, char *address, int port)
{
	size_t sendlen;
	int ret;
	int sockfd;

	sockfd = socket_create(client->address, ++client->port);
	if (sockfd == -1) {
		ERROR("failed to socket_create(): %s",
		      socket_get_error());
		goto RETURN_ERROR;
	}

	ret = socket_connect(sockfd, address, port);
	if (ret == -1) {
		ERROR("failed to connect(): %s", strerror(errno));
		goto SOCKET_DESTROY;
	}

	ret = provider->memcpy_from(client->buffer, client->context,
			     	    0, client->size);
	if (ret == -1)
		ERROR("failed to amdgpu_memory_provider->memcpy_from(): %s",
		      provider->get_error());

	sendlen = 0;
	while (sendlen < client->size) {
		ret = send(sockfd, client->buffer + sendlen,
	     		   client->size - sendlen, 0);
		if (ret == -1) {
			ERROR("failed to send(): %s", strerror(errno));
			goto SOCKET_DESTROY;
		}

		sendlen += ret;
	}

	if (socket_destroy(sockfd) == -1) {
		ERROR("failed to socket_destroy(): %s", socket_get_error());
		goto RETURN_ERROR;
	}

	return 0;

SOCKET_DESTROY:	socket_destroy(sockfd);
RETURN_ERROR:	return -1;
}

int client_run_as_dma(Client client, Memory dmabuf, char *address, int port,
		      char *interface, int dmabuf_id)
{
	char ctrl_data[CTRL_DATA_SIZE];
	struct iovec iov[MAX_IOV];

	int sockfd;
	int ret, opt;

	size_t sendlen;

	if (client->size > IOV_LEN * MAX_IOV) {
		ERROR("buffer is too long to send at once!");
		return -1;
	}

	sockfd = socket_create(client->address, client->port);
	if (sockfd == -1) {
		ERROR("failed to socket_create(): %s",
		      socket_get_error());
		goto RETURN_ERROR;
	}

	ret = setsockopt(
		sockfd, SOL_SOCKET, SO_BINDTODEVICE,
		interface, strlen(interface) + 1
	);
	if (ret == -1) {
		ERROR("failed to setsockopt(SO_BINDTODEVICE): %s",
		      strerror(errno));
		goto DESTROY_SOCKET;
	}

	opt = 1;
	ret = setsockopt(
		sockfd, SOL_SOCKET, SO_ZEROCOPY,
		&opt, sizeof(opt)
	);
	if (ret == -1) {
		ERROR("failed to setsockopt(SO_ZEROCOPY): %s",
		      strerror(errno));
		goto DESTROY_SOCKET;
	}
	
	if (socket_connect(sockfd, address, port) == -1) {
		ERROR("failed to socket_connect(): %s", socket_get_error());
		goto DESTROY_SOCKET;
	}

	ret = provider->memmove_to(client->context, dmabuf, 0, 0, client->size);
	if (ret == -1) {
		ERROR("failed to amdgpu_memory_provider->memmove_to(): %s",
		      provider->get_error());
		goto DESTROY_SOCKET;
	}

	sendlen = 0;
	while (sendlen < client->size) {
		struct msghdr msg;
		struct cmsghdr *cmsg;

		iov[0].iov_base = (void *) sendlen;
		iov[0].iov_len = client->size - sendlen;

		msg.msg_iov = iov;
		msg.msg_iovlen = 1;
		
		msg.msg_control = ctrl_data;
		msg.msg_controllen = CTRL_DATA_SIZE;

		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_DEVMEM_DMABUF;
		cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));

		*((uint32_t *) CMSG_DATA(cmsg)) = dmabuf_id;

		ret = sendmsg(sockfd, &msg, MSG_ZEROCOPY);
		if (ret == -1) {
			ERROR("failed to sendmsg(): %s", strerror(errno));
			goto DESTROY_SOCKET;
		}

		sendlen += ret;

		wait_compl(sockfd);
	}

	if (socket_destroy(sockfd) == -1) {
		ERROR("failed to socket_destroy(): %s", socket_get_error());
		return -1;
	}

	return 0;

DESTROY_SOCKET:	(void) socket_destroy(sockfd);
RETURN_ERROR:	return -1;
}


void client_cleanup(Client client)
{
	free(client->buffer);
	free(client);
}

char *client_get_error(void)
{
	return error;
}
