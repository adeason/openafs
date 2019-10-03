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

#include <tests/tap/basic.h>

#include "rx/rxgk.h"

#include "common.h"

#define OPAQUE(str) { sizeof(str) - 1, (str) }

int
main(void)
{
    int code;
    int tc_i;
    struct {
	char *k5_princ;
	struct rx_opaque gss_data;
    } *tc, test_cases[] = {
	{
	    "user@EXAMPLE.COM",
	    OPAQUE("\x04\x01\x00\x0b\x06\x09\x2a\x86\x48\x86\xf7\x12\x01\x02\x02"
		   "\x00\x00\x00\x10user@EXAMPLE.COM"),
	},
	{
	    "user/admin@EXAMPLE.COM",
	    OPAQUE("\x04\x01\x00\x0b\x06\x09\x2a\x86\x48\x86\xf7\x12\x01\x02\x02"
		   "\x00\x00\x00\x16user/admin@EXAMPLE.COM"),
	},
    };

    plan(4);

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct rx_opaque gss_got;

	memset(&gss_got, 0, sizeof(gss_got));

	code = rxgk_krb5_to_gss(tc->k5_princ, &gss_got);
	is_int(code, 0,
	       "rxgk_krb5_to_gss(%s) == 0", tc->k5_princ);

	is_opaque(&gss_got, &tc->gss_data,
		  "... result is correct");

	rx_opaque_freeContents(&gss_got);
    }

    return 0;
}
