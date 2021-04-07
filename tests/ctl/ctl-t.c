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

#include "common.h"

#include <afs/afsctl.h>
#include <opr/lock.h>

static char *sock_path;
static char *onlyin_arg = "input arg for _ONLYIN";
static char *onlyout_arg = "output argument for _ONLYOUT";
static char *botharg_in = "input argument for _BOTHARG";
static char *botharg_out = "output arg for _BOTHARG";
static char *hang_data = "packet data for _HANG";

static struct {
    enum afsctl_req_op op;
    int server_running;

    opr_mutex_t lock;
    opr_cv_t cv;
} data;

static int
test_request(struct afsctl_call *ctl, struct afsctl_req_inargs *in_args,
	     struct afsctl_req_outargs *out_args)
{
    afs_int32 code = 0;

    opr_mutex_enter(&data.lock);
    opr_Assert(!data.server_running);
    data.server_running = 1;
    is_int(data.op, in_args->op, "server received expected op");
    opr_mutex_exit(&data.lock);

    switch (in_args->op) {
    case TESTCTL_OP_NOARG:
	break;

    case TESTCTL_OP_ONLYIN:
	is_string(onlyin_arg, in_args->u.testctl_onlyin, "_ONLYIN arg matches");
	break;

    case TESTCTL_OP_ONLYOUT:
	out_args->u.testctl_onlyout = strdup(onlyout_arg);
	if (out_args->u.testctl_onlyout == NULL) {
	    goto enomem;
	}
	break;

    case TESTCTL_OP_BOTHARG:
	is_string(botharg_in, in_args->u.testctl_botharg, "_BOTHARG in arg matches");
	out_args->u.testctl_botharg = strdup(botharg_out);
	if (out_args->u.testctl_botharg == NULL) {
	    goto enomem;
	}
	break;

    case TESTCTL_OP_FAIL:
	code = in_args->u.testctl_fail;
	opr_Assert(code != 0);
	goto done;

    case TESTCTL_OP_HANG:
	{
	    struct afsctl_pkt pkt;
	    struct testctl_inargs_hang *hangin = &in_args->u.testctl_hang;

	    memset(&pkt, 0, sizeof(pkt));
	    pkt.ptype = TESTCTL_PKT_HANG;
	    pkt.u.testctl_hang = hang_data;

	    code = afsctl_send_pkt(ctl, &pkt);
	    is_int(0, code, "hang send pkt succeeded");
	    if (code != 0) {
		goto done;
	    }

	    code = afsctl_wait_recv(ctl, hangin->timeout_ms);
	    if (hangin->should_timeout) {
		is_int(ETIMEDOUT, code, "wait_recv call timed out");
	    } else {
		is_int(0, code, "wait_recv call succeeded");
	    }
	}
	break;

    default:
	code = ENOTSUP;
	goto done;
    }

    code = 0;

 done:
    opr_mutex_enter(&data.lock);
    opr_Assert(data.server_running);
    data.server_running = 0;
    opr_cv_broadcast(&data.cv);
    opr_mutex_exit(&data.lock);
    return code;

 enomem:
    code = ENOMEM;
    goto done;
}

static void
set_op(struct afsctl_req_inargs *inargs, enum afsctl_req_op op)
{
    memset(inargs, 0, sizeof(*inargs));
    inargs->op = op;
    opr_mutex_enter(&data.lock);
    data.op = op;
    opr_mutex_exit(&data.lock);
}

