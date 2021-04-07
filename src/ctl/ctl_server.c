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
 * Let up to MAX_CALLS calls run in parallel before we start refusing new
 * calls. This is pretty low for now just because we don't expect a lot of
 * calls over afsctl. Raise as needed; increasing this should not have much
 * negative impact.
 */
#define MAX_CALLS 8

/* Allow for registering up to AFSCTL_SVC_MAX services (raise as needed). */
#define AFSCTL_SVC_MAX 8
#define AFSCTL_SVC_MASK(op) (op & 0xFFFF0000)

#define ACCEPT_BACKLOG 10

struct ctl_service {
    afs_uint32 prefix;
    afsctl_reqfunc req_func;
};

/* Context for an afsctl server. */
struct afsctl_server {
    /* The socket we accept() new calls on. */
    int accept_sock;

    /* Is our accept() thread running? */
    int thread_running;

    /* How many services have been registered? */
    int n_services;

    /* The server_type for this server. */
    afs_uint32 server_type;

    /* Registered services. */
    struct ctl_service services[AFSCTL_SVC_MAX];

    /* How many calls are running. Protected by 'lock' */
    int n_calls;

    opr_mutex_t lock;
};

typedef void *(thread_func)(void *);
static void
spawn_thread(thread_func func, void *rock)
{
    pthread_attr_t attr;
    pthread_t tid;

    opr_Verify(pthread_attr_init(&attr) == 0);
    opr_Verify(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) == 0);
    opr_Verify(pthread_create(&tid, &attr, func, rock) == 0);
}

/*
 * Destroy a server-side ctl call. In this file, don't call afsctl_call_destroy
 * directly. Instead, call this, which will cleanup our server-side stuff, and
 * then call afsctl_call_destroy.
 */
static void
server_call_destroy(struct afsctl_call **a_ctl)
{
    struct afsctl_call *ctl = *a_ctl;
    if (ctl == NULL) {
	return;
    }
    *a_ctl = NULL;

    if (ctl->server != NULL) {
	struct afsctl_server *srv = ctl->server;

	opr_mutex_enter(&srv->lock);
	opr_Assert(srv->n_calls > 0);
	srv->n_calls--;
	opr_mutex_exit(&srv->lock);

	ctl->server = NULL;
    }

    afsctl_call_destroy(&ctl);
}

/* Send the _SHELLO packet and receive the _CHELLO packet. */
static int
server_hello(struct afsctl_call *ctl, struct afsctl_pkt *chello_pkt)
{
    int code;
    struct afsctl_pkt pkt;

    memset(&pkt, 0, sizeof(pkt));
    pkt.ptype = AFSCTL_PKT_SHELLO;
    pkt.u.shello_version = AFSCTL_SHELLO_VERSION;

    code = afsctl_send_pkt(ctl, &pkt);
    if (code != 0) {
	goto done;
    }

    code = afsctl_recv_pkt(ctl, AFSCTL_PKT_CHELLO, chello_pkt);
    if (code != 0) {
	goto done;
    }

 done:
    return code;
}

static struct ctl_service *
find_service(struct afsctl_server *srv, afs_uint32 op)
{
    afs_uint32 prefix = AFSCTL_SVC_MASK(op);
    int svc_i;

    for (svc_i = 0; svc_i < srv->n_services; svc_i++) {
	struct ctl_service *svc = &srv->services[svc_i];
	if (svc->prefix == prefix) {
	    return svc;
	}
    }
    return NULL;
}

/* The thread for handing a new client request. This gets spawned on every new
 * request that we handle. */
static void *
req_thread(void *rock)
{
    struct afsctl_call *ctl = rock;
    struct ctl_service *svc;
    struct afsctl_pkt chello;
    struct afsctl_pkt sbye;
    struct afsctl_req_inargs *in_args;
    struct afsctl_req_outargs *out_args;
    int code;

    memset(&chello, 0, sizeof(chello));
    memset(&sbye, 0, sizeof(sbye));

    opr_threadname_set("afsctl request");

    code = server_hello(ctl, &chello);
    if (code != 0) {
	goto done;
    }

    opr_Assert(chello.ptype == AFSCTL_PKT_CHELLO);
    in_args = &chello.u.chello.in_args;

    if (chello.u.chello.server_type != ctl->server->server_type) {
	code = EPROTOTYPE;
	goto abort;
    }

    ctl->message = chello.u.chello.message;
    chello.u.chello.message = NULL;

    sbye.ptype = AFSCTL_PKT_SBYE;
    out_args = &sbye.u.out_args;
    out_args->op = in_args->op;

    svc = find_service(ctl->server, in_args->op);
    if (svc == NULL) {
	code = EOPNOTSUPP;
	goto abort;
    }

    code = (*svc->req_func)(ctl, in_args, out_args);
    if (code != 0) {
	goto abort;
    }

    (void)afsctl_send_pkt(ctl, &sbye);

 done:
    server_call_destroy(&ctl);
    xdrfree_afsctl_pkt(&chello);
    xdrfree_afsctl_pkt(&sbye);
    return NULL;

 abort:
    opr_Assert(code != 0);
    ctl_send_abort(ctl, code);
    goto done;
}

