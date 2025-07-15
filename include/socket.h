#ifndef SOCKET_H__
#define SOCKET_H__

#include <stdbool.h>

extern int sockfd;

void socket_create(char *address, int port, bool is_server);

void socket_connect(void);
int socket_accept(void);

void socket_destroy(void);

#endif
