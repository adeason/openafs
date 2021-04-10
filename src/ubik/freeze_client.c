/*
 * Copyright (c) 2020 Sine Nomine Associates.
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

/* ubik_freeze.c - client routines for the ubik freeze API */

#include <afsconfig.h>
#include <afs/param.h>

#include <roken.h>
#include <afs/afsctl.h>
#include <afs/cellconfig.h>
#include <afs/com_err.h>
#include <afs/dirpath.h>
#include <opr/time64.h>

#include "ubik_internal.h"

#ifdef AFS_PTHREAD_ENV

# define DEFAULT_TIMEOUT_MS (60 * 1000)

struct ubik_freeze_client {
    struct afsctl_clientinfo ctl_cinfo;	/**< afsctl connection info for talking
					 *   to the server process. */

    struct afsctl_call *frz_ctl;    /**< afsctl call for the freeze. */

    afs_uint64 freezeid;
    struct ubik_version64 db_vers;  /**< db version the server told us when the
				     * freeze started. */
    char *db_path;  /**< path to the db that the server told us when the freeze
		     *   started. */

    int nested;	    /**< are we a nested freeze? (that is, env vars like
		     *   OPENAFS_VL_FREEZE_ID are set). */
    int started;    /**< have we started the freeze? */
    int running;    /**< is the db currently frozen? */
    int timeout_ms; /**< max time the freeze can run */
    int need_sync;  /**< do we need to contact the sync site? */

    /* Formatted env var names for OPENAFS_VL_FREEZE_ID, et al */
    char *env_socket;
    char *env_freezeid;
    char *env_version;
    char *env_dbpath;
};

