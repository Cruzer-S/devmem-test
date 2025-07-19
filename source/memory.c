#include "memory.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <net/if.h>

#include "ncdevmem.h"

#include "amdgpu_memory_provider.h"
#include "amdgpu_membuf_provider.h"
#include "amdgpu_dmabuf_provider.h"

#include "logger.h"

#define ERR(...) do {		\
	log(__VA_ARGS__);	\
	exit(EXIT_FAILURE);	\
} while (true)

struct amdgpu_membuf_buffer *membuf;
struct amdgpu_dmabuf_buffer *dmabuf;
struct ncdevmem *ncdevmem;

void memory_setup(size_t size, int ifindex, int queue)
{
	if (ifindex >= 0 && queue > 0) { // Use DMA
		dmabuf = amdgpu_dmabuf_provider.alloc(size);
		if (dmabuf == NULL)
			ERR(ERRN, "failed to provider->alloc()");

		ncdevmem = ncdevmem_setup(ifindex, queue, 1, dmabuf->fd);
		if (ncdevmem == NULL)
			ERR(ERRN, "failed to ncdevmem_setup(): %s",
       				  ncdevmem_get_error());
	} else {
		dmabuf = NULL;
		ncdevmem = NULL;
	}

	membuf = amdgpu_membuf_provider.alloc(size);
	if (membuf == NULL)
		ERR(ERRN, "failed to provider->alloc()");
}

void memory_cleanup(void)
{
	if (ncdevmem)	ncdevmem_cleanup(ncdevmem);
	if (dmabuf)	amdgpu_dmabuf_provider.free(dmabuf);

	amdgpu_membuf_provider.free(membuf);
}
