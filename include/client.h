#ifndef CLIENT_H__
#define CLIENT_H__

#include <stdbool.h>
#include <stddef.h>

void client_start(char *address, int port, bool is_dma, size_t buffer_size, char *ifname);

#endif
