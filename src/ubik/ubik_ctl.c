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
    char *db_prefix;
    char *db_descr;

    struct afsctl_clientinfo cinfo;
    int quiet;
    struct opr_progress_opts progopts;

    struct ubik_freeze_client *freeze;
    struct ubik_version64 frozen_vers;
    char *db_path;  /**< When freezing the db, the path to the db that the
		     *   server gave us. */

    char *out_path; /**< When dumping the db, the path to dump to. */
    char *in_path;  /**< When installing/restoring a db, the path to read from */

    char *backup_suffix;
		    /**< When installing a db, backup the existing db to this
		     *   suffix (or NULL to not backup the db) */
    int no_dist;    /**< When installing a db, don't distribute the db to other
		     *   sites */
    int need_dist;  /**< When installing a db, it's an error if we fail to
		     *   distribute the db to other sites */
};

enum {
    OPT_output,
    OPT_require_sync,

    OPT_input,
    OPT_backup_suffix,
    OPT_no_backup,
    OPT_dist,

    OPT_cmd,
    OPT_rw,

    OPT_freezeid,
    OPT_force,
    OPT_timeout_ms,

    OPT_reason,
    OPT_ctl_socket,
    OPT_quiet,
    OPT_progress,
    OPT_no_progress,
};

static int preamble(struct ubikctl *uctl, struct cmd_syndesc *as);

static void
AFS_ATTRIBUTE_FORMAT(__printf__, 2, 3)
print_nq(struct ubikctl *uctl, const char *fmt, ...)
{
    if (!uctl->quiet) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
}

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
    free(uctl->out_path);
    free(uctl->db_path);
    ubik_FreezeDestroy(&uctl->freeze);
    free(uctl);
}

static int
begin_freeze(struct ubikctl *uctl)
{
    int code;
    afs_uint64 freezeid = 0;

    print_nq(uctl, "Freezing database... ");

    code = ubik_FreezeBegin(uctl->freeze, &freezeid, &uctl->frozen_vers,
			    &uctl->db_path);
    if (code != 0) {
	print_error(code, "Failed to freeze db");
	return code;
    }

    print_nq(uctl, "done (freezeid %llu, db %lld.%lld).\n", freezeid,
	     uctl->frozen_vers.epoch64.clunks,
	     uctl->frozen_vers.counter64);
    return 0;
}

