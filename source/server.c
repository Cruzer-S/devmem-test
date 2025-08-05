#include "server.h"

#include <stdio.h>	// BUFSIZ
#include <stdbool.h>	// true, false
#include <stdlib.h>	// malloc()
#include <string.h>	// strerror()
#include <errno.h>	// errno

#define __iovec_defined	// do not define `struct iovec`
#include <sys/socket.h>	// accept(), recv(), send(), etc.

#include <linux/uio.h>	// struct iovec, struct dmabuf_cmsg

#include "memory_provider.h"

#include "socket.h"

#define CTRL_DATA_SIZE	CMSG_SPACE(sizeof(int) * 100)
#define BACKLOG		15

#define ERROR(...) do {					\
	snprintf(error, BUFSIZ, __VA_ARGS__);		\
} while(false)

struct server {
	Memory context;
	char *buffer;
	size_t size;

	int sockfd;
};

static char error[BUFSIZ];
static struct memory_provider *provider = &amdgpu_memory_provider;

Server server_setup(Memory context, char *address, int port)
{
	Server server;

	server = malloc(sizeof(struct server));
	if (server == NULL) {
		ERROR("failed to malloc(): %s", strerror(errno));
		goto RETURN_NULL;
	}

	server->sockfd = socket_create(address, port);
	if (server->sockfd == -1) {
		ERROR("failed to socket_create(): %s", socket_get_error());
		goto FREE_SERVER;
	}

	server->context = context;
	server->size = provider->get_size(context);

	if (listen(server->sockfd, BACKLOG) == -1) {
		ERROR("failed to listen(): %s", strerror(errno));
		goto SOCKET_DESTROY;
	}

	server->buffer = malloc(server->size);
	if (server->buffer == NULL) {
		ERROR("failed to malloc(): %s", strerror(errno));
		goto SOCKET_DESTROY;
	}

	return server;

SOCKET_DESTROY:		(void) socket_destroy(server->sockfd);
FREE_SERVER:		free(server);
RETURN_NULL:		return NULL;
}

int server_run_as_tcp(Server server)
{
	int clnt_fd;
	size_t recvlen;
	Memory context;
	int ret;

	context = server->context;
	
	clnt_fd = accept(server->sockfd, NULL, 0);
	if (clnt_fd == -1) {
		ERROR("failed to accept(): %s", strerror(errno));
		goto RETURN_ERROR;
	}

	recvlen = 0;
	while (true) {
		int ret = recv(clnt_fd, server->buffer + recvlen, 
		 			server->size - recvlen, 0);
		if (ret == -1) {
			ERROR("failed to recv(): %s", strerror(errno));
			goto SOCKET_DESTROY;
		}

		if (ret == 0)
			break;
	
		recvlen += ret;
	}

	ret = provider->memcpy_to(context, server->buffer, 0, server->size);
	if (ret == -1) {
		ERROR("failed to amdgpu_memory_provider->memcpy_to(): %s",
		      provider->get_error());
		goto SOCKET_DESTROY;
	}

	if (socket_destroy(clnt_fd) == -1)
		goto RETURN_ERROR;

	return 0;

SOCKET_DESTROY:	(void) socket_destroy(clnt_fd);
RETURN_ERROR:	return -1;
}

int server_run_as_dma(Server server, Memory dmabuf)
{
	char ctrl_data[CTRL_DATA_SIZE];

	int clnt_fd;
	size_t recvlen;
	int ret;

	clnt_fd = accept(server->sockfd, NULL, 0);
	if (clnt_fd == -1) {
		ERROR("failed to accept(): %s", strerror(errno));
		goto RETURN_ERROR;
	}

	recvlen = 0;
	while (true) {
		struct iovec iov;
		struct dmabuf_cmsg *dmabuf_cmsg;
		struct msghdr msg;
		struct dmabuf_token token;

		iov = (struct iovec) {
			.iov_base = server->buffer,
			.iov_len = server->size 
		};

		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = ctrl_data;
		msg.msg_controllen = CTRL_DATA_SIZE;

		ret = recvmsg(clnt_fd, &msg, MSG_SOCK_DEVMEM);
		if (ret == -1) {
			ERROR("failed to recvmsg(): %s", strerror(errno));
			goto SOCKET_DESTROY;
		}

		if (ret == 0)
			break;

		for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
		     cmsg;
		     cmsg = CMSG_NXTHDR(&msg, cmsg))
		{
			if (cmsg->cmsg_type != SCM_DEVMEM_DMABUF) {
				ERROR("cmsg_type is not SCM_DEVMEM_DMABUF");
				goto SOCKET_DESTROY;
			}

			dmabuf_cmsg = (struct dmabuf_cmsg *) CMSG_DATA(cmsg);

			ret = provider->memmove_to(
				server->context, dmabuf,
				recvlen, dmabuf_cmsg->frag_offset,
				dmabuf_cmsg->frag_size
			);
			if (ret == -1) {
				ERROR("failed to amdgpu_memory_provider->"
	  			      "memmove_to(): %s",
				      provider->get_error());
				goto SOCKET_DESTROY;

				// Unfreed tokens are not an issue;
				// `socket_destroy()` will release all
				// associated resources.
			}

			token.token_start = dmabuf_cmsg->frag_token;
			token.token_count = 1;
			if (setsockopt(clnt_fd, SOL_SOCKET, SO_DEVMEM_DONTNEED,
				       &token, sizeof(token)) == -1) {
				ERROR("failed to setsockopt(): %s",
	  			      strerror(errno));
				goto SOCKET_DESTROY;
			}

			recvlen += dmabuf_cmsg->frag_size;
		}
	}

	if (socket_destroy(clnt_fd) == -1)
		goto RETURN_ERROR;

	return 0;

SOCKET_DESTROY:	(void) socket_destroy(clnt_fd);
RETURN_ERROR:	return -1;
}

void server_cleanup(Server server)
{
	socket_destroy(server->sockfd);
	free(server->buffer);
	free(server);
}

char *server_get_error(void)
{
	return error;
}
