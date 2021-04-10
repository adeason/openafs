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

#include <roken.h>

#include <afs/opr.h>
#include <opr/time64.h>
#ifdef AFS_PTHREAD_ENV
# include <opr/lock.h>
#endif
#include <afs/afsutil.h>
#include <afs/afsctl.h>

#include "ubik_internal.h"

#ifdef AFS_PTHREAD_ENV

# define TIMEOUT_MIN 5
# define TIMEOUT_MAX (1000*60*60*24*21) /* 21 days */

/*
 * Data for the active freeze. We usually only have one of these instantiated
 * (ufreeze_active_frz), since only one freeze can exist at a time.
 */
struct ufreeze_ctx {
    afs_uint64 freezeid;

    struct afsctl_call *ctl;	/**< The UFRZCTL_OP_FREEZEDB ctl call for this
				 *   freeze. */

    int ended;	/**< Has the freeze ended? (that is, someone called
		 *   ufreeze_end, or the caller of ufreeze_freezedb died) */

    int successful; /**< Did the freeze end successfully? (that is, someone
		     *   called ufreeze_end without aborting) */

    int dbflags_set;	/**< Which flags we've set on ubik_dbase (e.g.
			 *   DBSENDING) */

    afs_uint32 timeout_ms;  /**< Max time the freeze can run for (or 0, for no
			     *   limit) */
    struct afs_time64 start_time;
};

static opr_mutex_t ufreeze_lock;
static opr_cv_t ufreeze_cv;

/* Protected by ufreeze_lock */
static struct ufreeze_ctx *ufreeze_active_frz;

/**
 * End the given freeze. Clear our db flags, clear ufreeze_active_frz, etc.
 *
 * @pre ufreeze_lock held
 */
static void
frz_end(struct ufreeze_ctx *frz)
{
    if (frz->ended) {
	return;
    }

    if (frz->dbflags_set != 0) {
	DBHOLD(ubik_dbase);
	ubik_clear_db_flags(ubik_dbase, frz->dbflags_set);
	DBRELE(ubik_dbase);
	frz->dbflags_set = 0;
    }

    frz->ended = 1;

    if (frz->successful) {
	ViceLog(0, ("ubik: Freeze %llu ended successfully.\n", frz->freezeid));
    } else {
	ViceLog(0, ("ubik: Freeze %llu failed.\n", frz->freezeid));
    }

    opr_Assert(ufreeze_active_frz == frz);
    ufreeze_active_frz = NULL;
}

/**
 * Free the freeze. Only do this in ufreeze_freezedb; for everyone else, if you
 * want a freeze to go away, frz_end() it, and get ufreeze_freezedb to wakeup
 * so it can free the freeze.
 *
 * @pre ufreeze_lock held
 */
static void
frz_destroy(struct ufreeze_ctx **a_frz)
{
    struct ufreeze_ctx *frz = *a_frz;
    if (frz == NULL) {
	return;
    }
    *a_frz = NULL;

    frz_end(frz);

    free(frz);
}

/**
 * Get the current freeze context.
 *
 * @param[in] freezeid	If not NULL, check that the active freeze matches this
 *			freezeid. If it doesn't, return USYNC.
 * @param[out] a_frz	On success, set to the active freeze.
 * @return ubik error codes
 *
 * @pre ufreeze_lock held
 */
static int
ufreeze_peekfrz_r(afs_uint64 *freezeid, struct ufreeze_ctx **a_frz)
{
    struct ufreeze_ctx *frz = ufreeze_active_frz;

    *a_frz = NULL;

    if (frz == NULL) {
	return UNOENT;
    }
    if (freezeid != NULL && frz->freezeid != *freezeid) {
	return USYNC;
    }

    *a_frz = frz;
    return 0;
}

/**
 * Check if it's okay to start a freeze on the db.
 *
 * @param[in] need_sync	Whether the freeze needs the sync site.
 * @return ubik error codes
 *
 * @pre DBHOLD held
 */
static int
ufreeze_checkdb_r(int need_sync)
{
    int readAny = need_sync ? 0 : 1;
    if (need_sync && !ubeacon_AmSyncSite()) {
	return UNOTSYNC;
    }
    if (!urecovery_AllBetter(ubik_dbase, readAny)) {
	return UNOQUORUM;
    }
    return 0;
}

