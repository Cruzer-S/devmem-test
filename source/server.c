#include "server.h"

#include <stdlib.h>
#include <string.h>

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

static size_t readlen = 0;
static int clnt_sock;

static const char *cmsg_type_str(int type)
{
	switch (type) {
	case SCM_DEVMEM_DMABUF:		return "SCM_DEVMEM_DMABUF";
	case SCM_DEVMEM_LINEAR:		return "SCM_DEVMEM_LINEAR";	
	}

	return "unknown";
}

static void handle_message(struct msghdr *msg)
{
	struct dmabuf_cmsg *dmabuf_cmsg;
	struct dmabuf_token token;

	int ret;

	for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg);
		     cmsg; cmsg = CMSG_NXTHDR(msg, cmsg))
	{
		/*
		INFO("cmsg->type: %s", cmsg_type_str(cmsg->cmsg_type));
		INFO("cmsg->len: %zu", cmsg->cmsg_len);
		INFO("cmsg->level: %d", cmsg->cmsg_level);
		*/

		dmabuf_cmsg = (struct dmabuf_cmsg *) CMSG_DATA(cmsg);

		if (cmsg->cmsg_type == SCM_DEVMEM_DMABUF) {
			/*
			INFO("\tfrag_size: %u", dmabuf_cmsg->frag_size);
			INFO("\tfrag_token: %u", dmabuf_cmsg->frag_token);
			INFO("\tfrag_offset: %llu", dmabuf_cmsg->frag_offset);
			INFO("\tflags: %u", dmabuf_cmsg->flags);
			INFO("\tid: %u", dmabuf_cmsg->dmabuf_id);
			*/

			amdgpu_dmabuf_provider.memmove_to(
				dmabuf, membuf->memory + readlen,
				dmabuf_cmsg->frag_offset,
				dmabuf_cmsg->frag_size
			);

			token.token_start = dmabuf_cmsg->frag_token;
			token.token_count = 1;

			ret = setsockopt(
				clnt_sock, SOL_SOCKET, SO_DEVMEM_DONTNEED,
				&token, sizeof(token)
			);
			if (ret == -1)
				ERR(PERRN, "failed to setsockopt(): ");
		} else if (cmsg->cmsg_type == SCM_DEVMEM_LINEAR) {
		}

		readlen += dmabuf_cmsg->frag_size;
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
			break;
		}

		INFO("ret: %d", ret);

		handle_message(&msg);
	}
}

static void server_tcp_start(void)
{
	char buffer[BUFSIZ];
	int ret;

	while (true) {
		ret = recv(clnt_sock, buffer, BUFSIZ, 0);
		if (ret == -1)
			ERR(PERRN, "failed to recv(): ");

		if (ret == 0) {
			INFO("close from %d", clnt_sock);
			break;
		}

		INFO("ret: %d", ret);

		amdgpu_membuf_provider.memcpy_to(membuf, buffer, readlen, ret);

		readlen += ret;
	}
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