static int
handle_req(struct afsctl_server *srv, int sock)
{
    struct afsctl_call *ctl = NULL;
    int code = 0;

    code = ctl_call_create(&sock, &ctl);
    if (code != 0) {
	goto done;
    }

    /*
     * If we have too many requests running, don't accept any more; instead,
     * send a "busy" message to the client and close the socket.
     */
    opr_mutex_enter(&srv->lock);
    if (srv->n_calls > MAX_CALLS) {
	code = EBUSY;
    } else {
	srv->n_calls++;
	ctl->server = srv;
    }
    opr_mutex_exit(&srv->lock);
    if (code != 0) {
	ctl_send_abort(ctl, code);
	goto done;
    }

    spawn_thread(req_thread, ctl);

    /* 'req_thread' is responsible for our ctl now; don't destroy it here. */
    ctl = NULL;

 done:
    if (sock >= 0) {
	close(sock);
    }
    server_call_destroy(&ctl);
    return code;
}

static void *
accept_thread(void *rock)
{
    struct afsctl_server *srv = rock;

    opr_threadname_set("afsctl accept");

    for (;;) {
	int code;
	int sock;
#ifdef SOCK_CLOEXEC
	sock = accept4(srv->accept_sock, NULL, NULL, SOCK_CLOEXEC);
#else
	sock = accept(srv->accept_sock, NULL, NULL);
#endif
	if (sock < 0) {
	    code = errno;
	    ViceLog(0, ("afsctl: Error %d accepting socket\n", code));
	    sleep(1);
	    continue;
	}

	code = handle_req(srv, sock);
	if (code != 0) {
	    ViceLog(0, ("afsctl: Error %d handling request\n", code));
	    continue;
	}
    }

    return NULL;
}

static void
server_free(struct afsctl_server **a_srv)
{
    struct afsctl_server *srv = *a_srv;
    if (srv == NULL) {
	return;
    }
    *a_srv = NULL;

    /* We haven't implemented shutting down a server after the accept thread
     * has started. So if we're here, it had better not be running. */
    opr_Assert(!srv->thread_running);

    if (srv->accept_sock >= 0) {
	close(srv->accept_sock);
	srv->accept_sock = -1;
    }
    opr_mutex_destroy(&srv->lock);
    free(srv);
}

char *
afsctl_call_message(struct afsctl_call *ctl)
{
    if (ctl->message == NULL) {
	return "";
    }
    return ctl->message;
}

/**
 * Create an afsctl server instance.
 *
 * This doesn't cause the server to start serving requests yet; you need to
 * register services with afsctl_server_reg, and then call
 * afsctl_server_listen; then we'll start serving requests.
 *
 * Note that an afsctl server cannot (currently) be shut down after it has
 * been started; we effectively shut down when the process exits.
 *
 * @return errno error codes
 */
int
afsctl_server_create(struct afsctl_serverinfo *sinfo,
		     struct afsctl_server **a_srv)
{
    int code;
    struct afsctl_server *srv = NULL;
    struct sockaddr_un addr;
    struct stat st;

    memset(&addr, 0, sizeof(addr));
    memset(&st, 0, sizeof(st));

    if (sinfo->server_type == 0) {
	code = EINVAL;
	goto done;
    }

    srv = calloc(1, sizeof(*srv));
    if (srv == NULL) {
	code = errno;
	goto done;
    }

    opr_mutex_init(&srv->lock);
    srv->accept_sock = -1;
    srv->server_type = sinfo->server_type;

    code = ctl_socket(sinfo->server_type, sinfo->sock_path, &srv->accept_sock,
		      &addr);
    if (code != 0) {
	goto done;
    }

    /*
     * If the socket already exists on disk, unlink it before we bind. Don't
     * unlink it if it's a non-socket, in case someone accidentally specifies
     * some other path.
     */
    code = stat(addr.sun_path, &st);
    if (code == 0 && S_ISSOCK(st.st_mode)) {
	(void)unlink(addr.sun_path);
    }

    code = bind(srv->accept_sock, &addr, sizeof(addr));
    if (code != 0) {
	code = errno;
	ViceLog(0, ("afsctl: Error %d binding to %s\n", code, addr.sun_path));
	goto done;
    }

    code = listen(srv->accept_sock, ACCEPT_BACKLOG);
    if (code != 0) {
	code = errno;
	ViceLog(0, ("afsctl: Error %d listening to socket\n", code));
	goto done;
    }

    *a_srv = srv;
    srv = NULL;

 done:
    server_free(&srv);
    return code;
}

/**
 * Register an afsctl 'service' with the afsctl server.
 *
 * @param[in] srv   The afsctl server.
 * @param[in] svc_prefix    The opcode prefix to register (e.g.
 *                          TESTCTL_OP_PREFIX).
 * @param[in] req_func      The function to handle the requests.
 * @return errno error codes
 */
int
afsctl_server_reg(struct afsctl_server *srv, afs_uint32 svc_prefix,
		  afsctl_reqfunc req_func)
{
    struct ctl_service *svc;
    int code = 0;

    if (srv->n_services >= AFSCTL_SVC_MAX) {
	code = ENOSPC;
	goto done;
    }

    if (AFSCTL_SVC_MASK(svc_prefix) != svc_prefix) {
	code = EINVAL;
	goto done;
    }

    svc = find_service(srv, svc_prefix);
    if (svc != NULL) {
	code = EEXIST;
	goto done;
    }

    svc = &srv->services[srv->n_services];
    svc->prefix = svc_prefix;
    svc->req_func = req_func;

    srv->n_services++;

 done:
    return code;
}

/* Start accepting and handling afsctl server requests. */
int
afsctl_server_listen(struct afsctl_server *srv)
{
    if (srv->n_services == 0) {
	return ENOTCONN;
    }

    /* Start accept()ing calls. */
    spawn_thread(accept_thread, srv);
    srv->thread_running = 1;

    return 0;
}
