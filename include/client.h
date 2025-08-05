#ifndef CLIENT_H__
#define CLIENT_H__

#include "memory_provider.h"

typedef struct client *Client;

Client client_setup(Memory context, char *address, int port);

int client_run_as_tcp(Client , char *address, int port);
int client_run_as_dma(Client , Memory dmabuf, char *address, int port,
		      char *interface, int dmabuf_id);

void client_cleanup(Client );

char *client_get_error(void);

#endif