int
main(void)
{
    static const afs_uint32 server_type = 1234;
    int code;
    char *dirname;
    struct afsctl_serverinfo sinfo;
    struct afsctl_clientinfo cinfo;
    struct afsctl_server *srv = NULL;
    struct afsctl_call *ctl = NULL;
    struct afsctl_pkt pkt;
    struct afsctl_req_inargs in_args;
    struct afsctl_req_outargs out_args;

    memset(&sinfo, 0, sizeof(sinfo));
    memset(&cinfo, 0, sizeof(cinfo));
    memset(&pkt, 0, sizeof(pkt));
    memset(&in_args, 0, sizeof(in_args));
    memset(&out_args, 0, sizeof(out_args));

    plan(34);

    opr_mutex_init(&data.lock);
    opr_cv_init(&data.cv);

    dirname = afstest_mkdtemp();
    sock_path = afstest_asprintf("%s/test.ctl.sock", dirname);

    sinfo.sock_path = sock_path;

    /* Start ctl server. */

    code = afsctl_server_create(&sinfo, &srv);
    is_int(EINVAL, code,
	   "afsctl_server_create without server_type fails with EINVAL");

    sinfo.server_type = server_type;

    code = afsctl_server_create(&sinfo, &srv);
    opr_Assert(code == 0);

    code = afsctl_server_reg(srv, TESTCTL_OP_PREFIX, test_request);
    opr_Assert(code == 0);

    code = afsctl_server_listen(srv);
    opr_Assert(code == 0);

    /* Run ctl client calls against server. */

    cinfo.sock_path = sock_path;

    set_op(&in_args, TESTCTL_OP_NOARG);
    code = afsctl_client_call(&cinfo, &in_args, NULL);
    is_int(EINVAL, code,
	   "client call without server_type fails with EINVAL");

    set_op(&in_args, TESTCTL_OP_NOARG);
    cinfo.server_type = server_type + 1;
    code = afsctl_client_call(&cinfo, &in_args, NULL);
    is_int(EPROTOTYPE, code,
	   "client call with wrong server_type fails with EPROTOTYPE");

    cinfo.server_type = server_type;

    set_op(&in_args, TESTCTL_OP_NOARG);
    code = afsctl_client_call(&cinfo, &in_args, NULL);
    is_int(0, code, "TESTCTL_OP_NOARG call succeeded");

    set_op(&in_args, TESTCTL_OP_ONLYIN);
    in_args.u.testctl_onlyin = onlyin_arg;
    code = afsctl_client_call(&cinfo, &in_args, NULL);
    is_int(0, code, "TESTCTL_OP_ONLYIN call succeeded");

    set_op(&in_args, TESTCTL_OP_ONLYOUT);
    code = afsctl_client_call(&cinfo, &in_args, &out_args);
    is_int(0, code, "TESTCTL_OP_ONLYOUT call succeeded");
    is_int(TESTCTL_OP_ONLYOUT, out_args.op, "outargs op matches");
    is_string(onlyout_arg, out_args.u.testctl_onlyout, "outargs string matches");
    xdrfree_afsctl_req_outargs(&out_args);

    set_op(&in_args, TESTCTL_OP_BOTHARG);
    in_args.u.testctl_botharg = botharg_in;
    code = afsctl_client_call(&cinfo, &in_args, &out_args);
    is_int(0, code, "TESTCTL_OP_BOTHARG call succeeded");
    is_int(TESTCTL_OP_BOTHARG, out_args.op, "bothargs out op matches");
    is_string(botharg_out, out_args.u.testctl_botharg, "bothargs out string matches");
    xdrfree_afsctl_req_outargs(&out_args);

    set_op(&in_args, TESTCTL_OP_FAIL);
    in_args.u.testctl_fail = 420;
    code = afsctl_client_call(&cinfo, &in_args, NULL);
    is_int(420, code, "TESTCTL_OP_FAIL call fails with 420");



    set_op(&in_args, TESTCTL_OP_HANG);
    in_args.u.testctl_hang.timeout_ms = 1;
    in_args.u.testctl_hang.should_timeout = 1;

    code = afsctl_client_start(&cinfo, &in_args, &ctl);
    opr_Assert(code == 0);

    code = afsctl_recv_pkt(ctl, TESTCTL_PKT_HANG, &pkt);
    opr_Assert(code == 0);
    is_int(TESTCTL_PKT_HANG, pkt.ptype, "hang packet type matches");
    is_string(hang_data, pkt.u.testctl_hang, "hang packet string matches");

    code = afsctl_wait_recv(ctl, 100);
    is_int(0, code, "client wait_recv succeeded");

    code = afsctl_client_end(ctl, TESTCTL_OP_HANG, NULL);
    is_int(0, code, "client_end succeeded");

    afsctl_call_destroy(&ctl);


    set_op(&in_args, TESTCTL_OP_HANG);
    in_args.u.testctl_hang.timeout_ms = 500;
    in_args.u.testctl_hang.should_timeout = 0;

    code = afsctl_client_start(&cinfo, &in_args, &ctl);
    opr_Assert(code == 0);

    code = afsctl_recv_pkt(ctl, TESTCTL_PKT_HANG, &pkt);
    opr_Assert(code == 0);
    is_int(TESTCTL_PKT_HANG, pkt.ptype, "hang packet type matches");
    is_string(hang_data, pkt.u.testctl_hang, "hang packet string matches");

    /* Wait 20ms to try to make sure the server thread is waiting inside
     * afsctl_wait_recv(). */
    code = afsctl_wait_recv(ctl, 20);
    is_int(ETIMEDOUT, code, "client wait_recv timed out");

    code = afsctl_client_end(ctl, TESTCTL_OP_HANG, NULL);
    is_int(0, code, "client_end succeeded");

    afsctl_call_destroy(&ctl);

    /* Wait for the server thread to finish running. It should finish quickly,
     * since we ended the ctl. */
    opr_mutex_enter(&data.lock);
    if (data.server_running) {
	struct timespec waittime;
	struct timeval tv;
	memset(&waittime, 0, sizeof(waittime));
	memset(&tv, 0, sizeof(tv));

	opr_Verify(gettimeofday(&tv, NULL) == 0);
	/* Wait 1 second for the server to finish. */
	waittime.tv_sec = tv.tv_sec + 1;
	waittime.tv_nsec = tv.tv_usec * 1000;
	code = opr_cv_timedwait(&data.cv, &data.lock, &waittime);
	opr_Assert(code == 0 || code == ETIMEDOUT);
    }
    is_int(0, data.server_running, "server thread finished");
    opr_mutex_exit(&data.lock);

    afstest_rmdtemp(dirname);

    return 0;
}
