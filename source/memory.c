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

#define SEED	100

struct amdgpu_membuf_buffer *membuf;
struct amdgpu_dmabuf_buffer *dmabuf;
struct ncdevmem *ncdevmem;
char *buffer;

void memory_setup(size_t size, int ifindex, int queue, bool is_server)
{
	if (ifindex >= 0 && queue > 0) { // Use DMA
		dmabuf = amdgpu_dmabuf_provider.alloc(size);
		if (dmabuf == NULL)
			ERR(ERRN, "failed to provider->alloc()");

		if (is_server)
			ncdevmem = ncdevmem_setup(ifindex, queue, 1, dmabuf->fd);
		else
			ncdevmem = ncdevmem_setup_tx(ifindex, queue, 1, dmabuf->fd);

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

	buffer = malloc(size);
	if (buffer == NULL)
		ERR(ERRN, "failed to malloc(): ");
}

void memory_initialize(size_t size)
{
	for (int i = 0; i < size; i++)
		buffer[i] = i % SEED;

	amdgpu_membuf_provider.memcpy_to(dmabuf, buffer, 0, size);

	memset(buffer, 0x00, size);
}

bool memory_validate(size_t size)
{
	int count = 0;
	amdgpu_membuf_provider.memcpy_from(buffer, membuf, 0, size);

	for (int i = 0; i < size; i++) {
		if (buffer[i] != (i % SEED)) {
			printf("invalid at %d [%d:%d]\n", i, buffer[i], i % SEED);
			count++;
		}

		if (count > 10)
			return false;
	}

	return true;
}

void memory_cleanup(void)
{
	if (ncdevmem)	ncdevmem_cleanup(ncdevmem);
	if (dmabuf)	amdgpu_dmabuf_provider.free(dmabuf);

	amdgpu_membuf_provider.free(membuf);

	free(buffer);
}
