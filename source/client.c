#include "client.h"

#include "memory_provider.h"

#include <stdio.h>	// BUFSIZ
#include <string.h>	// strerror()
#include <errno.h>	// errno
#include <stdlib.h>	// malloc()
#include <stddef.h>	// size_t
#include <stdbool.h>	// false

#include <unistd.h>	// close()

#include <sys/socket.h>	// connect()
#include <arpa/inet.h>	// struct sockaddr_in

#define ERROR(...) do {					\
	snprintf(error, BUFSIZ, __VA_ARGS__);		\
} while(false)

struct client {
	int sockfd;
	Memory context;
	size_t size;
};

static char error[BUFSIZ];
static struct memory_provider *provider = &amdgpu_memory_provider;

Client client_setup(int sockfd, Memory context)
{
	Client client;

	client = malloc(sizeof(struct client));
	if (client == NULL) {
		ERROR("failed to malloc(): %s", strerror(errno));
		return NULL;
	}

	client->sockfd = sockfd;
	client->context = context;
	client->size = provider->get_size(context);

	return client;
}

int client_run_as_tcp(Client client,
		      struct sockaddr *sockaddr, socklen_t addrlen)
{
	size_t sendlen;
	char *ubuffer;
	size_t size;
	Memory context;
	int ret;

	size = client->size;
	context = client->context;

	ubuffer = malloc(size);
	if (ubuffer == NULL) {
		ERROR("failed to malloc(): %s", strerror(errno));
		goto RETURN_ERROR;
	}

	ret = connect(client->sockfd, sockaddr, addrlen);
	if (ret == -1) {
		ERROR("failed to connect(): %s", strerror(errno));
		goto FREE_BUFFER;
	}

	sendlen = 0;
	while (sendlen < size) {
		ret = provider->memcpy_from(
			ubuffer, context, sendlen, size - sendlen
		);
		if (ret == -1)
			ERROR("failed to "
	 		      "amdgpu_memory_provider->memcpy_from(): %s",
	 		      provider->get_error());

		ret = send(client->sockfd, ubuffer, size - sendlen, 0);
		if (ret == -1) {
			ERROR("failed to send(): %s", strerror(errno));
			goto FREE_BUFFER;
		}

		sendlen += ret;
	}

	free(ubuffer);

	return 0;

FREE_BUFFER:	free(ubuffer);
RETURN_ERROR:	return -1;
}


void client_cleanup(Client client)
{
	free(client);
}

char *client_get_error(void)
{
	return error;
}
