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

/*
 * xdrctl - A simple XDR mechanism for sending XDR data over an 'afsctl' unix
 * domain socket. Note that:
 *
 * - integers are sent in net-byte-order, just to match normal XDR in case we
 * need to interact with any other XDR code.
 * - no buffering is done currently, but this could easily be added to the
 * xctl_ctx struct (but we'd probably also need some kind of flush function,
 * for sending).
 */

struct xctl_ctx {
    int sock;
    int code;
};

static int
ioloop_repeat(ssize_t nbytes, int *a_code, void **a_buf, size_t *a_len)
{
    size_t len = *a_len;
    char *buf = *a_buf;

    *a_code = 0;

    if (nbytes < 0) {
	*a_code = errno;
	goto done;
    }

    /* We read in exactly as many bytes as was remaining; we're done. */
    if (nbytes == len) {
	goto done;
    }

    /* If we only needed 'len' bytes, we had better not read in more. */
    opr_Assert(nbytes < len);

    if (nbytes == 0) {
	/* Premature eof? */
	*a_code = EIO;
	goto done;
    }

    len -= nbytes;
    *a_buf = buf + nbytes;
    goto repeat;

 repeat:
    return 1;

 done:
    return 0;
}

static int
ctl_sendall(int sock, void *buf, size_t len)
{
    ssize_t nbytes;
    int code = 0;
    do {
	nbytes = send(sock, buf, len, MSG_NOSIGNAL);
    } while (ioloop_repeat(nbytes, &code, &buf, &len));
    return code;
}

static int
ctl_recvall(int sock, void *buf, size_t len)
{
    ssize_t nbytes;
    int code = 0;
    do {
	nbytes = recv(sock, buf, len, MSG_WAITALL);
    } while (ioloop_repeat(nbytes, &code, &buf, &len));
    return code;
}

static bool_t
xctl_getbytes(XDR *xdrs, caddr_t addr, u_int len)
{
    struct xctl_ctx *xctl = (void*)xdrs->x_private;
    int code;
    code = ctl_recvall(xctl->sock, addr, len);
    if (code != 0) {
	xctl->code = code;
	return FALSE;
    }
    return TRUE;
}

static bool_t
xctl_putbytes(XDR *xdrs, caddr_t addr, u_int len)
{
    struct xctl_ctx *xctl = (void*)xdrs->x_private;
    int code;
    code = ctl_sendall(xctl->sock, addr, len);
    if (code != 0) {
	xctl->code = code;
	return FALSE;
    }
    return TRUE;
}

static bool_t
xctl_getint32(XDR *xdrs, afs_int32 * lp)
{
    afs_int32 val = 0;
    bool_t success = xctl_getbytes(xdrs, (void*)&val, sizeof(val));
    *lp = ntohl(val);
    return success;
}

static bool_t
xctl_putint32(XDR *xdrs, afs_int32 * lp)
{
    afs_int32 val = htonl(*lp);
    return xctl_putbytes(xdrs, (void*)&val, sizeof(val));
}

static u_int
xctl_getpos(XDR *xdrs)
{
    return 0;
}

static bool_t
xctl_setpos(XDR *xdrs, u_int pos)
{
    return FALSE;
}

static afs_int32 *
xctl_inline(XDR *xdrs, u_int len)
{
    return NULL;
}

static void
xctl_destroy(XDR *xdrs)
{
    struct xctl_ctx *xctl = (void*)xdrs->x_private;
    free(xctl);
}

static struct xdr_ops xdrctl_ops = {
    .x_getint32 = xctl_getint32,
    .x_putint32 = xctl_putint32,
    .x_getbytes = xctl_getbytes,
    .x_putbytes = xctl_putbytes,
    .x_getpostn = xctl_getpos,
    .x_setpostn = xctl_setpos,
    .x_inline = xctl_inline,
    .x_destroy = xctl_destroy
};

int
xdrctl_create(XDR *xdrs, int sock, enum xdr_op op)
{
    struct xctl_ctx *xctl;

    xctl = calloc(1, sizeof(*xctl));
    if (xctl == NULL) {
	return errno;
    }

    xctl->sock = sock;

    /*
     * Default to EIO, in case we don't set an actual error later. This is only
     * used in xdrctl_error(), and that should only be called if one our xdr
     * routines fails. If one of our routines fails, they should also set
     * xctl->code; if they haven't set it, then something weird is going on, so
     * default to an EIO error.
     */
    xctl->code = EIO;

    memset(xdrs, 0, sizeof(*xdrs));
    xdrs->x_ops = &xdrctl_ops;
    xdrs->x_op = op;
    xdrs->x_private = (void*)xctl;
    return 0;
}

/**
 * Retrieve the underlying xdrctl error.
 *
 * xdrctl sends and receives data via send() and recv() calls. If one of those
 * fails, we return an error to XDR, and set an internal error code to indicate
 * what the actual error is. Call this function after an xdr routine fails to
 * see what that underlying error code is.
 *
 * If we didn't see any underlying errors, we return EIO instead, as a default.
 *
 * @param[in] xdrs  The xdr context, initialized by xdrctl_create.
 * @param[in] code  The caller's existing error code. For convenience, if this
 *		    is nonzero, we just return this value instead.
 * @return The underlying xdrctl i/o error
 */
int
xdrctl_error(XDR *xdrs, int code)
{
    struct xctl_ctx *xctl = (void*)xdrs->x_private;
    if (code == 0) {
	code = xctl->code;
	/*
	 * If someone set xctl->code to 0, something is seriously wrong. It
	 * should only be set by xdrctl_create, or when one of our internal i/o
	 * functions fails.
	 */
	opr_Assert(code != 0);
    }
    return code;
}
