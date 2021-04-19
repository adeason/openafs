#include <afsconfig.h>
#include <afs/param.h>

#include <roken.h>

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include <rx/rx.h>

#include <afs/authcon.h>
#include <afs/cellconfig.h>
#include <ubik.h>

#include <tests/tap/basic.h>

#include "common.h"

struct afstest_server_type afstest_server_pt = {
    .logname = "PtLog",
    .ctl_sock = "pt.ctl.sock",
    .bin_path = "src/tptserver/ptserver",
    .db_name = "prdb",
    .exec_name = "ptserver",
    .startup_string = "Starting AFS ptserver",
    .service_name = AFSCONF_PROTSERVICE,
    .port = 7002,
};

struct afstest_server_type afstest_server_vl = {
    .logname = "VLLog",
    .ctl_sock = "vl.ctl.sock",
    .bin_path = "src/tvlserver/vlserver",
    .db_name = "vldb",
    .exec_name = "vlserver",
    .startup_string = "Starting AFS vlserver",
    .service_name = AFSCONF_VLDBSERVICE,
    .port = 7003,
};

static void
check_startup(struct afstest_server_type *server, pid_t pid, char *log,
	      int *a_started, int *a_stopped)
{
    int status;
    struct rx_connection *conn;
    struct ubik_debug udebug;
    afs_int32 isclone;
    int code;

    memset(&udebug, 0, sizeof(udebug));

    if (waitpid(pid, &status, WNOHANG) != 0) {
	/* pid is no longer running; vlserver must have died during startup */
	*a_stopped = 1;
	return;
    }

    if (!afstest_file_contains(log, server->startup_string)) {
	/* server hasn't logged the e.g. "Starting AFS vlserver" line yet, so
	 * it's presumably still starting up. */
	return;
    }

    /*
     * If we need to write to the db, we also need to wait until recovery has
     * the UBIK_RECHAVEDB state bit. Without that, we won't be able to start
     * write transactions. So use VOTE_XDebug to see if it's been set.
     */

    conn = rx_NewConnection(afstest_MyHostAddr(), htons(server->port),
			    VOTE_SERVICE_ID,
			    rxnull_NewClientSecurityObject(), 0);
    code = VOTE_XDebug(conn, &udebug, &isclone);
    rx_DestroyConnection(conn);
    if (code != 0) {
	diag("VOTE_XDebug returned %d while waiting for server startup",
	     code);
	return;
    }

    if (udebug.amSyncSite && (udebug.recoveryState & UBIK_RECHAVEDB) != 0) {
	/* Okay, it's set! We have finished startup. */
	*a_started = 1;
    }
}

int
afstest_StartServerOpts(struct afstest_server_opts *opts)
{
    char *dirname = opts->dirname;
    pid_t *serverPid = opts->serverPid;
    struct afstest_server_type *server = opts->server;
    pid_t pid;
    char *logPath;
    char *sock_path;
    int started = 0;
    int stopped = 0;
    int try;
    FILE *fh;
    int code = 0;

    logPath = afstest_asprintf("%s/%s", dirname, server->logname);
    sock_path = afstest_asprintf("%s/%s", dirname, server->ctl_sock);

    /* Create/truncate the log in advance (since we look at it to detect when
     * the server has started). */
    fh = fopen(logPath, "w");
    opr_Assert(fh != NULL);
    fclose(fh);

    pid = fork();
    if (pid == -1) {
	exit(1);
	/* Argggggghhhhh */

    } else if (pid == 0) {
	/* Child */

	#define MAX_ARGS 16
	char *binPath, *dbPath;
	char *argv[MAX_ARGS];
	int argc = 0;
	char **x_arg;

	memset(argv, 0, sizeof(argv));

	binPath = afstest_obj_path(server->bin_path);
	dbPath = afstest_asprintf("%s/%s", dirname, server->db_name);

	argv[argc++] = server->exec_name;
	argv[argc++] = "-logfile";
	argv[argc++] = logPath;
	argv[argc++] = "-database";
	argv[argc++] = dbPath;
	argv[argc++] = "-config";
	argv[argc++] = dirname;
	argv[argc++] = "-ctl-socket";
	argv[argc++] = sock_path;

	if (opts->extra_argv != NULL) {
	    for (x_arg = opts->extra_argv; x_arg[0] != NULL; x_arg++) {
		argv[argc++] = x_arg[0];
		opr_Assert(argc < MAX_ARGS);
	    }
	}

	execv(binPath, argv);
	fprintf(stderr, "Running %s failed\n", binPath);
	exit(1);

	#undef MAX_ARGS
    }

    /*
     * Wait for the vlserver to startup. Try to check if the vlserver is ready
     * by checking the log file and the urecovery_state (check_startup()), but
     * if it's taking too long, just return success anyway. If the vlserver
     * isn't ready yet, then the caller's tests may fail, but we can't wait
     * forever.
     */

    diag("waiting for server to startup");

    usleep(5000); /* 5ms */
    check_startup(server, pid, logPath, &started, &stopped);
    for (try = 0; !started && !stopped; try++) {
	if (try > 100 * 5) {
	    diag("waited too long for server to finish starting up; "
		 "proceeding anyway");
	    goto done;
	}

	usleep(1000 * 10); /* 10ms */
	check_startup(server, pid, logPath, &started, &stopped);
    }

    if (stopped) {
	fprintf(stderr, "server died during startup\n");
	code = -1;
	goto done;
    }

    diag("server started after try %d", try);

 done:
    *serverPid = pid;

    free(logPath);
    free(sock_path);

    return code;
}

int
afstest_StartServer(struct afstest_server_type *server, char *dirname,
		    pid_t *serverPid)
{
    struct afstest_server_opts opts;
    memset(&opts, 0, sizeof(opts));

    opts.server = server;
    opts.dirname = dirname;
    opts.serverPid = serverPid;

    return afstest_StartServerOpts(&opts);
}

int
afstest_StopServer(pid_t serverPid)
{
    int status;

    kill(serverPid, SIGTERM);

    waitpid(serverPid, &status, 0);

    if (WIFSIGNALED(status) && WTERMSIG(status) != SIGTERM) {
	fprintf(stderr, "Server died exited on signal %d\n", WTERMSIG(status));
	return -1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
	fprintf(stderr, "Server exited with code %d\n", WEXITSTATUS(status));
	return -1;
    }
    return 0;
}

int
afstest_StartTestRPCService(const char *configPath,
			    pid_t signal_pid,
			    u_short port,
			    u_short serviceId,
			    afs_int32 (*proc) (struct rx_call *))
{
    struct afsconf_dir *dir;
    struct rx_securityClass **classes;
    afs_int32 numClasses;
    int code;
    struct rx_service *service;
    struct afsconf_bsso_info bsso;

    memset(&bsso, 0, sizeof(bsso));

    dir = afsconf_Open(configPath);
    if (dir == NULL) {
        fprintf(stderr, "Server: Unable to open config directory\n");
        return -1;
    }

    code = rx_Init(htons(port));
    if (code != 0) {
	fprintf(stderr, "Server: Unable to initialise RX\n");
	return -1;
    }

    if (signal_pid != 0) {
	kill(signal_pid, SIGUSR1);
    }

    bsso.dir = dir;
    afsconf_BuildServerSecurityObjects_int(&bsso, &classes, &numClasses);
    service = rx_NewService(0, serviceId, "test", classes, numClasses,
                            proc);
    if (service == NULL) {
        fprintf(stderr, "Server: Unable to start to test service\n");
        return -1;
    }

    rx_StartServer(1);

    return 0; /* Not reached, we donated ourselves to StartServer */
}