/* @pre ufreeze_lock held */
static afs_uint64
freezeid_gen(void)
{
    static afs_uint64 counter;
    if (counter == 0) {
	counter++;
    }
    return counter++;
}

static int
ufreeze_freezedb(struct afsctl_call *ctl,
		 struct ufrzctl_inargs_freezedb *in_args)
{
    int code;
    int need_sync = 0;
    struct ufreeze_ctx *frz = NULL;
    struct ubik_version version;
    struct afsctl_pkt frzinfo;
    afs_uint32 timeout_ms;
    char *reason = afsctl_call_message(ctl);
    char *db_path = NULL;

    memset(&frzinfo, 0, sizeof(frzinfo));
    memset(&version, 0, sizeof(version));

    if (in_args->need_sync) {
	need_sync = 1;
    }

    if (in_args->no_timeout) {
	timeout_ms = 0;
    } else {
	timeout_ms = in_args->timeout_ms;
	if (timeout_ms < TIMEOUT_MIN || timeout_ms > TIMEOUT_MAX) {
	    ViceLog(0, ("ubik: ufreeze_freezedb (%s): bad timeout %u\n",
		    reason, timeout_ms));
	    return UINTERNAL;
	}
    }

    opr_mutex_enter(&ufreeze_lock);

    if (ufreeze_active_frz != NULL) {
	ViceLog(0, ("ubik: ufreeze_freezedb (%s): Cannot start freeze; "
		    "existing freeze %llu is still running (started at "
		    "%lld).\n",
		    reason, ufreeze_active_frz->freezeid,
		    ufreeze_active_frz->start_time.clunks));
	code = USYNC;
	goto done;
    }

    DBHOLD(ubik_dbase);

    code = ufreeze_checkdb_r(need_sync);
    if (code != 0) {
	goto done_dblocked;
    }

    if (ubik_wait_db_flags(ubik_dbase, DBWRITING | DBSENDING)) {
	ViceLog(0, ("ubik: Error during FreezeBegin (%s): saw unexpected "
		    "database flags 0x%x.\n",
		    reason, ubik_dbase->dbFlags));
	code = UINTERNAL;
	goto done_dblocked;
    }

    code = ufreeze_checkdb_r(need_sync);
    if (code != 0) {
	goto done_dblocked;
    }

    frz = calloc(1, sizeof(*frz));
    if (frz == NULL) {
	code = UNOMEM;
	goto done_dblocked;
    }

    frz->dbflags_set = DBSENDING;
    ubik_set_db_flags(ubik_dbase, frz->dbflags_set);

    frz->freezeid = freezeid_gen();
    opr_Assert(frz->freezeid != 0);

    frz->ctl = ctl;
    frz->timeout_ms = timeout_ms;

    code = opr_time64_now(&frz->start_time);
    if (code != 0) {
	code = UINTERNAL;
	goto done_dblocked;
    }

    /* Set this frz as the 'active' freeze. */
    opr_Assert(ufreeze_active_frz == NULL);
    ufreeze_active_frz = frz;

    code = uphys_getlabel(ubik_dbase, 0, &version);
    if (code != 0) {
	ViceLog(0, ("ubik: ufreeze_freezedb (%s): Cannot get db label, code %d\n",
		    reason, code));
	goto done_dblocked;
    }

    ViceLog(0, ("ubik: Freeze id %llu started (%s), version %d.%d "
	    "timeout %u ms.\n",
	    frz->freezeid, reason, version.epoch, version.counter,
	    timeout_ms));

    DBRELE(ubik_dbase);

    code = udb_path(ubik_dbase, NULL, &db_path);
    if (code != 0) {
	goto done;
    }

    /* db is now frozen; send info about the freeze to our peer. */

    frzinfo.ptype = UFRZCTL_PKT_FRZINFO;
    frzinfo.u.ufrz_frzinfo.freezeid = frz->freezeid;
    udb_v32to64(&version, &frzinfo.u.ufrz_frzinfo.db_version);
    frzinfo.u.ufrz_frzinfo.db_path = db_path;

    code = afsctl_send_pkt(ctl, &frzinfo);
    if (code != 0) {
	goto done;
    }

