#ifndef MEMORY_H__
#define MEMORY_H__

#include <stddef.h>	// size_t

#include "memory_provider.h"

Memory memory_allocate(size_t );
Memory memory_allocate_dmabuf(size_t , int *dmabuf_fd);

int memory_validate(Memory );
int memory_initialize(Memory );

int memory_free(Memory );
int memory_free_dmabuf(Memory );

const char *memory_get_error(void);

#endif
