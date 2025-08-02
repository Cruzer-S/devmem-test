#ifndef SERVER_H__
#define SERVER_H__

#include "amdgpu_membuf_provider.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct server *Server;

Server server_setup(int sockfd, struct amdgpu_membuf_buffer *);

int server_run_as_tcp(Server );
int server_run_as_dma(Server );

void server_cleanup(Server );

#endif
