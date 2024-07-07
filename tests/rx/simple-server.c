/* FIXME: license */

#include <afsconfig.h>
#include <afs/param.h>

#include <roken.h>
#include <err.h>

#include <afs/opr.h>
#include <rx/rx.h>
#include <rx/rx_null.h>

#define MAX_SIZE 16

/* Arbitrary error code constants. */
#define ERROR_TOOBIG 201

static afs_uint32
str2int(const char *str)
{
    return strtoul(str, NULL, 10);
}

static void
rot13(char *buf, afs_int32 nbytes)
{
    int i;
    for (i = 0; i < nbytes; i++) {
	if ((buf[i] >= 'n' && buf[i] <= 'z') ||
	    (buf[i] >= 'N' && buf[i] <= 'Z'))
	{
	    buf[i] -= 13;
	} else if ((buf[i] >= 'a' && buf[i] <= 'm') ||
		   (buf[i] >= 'A' && buf[i] <= 'M'))
	{
	    buf[i] += 13;
	}
    }
}

static afs_int32
rxsimple_ExecuteRequest(struct rx_call *call)
{
    afs_int32 nbytes;
    afs_int32 nbytes_n;
    afs_int32 code = RX_PROTOCOL_ERROR;
    char buf[MAX_SIZE];

    memset(buf, 0, sizeof(buf));

    if (rx_Read32(call, &nbytes_n) != sizeof(nbytes_n)) {
	warnx("rx_Read32 failed");
	goto done;
    }
    nbytes = ntohl(nbytes_n);

    if (nbytes > sizeof(buf)) {
	warnx("nbytes too large: %d > %d", nbytes, (int)sizeof(buf));
	code = ERROR_TOOBIG;
	goto done;
    }

    if (rx_Read(call, buf, nbytes) != nbytes) {
	warnx("rx_Read failed");
	goto done;
    }

    rot13(buf, nbytes);

    if (rx_Write(call, buf, nbytes) != nbytes) {
	warnx("rx_Write failed");
	goto done;
    }

    code = 0;

 done:
    return code;
}

int
main(int argc, char **argv)
{
    afs_uint16 port;
    afs_uint16 service_id;
    afs_int32 code;
    struct rx_service *service;
    struct rx_securityClass *secobj;

    setprogname(argv[0]);

    if (argc != 3) {
	errx(1, "Usage: %s <port> <service_id>", getprogname());
    }

    port = str2int(argv[1]);
    service_id = str2int(argv[2]);

    code = rx_Init(htons(port));
    if (code != 0) {
	errx(1, "rx_Init failed with %d", code);
    }

    secobj = rxnull_NewClientSecurityObject();
    service = rx_NewService(0, service_id, "rxsimple", &secobj, 1,
			    rxsimple_ExecuteRequest);
    opr_Assert(service != NULL);

    rx_StartServer(0);

    printf("%s: Ready to receive calls\n", getprogname());

    /* Close stdout to indicate that the server is ready to receive calls. */
    fclose(stdout);

    rx_ServerProc(NULL);
    abort();

    return 1;
}
