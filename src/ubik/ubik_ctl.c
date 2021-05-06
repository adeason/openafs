/*
 * Copyright (c) 2021 Sine Nomine Associates.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* ubik_ctl.c - Code for implementing the 'openafs-ctl *db-info' et al
 * subcommands for openafs-ctl. */

#include <afsconfig.h>
#include <afs/param.h>

#include <roken.h>

#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <afs/cmd.h>
#include <afs/com_err.h>
#include <afs/afsctl.h>

#include "ubik_internal.h"

struct ubikctl {
    char *db_descr;

    struct afsctl_clientinfo cinfo;
};

enum {
    OPT_reason,
    OPT_ctl_socket,
};

static int preamble(struct ubikctl *uctl, struct cmd_syndesc *as);

static void
AFS_ATTRIBUTE_FORMAT(__printf__, 2, 3)
print_error(int code, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "\n%s: ", getprogname());
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (code != 0) {
	fprintf(stderr, ": %s\n", afs_error_message(code));
    } else {
	fprintf(stderr, "\n");
    }
}

static void
postamble(struct ubikctl **a_uctl)
{
    struct ubikctl *uctl = *a_uctl;
    if (uctl == NULL) {
	return;
    }
    *a_uctl = NULL;

    free(uctl->cinfo.sock_path);
    free(uctl->cinfo.message);
    free(uctl);
}

static int
preamble(struct ubikctl *uctl, struct cmd_syndesc *as)
{
    cmd_OptionAsString(as, OPT_reason, &uctl->cinfo.message);
    cmd_OptionAsString(as, OPT_ctl_socket, &uctl->cinfo.sock_path);

    return 0;
}

static int
DbInfoCmd(struct cmd_syndesc *as, void *rock)
{
    struct ubikctl *uctl = rock;
    struct afsctl_req_inargs inargs;
    struct afsctl_call *ctlcall = NULL;
    struct afsctl_pkt pkt;
    int code;

    memset(&inargs, 0, sizeof(inargs));
    memset(&pkt, 0, sizeof(pkt));

    code = preamble(uctl, as);
    if (code != 0) {
	goto error;
    }

    inargs.op = UBIKCTL_OP_DBINFO;
    code = afsctl_client_start(&uctl->cinfo, &inargs, &ctlcall);
    if (code != 0) {
	print_error(code, "Failed to get db info");
	goto error;
    }

    printf("%s database info:\n", uctl->db_descr);

    for (;;) {
	struct ubikctl_dbinfo *dbinfo;

	code = afsctl_recv_pkt(ctlcall, UBIKCTL_PKT_DBINFO, &pkt);
	if (code != 0) {
	    print_error(code, "Error receiving db info");
	    goto error;
	}

	dbinfo = pkt.u.ubikctl_dbinfo;
	if (dbinfo == NULL) {
	    /* eof */
	    break;
	}

	printf("  %s: %s\n", dbinfo->key, dbinfo->val);

	xdrfree_afsctl_pkt(&pkt);
    }

    code = afsctl_client_end(ctlcall, inargs.op, NULL);
    if (code != 0) {
	print_error(code, "Failed to finish getting db info");
	goto error;
    }

    code = 0;

 done:
    postamble(&uctl);
    xdrfree_afsctl_pkt(&pkt);
    afsctl_call_destroy(&ctlcall);
    return code;

 error:
    code = -1;
    goto done;
}

static void
common_opts(struct cmd_syndesc *as)
{
    cmd_AddParmAtOffset(as, OPT_reason, "-reason", CMD_SINGLE, CMD_OPTIONAL,
			"reason to log for operation");
    cmd_AddParmAtOffset(as, OPT_ctl_socket, "-ctl-socket", CMD_SINGLE,
			CMD_OPTIONAL,
			"path to ctl unix socket");
}

static char *
AFS_ATTRIBUTE_FORMAT(__printf__, 2, 3)
do_snprintf(struct rx_opaque *buf, const char *fmt, ...)
{
    va_list ap;
    int nbytes;

    va_start(ap, fmt);
    nbytes = vsnprintf(buf->val, buf->len, fmt, ap);
    va_end(ap);

    opr_Assert(nbytes < buf->len);
    return buf->val;
}

int
ubikctl_CreateSyntax(struct ubikctl_info *info)
{
    static char buf_name[64];
    static char buf_descr[128];

    struct rx_opaque name = {
	.val = buf_name,
	.len = sizeof(buf_name),
    };
    struct rx_opaque descr = {
	.val = buf_descr,
	.len = sizeof(buf_descr),
    };
    char *prefix = info->cmd_prefix;
    char *db = info->db_descr;
    struct cmd_syndesc *ts;
    struct ubikctl *uctl = NULL;

    uctl = calloc(1, sizeof(*uctl));
    if (uctl == NULL) {
	goto error;
    }

    uctl->db_descr = strdup(db);
    if (uctl->db_descr == NULL) {
	goto error;
    }

    uctl->cinfo.server_type = info->server_type;

    ts = cmd_CreateSyntax(do_snprintf(&name, "%sdb-info", prefix),
			  DbInfoCmd, uctl, 0,
			  do_snprintf(&descr, "get %s info", db));
    common_opts(ts);

    return 0;

 error:
    if (uctl != NULL) {
	free(uctl->db_descr);
	free(uctl);
    }
    return -1;
}
