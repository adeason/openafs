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

    /*
     * When someone calls frz_end(), 'ending' is set. Then when the freeze
     * actually ends, 'ended' is set. We may not set 'ended' until some time
     * later, if a long-running rpc (running_ctl) is running when someone tries
     * to end the freeze. We can't end the freeze immediately in that case,
     * because that rpc is using the freeze; when the rpc finally finishes,
     * then the freeze will end and 'ended' will get set.
     */
    int ending;
    int ended;

    int successful; /**< Did the freeze end successfully? (that is, someone
		     *   called ufreeze_end without aborting) */

    int freeze_rw;  /**< Can a new db get installed during this freeze? */
    int db_changed; /**< Has someone installed a new db during this freeze? */

    /*
     * If this is non-NULL, a long-running request is running for the freeze
     * (which is running without ufreeze_lock held). When the freeze is ending,
     * we must wait for this to go away before we can end and free the freeze.
     */
    struct afsctl_call *running_ctl;

    /*
     * When a new dbase has been installed during a freeze, we save the
     * original db in 'backup_suffix', which is db version 'backup_vers'. If
     * 'unlink_backup' is set, we delete this backup copy when the freeze has
     * ended successfully. 'backup_suffix' must be freed when we free the
     * freeze.
     */
    char *backup_suffix;
    struct ubik_version backup_vers;
    int unlink_backup;

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
 * End the given freeze.
 *
 * Ending the freeze means we clear any db flags we've set during the freeze,
 * and we clear ufreeze_active_frz. If the freeze was not successful, this is
 * also when we revert the database back to the original version. If the freeze
 * was successful, we delete the backup copy of the database if 'unlink_backup'
 * is set.
 *
 * @param[in] frz   The freeze we are ending.
 * @param[in] wait  If there's a long-running rpc using the freeze
 *		    (running_ctl), and 'wait' is zero, we'll return EBUSY. If
 *		    'wait' is non-zero, we'll wait until running_ctl goes away,
 *		    and then end the freeze.
 * @return errno error codes
 * @retval EBUSY    There's a long-running rpc using the freeze, and 'wait' is
 *		    0.
 *
 * @pre ufreeze_lock held
 */
