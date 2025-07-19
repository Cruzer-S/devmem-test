#ifndef MEMORY_H__
#define MEMORY_H__

#include <stddef.h>
#include <stdbool.h>

#include "amdgpu_memory_provider.h"

extern struct amdgpu_membuf_buffer *membuf;
extern struct amdgpu_dmabuf_buffer *dmabuf;
extern struct ncdevmem *ncdevmem;
extern char *buffer;

void memory_setup(size_t size, int ifindex, int nqueue);

void memory_cleanup(void);

#endif
