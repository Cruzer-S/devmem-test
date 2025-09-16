#include "server.h"

#include <stdio.h>	// BUFSIZ
#include <stdbool.h>	// true, false
#include <stdlib.h>	// malloc()
#include <string.h>	// strerror()
#include <errno.h>	// errno

#include <unistd.h>

#define __iovec_defined	// do not define `struct iovec`
#include <sys/socket.h>	// accept(), recv(), send(), etc.

#include <linux/uio.h>	// struct iovec, struct dmabuf_cmsg

#include "socket.h"

#include "memory_provider.h"

#define CTRL_DATA_SIZE	CMSG_SPACE(sizeof(int) * 100)
#define BACKLOG		15

#define ERROR(...) do {					\
	snprintf(error, BUFSIZ, __VA_ARGS__);		\
} while(false)

struct server {
	Memory context;
	Memory buffer;
	size_t size;

	int sockfd;
};

static char error[BUFSIZ];
extern MemoryProvider gp, hp;

Server server_setup(Memory context, size_t size, char *address, int port)
{
	Server server;

	server = (Server) malloc(sizeof(struct server));
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
	server->size = size;

	if (listen(server->sockfd, BACKLOG) == -1) {
		ERROR("failed to listen(): %s", strerror(errno));
		goto SOCKET_DESTROY;
	}

	server->buffer = memory_provider_alloc(hp, size);
	if (server->buffer == NULL) {
		ERROR("failed to malloc(): %s", strerror(errno));
		goto SOCKET_DESTROY;
	}

	return server;

SOCKET_DESTROY:		(void) socket_destroy(server->sockfd);
FREE_SERVER:		memory_provider_free(hp, server->buffer);
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
		int ret = recv(clnt_fd, ((char *) server->buffer) + recvlen, 
		 			server->size - recvlen, 0);
		if (ret == -1) {
			ERROR("failed to recv(): %s", strerror(errno));
			goto SOCKET_DESTROY;
		}

		if (ret == 0)
			break;

		recvlen += ret;
	}

	if (memory_provider_allow_access(hp, gp, server->buffer) == -1)
		return -1;

	ret = memory_provider_copy(
		gp, context, server->buffer, server->size
	);
	if (ret == -1) {
		ERROR("failed to amdgpu_memory_provider->memcpy_to(): %s",
		       memory_provider_get_error(gp));
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

	int last_token;

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

			ret = memory_provider_copy(
				gp, (Memory) (((char *) server->context) + recvlen),
				(Memory) (((char *) dmabuf) + dmabuf_cmsg->frag_offset),
				dmabuf_cmsg->frag_size
			);
			if (ret == -1) {
				ERROR("failed to amdgpu_memory_provider->"
	  			      "memmove_to(): %s",
				      memory_provider_get_error(gp));
				goto SOCKET_DESTROY;
			}
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

	/*
	if (memory_provider_wait(gp) == -1)
		return -1;
	*/

	return 0;

SOCKET_DESTROY:	(void) socket_destroy(clnt_fd);
RETURN_ERROR:	return -1;
}

void server_cleanup(Server server)
{
	socket_destroy(server->sockfd);
	memory_provider_free(hp, server->buffer);
	free(server);
}

char *server_get_error(void)
{
	return error;
}
