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
#define WARN(...) log(WARN, __VA_ARGS__)
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
static struct dmabuf_token token;

static int dmabuf_id;

static const char *C2S(int type)
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

	struct cmsghdr *cmsg;
	int ret;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg))
	{
		if (cmsg->cmsg_type != SCM_DEVMEM_DMABUF) {
			WARN("can't handle %s message", C2S(cmsg->cmsg_type));
			continue;
		}

		dmabuf_cmsg = (struct dmabuf_cmsg *) CMSG_DATA(cmsg);	
		if (dmabuf_cmsg->dmabuf_id != dmabuf_id) {
			WARN("invalid dmabuf_id: %d (expected %d)\n",
			     dmabuf_cmsg->dmabuf_id, dmabuf_id);
			continue;
		}
		
		hipMemcpy(
			membuf->memory + total_received,
			dmabuf->memory + dmabuf_cmsg->frag_offset,
			dmabuf_cmsg->frag_size,
			hipMemcpyDeviceToDevice
		);

		token = (struct dmabuf_token) {
      			.token_start = dmabuf_cmsg->frag_token,
			.token_count = 1
		};
		ret = setsockopt(clnt_sock, SOL_SOCKET,
			 	 SO_DEVMEM_DONTNEED,
			 	 &token, sizeof(struct dmabuf_token));
		if (ret != token.token_count)
			WARN("failed to setsockopt(): %d\t"
			     "token_count: %d", ret, token.token_count);

		total_received += dmabuf_cmsg->frag_size;
	}
}

static void server_dma_start(size_t buffer_size)
{
	struct iovec iovec;
	struct msghdr msg;

	char iobuffer[IOBUFSIZ];
	char ctrl_data[CTRL_DATA_SIZE];

	int ret;

	dmabuf_id = ncdevmem_get_dmabuf_id(ncdevmem);

	token.token_count = 0;
	token.token_start = 0;

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

	INFO("accept client: %d", clnt_sock);

	total_received = 0;

	clock_gettime(CLOCK_MONOTONIC, &start);
	if (is_dma)
		server_dma_start(buffer_size);
	else
		server_tcp_start(buffer_size);
	clock_gettime(CLOCK_MONOTONIC, &end);

	mib = total_received / (1024.0 * 1024.0);
	seconds = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

	INFO("transferred: %.2f MiB in %.3f seconds", mib, seconds);
	INFO("speed: %.2f Gbps", (total_received * 8.0) / (1e9 * seconds));

	close(clnt_sock);

	INFO("total_received: %zu", total_received);
}
