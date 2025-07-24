#include "socket.h"

#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>

#include <arpa/inet.h>

#include "logger.h"

#define ERR(...) do {		\
	log(__VA_ARGS__);	\
	exit(EXIT_FAILURE);	\
} while (true)

#define BACKLOG		15

static struct sockaddr_in sockaddr;
int sockfd;

static void socket_reuseaddr(int fd)
{
	int ret, opt;

	opt = 1;
	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret == -1)
		ERR(PERRN, "failed to setsockopt(): ");

	opt = 1;
	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
	if (ret == -1)
		ERR(PERRN, "failed to setsockup(): ");
}

static int setsockopt_linger(int fd)
{
	struct linger so_linger = { .l_onoff = 1, .l_linger = 0 };

	if (setsockopt(fd, SOL_SOCKET, SO_LINGER,
		       &so_linger, sizeof(so_linger)) == -1)
		ERR(PERRN, "failed to setsockopt(): ");

	return 0;
}

static void socket_nonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1)
		ERR(PERRN, "failed to fcntl(): ");

	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		ERR(PERRN, "failed to fcntl(): ");
}

void socket_create(char *address, int port, bool is_server)
{
	int ret;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
		ERR(PERRN, "failed to socket(): ");

	memset(&sockaddr, 0x00, sizeof(struct sockaddr_in));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(port);
	sockaddr.sin_addr.s_addr = inet_addr(address);

	socket_reuseaddr(sockfd);
	// setsockopt_linger(sockfd);
	/*
	if (is_server)
		socket_nonblock(sockfd);
	*/

	ret = bind(sockfd, (struct sockaddr *) &sockaddr,
		   sizeof(struct sockaddr_in));
	if (ret == -1)
		ERR(PERRN, "failed to bind(): ");

	if (is_server) {
		if (listen(sockfd, BACKLOG) == -1)
			ERR(PERRN, "failed to listen(): ");
	}
}

void socket_connect(char *address, int port)
{
	int ret;

	memset(&sockaddr, 0x00, sizeof(struct sockaddr_in));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(port);
	sockaddr.sin_addr.s_addr = inet_addr(address);

	do {
		ret = connect(sockfd, (struct sockaddr *) &sockaddr,
		      sizeof(struct sockaddr_in));
		if (ret == -1) {
			if (errno == EINPROGRESS)
				continue;

			if (errno == EALREADY)
				continue;

			ERR(PERRN, "failed to connect(): ");
		}
	} while (ret < 0);
}

int socket_accept(void)
{
	int ret;

	do {
		ret = accept(sockfd, NULL, 0);
		if (ret == -1) {
			if (errno == EAGAIN)
				continue;

			ERR(PERRN, "failed to accept(): ");
		}
	} while (ret < 0);

	return ret;
}

void socket_destroy(void)
{
	close(sockfd);
}
