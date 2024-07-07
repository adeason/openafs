/* FIXME: license */

#include <afsconfig.h>
#include <afs/param.h>

#include <roken.h>
#include <stdio.h>
#include <err.h>

#include <afs/opr.h>
#include <rx/rx.h>
#include <rx/rx_null.h>
#include <afs/afsutil.h>

#include <tests/tap/basic.h>

static const char *whoami;

static afs_uint32
str2addr(char *str)
{
    afs_int32 addr = 0;
    struct hostent *th = hostutil_GetHostByName(str);
    if (th == NULL) {
	errx(1, "%s: Could not resolve host '%s'", whoami, str);
    }
    memcpy(&addr, th->h_addr, sizeof(addr));
    return addr;
}

static afs_uint32
str2int(const char *str)
{
    return strtoul(str, NULL, 10);
}

static int
run_call(struct rx_connection *conn, char *message)
{
    struct rx_call *call;
    afs_int32 nbytes = strlen(message);
    afs_int32 nbytes_n;
    char *recvbuf = bcalloc(nbytes + 1, 1);
    afs_int32 code = RX_PROTOCOL_ERROR;

    call = rx_NewCall(conn);
    opr_Assert(call != NULL);

    nbytes_n = htonl(nbytes);
    if (rx_Write32(call, &nbytes_n) != sizeof(nbytes_n)) {
	warnx("%s: rx_Write32 failed", whoami);
	goto done;
    }

    if (rx_Write(call, message, nbytes) != nbytes) {
	warnx("%s: rx_Write failed", whoami);
	goto done;
    }

    if (rx_Read(call, recvbuf, nbytes) != nbytes) {
	warnx("%s: rx_Read failed", whoami);
	goto done;
    }

    printf("%s\n", recvbuf);

    code = 0;

 done:
    code = rx_EndCall(call, code);
    if (code != 0) {
	warnx("%s: call aborted with code %d", whoami, code);
    }
    free(recvbuf);
    return code;
}

int
main(int argc, char **argv)
{
    afs_uint32 host;
    afs_uint16 port;
    afs_uint16 service_id;
    char *message;
    int code;
    struct rx_connection *conn;

    setprogname(argv[0]);
    whoami = getprogname();

    if (argc != 5) {
	errx(1, "Usage: %s <host> <port> <service_id> <message>\n", getprogname());
    }

    host = str2addr(argv[1]);
    port = str2int(argv[2]);
    service_id = str2int(argv[3]);
    message = argv[4];

    code = rx_Init(0);
    if (code != 0) {
	errx(1, "%s: rx_Init failed with %d", whoami, code);
    }

    conn = rx_NewConnection(host, htons(port), service_id,
			    rxnull_NewClientSecurityObject(), 0);
    opr_Assert(conn != NULL);

    code = run_call(conn, message);

    rx_DestroyConnection(conn);

    return code;
}