static int
end_freeze(struct ubikctl *uctl)
{
    int code;

    print_nq(uctl, "Ending freeze... ");

    code = ubik_FreezeEnd(uctl->freeze, NULL);
    if (code != 0) {
	print_error(code, "Error ending freeze");
	return code;
    }

    print_nq(uctl, "done.\n");

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

static int
DumpCmd(struct cmd_syndesc *as, void *rock)
{
    struct ubikctl *uctl = rock;
    int code;

    code = preamble(uctl, as);
    if (code != 0) {
	goto error;
    }

    opr_Assert(uctl->freeze != NULL);
    opr_Assert(uctl->out_path != NULL);

    print_nq(uctl, "Dumping database... ");

    code = uphys_copydb(uctl->db_path, uctl->out_path);
    if (code != 0) {
	print_error(code, "Failed to dump db to %s", uctl->out_path);
	goto error;
    }

    print_nq(uctl, "done.\n");

    code = end_freeze(uctl);
    if (code != 0) {
	goto error;
    }

    print_nq(uctl, "Database dumped to %s, version %lld.%lld\n",
	     uctl->out_path, uctl->frozen_vers.epoch64.clunks,
	     uctl->frozen_vers.counter64);

 done:
    postamble(&uctl);
    return code;

 error:
    code = -1;
    goto done;
}

static int
do_install(struct ubikctl *uctl, struct cmd_syndesc *as, int restore)
{
    int code;
    char *db_path_free = NULL;
    char *db_path;

    code = preamble(uctl, as);
    if (code != 0) {
	goto error;
    }

    opr_Assert(uctl->freeze != NULL);

    if (restore) {
	print_nq(uctl, "Making copy of %s... ", uctl->in_path);

	if (asprintf(&db_path_free, "%s.TMP", uctl->db_path) < 0) {
	    code = errno;
	    print_error(code, "Failed to construct db path");
	    goto error;
	}

	db_path = db_path_free;
	code = udb_delpath(db_path);
	if (code != 0) {
	    print_error(code, "Failed to delete tmp path %s", db_path);
	    goto error;
	}

	code = uphys_copydb(uctl->in_path, db_path);
	if (code != 0) {
	    print_error(code, "Failed to copy db to %s", db_path);
	    goto error;
	}

	print_nq(uctl, "done.\n");

    } else {
	db_path = uctl->in_path;
    }

    print_nq(uctl, "Installing db %s... ", db_path);

    code = ubik_FreezeInstall(uctl->freeze, db_path, uctl->backup_suffix);
    if (code != 0) {
	print_error(code, "Failed to install db");
	goto error;
    }

    print_nq(uctl, "done.\n");

    if (!uctl->no_dist) {
	struct opr_progress_opts progopts = uctl->progopts;
	struct opr_progress *progress;
	progopts.bkg_spinner = 1;

	progress = opr_progress_start(&progopts, "Distributing db");
	code = ubik_FreezeDistribute(uctl->freeze);
	opr_progress_done(&progress, code);

	if (code != 0) {
	    print_error(code, "Failed to distribute db");
	    if (uctl->need_dist) {
		goto error;
	    }

	    fprintf(stderr, "warning: We failed to distribute the new database "
		    "to other ubik sites, but the\n");
	    fprintf(stderr, "warning: database was installed successfully. Ubik "
		    "itself may distribute the db\n");
	    fprintf(stderr, "warning: on its own in the background.\n");
	    fprintf(stderr, "\n");
	}
    }

    code = end_freeze(uctl);
    if (code != 0) {
	goto error;
    }

 done:
    free(db_path_free);
    return code;

 error:
    code = -1;
    goto done;
}

static int
RestoreCmd(struct cmd_syndesc *as, void *rock)
{
    struct ubikctl *uctl = rock;
    int code;

    code = do_install(uctl, as, 1);
    if (code != 0) {
	goto done;
    }

    print_nq(uctl, "\n");
    print_nq(uctl, "Restored ubik database from %s\n", uctl->in_path);
    if (uctl->backup_suffix != NULL) {
	print_nq(uctl, "Existing database backed up to suffix %s\n",
		 uctl->backup_suffix);
    }

    code = 0;

 done:
    postamble(&uctl);
    return code;
}

static int
InstallCmd(struct cmd_syndesc *as, void *rock)
{
    struct ubikctl *uctl = rock;
    int code;

    code = do_install(uctl, as, 0);
    if (code != 0) {
	goto done;
    }

    print_nq(uctl, "\n");
    print_nq(uctl, "Installed ubik database %s\n", uctl->in_path);
    if (uctl->backup_suffix != NULL) {
	print_nq(uctl, "Existing database backed up to suffix %s\n",
		 uctl->backup_suffix);
    }

    code = 0;

 done:
    postamble(&uctl);
    return code;
}

static int
FreezeRunCmd(struct cmd_syndesc *as, void *rock)
{
    static struct cmd_item default_cmd = {
	.data = "/bin/sh",
    };

    struct ubikctl *uctl = rock;
    int code;
    int argc;
    int n_args = 0;
    char **argv = NULL;
    struct cmd_item *cmd_args = NULL;
    struct cmd_item *cur;
    pid_t child;
    int status = 0;
    int nocmd = 0;

    code = preamble(uctl, as);
    if (code != 0) {
	goto error;
    }

    opr_Assert(uctl->freeze != NULL);

    code = ubik_FreezeSetEnv(uctl->freeze);
    if (code != 0) {
	print_error(code, "Failed to set freeze env");
	goto error;
    }

    cmd_OptionAsList(as, OPT_cmd, &cmd_args);
    if (cmd_args == NULL) {
	nocmd = 1;
	cmd_args = &default_cmd;
    }

    for (cur = cmd_args; cur != NULL; cur = cur->next) {
	n_args++;
    }

    argv = calloc(n_args+1, sizeof(argv[0]));
    if (argv == NULL) {
	print_error(errno, "calloc");
	goto error;
    }

    argc = 0;
    for (cur = cmd_args; cur != NULL; cur = cur->next) {
	opr_Assert(argc < n_args);
	argv[argc++] = cur->data;
    }

    if (nocmd && !uctl->quiet) {
	printf("\n");
	printf("No -cmd given; spawning a shell to run for the duration of "
	       "the freeze.\n");
	printf("Exit the shell to end the freeze.\n");
	printf("\n");
	ubik_FreezePrintEnv(uctl->freeze, stdout);
    }

    child = fork();
    if (child < 0) {
	print_error(code, "fork");
	goto error;
    }

    if (child == 0) {
	execvp(argv[0], argv);
	print_error(errno, "Failed to exec %s", argv[0]);
	exit(1);
    }

    code = waitpid(child, &status, 0);
    if (code < 0) {
	print_error(errno, "waitpid");
	goto error;
    }

    if (WIFEXITED(status)) {
	code = WEXITSTATUS(status);
	if (code != 0 || !uctl->quiet) {
	    print_error(0, "Command exited with status %d", code);
	}
	if (code != 0) {
	    /* child process failed */
	    goto done;
	}

    } else {
	print_error(0, "Command died with 0x%x", status);
	goto error;
    }

    code = end_freeze(uctl);
    if (code != 0) {
	goto error;
    }

    code = 0;

 done:
    free(argv);
    postamble(&uctl);
    return code;

 error:
    code = -1;
    goto done;
}

static int
FreezeDistCmd(struct cmd_syndesc *as, void *rock)
{
    struct ubikctl *uctl = rock;
    int code;

    code = preamble(uctl, as);
    if (code != 0) {
	goto error;
    }

    print_nq(uctl, "Distributing restored database (may take a while)... ");

    code = ubik_FreezeDistribute(uctl->freeze);
    if (code != 0) {
	print_error(code, "Failed to distribute db");
	goto error;
    }

    print_nq(uctl, "done.\n");

    code = 0;

 done:
    postamble(&uctl);
    return code;

 error:
    code = -1;
    goto done;
}

static int
FreezeAbortCmd(struct cmd_syndesc *as, void *rock)
{
    struct ubikctl *uctl = rock;
    afs_uint64 freezeid = 0;
    unsigned int opt_freezeid = 0;
    int force = 0;
    int have_freezeid = 0;
    int code;

    code = preamble(uctl, as);
    if (code != 0) {
	goto error;
    }

    if (cmd_OptionAsUint(as, OPT_freezeid, &opt_freezeid) == 0) {
	have_freezeid = 1;
	freezeid = opt_freezeid;
    }
    cmd_OptionAsFlag(as, OPT_force, &force);

    if (have_freezeid && force) {
	print_error(0, "You cannot specify both -freezeid and -abort.");
	goto error;
    }

    if (!have_freezeid && !force) {
	afs_uint64 nested_freezeid = 0;
	if (ubik_FreezeIsNested(uctl->freeze, &nested_freezeid)) {
	    freezeid = nested_freezeid;
	} else {
	    print_error(0, "You must specify either -freezeid or -abort, if "
			"not running inside '%s %sdb-freeze-run'.",
			getprogname(), uctl->db_prefix);
	    goto error;
	}
    }

    if (force) {
	print_nq(uctl, "Aborting freeze... ");
	code = ubik_FreezeAbortForce(uctl->freeze, NULL);
    } else {
	print_nq(uctl, "Aborting freeze %llu... ", freezeid);
	code = ubik_FreezeAbortId(uctl->freeze, freezeid, NULL);
    }
    if (code != 0) {
	print_error(code, "Error aborting freeze");
	goto error;
    }

    print_nq(uctl, "done.\n");

 done:
    postamble(&uctl);
    return code;

 error:
    code = -1;
    goto done;
}

/* Does this command freeze the db? */
static int
creates_freeze(struct cmd_syndesc *as)
{
    if (as->proc == DumpCmd ||
	as->proc == RestoreCmd ||
	as->proc == InstallCmd ||
	as->proc == FreezeRunCmd) {
	return 1;
    }
    return 0;
}

/* Does this command interact with db freezes? (does it create a freeze
 * context?) */
static int
freeze_cmd(struct cmd_syndesc *as)
{
    if (creates_freeze(as) ||
	as->proc == FreezeDistCmd ||
	as->proc == FreezeAbortCmd) {
	return 1;
    }
    return 0;
}

/* Does this command use the -quiet option? That is, does it use print_nq? */
static int
can_quiet(struct cmd_syndesc *as) {
    if (as->proc == DbInfoCmd) {
	return 0;
    }
    return 1;
}

static int
preamble_freeze(struct ubikctl *uctl, struct cmd_syndesc *as)
{
    struct ubik_freezeinit_opts fopts;
    int code;
    unsigned int timeout_ms = 0;

    memset(&fopts, 0, sizeof(fopts));

    if (as->proc == DumpCmd || as->proc == FreezeRunCmd) {
	cmd_OptionAsFlag(as, OPT_require_sync, &fopts.fi_needsync);
    }
    if (as->proc == RestoreCmd || as->proc == InstallCmd) {
	fopts.fi_needrw = 1;
    }
    if (as->proc == FreezeRunCmd) {
	cmd_OptionAsFlag(as, OPT_rw, &fopts.fi_needrw);
	fopts.fi_nonest = 1;
    }
    if (as->proc == FreezeDistCmd) {
	fopts.fi_forcenest = 1;
    }

    if (creates_freeze(as)) {
	if (cmd_OptionAsUint(as, OPT_timeout_ms, &timeout_ms) == 0) {
	    fopts.fi_timeout_ms = timeout_ms;
	}
    }

    fopts.fi_cinfo = &uctl->cinfo;

    code = ubik_FreezeInit(&fopts, &uctl->freeze);
    if (code != 0) {
	print_error(code, "Failed to initialize freeze");
	goto error;
    }

    if (creates_freeze(as)) {
	code = begin_freeze(uctl);
	if (code != 0) {
	    goto error;
	}
    }

    code = 0;

 done:
    return code;

 error:
    code = -1;
    goto done;
}

static int
preamble(struct ubikctl *uctl, struct cmd_syndesc *as)
{
    int code;
    char *dist = NULL;

    cmd_OptionAsString(as, OPT_reason, &uctl->cinfo.message);
    cmd_OptionAsString(as, OPT_ctl_socket, &uctl->cinfo.sock_path);

    if (can_quiet(as)) {
	cmd_OptionAsFlag(as, OPT_quiet, &uctl->quiet);
	cmd_OptionAsFlag(as, OPT_progress, &uctl->progopts.force_enable);
	cmd_OptionAsFlag(as, OPT_no_progress, &uctl->progopts.force_disable);

	uctl->progopts.quiet = uctl->quiet;
    }

    if (as->proc == DumpCmd) {
	/* Check that we can create the output db. */
	cmd_OptionAsString(as, OPT_output, &uctl->out_path);
	opr_Assert(uctl->out_path != NULL);

	code = mkdir(uctl->out_path, 0700);
	if (code != 0) {
	    print_error(errno, "Could not create %s", uctl->out_path);
	    goto error;
	}

	code = rmdir(uctl->out_path);
	if (code != 0) {
	    print_error(errno, "Could not rmdir %s", uctl->out_path);
	    goto error;
	}
    }

    if (as->proc == RestoreCmd || as->proc == InstallCmd) {
	struct ubik_version in_vers;
	int no_backup = 0;

	cmd_OptionAsString(as, OPT_input, &uctl->in_path);
	opr_Assert(uctl->in_path != NULL);

	cmd_OptionAsString(as, OPT_backup_suffix, &uctl->backup_suffix);
	cmd_OptionAsFlag(as, OPT_no_backup, &no_backup);
	cmd_OptionAsString(as, OPT_dist, &dist);

	if (uctl->backup_suffix == NULL && !no_backup) {
	    print_error(0, "You must specify either -backup-suffix or "
			"-no-backup");
	    goto error;
	}
	if (no_backup) {
	    free(uctl->backup_suffix);
	    uctl->backup_suffix = NULL;
	}

	if (dist == NULL || strcmp(dist, "try") == 0) {
	    /* noop; this is the default */
	} else if (strcmp(dist, "skip") == 0) {
	    uctl->no_dist = 1;
	} else if (strcmp(dist, "required") == 0) {
	    uctl->need_dist = 1;
	} else {
	    print_error(0, "Bad value for -dist: %s", dist);
	    goto error;
	}

	/* Check that we can open the input db. */
	code = uphys_getlabel_path(uctl->in_path, &in_vers);
	if (code != 0) {
	    print_error(code, "Failed to get version of %s", uctl->in_path);
	    goto error;
	}
    }

    if (freeze_cmd(as)) {
	code = preamble_freeze(uctl, as);
	if (code != 0) {
	    goto error;
	}
    }

    code = 0;

 done:
    free(dist);
    return code;

 error:
    code = -1;
    goto done;
}

static void
install_opts(struct cmd_syndesc *as)
{
    cmd_AddParmAtOffset(as, OPT_input, "-input", CMD_SINGLE, CMD_REQUIRED,
			"input db path");
    cmd_AddParmAtOffset(as, OPT_backup_suffix, "-backup-suffix", CMD_SINGLE,
			CMD_OPTIONAL,
			"backup db suffix");
    cmd_AddParmAtOffset(as, OPT_no_backup, "-no-backup", CMD_FLAG,
			CMD_OPTIONAL,
			"do not generate db backup");
    cmd_AddParmAtOffset(as, OPT_dist, "-dist", CMD_SINGLE, CMD_OPTIONAL,
			"try | skip | required");
}

static void
common_opts(struct cmd_syndesc *as)
{
    if (creates_freeze(as)) {
	cmd_AddParmAtOffset(as, OPT_timeout_ms, "-timeout-ms", CMD_SINGLE,
			    CMD_OPTIONAL,
			    "max time for db freeze (in ms)");
    }
    if (can_quiet(as)) {
	cmd_AddParmAtOffset(as, OPT_quiet, "-quiet", CMD_FLAG, CMD_OPTIONAL,
			    "talk less");
	cmd_AddParmAtOffset(as, OPT_progress, "-progress", CMD_FLAG,
			    CMD_OPTIONAL,
			    "Enable progress reporting");
	cmd_AddParmAtOffset(as, OPT_no_progress, "-no-progress", CMD_FLAG,
			    CMD_OPTIONAL,
			    "Disable progress reporting");
    }

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

    uctl->db_prefix = strdup(prefix);
    uctl->db_descr = strdup(db);
    if (uctl->db_prefix == NULL || uctl->db_descr == NULL) {
	goto error;
    }

    uctl->cinfo.server_type = info->server_type;

    ts = cmd_CreateSyntax(do_snprintf(&name, "%sdb-info", prefix),
			  DbInfoCmd, uctl, 0,
			  do_snprintf(&descr, "get %s info", db));
    common_opts(ts);

    ts = cmd_CreateSyntax(do_snprintf(&name, "%sdb-dump", prefix),
			  DumpCmd, uctl, 0,
			  do_snprintf(&descr, "dump %s", db));
    cmd_AddParmAtOffset(ts, OPT_output, "-output", CMD_SINGLE, CMD_REQUIRED,
			"output db path");
    cmd_AddParmAtOffset(ts, OPT_require_sync, "-require-sync", CMD_FLAG,
			CMD_OPTIONAL,
			"fail if using non-sync-site");
    common_opts(ts);

    ts = cmd_CreateSyntax(do_snprintf(&name, "%sdb-restore", prefix),
			  RestoreCmd, uctl, 0,
			  do_snprintf(&descr, "restore %s", db));
    install_opts(ts);
    common_opts(ts);

    ts = cmd_CreateSyntax(do_snprintf(&name, "%sdb-install", prefix),
			  InstallCmd, uctl, 0,
			  do_snprintf(&descr, "install %s", db));
    install_opts(ts);
    common_opts(ts);

    ts = cmd_CreateSyntax(do_snprintf(&name, "%sdb-freeze-run", prefix),
			  FreezeRunCmd, uctl, 0,
			  do_snprintf(&descr, "freeze %s during command", db));
    cmd_AddParmAtOffset(ts, OPT_cmd, "-cmd", CMD_LIST, CMD_OPTIONAL,
			"command (and args) to run during freeze");
    cmd_AddParmAtOffset(ts, OPT_rw, "-rw", CMD_FLAG, CMD_OPTIONAL,
			"allow database to be modified during freeze");
    cmd_AddParmAtOffset(ts, OPT_require_sync, "-require-sync", CMD_FLAG,
			CMD_OPTIONAL,
			"fail if using non-sync-site");
    common_opts(ts);

    ts = cmd_CreateSyntax(do_snprintf(&name, "%sdb-freeze-dist", prefix),
			  FreezeDistCmd, uctl, 0,
			  do_snprintf(&descr,
				      "distribute installed %s during a freeze",
				      db));
    common_opts(ts);

    ts = cmd_CreateSyntax(do_snprintf(&name, "%sdb-freeze-abort", prefix),
			  FreezeAbortCmd, uctl, 0, "abort a running freeze");
    cmd_AddParmAtOffset(ts, OPT_freezeid, "-freezeid", CMD_SINGLE,
			CMD_OPTIONAL,
			"freezeid to abort");
    cmd_AddParmAtOffset(ts, OPT_force, "-force", CMD_FLAG, CMD_OPTIONAL,
			"abort whatever freeze is running");
    common_opts(ts);

    return 0;

 error:
    if (uctl != NULL) {
	free(uctl->db_prefix);
	free(uctl->db_descr);
	free(uctl);
    }
    return -1;
}
