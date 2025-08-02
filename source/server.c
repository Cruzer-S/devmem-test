#include "server.h"

#include <stdio.h>	// BUFSIZ
#include <stdbool.h>	// true, false
#include <stdlib.h>	// malloc()
#include <string.h>	// strerror()
#include <errno.h>	// errno

#include <unistd.h>	// close()

#include <sys/socket.h>	// accept(), recv(), send(), etc.

#include "memory_provider.h"

#define BACKLOG		15

#define ERROR(...) do {					\
	snprintf(error, BUFSIZ, __VA_ARGS__);		\
} while(false)

struct server {
	int sockfd;
	Memory context;
	size_t size;
};

static char error[BUFSIZ];
static struct memory_provider *provider = &amdgpu_memory_provider;

Server server_setup(int sockfd, Memory context)
{
	Server server;

	server = malloc(sizeof(struct server));
	if (server == NULL) {
		ERROR("failed to malloc(): %s", strerror(errno));
		return NULL;
	}

	server->sockfd = sockfd;
	server->context = context;
	server->size = provider->get_size(context);

	if (listen(sockfd, BACKLOG) == -1) {
		free(server);
		ERROR("failed to listen(): %s", strerror(errno));
		return NULL;
	}

	return server;
}

int server_run_as_tcp(Server server)
{
	int clnt_fd;
	size_t recvlen;
	char *ubuffer;
	size_t size;
	Memory context;

	size = server->size;
	context = server->context;

	ubuffer = malloc(size);
	if (ubuffer == NULL) {
		ERROR("failed to malloc(): %s", strerror(errno));
		goto RETURN_ERROR;
	}

	clnt_fd = accept(server->sockfd, NULL, 0);
	if (clnt_fd == -1) {
		ERROR("failed to accept(): %s", strerror(errno));
		goto FREE_BUFFER;
	}

	recvlen = 0;
	while (true) {
		int ret = recv(clnt_fd, ubuffer, size - recvlen, 0);
		if (ret == -1) {
			ERROR("failed to recv(): %s", strerror(errno));
			goto CLOSE_CLNT_FD;
		}

		if (ret == 0)
			break;

		ret = provider->memcpy_to(context, ubuffer, recvlen, ret);
		if (ret == -1) {
			ERROR("failed to amdgpu_memory_provider->memcpy_to(): "
	 		      "%s", provider->get_error());
			goto CLOSE_CLNT_FD;
		}

		recvlen += ret;
	}

	close(clnt_fd);
	free(ubuffer);

	return 0;

CLOSE_CLNT_FD:	close(clnt_fd);
FREE_BUFFER:	free(ubuffer);
RETURN_ERROR:	return -1;
}

void server_cleanup(Server server)
{
	free(server);
}

char *server_get_error(void)
{
	return error;
}
