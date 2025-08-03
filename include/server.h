#ifndef SERVER_H__
#define SERVER_H__

#include "memory_provider.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct server *Server;

Server server_setup(Memory , char *address, int port);

int server_run_as_tcp(Server );
int server_run_as_dma(Server );

void server_cleanup(Server );

char *server_get_error(void);

#endif
