#include "server.h"

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
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
#define IOBUFSIZ	819200

#define PAGE_SHIFT	12
#define MAX_TOKENS	128
#define MAX_FRAGS	1024

#define NUM_CTRL_DATA	10000
#define CTRL_DATA_SIZE	(CMSG_SPACE(sizeof(struct dmabuf_cmsg)) * NUM_CTRL_DATA)

static size_t total_received;
static int clnt_sock;

static size_t frag_start, frag_end;
static int dmabuf_id;

size_t aligned, non_aligned;

static const char *cmsg_type_str(int type)
{
	switch (type) {
	case SCM_DEVMEM_DMABUF:	return "SCM_DEVMEM_DMABUF";
	case SCM_DEVMEM_LINEAR:	return "SCM_DEVMEM_LINEAR";	
	}

	return "unknown";
}

static void handle_message(struct msghdr *msg, size_t buffer_size)
{
	struct dmabuf_cmsg *dmabuf_cmsg;
	struct dmabuf_token token;
	struct cmsghdr *cmsg;
	int ret;

	token.token_count = 0;
	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg))
	{
		if (cmsg->cmsg_type != SCM_DEVMEM_DMABUF) {
			log(WARN, "can't handle %s message",
       				  cmsg_type_str(cmsg->cmsg_type));
			continue;
		}

		dmabuf_cmsg = (struct dmabuf_cmsg *) CMSG_DATA(cmsg);	
		if (dmabuf_cmsg->dmabuf_id != dmabuf_id) {
			log(WARN, "invalid dmabuf_id: %d (expected %d)\n",
			           dmabuf_cmsg->dmabuf_id, dmabuf_id);
			continue;
		}

		/*
		log(INFO, "received frag_page=%-6llu, in_page_offset=%-6llu, frag_offset=%-10p, frag_size=%6u, token=%-6u, total_received_received=%lu, diff=%u",
			  dmabuf_cmsg->frag_offset >> PAGE_SHIFT,
      			  dmabuf_cmsg->frag_offset % getpagesize(),
      			  dmabuf_cmsg->frag_offset,
      			  dmabuf_cmsg->frag_size, dmabuf_cmsg->frag_token,
			  total_received, dmabuf_cmsg->frag_offset - frag_end);
      		*/

		if (token.token_count == 0)
			token.token_start = dmabuf_cmsg->frag_token;
	
		if (frag_end == -1) {
			frag_start = frag_end = dmabuf_cmsg->frag_offset;
		} else {
			if (frag_end == dmabuf_cmsg->frag_offset) {
				aligned++;
			} else {
				hipMemcpy(
					membuf->memory + total_received,
					dmabuf->memory + frag_start,
					frag_end - frag_start,
					hipMemcpyDeviceToDevice
				);
				frag_start = frag_end = dmabuf_cmsg->frag_offset;
				non_aligned++;
			}
		}

		frag_end += dmabuf_cmsg->frag_size;	
		token.token_count++;

		total_received += dmabuf_cmsg->frag_size;
	}

	hipMemcpy(
		membuf->memory + total_received,
		dmabuf->memory + frag_start,
		frag_end - frag_start,
		hipMemcpyDeviceToDevice
	);

	ret = setsockopt(clnt_sock, SOL_SOCKET,
			 SO_DEVMEM_DONTNEED,
			 &token, sizeof(struct dmabuf_token));
	if (ret != token.token_count)
		ERR(PERRN, "failed to setsockopt(): ");	
}

static void server_dma_start(size_t buffer_size)
{
	struct iovec iovec;
	struct msghdr msg;

	char iobuffer[IOBUFSIZ];
	char ctrl_data[CTRL_DATA_SIZE];

	int ret;

	dmabuf_id = ncdevmem_get_dmabuf_id(ncdevmem);

	while (true) {
		iovec.iov_base = iobuffer;
		iovec.iov_len = IOBUFSIZ;

		msg.msg_iov = &iovec;
		msg.msg_iovlen = 1;

		msg.msg_control = ctrl_data;
		msg.msg_controllen = CTRL_DATA_SIZE;

		ret = recvmsg(clnt_sock, &msg, MSG_SOCK_DEVMEM);
		if (ret == -1)
			ERR(PERRN, "failed to recvmsg(): ");

		if (ret == 0) {	
			INFO("close from %d", clnt_sock);
			break;
		}

		if (msg.msg_flags & MSG_CTRUNC) {
			ERR(ERRN, "fatal, cmsg truncated: ctrl_len: %zu ",
       				   CTRL_DATA_SIZE);
		}

		// log(INFO, "recvmsg ret=%d", ret);

		handle_message(&msg, buffer_size);
	}
}

static void server_tcp_start(size_t buffer_size)
{
	size_t readlen = 0;

	while (true) {
		int ret = recv(
			clnt_sock, buffer, buffer_size, 0
		);
		if (ret == -1)
			ERR(PERRN, "failed to recv(): ");

		if (ret == 0) {
			INFO("close from %d", clnt_sock);
			break;
		}

		readlen += ret;
		total_received += ret;

		if (readlen >= buffer_size) {
			amdgpu_membuf_provider.memcpy_to(
				membuf, buffer, 0, buffer_size
			);
			readlen = 0;
		}
	}
}

void server_start(size_t buffer_size, bool is_dma)
{
	struct timespec start, end;
	double seconds, mib;

	clnt_sock = socket_accept();
	if (clnt_sock == -1)
		ERR(PERRN, "failed to accept(): ");

	log(INFO, "accept client: %d", clnt_sock);

	aligned = non_aligned = 0;
	total_received = 0;
	frag_end = -1;

	clock_gettime(CLOCK_MONOTONIC, &start);
	if (is_dma)
		server_dma_start(buffer_size);
	else
		server_tcp_start(buffer_size);
	clock_gettime(CLOCK_MONOTONIC, &end);

	mib = total_received / (1024.0 * 1024.0);
	seconds = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

	log(INFO, "alinged: %zu\tnon-aligned: %zu", aligned, non_aligned);
	log(INFO, "transferred: %.2f MiB in %.3f seconds", mib, seconds);
	log(INFO, "speed: %.2f Gbps", (total_received * 8.0) / (1e9 * seconds));

	close(clnt_sock);

	INFO("total_received: %zu", total_received);
}
