/*
 * Copyright (c) 2019 Sine Nomine Associates. All rights reserved.
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

#include <tests/tap/basic.h>

#include "rx/rx_identity.h"
#include "rx/rxgk.h"

#include "common.h"

static struct rx_identity *
create_gss_id(char *princ)
{
    struct rx_opaque data;
    struct rx_identity *rxid;
    afs_int32 code;

    memset(&data, 0, sizeof(data));

    code = rxgk_krb5_to_gss(princ, &data);
    opr_Assert(code == 0);

    rxid = rx_identity_new(RX_ID_GSS, princ, data.val, data.len);
    opr_Assert(rxid != NULL);

    rx_opaque_freeContents(&data);

    return rxid;
}

static void
test_princs(void)
{
    int code;
    int tc_i;
    struct {
	char *k5_princ;
	char *k4_princ;
	afs_uint32 flags;

    } *tc, test_cases[] = {
	{
	    "user@EXAMPLE.COM",
	    "user@EXAMPLE.COM",
	},
	{
	    "user/admin@EXAMPLE.COM",
	    "user.admin@EXAMPLE.COM",
	},
	{
	    "user/admin@EXAMPLE.COM",
	    "user.admin@EXAMPLE.COM",
	    RXGK_524CONV_DISABLE_DOTCHECK,
	},
	{
	    "host/e40-po.mit.edu@ATHENA.MIT.EDU",
	    "rcmd.e40-po@ATHENA.MIT.EDU",
	},
	{
	    "ftp/public.example.org@EXAMPLE.ORG",
	    "ftp.public@EXAMPLE.ORG",
	},
	{
	    "zephyr/zephyr@EXAMPLE.NET",
	    "zephyr@EXAMPLE.NET",
	},

	{
	    "user.admin@EXAMPLE.COM",
	},
	{
	    "user.admin@EXAMPLE.COM",
	    "user.admin@EXAMPLE.COM",
	    RXGK_524CONV_DISABLE_DOTCHECK,
	},

	{
	    "user.admin/admin@EXAMPLE.COM",
	},
	{
	    "user.admin/admin@EXAMPLE.COM",
	    "user.admin.admin@EXAMPLE.COM",
	    RXGK_524CONV_DISABLE_DOTCHECK,
	},

	{
	    "user@EXAMPLE.COM@EXAMPLE.NET",
	},
	{
	    "user/foo/bar@EXAMPLE.COM",
	},
	{
	    "ftp/public@EXAMPLE.ORG",
	},
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct rx_identity *gss_id = create_gss_id(tc->k5_princ);
	struct rx_identity *k4_id = NULL;
	int exp_code = 0;

	if (tc->k4_princ == NULL) {
	    exp_code = RXGK_BAD_TOKEN;
	}

	code = rxgk_524_conv_id(gss_id, tc->flags, &k4_id);

	is_int(code, exp_code,
	       "rxgk_524_conv_id(%s, 0x%x) == %d",
	       tc->k5_princ, tc->flags, exp_code);

	if (exp_code == 0) {
	    is_int(k4_id->kind, RX_ID_KRB4,
		   "... returns type RX_ID_KRB4");
	    is_string(k4_id->displayName, tc->k4_princ,
		      "... returns displayName %s", tc->k4_princ);
	}
	rx_identity_free(&gss_id);
	rx_identity_free(&k4_id);
    }

}

int
main(void)
{
    int code;
    struct rx_identity rxid, *dummy = NULL;

    plan(34);

    test_princs();

    memset(&rxid, 0, sizeof(rxid));
    rxid.kind = RX_ID_GSS;
    rxid.exportedName.val = "\x04\x01\x00\x0b\x06\x09\x2a\x86\x48\x86\xf7\x12\x01\x02\x02";
    rxid.exportedName.len = 15;
    code = rxgk_524_conv_id(&rxid, 0, &dummy);
    ok(code != 0, "rxgk_524_conv_id for short exportedName fails (15)");

    memset(&rxid, 0, sizeof(rxid));
    rxid.kind = RX_ID_GSS;
    rxid.exportedName.val = "\x04\x01\x00\x0b\x06\x09\x2a\x86\x48\x86\xf7\x12\x01\x02\x02\x00\x00\x00\x00";
    rxid.exportedName.len = 19;
    code = rxgk_524_conv_id(&rxid, 0, &dummy);
    ok(code != 0, "rxgk_524_conv_id for short exportedName fails (19)");

    memset(&rxid, 0, sizeof(rxid));
    rxid.kind = RX_ID_GSS;
    rxid.exportedName.val = "\x04\x01\x00\x0b\x06\x09\x2a\x86\x48\x86\xf7\x12\x01\x02\x02\x00\x00\x00\x05";
    rxid.exportedName.len = 20;
    code = rxgk_524_conv_id(&rxid, 0, &dummy);
    ok(code != 0, "rxgk_524_conv_id for short exportedName fails (20)");

    {
	struct rx_identity *gss_id = create_gss_id("user@EXAMPLE.COM");
	((char*)gss_id->exportedName.val)[1] = '\x02';
	code = rxgk_524_conv_id(gss_id, 0, &dummy);
	rx_identity_free(&gss_id);
	ok(code != 0, "rxgk_524_conv_id for bad-prefix exportedName fails");
    }

    memset(&rxid, 0, sizeof(rxid));
    rxid.kind = RX_ID_SUPERUSER;
    code = rxgk_524_conv_id(&rxid, 0, &dummy);
    ok(code != 0, "rxgk_524_conv_id for non-gss type fails");

    rx_identity_free(&dummy);

    return 0;
}
