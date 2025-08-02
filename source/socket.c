#include "socket.h"

#include <stdio.h>		// sprintf(), BUFSIZ
#include <string.h>		// memset(), strerror()
#include <errno.h>		// errno

#include <unistd.h>		// close()

#include <sys/socket.h>		// socket(), bind(), setsockopt() ...
#include <arpa/inet.h>		// struct sockaddr_in

#define ERROR(...) do {				\
	snprintf(error, BUFSIZ, __VA_ARGS__);	\
	return -1;				\
} while (false)

static char error[BUFSIZ];

static int socket_reuseaddr(int fd)
{
	int ret, opt;

	opt = 1;
	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret == -1)
		ERROR("failed to setsockopt(): %s", strerror(errno));

	opt = 1;
	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
	if (ret == -1)
		ERROR("failed to setsockopt(): %s", strerror(errno));

	return 0;
}

int socket_create(char *address, int port)
{
	struct sockaddr_in sockaddr;
	int sockfd;
	int ret;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
		ERROR("failed to socket(): %s", strerror(errno));

	memset(&sockaddr, 0x00, sizeof(struct sockaddr_in));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(port);
	sockaddr.sin_addr.s_addr = inet_addr(address);

	if (socket_reuseaddr(sockfd) == -1)
		return -1;

	ret = bind(sockfd, (struct sockaddr *) &sockaddr,
		   sizeof(struct sockaddr_in));
	if (ret == -1)
		ERROR("failed to bind(): %s", strerror(errno));

	return sockfd;
}

char *socket_get_error(void)
{
	return error;
}

void socket_destroy(int sockfd)
{
	close(sockfd);
}
