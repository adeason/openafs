/*
 * Copyright (c) 2021 Sine Nomine Associates. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * afsctl - A mechanism for issuing RPCs between processes on the same host
 * over a unix domain socket, primarily for the purpose of interacting with or
 * controlling daemons (hence the 'ctl', for 'control'). A general overview of
 * how this works:
 *
 * All communication between peers is done via XDR-encoded "packets" (see the
 * 'afsctl_pkt' type), and all packet types and payloads are defined in
 * afsctl_int.xg.
 *
 * Since this is a local-only protocol, there's no need to serialize data into
 * net-byte order, but we use XDR for convenience for serializing complex or
 * dynamically-sized objects. If ever desired, we could easily change the code
 * to use a variant of XDR that doesn't byte-swap the ints (for performance),
 * but at least for now we use normal XDR to avoid having a weird variant to
 * XDR floating around. Performance isn't a big concern here, since this is
 * intended for things like administrative operations or querying internal
 * state; we shouldn't constantly be handling e.g. multiple requests per
 * second.
 *
 * There is no security layer or authentication/authorization of any kind; we
 * just rely on the OS restricting access to the underlying unix socket
 * (typically requires root, since the socket is in afslocaldir).
 *
 * There is also not so much need for backwards compatibility. While it's
 * important to detect protocol mismatches (and bail out), we don't need to
 * actually work across versions, since the presumption is that the server and
 * client will be running the same release. The protocol is considered a
 * private interface, and can change across releases.
 *
 * When the server receives a request from a client, it spawns a new thread to
 * handle the request. The max number of requests we can serve in parallel is
 * MAX_REQS; if we're at that limit and another request comes in, we abort the request
 * and close the socket before spawning a thread.
 *
 * Requests can be long-lived (unlike SYNC, where that is not advised). That
 * means clients can issue a request to be immediately notified when something
 * happens without needing polling (e.g. we could issue a request for noticing
 * when ubik sync site status changes, even if that means we stay idle for
 * hours or months). The only limiting factor is exceeding the MAX_REQS limit,
 * which can easily be raised (the only downside is increased threads, memory
 * usage, file descriptors, etc).
 *
 * The server can abort a client request at any time by sending the client a
 * AFSCTL_PKT_ABORT packet and closing the socket, instead of sending the
 * packet of data that would normally be sent. The abort packet contains an
 * error code for the client to return. A client can prematurely end its call
 * by just closing the socket (with or without sending an abort).
 *
 * When a client connects to a server, we have both an opening and closing
 * handshake (separated by request-specific traffic). The sequence of steps
 * generally looks like this:
 *
 * - Client connects to the server socket, and waits for a reply (a
 * AFSCTL_PKT_SHELLO packet).
 * - If the server is too busy to accept a new request, the server aborts the
 * request with EBUSY, and closes the socket.
 * - If the server can accept a new request, the server sends a
 * AFSCTL_PKT_SHELLO packet, which contains a version the client can use to
 * check for compatibility.
 * - The client checks the AFSCTL_PKT_SHELLO packet, and (if the version is
 * okay) sends a AFSCTL_PKT_CHELLO packet, with the opcode for the requested
 * call and any input arguments. The _CHELLO packet also contains the
 * "server_type" (an arbitrary id to indicate we're talking to the ptserver,
 * vlserver, etc), and an arbitrary message string (defaults to the pid and
 * process name of the calling process).
 * - The client and server then send/receive call-specific packets.
 * - When the client is done, it shuts down its write-side half of the socket,
 * and waits for a AFSCTL_PKT_SBYE packet.
 * - When the server is done, it sends a AFSCTL_PKT_SBYE packet, and closes the
 * socket. The _SBYE packet contains the output arguments for the call, if any.
 * - The client receives the AFSCTL_PKT_SBYE packet, and closes its socket.
 *
 * The client can ignore the SBYE steps and just close the socket, if it
 * doesn't really care when the server side of the request is done or not, and
 * it doesn't need the ending output arguments (e.g., we're making some kind of
 * aync/best-effort ping to the server, or we've already received the data we
 * needed during the call, etc).
 *
 * Note that we do not currently support shutting down an afsctl server after
 * it has started running. This could be implemented, of course (but
 * interrupting the accept() thread is nontrivial across platforms), but no
 * callers currently need it. Just let the process die, and the sockets will
 * close.
 */

#include <afsconfig.h>
#include <afs/param.h>

#include <poll.h>

#include <afs/cellconfig.h>

#include "ctl_internal.h"

/*
 * Let up to 8 calls run in parallel before we start refusing new calls. This
 * is pretty low for now just because we don't expect a lot of calls over
 * afsctl. Raise as needed; increasing this should not have noticeable negative
 * impact.
 */
#define MAX_REQS 8

#ifdef SOCK_CLOEXEC
static const int sock_cloexec = SOCK_CLOEXEC;
#else
static const int sock_cloexec;
#endif

/* Create a unix socket and the sockaddr to use for it. */
int
ctl_socket(afs_uint32 server_type, const char *path, int *a_sock,
	   struct sockaddr_un *addr)
{
    int sock = -1;
    int code = 0;

    memset(addr, 0, sizeof(*addr));

    if (path == NULL) {
	switch (server_type) {
	case AFSCONF_PROTPORT:
	    path = AFSDIR_SERVER_PTSERVER_CTLSOCK_FILEPATH;
	    break;
	case AFSCONF_VLDBPORT:
	    path = AFSDIR_SERVER_VLSERVER_CTLSOCK_FILEPATH;
	    break;

	default:
	    ViceLog(0, ("afsctl: Internal error: no path for unknown server "
		    "type 0x%x\n", server_type));
	    code = ENOPROTOOPT;
	    goto done;
	}
    }