static int
frz_end(struct ufreeze_ctx *frz, int wait)
{
    int code;

    if (frz->ended) {
	return 0;
    }

    frz->ending = 1;

    while (frz->running_ctl != NULL) {
	if (!wait) {
	    ViceLog(0, ("ubik: Deferring ending freeze %llu until request (%s) "
		    "finishes\n",
		    frz->freezeid, afsctl_call_message(frz->running_ctl)));
	    return EBUSY;
	}

	ViceLog(0, ("ubik: Waiting for request (%s) to finish before ending "
		"freeze %llu\n",
		afsctl_call_message(frz->running_ctl), frz->freezeid));
	opr_cv_wait(&ufreeze_cv, &ufreeze_lock);
    }

    /* Re-check; someone may have ended the freeze while we were waiting. */
    if (frz->ended) {
	return 0;
    }

    if (!frz->successful && frz->db_changed) {
	/*
	 * The freeze was aborted for whatever reason. If we installed a new db
	 * during the freeze, try to revert the db back to the original
	 * version.
	 */
	opr_Assert(frz->backup_suffix != NULL);
	ViceLog(0, ("ubik: Reverting db to original frozen version (%s, %d.%d)\n",
		frz->backup_suffix,
		frz->backup_vers.epoch,
		frz->backup_vers.counter));
	code = udb_install(ubik_dbase, frz->backup_suffix, NULL, &frz->backup_vers);
	if (code != 0) {
	    ViceLog(0, ("ubik: Failed to revert db (code %d); proceeding with "
		    "new db from aborted freeze.\n", code));
	} else {
	    frz->db_changed = 0;
	    frz->unlink_backup = 0;
	}
    }

    if (frz->unlink_backup) {
	opr_Assert(frz->backup_suffix != NULL);
	code = udb_del_suffixes(ubik_dbase, NULL, frz->backup_suffix);
	if (code != 0) {
	    ViceLog(0, ("ubik: warning: failed to cleanup old dbase suffix %s "
		    "(code %d)\n", frz->backup_suffix, code));
	}
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

    return 0;
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

    opr_Assert(frz->running_ctl == NULL);

    (void)frz_end(frz, 1);

    free(frz->backup_suffix);
    free(frz);
}

/**
 * Get the current freeze context.
 *
 * Note that the caller can only use the returned a_frz context while
 * ufreeze_lock is held. If you need to drop ufreeze_lock, use ufreeze_getfrz
 * instead.
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
 * Get a reference to the current freeze context for a long-running rpc.
 *
 * Use this instead of ufreeze_peekfrz_r to get a reference to the current
 * freeze context, and to be able to use it without holding ufreeze_lock. Only
 * one such rpc can use the freeze context at a time; if another rpc is already
 * using the freeze, we'll return USYNC.
 *
 * @param[in] func  Name of the calling function/rpc (for logging).
 * @param[in] ctl   The afsctl call for the rpc using the freeze.
 * @param[in] freezeid	The freezeid of the freeze.
 * @param[out] a_frz	On success, set to the active freeze.
 *
 * @return ubik error codes
 * @retval USYNC    Another rpc is already using the freeze.
 */
static int
ufreeze_getfrz(char *func, struct afsctl_call *ctl, afs_uint64 freezeid,
	       struct ufreeze_ctx **a_frz)
{
    int code;
    char *reason = afsctl_call_message(ctl);
    struct ufreeze_ctx *frz = NULL;

    opr_mutex_enter(&ufreeze_lock);

    code = ufreeze_peekfrz_r(&freezeid, &frz);
    if (code != 0) {
	goto done;
    }

    if (frz->ending) {
	code = UDONE;
	goto done;
    }

    if (frz->running_ctl != NULL) {
	ViceLog(0, ("ubik: %s (%s) failed for freezeid %llu: another request "
		"for the freeze is still running (%s).\n",
		func, reason, frz->freezeid,
		afsctl_call_message(frz->running_ctl)));
	code = USYNC;
	goto done;
    }

    frz->running_ctl = ctl;
    *a_frz = frz;

 done:
    opr_mutex_exit(&ufreeze_lock);
    return code;
}

/**
 * Put back a freeze obtained from ufreeze_getfrz.
 *
 * @param[in] ctl   The afsctl call for the rpc using the freeze.
 * @param[inout] a_frz	The freeze context to put back. If NULL, this is a
 *			no-op. Set to NULL on return.
 */
static void
ufreeze_putfrz(struct afsctl_call *ctl, struct ufreeze_ctx **a_frz)
{
    int code;
    struct ufreeze_ctx *frz = *a_frz;
    struct ufreeze_ctx *active_frz = NULL;

    if (frz == NULL) {
	return;
    }

    *a_frz = NULL;

    opr_mutex_enter(&ufreeze_lock);

    /*
     * If our caller has an 'frz', it had better be the active freeze.
     * Otherwise, what is it? Our caller must have gotten its 'frz' from
     * ufreeze_getfrz, and nobody can end the freeze until we put back our ref.
     * So if the active frz is something else, is our 'frz' referencing freed
     * memory somehow?
     *
     * So, ufreeze_peekfrz_r really must succeed here, and the frz we get back
     * must be ours, and it must reference our 'ctl' call. If any of that isn't
     * true, something is seriously wrong, so assert immediately.
     */
    code = ufreeze_peekfrz_r(NULL, &active_frz);
    opr_Assert(code == 0);
    opr_Assert(active_frz == frz);
    opr_Assert(frz->running_ctl == ctl);

    frz->running_ctl = NULL;

    if (frz->ending) {
	/*
	 * Someone probably tried to end the freeze and couldn't, because we
	 * were still running with an active reference. Now that we've put our
	 * reference back, the freeze should be able to end now; do so now, to
	 * make it end as soon as possible.
	 */
	(void)frz_end(frz, 0);
    }

    opr_cv_broadcast(&ufreeze_cv);

    opr_mutex_exit(&ufreeze_lock);
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
    int freeze_ro = 0;
    int freeze_rw = 0;
    int need_sync = 0;
    struct ufreeze_ctx *frz = NULL;
    struct ubik_version version;
    struct afsctl_pkt frzinfo;
    afs_uint32 timeout_ms;
    char *reason = afsctl_call_message(ctl);
    char *db_path = NULL;

    memset(&frzinfo, 0, sizeof(frzinfo));
    memset(&version, 0, sizeof(version));

    if (in_args->readonly) {
	freeze_ro = 1;
    }
    if (in_args->readwrite) {
	freeze_rw = 1;
	need_sync = 1;
    }
    if (in_args->need_sync) {
	need_sync = 1;
    }
    if (freeze_ro == freeze_rw) {
	ViceLog(0, ("ubik: ufreeze_freezedb (%s): bad ro/rw %d/%d\n",
		reason, (int)in_args->readonly, (int)in_args->readwrite));
	return UINTERNAL;
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

    frz->freeze_rw = freeze_rw;

    if (freeze_rw) {
	frz->dbflags_set = DBRECEIVING;
    } else {
	frz->dbflags_set = DBSENDING;
    }
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

    code = udb_getlabel_db(ubik_dbase, &version);
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

    if (!frz->ending) {
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

    (void)frz_end(frz, 1);

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

    if (frz->ending) {
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
    {
	int code_end = frz_end(frz, 0);
	if (code == 0) {
	    code = code_end;
	}
    }

 done:
    opr_mutex_exit(&ufreeze_lock);
    return code;
}

static int
ufreeze_install(struct afsctl_call *ctl,
		struct ufrzctl_inargs_install *in_args)
{
    int code;
    int keep_old;
    char *backup_suffix = in_args->backup_suffix;
    struct ubik_version64 stream_vers;
    struct ubik_version64 disk_vers;
    struct ubik_version disk_vers32;
    struct ubik_version old_vers32;
    struct ubik_version new_vers32;
    int others_exist;
    struct ubik_server *ts;
    char *path = NULL;
    char *backup_path = NULL;
    char *reason = afsctl_call_message(ctl);
    struct ubik_dbase *dbase = ubik_dbase;
    struct ufreeze_ctx *frz = NULL;
    afs_uint64 now;

    memset(&stream_vers, 0, sizeof(stream_vers));
    memset(&disk_vers, 0, sizeof(disk_vers));
    memset(&disk_vers32, 0, sizeof(disk_vers32));
    memset(&old_vers32, 0, sizeof(old_vers32));
    memset(&new_vers32, 0, sizeof(new_vers32));

    if (udb_vcmp64(&in_args->old_version, &in_args->new_version) >= 0) {
	/* If we're changing the db contents, the new version had better be
	 * newer than the old version. */
	ViceLog(0, ("ubik: ufreeze_install (%s): Cannot install db: nonsense "
		    "versions %lld.%lld -> %lld.%lld\n",
		    reason,
		    in_args->old_version.epoch64.clunks,
		    in_args->old_version.counter64,
		    in_args->new_version.epoch64.clunks,
		    in_args->new_version.counter64));
	code = UINTERNAL;
	goto done;
    }

    now = time(NULL);
    if (opr_time64_toSecs(&in_args->new_version.epoch64) >= now) {
	/*
	 * Don't let the caller install a db with an epoch of 'now' or newer,
	 * to make sure that any db labelled after this will get a 'newer'
	 * version.
	 */
	ViceLog(0, ("ubik: ufreeze_install (%s): Cannot install db: new db "
		"version looks too new (%lld)\n",
		reason, in_args->new_version.epoch64.clunks));
	code = UINTERNAL;
	goto done;
    }

    if (in_args->new_suffix[0] == '\0') {
	ViceLog(0, ("ubik: ufreeze_install (%s): Cannot install db: blank db suffix.\n",
		reason));
	code = UINTERNAL;
	goto done;
    }

    if (in_args->backup_suffix[0] == '\0') {
	backup_suffix = strdup(".OLD");
	keep_old = 0;
    } else {
	backup_suffix = strdup(in_args->backup_suffix);
	keep_old = 1;
    }
    if (backup_suffix == NULL) {
	code = UNOMEM;
	goto done;
    }

    code = ufreeze_getfrz("ufreeze_install", ctl, in_args->freezeid, &frz);
    if (code != 0) {
	goto done;
    }

    if (!frz->freeze_rw || frz->dbflags_set != DBRECEIVING) {
	ViceLog(0, ("ubik: Cannot install db for freezeid %llu (%s); freeze is "
		"readonly.\n", frz->freezeid, reason));
	code = UBADTYPE;
	goto done;
    }

    if (frz->db_changed) {
	/*
	 * Don't allow multiple installs/restores for the same freeze. This
	 * makes the backup/revert/etc logic simpler, and it's hard to see why
	 * anyone would want to do this; just start a new freeze.
	 */
	ViceLog(0, ("ubik: Cannot install db for freezeid %llu (%s): a new db "
		"for this freeze has already been installed.\n", frz->freezeid,
		reason));
	code = UINTERNAL;
	goto done;
    }

    /* Check that the existing db matches what we're given. */

    DBHOLD(dbase);
    code = udb_getlabel_db(dbase, &disk_vers32);
    DBRELE(dbase);
    if (code != 0) {
	goto done;
    }

    old_vers32 = disk_vers32;
    udb_v32to64(&disk_vers32, &disk_vers);
    if (udb_vcmp64(&in_args->old_version, &disk_vers) != 0) {
	ViceLog(0, ("ubik: Cannot install db for freezeid %llu: old_version "
		    "mismatch: %lld.%lld != %lld.%lld\n",
		    frz->freezeid,
		    in_args->old_version.epoch64.clunks,
		    in_args->old_version.counter64,
		    disk_vers.epoch64.clunks,
		    disk_vers.counter64));
	code = UINTERNAL;
	goto done;
    }

    frz->backup_vers = disk_vers32;

    code = udb_path(dbase, in_args->new_suffix, &path);
    if (code != 0) {
	goto done;
    }

    code = udb_path(dbase, backup_suffix, &backup_path);
    if (code != 0) {
	goto done;
    }

    if (access(backup_path, F_OK) == 0) {
	ViceLog(0, ("ubik: Cannot install new db with backup to %s; backup "
		"path already exists\n", backup_path));
	code = UIOERROR;
	goto done;
    }

    /* Check that the new db on disk matches the version we were given. */

    code = udb_getlabel_path(path, &disk_vers32);
    if (code != 0) {
	ViceLog(0, ("ubik: Cannot install new db for freezeid %llu: cannot open "
		    "new database suffix %s (code %d)\n",
		    frz->freezeid, in_args->new_suffix, code));
	code = UIOERROR;
	goto done;
    }

    new_vers32 = disk_vers32;
    udb_v32to64(&disk_vers32, &disk_vers);
    if (udb_vcmp64(&in_args->new_version, &disk_vers) != 0) {
	ViceLog(0, ("ubik: Cannot install new db for freezeid %llu: version "
		    "mismatch: %lld.%lld != %lld.%lld\n",
		    frz->freezeid,
		    in_args->new_version.epoch64.clunks,
		    in_args->new_version.counter64,
		    disk_vers.epoch64.clunks,
		    disk_vers.counter64));
	code = UINTERNAL;
	goto done;
    }

    code = udb_check_contents(dbase, path);
    if (code != 0) {
	ViceLog(0, ("ubik: Cannot install new db for freezeid %llu: db does not "
		    "look valid (code %d)\n",
		    frz->freezeid, code));
	code = UIOERROR;
	goto done;
    }

    ViceLog(0, ("ubik: Installing new database %d.%d for freezeid %llu\n",
	    disk_vers32.epoch,
	    disk_vers32.counter,
	    frz->freezeid));

    /*
     * Everything looks good, so go ahead and install the new database. Note
     * that udb_install can theoretically take some time to run (it needs to
     * wait for the transactions on the old db to go away), so we don't hold
     * ufreeze_lock during this.
     */
    code = udb_install(dbase, in_args->new_suffix, backup_suffix,
		       &disk_vers32);
    if (code != 0) {
	ViceLog(0, ("ubik: Error %d installing new db for freezeid %llu\n",
		code, frz->freezeid));
	goto done;
    }

    if (keep_old) {
	ViceLog(0, ("ubik: Installed new db for freezeid %llu (%s). Database "
		    "updated from %d.%d to %d.%d (old db saved to "
		    "%s).\n",
		    frz->freezeid, reason,
		    old_vers32.epoch,
		    old_vers32.counter,
		    new_vers32.epoch,
		    new_vers32.counter,
		    backup_suffix));
    } else {
	frz->unlink_backup = 1;
	ViceLog(0, ("ubik: Installed new db for freezeid %llu (%s). Database "
		    "updated from %d.%d to %d.%d.\n",
		    frz->freezeid, reason,
		    old_vers32.epoch,
		    old_vers32.counter,
		    new_vers32.epoch,
		    new_vers32.counter));
    }

    frz->db_changed = 1;
    frz->backup_suffix = backup_suffix;
    backup_suffix = NULL;

    /*
     * We've pivoted the new db into place; mark all other sites as having a
     * stale db, so the recovery thread will distribute the new db to other
     * sites (if we don't do it ourselves later during the freeze).
     */

    DBHOLD(dbase);

    others_exist = 0;
    for (ts = ubik_servers; ts; ts = ts->next) {
	others_exist = 1;
	ts->currentDB = 0;
    }
    if (others_exist) {
	/*
	 * Note that we only clear UBIK_RECSENTDB here; we don't do a full
	 * urecovery_ResetState(). We don't need to clear the other urecovery
	 * states, since we know we have the best db version, and we don't need
	 * to relabel the db (we know the installed db has a higher epoch than
	 * the previous db).
	 *
	 * We specifically do _not_ want to clear UBIK_RECLABELDB. If we
	 * cleared that, then on the next commit, we'd relabel the db using
	 * ubik_epochTime, which is _older_ than the epoch of the db we just
	 * installed. That would effectively make the db older, and we don't
	 * want that!
	 */
	urecovery_state &= ~UBIK_RECSENTDB;
    }

    DBRELE(ubik_dbase);

 done:
    free(path);
    free(backup_path);
    ufreeze_putfrz(ctl, &frz);
    return code;
}

static int
ufreeze_dist(struct afsctl_call *ctl, afs_uint64 freezeid)
{
    int code;
    struct ufreeze_ctx *frz = NULL;
    int db_disted = 0;
    char *reason = afsctl_call_message(ctl);

    code = ufreeze_getfrz("ufreeze_dist", ctl, freezeid, &frz);
    if (code != 0) {
	goto done;
    }

    if (!frz->db_changed) {
	ViceLog(0, ("ubik: ufreeze_dist (%s) for %llu failed; db hasn't "
		"changed.\n", reason, frz->freezeid));
	code = UINTERNAL;
	goto done;
    }

    DBHOLD(ubik_dbase);

    /* 'db_changed' must be set, from a check above. If db_changed is set,
     * freeze_rw and dbflags_set had better also be set. */
    opr_Assert(frz->freeze_rw);
    opr_Assert(frz->dbflags_set != 0);

    /* Switch from DBRECEIVING to DBSENDING. Do this atomically under DBHOLD,
     * so we still prevent anyone from starting a write tx. */
    ubik_clear_db_flags(ubik_dbase, frz->dbflags_set);
    ubik_set_db_flags(ubik_dbase, DBSENDING);
    frz->dbflags_set = DBSENDING;

    db_disted = 1;

    if (ubik_servers == NULL) {
	/*
	 * No other sites exist, so we don't need to distribute the db to
	 * anyone, so there's no real work to do. We still clear db_changed
	 * below, to make it so we still avoid reverting the db back to the
	 * original db if the freeze fails.
	 */
	ViceLog(0, ("ubik: Marking newly-installed db for freezeid "
		    "%llu (%s) as distributed (we are the only site).\n",
		    frz->freezeid, reason));

    } else {
	int n_sent = 0;

	ViceLog(0, ("ubik: Distributing newly-installed db for freezeid "
		    "%llu (%s).\n", frz->freezeid, reason));

	/*
	 * Distribution may fail to some sites. That's fine; the normal
	 * recovery thread will handle them eventually. But if we failed to
	 * send the db to some sites, return an error to the caller, so they
	 * know something went wrong. The caller can then decide whether or not
	 * they care.
	 *
	 * If we failed to send to _all_ sites, and didn't successfully send
	 * the db to anyone, allow the local db to be reverted if this freeze
	 * later aborts.
	 */
	code = urecovery_distribute_db(ubik_dbase, &n_sent);
	if (code != 0) {
	    ViceLog(0, ("ubik: Failed to distribute db for freezeid %llu to all "
		    "sites (code %d, n_sent %d).\n",
		    frz->freezeid, code, n_sent));
	    code = USYNC;
	    if (n_sent == 0) {
		db_disted = 0;
	    }
	} else {
	    ViceLog(0, ("ubik: Finished distributing db for freezeid %llu.\n",
			frz->freezeid));
	}
    }

    DBRELE(ubik_dbase);

    if (db_disted) {
	/*
	 * We've transmitted our new db to some other sites. So from now on,
	 * act like the db hasn't changed, to prevent someone from running
	 * FreezeDistribute again, and to make sure we don't try to revert the
	 * db back to the original version.
	 */
	frz->db_changed = 0;
    }

 done:
    ufreeze_putfrz(ctl, &frz);
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

    case UFRZCTL_OP_INSTALL:
	code = ufreeze_install(ctl, &in_args->u.ufrz_inst);
	break;

    case UFRZCTL_OP_DIST:
	code = ufreeze_dist(ctl, in_args->u.ufrz_dist_freezeid);
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
