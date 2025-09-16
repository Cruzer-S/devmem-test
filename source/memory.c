#include "memory.h"

#include <stdlib.h>	// malloc(), free()
#include <string.h>	// strerror()
#include <errno.h>	// errno
#include <stdbool.h>	// false
#include <stdio.h>	// BUFSIZ, snprintf()

#include "memory_provider.h"

#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>

#define SEED	10

#define ERROR(...) do {				\
	snprintf(error, BUFSIZ, __VA_ARGS__);	\
} while (false)

MemoryProvider gp;
MemoryProvider hp;

static char error[BUFSIZ];

Memory memory_allocate(MemoryProvider mp, size_t size)
{
	Memory memory;

	memory = memory_provider_alloc(mp, size);
	if (memory == NULL) {
		ERROR("failed to amdgpu_memory_provider->alloc(): %s",
		      memory_provider_get_error(mp));
		return NULL;
	}

	return memory;
}

int memory_export(MemoryProvider mp, Memory memory, size_t size)
{
	hsa_status_t status;
	uint64_t offset;
	int dmabuf_fd;
	int ret;

	status = hsa_amd_portable_export_dmabuf(
		memory, size, &dmabuf_fd, &offset
	);
	if (status != HSA_STATUS_SUCCESS) {
		const char *message;
		hsa_status_string(status, &message);
		ERROR("failed to amdgpu_memory_export_dmabuf(): %s", message);
		goto FREE_MEMORY;
	}

	return dmabuf_fd;

FREE_MEMORY:	(void) memory_provider_free(gp, memory);
RETURN_ERROR:	return -1;
}

int memory_close(int dmabuf_fd)
{
	return hsa_amd_portable_close_dmabuf(dmabuf_fd) == HSA_STATUS_SUCCESS 
	       ? 0 : -1;
}

int memory_free(MemoryProvider mp, Memory memory)
{
	int ret;

	ret = memory_provider_free(mp, memory);
	if (ret == -1) {
		ERROR("failed to amdgpu_memory_provider->free(): %s",
		      memory_provider_get_error(mp));
		return -1;
	}

	return 0;
}

int memory_initialize(Memory memory, size_t size)
{
	Memory buffer;
	int ret;

	buffer = memory_provider_alloc(hp, size);
	if (buffer == NULL) {
		ERROR("failed to malloc(): %s", strerror(errno));
		goto RETURN_ERROR;
	}

	for (size_t i = 0; i < size; i++)
		((char *) buffer)[i] = i % SEED;

	if (memory_provider_allow_access(hp, gp, buffer) == -1) {
		ERROR("failed to memory_provider_allow_access(): %s",
		      memory_provider_get_error(hp));
		goto FREE_BUFFER;
	}

	ret = memory_provider_copy(hp, memory, buffer, size);
	if (ret == -1) {
		ERROR("failed to amdgpu_memory_provider->memcpy_to(): %s",
		      memory_provider_get_error(hp));
		goto FREE_BUFFER;
	}

	if (memory_provider_free(hp, buffer) == -1) {
		ERROR("failed to memory_provider_free(): %s",
		      memory_provider_get_error(hp));
		goto RETURN_ERROR;
	}

	return 0;

FREE_BUFFER:	memory_provider_free(hp, buffer);
RETURN_ERROR:	return -1;
}

int memory_validate(Memory memory, size_t size)
{
	Memory buffer;

	buffer = memory_provider_alloc(hp, size);
	if (buffer == NULL)
		return -1;

	if (memory_provider_allow_access(hp, gp, buffer) == -1)
		return -1;

	if (memory_provider_copy(hp, buffer, memory, size) == -1) {
		memory_provider_free(hp, buffer); 
		return -1;
	}

	for (size_t i = 0; i < size; i++) {
		if (((char *) buffer)[i] != i % SEED) {
			ERROR("invalid at %zu (expected %zu, but %d)",
			      i, i % SEED, ((char *) buffer)[i]);
			memory_provider_free(hp, buffer);
			return -1;
		}
	}

	if (memory_provider_free(hp, buffer) == -1)
		return -1;

	return 0;
}

const char *memory_get_error(void)
{
	return error;
}

int memory_init(void)
{
	gp = memory_provider_create("gfx1101");
	if (gp == NULL)
		goto RETURN_ERR;

	hp = memory_provider_create("AMD Ryzen 5 9600X 6-Core Processor");
	if (hp == NULL)
		goto DESTROY_MEMORY_PROVIDER;

	return 0;

DESTROY_MEMORY_PROVIDER: memory_provider_destroy(hp);
RETURN_ERR:		 return -1;
}

void memory_cleanup(void)
{
	memory_provider_destroy(gp);
	memory_provider_destroy(hp);
}
