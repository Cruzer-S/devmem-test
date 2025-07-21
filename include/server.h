#ifndef SERVER_H__
#define SERVER_H__

#include <stdbool.h>
#include <stddef.h>

void server_start(size_t buffer_size, bool is_dma);

#endif
