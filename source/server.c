#include "server.h"

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <unistd.h>

#include <linux/uio.h>
#define __iovec_defined

#include <net/if.h>

#include "ncdevmem.h"
#include "logger.h"

#include "memory.h"
#include "socket.h"

#include "amdgpu_memory_provider.h"
#include "amdgpu_membuf_provider.h"
#include "amdgpu_dmabuf_provider.h"

#define INFO(...) log(INFO, __VA_ARGS__)
#define ERR(...) do {		\
	log(__VA_ARGS__);	\
	exit(EXIT_FAILURE);	\
} while (true)

#define PAGE_SIZE	4096
#define BUFFER_SIZE	(PAGE_SIZE * NUM_PAGES)

#define NUM_PAGES	16000

#define TOKEN_SIZE	4096
#define MAX_FRAGS	1024
#define MAX_TOKENS	128

#define NUM_CTRL_DATA	1
#define CTRL_DATA_SIZE	CMSG_SPACE(sizeof(struct dmabuf_cmsg) * NUM_CTRL_DATA)

static size_t readlen, total;
static int clnt_sock;
static struct dmabuf_token token;
static uint64_t frag_start, frag_end;
static int dmabuf_id;

size_t aligned, not_aligned;

hipStream_t myStream;

static const char *cmsg_type_str(int type)
{
	switch (type) {
	case SCM_DEVMEM_DMABUF:	return "SCM_DEVMEM_DMABUF";
	case SCM_DEVMEM_LINEAR:	return "SCM_DEVMEM_LINEAR";	
	}

	return "unknown";
}

static void flush_dmabuf(size_t start, size_t end)
{
	if (readlen + (end - start) >= BUFFER_SIZE)
		readlen = 0;

	hipMemcpyAsync(
		membuf->memory + readlen, dmabuf->memory + start,
		end - start, hipMemcpyDeviceToDevice, myStream
	);

	readlen += end - start;
	total += end - start;
}

static int free_frags(void)
{
	int ret;

	hipStreamSynchronize(myStream);

	ret = setsockopt(
		clnt_sock, SOL_SOCKET,
		SO_DEVMEM_DONTNEED,
		&token, sizeof(struct dmabuf_token)
	);
	if (ret == -1)
		ERR(PERRN, "failed to setsockopt(): ");

	return ret;
}

static void handle_message(struct msghdr *msg)
{	
	struct dmabuf_cmsg *dmabuf_cmsg;

	for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg);
	     cmsg; cmsg = CMSG_NXTHDR(msg, cmsg))
	{
		dmabuf_cmsg = (struct dmabuf_cmsg *) CMSG_DATA(cmsg);

		if (cmsg->cmsg_type != SCM_DEVMEM_DMABUF) {
			log(WARN, "can't handle %s message",
       				  cmsg_type_str(cmsg->cmsg_type));
			continue;
		}

		if (dmabuf_cmsg->dmabuf_id != dmabuf_id) {
			log(WARN, "invalid dmabuf_id: %d (expected %d)\n",
			           dmabuf_cmsg->dmabuf_id, dmabuf_id);
			continue;
		}
	
		if (token.token_count == 0) {
			token.token_start = dmabuf_cmsg->frag_token;
			frag_start = frag_end = dmabuf_cmsg->frag_offset;
		}

		if (frag_end != dmabuf_cmsg->frag_offset) {
			flush_dmabuf(frag_start, frag_end);
			frag_start = frag_end = dmabuf_cmsg->frag_offset;
			not_aligned++;
		} else {
			aligned ++;
		}

		frag_end += dmabuf_cmsg->frag_size;

		token.token_count++;
	}

	if (token.token_count + NUM_CTRL_DATA >= MAX_FRAGS) {
		flush_dmabuf(frag_start, frag_end);

		if (free_frags() != token.token_count)
			log(ERRN, "failed to free_frags()");

		token.token_count = 0;
	}
}

static void server_dma_start(void)
{
	struct iovec iovec;
	struct msghdr msg;

	char iobuffer[BUFSIZ];
	char ctrl_data[CTRL_DATA_SIZE];

	int ret;

	hipStreamCreate(&myStream);

	token.token_count = 0;
	dmabuf_id = ncdevmem_get_dmabuf_id(ncdevmem);

	iovec.iov_base = iobuffer;
	iovec.iov_len = BUFSIZ;

	msg.msg_iov = &iovec;
	msg.msg_iovlen = 1;

	msg.msg_control = ctrl_data;
	msg.msg_controllen = CTRL_DATA_SIZE;

	not_aligned = aligned = 0;

	while (true) {
		ret = recvmsg(clnt_sock, &msg, MSG_SOCK_DEVMEM);
		if (ret == -1)
			ERR(PERRN, "failed to recvmsg(): ");

		if (ret == 0) {	
			INFO("close from %d", clnt_sock);
			flush_dmabuf(frag_start, frag_end);
			break;
		}

		handle_message(&msg);
	}

	INFO("Alinged: %zu\tNot-Aligned: %zu", aligned, not_aligned);

	hipStreamDestroy(myStream);
}

static void server_tcp_start(void)
{
	while (true) {
		int ret = recv(
			clnt_sock, buffer + readlen,
			BUFFER_SIZE - readlen, 0
		);
		if (ret == -1)
			ERR(PERRN, "failed to recv(): ");

		if (ret == 0) {
			INFO("close from %d", clnt_sock);
			break;
		}

		readlen += ret;
		total += ret;

		if (readlen >= BUFFER_SIZE) {
			amdgpu_membuf_provider.memcpy_to(
				membuf, buffer, 0, readlen
			);
			readlen = 0;
		}
	}


}

void server_start(bool is_dma)
{
	struct timespec start, end;
	double seconds;

	clnt_sock = socket_accept();
	if (clnt_sock == -1)
		ERR(PERRN, "failed to accept(): ");

	readlen = total = 0;

	clock_gettime(CLOCK_MONOTONIC, &start);
	if (is_dma)
		server_dma_start();
	else
		server_tcp_start();
	clock_gettime(CLOCK_MONOTONIC, &end);

	seconds = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

	log(INFO, "transferred: %.2f MiB in %.3f seconds",
     		   ((double) total) / (1024.0 * 1024.0), seconds);
	log(INFO, "speed: %.2f Gbps", (total * 8.0) / (seconds * 1e9));

	close(clnt_sock);

	INFO("total: %zu", total);
}
