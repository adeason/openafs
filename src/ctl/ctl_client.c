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

#include <afsconfig.h>
#include <afs/param.h>

#include "ctl_internal.h"

static int
client_hello(struct afsctl_call *ctl, afs_uint32 server_type, char *message,
	     struct afsctl_req_inargs *in_args)
{
    int code;
    struct afsctl_pkt pkt;

    memset(&pkt, 0, sizeof(pkt));

    if (server_type == 0) {
	code = EINVAL;
	goto done;
    }

    code = afsctl_recv_pkt(ctl, AFSCTL_PKT_SHELLO, &pkt);
    if (code == 0) {
	if (strcmp(pkt.u.shello_version, AFSCTL_SHELLO_VERSION) != 0) {
	    code = EPROTONOSUPPORT;
	}
    }
    xdrfree_afsctl_pkt(&pkt);
    if (code != 0) {
	goto done;
    }

    memset(&pkt, 0, sizeof(pkt));
    pkt.ptype = AFSCTL_PKT_CHELLO;
    pkt.u.chello.server_type = server_type;
    pkt.u.chello.message = message;
    pkt.u.chello.in_args = *in_args;

    code = afsctl_send_pkt(ctl, &pkt);
    if (code != 0) {
	goto done;
    }

 done:
    return code;
}

/**
 * Start a call to an afsctl server.
 *
 * @param[in] cinfo Connection info.
 * @param[in] in_args	Input arguments for the call. The caller must at least
 *			set in_args->op (to indicate what call is being made),
 *			and any associated in_args->u.* data for that call.
 * @param[out] a_ctl	On success, set to the call context for the call.
 *
 * @return errno error codes
 */
int
afsctl_client_start(struct afsctl_clientinfo *cinfo,
		    struct afsctl_req_inargs *in_args,
		    struct afsctl_call **a_ctl)
{
    int code;
    struct sockaddr_un addr;
    char *message;
    char *message_free = NULL;
    struct afsctl_call *ctl = NULL;
    int sock = -1;

    memset(&addr, 0, sizeof(addr));

    code = ctl_socket(cinfo->server_type, cinfo->sock_path, &sock, &addr);
    if (code != 0) {
	goto done;
    }

    code = ctl_call_create(&sock, &ctl);
    if (code != 0) {
	goto done;
    }

    code = connect(ctl->sock, &addr, sizeof(addr));
    if (code != 0) {
	code = errno;
	goto done;
    }

    message = cinfo->message;
    if (message == NULL) {
	if (asprintf(&message_free, "pid %d, %s", (int)getpid(),
						  getprogname()) < 0) {
	    message_free = NULL;
	    code = errno;
	    goto done;
	}
	message = message_free;
    }

    code = client_hello(ctl, cinfo->server_type, message, in_args);
    if (code != 0) {
	goto done;
    }

    *a_ctl = ctl;
    ctl = NULL;

 done:
    free(message_free);
    afsctl_call_destroy(&ctl);
    if (sock >= 0) {
	close(sock);
    }
    return code;
}

/**
 * End a client call.
 *
 * Use of this function is optional. When used, we'll wait for the server to
 * end its side of the request before returning. If you don't want to wait for
 * the server to finish (and don't care about any output arguments), just
 * destroy the call with afsctl_call_destroy.
 *
 * @param[in] ctl   The afsctl call.
 * @param[in] op    The call we are ending. If this doesn't match what the
 *		    server gives us, we'll return an EIO error.
 * @param[out] out_args	Optional. If not NULL, set to the output arguments
 *			returned by the server. If NULL, the output argument
 *			data is discarded.
 * @return errno error codes
 */
int
afsctl_client_end(struct afsctl_call *ctl, enum afsctl_req_op op,
		  struct afsctl_req_outargs *out_args)
{
    int code;
    struct afsctl_pkt pkt;

    memset(&pkt, 0, sizeof(pkt));

    /* If the server is waiting for more data from us, make sure it knows it's
     * not going to get any. */
    code = shutdown(ctl->sock, SHUT_WR);
    if (code != 0) {
	return errno;
    }

    code = afsctl_recv_pkt(ctl, AFSCTL_PKT_SBYE, &pkt);
    if (code != 0) {
	goto done;
    }

    if (pkt.u.out_args.op != op) {
	code = EIO;
	goto done;
    }

    if (out_args != NULL) {
	*out_args = pkt.u.out_args;
	memset(&pkt.u.out_args, 0, sizeof(pkt.u.out_args));
    }

 done:
    xdrfree_afsctl_pkt(&pkt);
    return code;
}

/**
 * Make a simple afsctl call.
 *
 * This is a simple convenience wrapper around afsctl_client_start,
 * afsctl_client_end, and afsctl_call_destroy.
 *
 * @param[in] cinfo Connection info.
 * @param[in] in_args	Input arguments. Caller must set at least in_args->op.
 * @param[out] out_args	Optional. If not NULL, populated with the output
 *			arguments of the call from the server.
 * @return errno error codes
 */
int
afsctl_client_call(struct afsctl_clientinfo *cinfo,
		   struct afsctl_req_inargs *in_args,
		   struct afsctl_req_outargs *out_args)
{
    int code;
    struct afsctl_call *ctl = NULL;

    code = afsctl_client_start(cinfo, in_args, &ctl);
    if (code != 0) {
	goto done;
    }

    code = afsctl_client_end(ctl, in_args->op, out_args);
    if (code != 0) {
	goto done;
    }

 done:
    afsctl_call_destroy(&ctl);
    return code;
}
