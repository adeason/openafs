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

#ifndef OPENAFS_CTL_INTERNAL_H
#define OPENAFS_CTL_INTERNAL_H

#include <roken.h>

#include <afs/afsctl.h>
#include <afs/afsutil.h>
#include <rx/xdr.h>

#include <sys/socket.h>
#include <sys/un.h>

struct afsctl_call {
    int sock;

    char *message;

    struct afsctl_server *server;

    XDR xdrs_send;
    XDR xdrs_recv;
};

/*** ctl_common.c ***/

int ctl_socket(afs_uint32 server_type, const char *path, int *a_sock,
	       struct sockaddr_un *addr);
int ctl_call_create(int *a_sock, struct afsctl_call **a_ctl);
void ctl_send_abort(struct afsctl_call *ctl, int code);

/*** ctl_xdr.c ***/

int xdrctl_create(XDR *xdrs, int sock, enum xdr_op op);
int xdrctl_error(XDR *xdrs, int code);

#endif /* OPENAFS_CTL_INTERNAL_H */
