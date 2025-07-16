#include "server.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <unistd.h>

#include <linux/uio.h>
#define __iovec_defined

#include <net/if.h>

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

#define NUM_PAGES	16000 * 1024
#define BUFFER_SIZE	(getpagesize() * NUM_PAGES)

#define TOKEN_SIZE	4096
#define MAX_TOKENS	1024
#define MAX_FRAGSS	128

static size_t readlen = 0;
static int clnt_sock;
static struct dmabuf_token token = {
	.token_count = 0, .token_start = 0
};

static uint64_t token_start, token_end;

static const char *cmsg_type_str(int type)
{
	switch (type) {
	case SCM_DEVMEM_DMABUF:	return "SCM_DEVMEM_DMABUF";
	case SCM_DEVMEM_LINEAR:	return "SCM_DEVMEM_LINEAR";	
	}

	return "unknown";
}

void flush_dmabuf(size_t start, size_t size)
{
	amdgpu_dmabuf_provider.memmove_to(
		dmabuf, membuf->memory + readlen,
		start, size
	);

	readlen += size;
}

void free_token(void)
{
	int ret = setsockopt(
		clnt_sock, SOL_SOCKET,
		SO_DEVMEM_DONTNEED,
		&token, sizeof(struct dmabuf_token)
	);
	if (ret == -1)
		ERR(PERRN, "failed to setsockopt(): ");
}

static void handle_message(struct msghdr *msg)
{	
	struct dmabuf_cmsg *dmabuf_cmsg;

	for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg);
	     cmsg; cmsg = CMSG_NXTHDR(msg, cmsg))
	{
		dmabuf_cmsg = (struct dmabuf_cmsg *) CMSG_DATA(cmsg);

		if (cmsg->cmsg_type != SCM_DEVMEM_DMABUF)
			continue;

		if (token.token_count == 0) {
			token.token_start = dmabuf_cmsg->frag_token;

			token_start = dmabuf_cmsg->frag_offset;
			token_end = token_start;
		}

		if (token_end != dmabuf_cmsg->frag_offset) {
			flush_dmabuf(token_start, token_end - token_start);

			token_start = dmabuf_cmsg->frag_offset;
			token_end = token_start;
		}

		token.token_count++;
		token_end += dmabuf_cmsg->frag_size;

		if (token.token_count >= MAX_TOKENS) {
			flush_dmabuf(token_start, token_end - token_start);
			free_token();

			token.token_count = 0;
		}
	}
}

static void server_dma_start(void)
{
	struct iovec iovec;
	struct msghdr msg;

	char iobuffer[BUFSIZ];
	char ctrl_data[CMSG_SPACE(sizeof(struct dmabuf_cmsg))];

	int ret;

	memset(iobuffer, 0x00, BUFSIZ);

	while (true) {
		memset(&msg, 0x00, sizeof(struct msghdr));

		iovec.iov_base = iobuffer;
		iovec.iov_len = BUFSIZ;

		msg.msg_iov = &iovec;
		msg.msg_iovlen = 1;

		msg.msg_control = ctrl_data;
		msg.msg_controllen = CMSG_SPACE(sizeof(struct dmabuf_cmsg));

		ret = recvmsg(clnt_sock, &msg, MSG_SOCK_DEVMEM);
		if (ret == -1)
			ERR(PERRN, "failed to recvmsg(): ");

		if (ret == 0) {	
			INFO("close from %d", clnt_sock);

			flush_dmabuf(token_start, token_end - token_start);
			free_token();
			break;
		}

		handle_message(&msg);
	}
}

static void server_tcp_start(void)
{
	char *buffer;

	buffer = malloc(BUFFER_SIZE);
	if (buffer == NULL)
		ERR(ERRN, "failed to malloc(): ");

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
	}

	amdgpu_membuf_provider.memcpy_to(membuf, buffer, 0, readlen);

	free(buffer);
}

void server_start(bool is_dma)
{
	clnt_sock = socket_accept();
	if (clnt_sock == -1)
		ERR(PERRN, "failed to accept(): ");

	if (is_dma)
		server_dma_start();
	else
		server_tcp_start();

	close(clnt_sock);

	INFO("readlen: %zu", readlen);
}
