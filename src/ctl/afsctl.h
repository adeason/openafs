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

#ifndef OPENAFS_AFSCTL_H
#define OPENAFS_AFSCTL_H

#include <afs/afsctl_int.h>

/*** common.c ***/

struct afsctl_pkt;
struct afsctl_call;

int afsctl_send_pkt(struct afsctl_call *ctl, struct afsctl_pkt *pkt);
int afsctl_recv_pkt(struct afsctl_call *ctl, int tag, struct afsctl_pkt *pkt);
int afsctl_wait_recv(struct afsctl_call *ctl, afs_uint32 timeout_ms);
int afsctl_call_shutdown_read(struct afsctl_call *ctl);
void afsctl_call_destroy(struct afsctl_call **a_ctl);

/*** server.c ***/

struct afsctl_server;
struct afsctl_req_inargs;
struct afsctl_req_outargs;

struct afsctl_serverinfo {
    afs_uint32 server_type;
    char *sock_path;
};

typedef int (*afsctl_reqfunc)(struct afsctl_call *ctl,
			      struct afsctl_req_inargs *inargs,
			      struct afsctl_req_outargs *outargs);

int afsctl_server_create(struct afsctl_serverinfo *sinfo,
			 struct afsctl_server **a_srv);
int afsctl_server_reg(struct afsctl_server *srv, afs_uint32 svc_prefix,
		      afsctl_reqfunc req_func);
int afsctl_server_listen(struct afsctl_server *srv);
char * afsctl_call_message(struct afsctl_call *ctl);

/*** client.c ***/

struct afsctl_clientinfo {
    afs_uint32 server_type;
    char *sock_path;
    char *message;
};

int afsctl_client_start(struct afsctl_clientinfo *cinfo,
			struct afsctl_req_inargs *in_args,
			struct afsctl_call **a_ctl);
int afsctl_client_end(struct afsctl_call *ctl, enum afsctl_req_op op,
		      struct afsctl_req_outargs *out_args);
int afsctl_client_call(struct afsctl_clientinfo *cinfo,
		       struct afsctl_req_inargs *inargs,
		       struct afsctl_req_outargs *outargs);

#endif /* OPENAFS_AFSCTL_H */
