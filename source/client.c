#include "client.h"

#include <stdio.h>	// BUFSIZ
#include <string.h>	// strerror()
#include <errno.h>	// errno
#include <stdlib.h>	// malloc()
#include <stddef.h>	// size_t
#include <stdbool.h>	// false

#include <sys/socket.h>

#include "memory_provider.h"

#include "socket.h"

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


void client_cleanup(Client client)
{
	free(client->buffer);
	free(client);
}

char *client_get_error(void)
{
	return error;
}