    sock = socket(AF_UNIX, SOCK_STREAM | sock_cloexec, 0);
    if (sock < 0) {
	code = errno;
	goto done;
    }

    if (strlcpy(addr->sun_path, path,
		sizeof(addr->sun_path)) >= sizeof(addr->sun_path)) {
	code = ENAMETOOLONG;
	goto done;
    }
    addr->sun_family = AF_UNIX;

    *a_sock = sock;
    sock = -1;

 done:
    if (sock >= 0) {
	close(sock);
    }
    return code;
}

int
afsctl_send_pkt(struct afsctl_call *ctl, struct afsctl_pkt *pkt)
{
    int code = 0;

    if (!xdr_afsctl_pkt(&ctl->xdrs_send, pkt)) {
	code = xdrctl_error(&ctl->xdrs_send, 0);
    }

    return code;
}

void
ctl_send_abort(struct afsctl_call *ctl, int code)
{
    struct afsctl_pkt pkt;

    memset(&pkt, 0, sizeof(pkt));
    pkt.ptype = AFSCTL_PKT_ABORT;
    pkt.u.abort_code = code;

    opr_Assert(code != 0);

    (void)afsctl_send_pkt(ctl, &pkt);
}

/**
 * Receive a packet on an afsctl call.
 *
 * @param[in] ctl   The ctl call.
 * @param[in] ptype The packet type we're expecting. If we see any other packet
 *		    type, it's an error.
 * @param[out] pkt  The received packet.
 *
 * @return errno error codes
 *
 * @post On success, pkt->ptype == ptype
 */
int
afsctl_recv_pkt(struct afsctl_call *ctl, int ptype, struct afsctl_pkt *pkt)
{
    int code;

    memset(pkt, 0, sizeof(*pkt));

    if (!xdr_afsctl_pkt(&ctl->xdrs_recv, pkt)) {
	code = xdrctl_error(&ctl->xdrs_recv, 0);
	goto done;
    }

    if (pkt->ptype == AFSCTL_PKT_ABORT) {
	code = pkt->u.abort_code;
	goto done;
    }

    if (pkt->ptype != ptype) {
	code = EPROTO;
	goto done;
    }

    code = 0;

 done:
    if (code != 0) {
	xdrfree_afsctl_pkt(pkt);
	memset(pkt, 0, sizeof(*pkt));
    }
    return code;
}

/**
 * Wait for peer to send data on an afsctl call.
 *
 * @param[in] ctl   The ctl call.
 * @param[in] a_ms  How many milliseconds to wait (or 0 to wait forever). If
 *		    this much time has passed and the peer hasn't sent any data
 *		    (or shutdown the socket), return ETIMEDOUT.
 * @return errno error codes
 * @retval ETIMEDOUT we timed out waiting for the peer to send something
 */
int
afsctl_wait_recv(struct afsctl_call *ctl, afs_uint32 a_ms)
{
    struct pollfd fds;
    int timeout_ms;
    int code;

    if (a_ms == 0) {
	/* Wait forever */
	timeout_ms = -1;
    } else {
	timeout_ms = a_ms;
    }

    memset(&fds, 0, sizeof(fds));
    fds.fd = ctl->sock;
    fds.events = POLLIN;

    code = poll(&fds, 1, timeout_ms);
    if (code < 0) {
	code = errno;
	/* Signals should be handled by another thread via softsig; make sure
	 * we don't accidentally report an EINTR as an error. */
	opr_Assert(code != EINTR);
	return code;
    }
    if (code == 0) {
	return ETIMEDOUT;
    }
    return 0;
}

int
ctl_call_create(int *a_sock, struct afsctl_call **a_ctl)
{
    struct afsctl_call *ctl;
    int code;

    *a_ctl = NULL;

    ctl = calloc(1, sizeof(*ctl));
    if (ctl == NULL) {
	code = errno;
	goto done;
    }

    ctl->sock = *a_sock;
    *a_sock = -1;

    code = xdrctl_create(&ctl->xdrs_send, ctl->sock, XDR_ENCODE);
    if (code != 0) {
	goto done;
    }

    code = xdrctl_create(&ctl->xdrs_recv, ctl->sock, XDR_DECODE);
    if (code != 0) {
	goto done;
    }

    *a_ctl = ctl;
    ctl = NULL;

 done:
    afsctl_call_destroy(&ctl);
    return code;
}

int
afsctl_call_shutdown_read(struct afsctl_call *ctl)
{
    int code;
    code = shutdown(ctl->sock, SHUT_RD);
    if (code != 0) {
	return errno;
    }
    return 0;
}

void
afsctl_call_destroy(struct afsctl_call **a_ctl)
{
    struct afsctl_call *ctl = *a_ctl;
    if (ctl == NULL) {
	return;
    }
    *a_ctl = NULL;

    /* If this is a server call, server_call_destroy() must have already
     * released its ref on the server instance. */
    opr_Assert(ctl->server == NULL);

    free(ctl->message);
    ctl->message = NULL;

    xdr_destroy(&ctl->xdrs_send);
    xdr_destroy(&ctl->xdrs_recv);

    if (ctl->sock >= 0) {
	close(ctl->sock);
	ctl->sock = -1;
    }

    free(ctl);
}
