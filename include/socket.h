#ifndef SOCKET_H__
#define SOCKET_H__

#include <stdbool.h>

int socket_create(char *address, int port);

int socket_connect(int fd, char *address, int port);

int socket_destroy(int sockfd);

char *socket_get_error(void);

#endif
