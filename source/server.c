#include "server.h"

#include <stdio.h>	// BUFSIZ
#include <stdbool.h>	// true, false
#include <stdlib.h>	// malloc()
#include <string.h>	// strerror()
#include <errno.h>	// errno

#include <sys/socket.h>	// accept(), recv(), send(), etc.

#include "socket.h"

#include "memory_provider.h"

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

SOCKET_DESTROY:	(void) socket_destroy(server->sockfd);
FREE_SERVER:	free(server);
RETURN_NULL:	return NULL;
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
