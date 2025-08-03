#ifndef CLIENT_H__
#define CLIENT_H__

#include "memory_provider.h"

#include <arpa/inet.h>	// struct sockaddr, socklen_t 
//
typedef struct client *Client;

Client client_setup(int sockfd, Memory context);

int client_run_as_tcp(Client client, struct sockaddr *, socklen_t );
void client_cleanup(Client client);

char *client_get_error(void);

#endif