    /*
     * Wait for either the caller to die (shutting down the socket), or for
     * another thread to interrupt us (ufreeze_end), or for the timeout to
     * trigger.
     */
    opr_mutex_exit(&ufreeze_lock);
    code = afsctl_wait_recv(ctl, timeout_ms);
    opr_mutex_enter(&ufreeze_lock);

    if (!frz->ended) {
	if (code == 0) {
	    code = UIOERROR;
	    ViceLog(0, ("ubik: Aborting freeze %llu: peer died\n", frz->freezeid));

	} else if (code == ETIMEDOUT) {
	    ViceLog(0, ("ubik: Aborting freeze %llu: timed out\n", frz->freezeid));

	} else {
	    ViceLog(0, ("ubik: Aborting freeze %llu: wait_recv returned %d\n",
		    frz->freezeid, code));
	}
    }

    frz_end(frz);

    if (code == 0 && !frz->successful) {
	/* If the freeze was not successful (e.g. it was aborted), make sure we
	 * return an error to the caller. */
	code = UDONE;
    }

 done:
    frz_destroy(&frz);
    opr_mutex_exit(&ufreeze_lock);
    free(db_path);
    return code;

 done_dblocked:
    DBRELE(ubik_dbase);
    goto done;
}

static int
ufreeze_end(struct afsctl_call *ctl, struct ufrzctl_inargs_end *in_args)
{
    int code;
    struct ufreeze_ctx *frz = NULL;
    char *reason = afsctl_call_message(ctl);

    opr_mutex_enter(&ufreeze_lock);

    if (in_args->force_abort) {
	/* Don't check the freezeid, just abort whatever frz we find. */
	in_args->abort = 1;
	code = ufreeze_peekfrz_r(NULL, &frz);
    } else {
	code = ufreeze_peekfrz_r(&in_args->freezeid, &frz);
    }
    if (code != 0) {
	goto done;
    }

    if (frz->ended) {
	code = UTWOENDS;
	goto done;
    }

    /* Shutdown the socket for reading, so ufreeze_freezedb wakes up and sees
     * that the freeze has ended. */
    code = afsctl_call_shutdown_read(frz->ctl);
    if (code != 0) {
	ViceLog(0, ("ubik: ufreeze_end (%s) failed to shutdown socket "
		"(error %d).\n", reason, code));
	code = UIOERROR;
	goto done_end;
    }

    frz->successful = 0;
    if (in_args->force_abort) {
	ViceLog(0, ("ubik: Forcibly aborting freeze %llu (%s)\n", frz->freezeid,
		reason));

    } else if (in_args->abort) {
	ViceLog(0, ("ubik: Aborting freeze %llu (%s)\n", frz->freezeid,
		reason));

    } else {
	frz->successful = 1;
	ViceLog(0, ("ubik: Ending freeze %llu (%s)\n", frz->freezeid, reason));
    }

 done_end:
    frz_end(frz);

 done:
    opr_mutex_exit(&ufreeze_lock);
    return code;
}

static int
ufreeze_request(struct afsctl_call *ctl, struct afsctl_req_inargs *in_args,
		struct afsctl_req_outargs *out_args)
{
    int code;

    switch (in_args->op) {
    case UFRZCTL_OP_FREEZEDB:
	code = ufreeze_freezedb(ctl, &in_args->u.ufrz_freezedb);
	break;

    case UFRZCTL_OP_END:
	code = ufreeze_end(ctl, &in_args->u.ufrz_end);
	break;

    default:
	code = ENOTSUP;
    }
    return code;
}

void
ufreeze_Init(struct ubik_serverinit_opts *opts)
{
    int code;

    opr_mutex_init(&ufreeze_lock);
    opr_cv_init(&ufreeze_cv);

    if (opts->ctl_server == NULL) {
	return;
    }

    code = afsctl_server_reg(opts->ctl_server, UFRZCTL_OP_PREFIX,
			     ufreeze_request);
    if (code != 0) {
	ViceLog(0, ("ubik: Failed to register ufreeze ctl ops (error %d); "
		"startup wil continue, but freeze functionality will be "
		"unavailable.\n", code));
	code = 0;
    }
}

#else /* AFS_PTHREAD_ENV */

void
ufreeze_Init(struct ubik_serverinit_opts *opts)
{
    opr_Assert(opts->ctl_server == NULL);
}

#endif /* AFS_PTHREAD_ENV */