static void
AFS_ATTRIBUTE_FORMAT(__printf__, 1, 2)
printerr(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static int
ctlcall_freeze_start(struct ubik_freeze_client *freeze,
		     struct ufrzctl_inargs_freezedb *freezedb,
		     struct ufrzctl_pkt_frzinfo *frzinfo,
		     struct afsctl_call **a_ctl)
{
    struct afsctl_call *ctl = NULL;
    struct afsctl_req_inargs inargs;
    struct afsctl_pkt info_pkt;
    int code;

    memset(&inargs, 0, sizeof(inargs));
    memset(&info_pkt, 0, sizeof(info_pkt));

    inargs.op = UFRZCTL_OP_FREEZEDB;
    inargs.u.ufrz_freezedb = *freezedb;

    code = afsctl_client_start(&freeze->ctl_cinfo, &inargs, &ctl);
    if (code != 0) {
	goto done;
    }

    code = afsctl_recv_pkt(ctl, UFRZCTL_PKT_FRZINFO, &info_pkt);
    if (code != 0) {
	goto done;
    }

    *frzinfo = info_pkt.u.ufrz_frzinfo;

    *a_ctl = ctl;
    ctl = NULL;

 done:
    afsctl_call_destroy(&ctl);
    return code;
}

static int
ctlcall_freeze_end(struct ubik_freeze_client *freeze, afs_uint64 freezeid,
		   char *message, int abort, int force_abort)
{
    struct afsctl_clientinfo cinfo;
    struct afsctl_req_inargs inargs;

    memset(&inargs, 0, sizeof(inargs));

    cinfo = freeze->ctl_cinfo;
    if (message != NULL) {
	cinfo.message = message;
    }

    inargs.op = UFRZCTL_OP_END;
    inargs.u.ufrz_end.freezeid = freezeid;
    inargs.u.ufrz_end.abort = abort;
    inargs.u.ufrz_end.force_abort = force_abort;

    return afsctl_client_call(&cinfo, &inargs, NULL);
}

static int
env_get(struct ubik_freeze_client *freeze, char *suffix, char **a_name,
	char **a_value)
{
    char *name;
    char *prefix;
    afs_uint32 srvtype = freeze->ctl_cinfo.server_type;

    *a_name = NULL;
    *a_value = NULL;

    switch (srvtype) {
    case AFSCONF_PROTPORT:
	prefix = "PT";
	break;
    case AFSCONF_VLDBPORT:
	prefix = "VL";
	break;

    default:
	printerr("ubik: Internal error: bad srvtype 0x%x\n", srvtype);
	return UINTERNAL;
    }

    if (asprintf(&name, "OPENAFS_%s_%s", prefix, suffix) < 0) {
	return UNOMEM;
    }

    *a_name = name;
    *a_value = getenv(name);

    return 0;
}

static int
check_nested(struct ubik_freeze_client *freeze)
{
    afs_uint64 id;
    struct ubik_version64 vers;
    char *sock_path = NULL;
    char *id_str = NULL;
    char *vers_str = NULL;
    char *db_path = NULL;
    int code;
    int n_scan;

    memset(&vers, 0, sizeof(vers));

    code = env_get(freeze, "FREEZE_CTL_SOCKET", &freeze->env_socket, &sock_path);
    if (code != 0) {
	goto done;
    }

    code = env_get(freeze, "FREEZE_ID", &freeze->env_freezeid, &id_str);
    if (code != 0) {
	goto done;
    }

    code = env_get(freeze, "FREEZE_VERSION", &freeze->env_version, &vers_str);
    if (code != 0) {
	goto done;
    }

    code = env_get(freeze, "FREEZE_DB_PATH", &freeze->env_dbpath, &db_path);
    if (code != 0) {
	goto done;
    }

    if (sock_path == NULL || id_str == NULL || vers_str == NULL ||
	db_path == NULL) {
	goto done;
    }

    n_scan = sscanf(id_str, "%llu", &id);
    if (n_scan != 1) {
	printerr("ubik: Ignoring invalid %s (%s).\n",
		 freeze->env_freezeid, id_str);
	goto done;
    }

    n_scan = sscanf(vers_str, "%lld.%lld", &vers.epoch64.clunks, &vers.counter64);
    if (n_scan != 2) {
	printerr("ubik: Ignoring invalid %s (%s).\n", freeze->env_version,
		 vers_str);
	goto done;
    }

    freeze->db_path = strdup(db_path);
    if (freeze->ctl_cinfo.sock_path == NULL) {
	freeze->ctl_cinfo.sock_path = strdup(sock_path);
    }
    if (freeze->ctl_cinfo.sock_path == NULL || freeze->db_path == NULL) {
	code = UNOMEM;
	goto done;
    }

    freeze->freezeid = id;
    freeze->db_vers = vers;

    freeze->nested = 1;

    code = 0;

 done:
    return code;
}

static int
cinfo_copy(struct afsctl_clientinfo *src,
	   struct afsctl_clientinfo *dest)
{
    dest->server_type = src->server_type;
    dest->sock_path = NULL;
    dest->message = NULL;

    if (src->sock_path != NULL) {
	dest->sock_path = strdup(src->sock_path);
	if (dest->sock_path == NULL) {
	    return UNOMEM;
	}
    }

    if (src->message != NULL) {
	dest->message = strdup(src->message);
	if (dest->message == NULL) {
	    return UNOMEM;
	}
    }

    return 0;
}

/**
 * Init a freeze context.
 *
 * Note that this doesn't actually freeze the db; calling ubik_FreezeBegin does
 * that.
 *
 * @param[in] opts	Various options.
 * @param[out] a_freeze	Freeze context.
 * @return ubik error codes
 */
int
ubik_FreezeInit(struct ubik_freezeinit_opts *opts,
		struct ubik_freeze_client **a_freeze)
{
    struct ubik_freeze_client *freeze = NULL;
    int code = 0;

    freeze = calloc(1, sizeof(*freeze));
    if (freeze == NULL) {
	code = UNOMEM;
	goto done;
    }

    code = cinfo_copy(opts->fi_cinfo, &freeze->ctl_cinfo);
    if (code != 0) {
	goto done;
    }

    code = check_nested(freeze);
    if (code != 0) {
	goto done;
    }

    if (freeze->nested && opts->fi_nonest) {
	printerr("ubik: Refusing to run nested freeze for id %llu.\n",
		 freeze->freezeid);
	code = UBADTYPE;
	goto done;
    }

    if (freeze->ctl_cinfo.sock_path == NULL) {
	/* We can give afsctl a NULL sock_path, but we need to know the actual
	 * sock_path used, if we ubik_FreezeSetEnv later on. */
	code = afsctl_socket_path(freeze->ctl_cinfo.server_type,
				  &freeze->ctl_cinfo.sock_path);
	if (code != 0) {
	    goto done;
	}
    }

    freeze->timeout_ms = opts->fi_timeout_ms;
    if (freeze->timeout_ms == 0) {
	freeze->timeout_ms = DEFAULT_TIMEOUT_MS;
    }

    freeze->need_sync = opts->fi_needsync;

    *a_freeze = freeze;
    freeze = NULL;

 done:
    ubik_FreezeDestroy(&freeze);
    return code;
}

/**
 * Check whether a freeze is 'nested'.
 *
 * A 'nested' freeze means that this freeze was initialized inside the context
 * of another running freeze. We detect this by looking at various environment
 * variables, such as e.g. OPENAFS_VL_FREEZE_ID.
 *
 * @param[in] freeze	    Freeze context.
 * @param[out] a_freezeid   If we are a nested freeze and this is non-NULL,
 *			    this is set to the freezeid for the (existing)
 *			    freeze.
 * @retval 0 freeze is not nested
 * @retval 1 freeze is nested
 */
int
ubik_FreezeIsNested(struct ubik_freeze_client *freeze, afs_uint64 *a_freezeid)
{
    if (!freeze->nested) {
	return 0;
    }
    if (a_freezeid != NULL) {
	*a_freezeid = freeze->freezeid;
    }
    return 1;
}

static int
do_setenv(char *name, char *value)
{
    int code;

    opr_Assert(name != NULL);
    opr_Assert(value != NULL);

    code = setenv(name, value, 1);
    if (code != 0) {
	return UIOERROR;
    }
    return 0;
}

/**
 * Set the freeze environment variables.
 *
 * This sets several environment variables like OPENAFS_VL_FREEZE_ID, in order
 * to run some other command in the context of this freeze.
 *
 * @param[in] freeze	Freeze context.
 * @return ubik error codes
 */
int
ubik_FreezeSetEnv(struct ubik_freeze_client *freeze)
{
    char *id_str = NULL;
    char *vers_str = NULL;
    int code;

    if (freeze->nested) {
	/* If we're nested, the env should already be set. */
	return 0;
    }

    if (freeze->freezeid == 0) {
	printerr("ubik: Cannot SetEnv for freeze; freeze hasn't started yet.\n");
	code = UINTERNAL;
	goto done;
    }

    code = do_setenv(freeze->env_socket, freeze->ctl_cinfo.sock_path);
    if (code != 0) {
	goto done;
    }

    code = asprintf(&id_str, "%llu", freeze->freezeid);
    if (code < 0) {
	id_str = NULL;
	code = UNOMEM;
	goto done;
    }

    code = do_setenv(freeze->env_freezeid, id_str);
    if (code != 0) {
	goto done;
    }

    code = asprintf(&vers_str, "%lld.%lld", freeze->db_vers.epoch64.clunks,
		    freeze->db_vers.counter64);
    if (code < 0) {
	vers_str = NULL;
	code = UNOMEM;
	goto done;
    }

    code = do_setenv(freeze->env_version, vers_str);
    if (code != 0) {
	goto done;
    }

    code = do_setenv(freeze->env_dbpath, freeze->db_path);
    if (code != 0) {
	goto done;
    }

 done:
    free(id_str);
    free(vers_str);
    return code;
}

static void
doprint(FILE *fh, char *env_var)
{
    opr_Assert(env_var != NULL);
    fprintf(fh, "export %s=%s\n", env_var, getenv(env_var));
}

/**
 * Print freeze environment variables to the given file handle.
 *
 * @pre freeze is nested, or ubik_FreezeSetEnv has been called.
 *
 * @param[in] freeze	Freeze context.
 * @param[in] fh	The file handle to print to.
 */
void
ubik_FreezePrintEnv(struct ubik_freeze_client *freeze, FILE *fh)
{
    doprint(fh, freeze->env_socket);
    doprint(fh, freeze->env_freezeid);
    doprint(fh, freeze->env_version);
    doprint(fh, freeze->env_dbpath);
}

/**
 * Free the given freeze context.
 *
 * If the freeze is actively running, we end the freeze and wait for it to die
 * on the server before returning.
 *
 * @param[inout] a_freeze   The freeze context to free. If NULL, this is a
 *			    no-op. Set to NULL on return.
 */
void
ubik_FreezeDestroy(struct ubik_freeze_client **a_freeze)
{
    struct ubik_freeze_client *freeze = *a_freeze;
    if (freeze == NULL) {
	return;
    }
    *a_freeze = NULL;

    if (freeze->frz_ctl != NULL) {
	/*
	 * If we started a UFRZCTL_OP_FREEZEDB call on the server, wait for the
	 * call to finish. We want to do this even if the freeze was aborted,
	 * so we know that the freeze has ended on the server size before we
	 * return (so we know any partially-installed db has been reverted, and
	 * we know it's okay to start a new freeze, etc).
	 */
	(void)afsctl_client_end(freeze->frz_ctl, UFRZCTL_OP_FREEZEDB, NULL);
    }
    afsctl_call_destroy(&freeze->frz_ctl);

    free(freeze->ctl_cinfo.sock_path);
    free(freeze->ctl_cinfo.message);

    free(freeze->db_path);
    free(freeze->env_socket);
    free(freeze->env_freezeid);
    free(freeze->env_version);
    free(freeze->env_dbpath);

    free(freeze);
}

static int
check_version(char *db_path, struct ubik_version64 *version)
{
    struct ubik_version disk_vers32;
    struct ubik_version64 disk_vers;
    int code;

    memset(&disk_vers32, 0, sizeof(disk_vers32));
    memset(&disk_vers, 0, sizeof(disk_vers));

    code = uphys_getlabel_path(db_path, &disk_vers32);
    if (code != 0) {
	printerr("ubik: Cannot access db %s (code %d)\n", db_path, code);
	return code;
    }
    udb_v32to64(&disk_vers32, &disk_vers);

    if (udb_vcmp64(&disk_vers, version) != 0) {
	printerr("ubik: Error: db version on disk (%lld.%lld) disagrees with "
		 "server (%lld.%lld)\n",
		 disk_vers.epoch64.clunks, disk_vers.counter64,
		 version->epoch64.clunks, version->counter64);
	return USYNC;
    }
    return 0;
}

/**
 * Start freezing the ubik db.
 *
 * If we are a nested freeze, we don't actually talk to the server, since
 * whatever parent process that actually started the freeze has already done
 * so.
 *
 * @param[in] freeze	Freeze context.
 * @param[out] a_freezeid   If not NULL, set to the freeze id on success.
 * @param[out] a_version    If not NULL, set to the frozen db version on
 *			    success.
 * @param[out] a_dbpath	    If not NULL, set to the frozen db path. Caller must
 *			    free.
 * @return ubik error codes
 */
int
ubik_FreezeBegin(struct ubik_freeze_client *freeze, afs_uint64 *a_freezeid,
		 struct ubik_version64 *a_version, char **a_dbpath)
{
    struct ufrzctl_inargs_freezedb freeze_args;
    struct ufrzctl_pkt_frzinfo frzinfo;
    int code;

    memset(&freeze_args, 0, sizeof(freeze_args));
    memset(&frzinfo, 0, sizeof(frzinfo));

    if (freeze->started) {
	return UTWOENDS;
    }

    if (freeze->nested) {
	goto success;
    }

    freeze_args.need_sync = freeze->need_sync;
    freeze_args.timeout_ms = freeze->timeout_ms;

    code = ctlcall_freeze_start(freeze, &freeze_args, &frzinfo,
				&freeze->frz_ctl);
    if (code != 0) {
	goto done;
    }

    freeze->freezeid = frzinfo.freezeid;
    freeze->db_vers = frzinfo.db_version;
    freeze->db_path = frzinfo.db_path;
    frzinfo.db_path = NULL;

    code = check_version(freeze->db_path, &freeze->db_vers);
    if (code != 0) {
	goto done;
    }

 success:
    code = 0;
    if (a_freezeid != NULL) {
	*a_freezeid = freeze->freezeid;
    }
    if (a_version != NULL) {
	*a_version = freeze->db_vers;
    }
    if (a_dbpath != NULL) {
	*a_dbpath = strdup(freeze->db_path);
	if (*a_dbpath == NULL) {
	    code = UNOMEM;
	    goto done;
	}
    }
    freeze->started = 1;
    freeze->running = 1;

 done:
    xdrfree_ufrzctl_pkt_frzinfo(&frzinfo);
    return code;
}

static int
end_freeze(struct ubik_freeze_client *freeze, afs_uint64 freezeid,
	   char *message, int abort, int force_abort)
{
    int code;
    char *message_free = NULL;

    if (freeze->nested && !abort) {
	/*
	 * For a nested freeze, don't actually end the freeze here; the
	 * top-level freeze will end it if everything is successful. But if
	 * we're tring to abort the nested freeze, we can do that in the nested
	 * freeze, to make sure it is aborted.
	 */
	code = 0;
	goto done;
    }

    code = ctlcall_freeze_end(freeze, freezeid, message, abort, force_abort);
    if (code != 0) {
	goto done;
    }

 done:
    freeze->running = 0;
    free(message_free);
    return code;
}

/**
 * Abort the given freeze.
 *
 * @param[in] freeze	Freeze context.
 * @param[in] message	Optional message to give to the server to log.
 * @return ubik error codes
 */
int
ubik_FreezeAbort(struct ubik_freeze_client *freeze, char *message)
{
    if (!freeze->running) {
	return UTWOENDS;
    }
    return end_freeze(freeze, freeze->freezeid, message, 1, 0);
}

/**
 * End the given freeze successfully.
 *
 * @param[in] freeze	Freeze context.
 * @param[in] message	Optional message to give to the server to log.
 * @return ubik error codes
 */
int
ubik_FreezeEnd(struct ubik_freeze_client *freeze, char *message)
{
    if (!freeze->running) {
	return UTWOENDS;
    }
    return end_freeze(freeze, freeze->freezeid, message, 0, 0);
}

/**
 * End a freeze by id.
 *
 * @param[in] freeze	Freeze context.
 * @param[in] freezeid	Freeze id of the freeze to abort.
 * @param[in] message	Optional message to give to the server to log.
 * @return ubik error codes
 */
int
ubik_FreezeAbortId(struct ubik_freeze_client *freeze, afs_uint64 freezeid, char *message)
{
    return end_freeze(freeze, freezeid, message, 1, 0);
}

/**
 * End whatever freeze is running.
 *
 * @param[in] freeze	Freeze context.
 * @param[in] message	Optional message to give to the server to log.
 * @return ubik error codes
 */
int
ubik_FreezeAbortForce(struct ubik_freeze_client *freeze, char *message)
{
    return end_freeze(freeze, 0, message, 1, 1);
}

#else /* AFS_PTHREAD_ENV */

int
ubik_FreezeInit(struct ubik_freezeinit_opts *opts,
		struct ubik_freeze_client **a_freeze)
{
    return UINTERNAL;
}

int
ubik_FreezeIsNested(struct ubik_freeze_client *freeze, afs_uint64 *a_freezeid)
{
    return 0;
}

int
ubik_FreezeSetEnv(struct ubik_freeze_client *freeze)
{
    return UINTERNAL;
}

void
ubik_FreezeDestroy(struct ubik_freeze_client **a_freeze)
{
}

int
ubik_FreezeBegin(struct ubik_freeze_client *freeze, afs_uint64 *a_freezeid,
		 struct ubik_version64 *a_version, char **a_dbpath)
{
    return UINTERNAL;
}

int
ubik_FreezeAbort(struct ubik_freeze_client *freeze, char *message)
{
    return UINTERNAL;
}

int
ubik_FreezeEnd(struct ubik_freeze_client *freeze, char *message)
{
    return UINTERNAL;
}

int
ubik_FreezeAbortId(struct ubik_freeze_client *freeze, afs_uint64 freezeid,
		   char *message)
{
    return UINTERNAL;
}

int
ubik_FreezeAbortForce(struct ubik_freeze_client *freeze, char *message)
{
    return UINTERNAL;
}

#endif /* AFS_PTHREAD_ENV */
