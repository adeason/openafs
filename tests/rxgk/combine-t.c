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
#include <assert.h>

#include <rx/rx_identity.h>
#include <rx/rxgk.h>
#include <afs/rfc3961.h>

#include "common.h"

#define OPAQUE(str) { sizeof(str) - 1, str }

static void
key2data(rxgk_key key, struct rx_opaque *data)
{
    struct key_impl {
	krb5_context ctx;
	krb5_keyblock key;
    };
    krb5_keyblock *keyblock = &((struct key_impl *)key)->key;
    data->len = keyblock->keyvalue.length;
    data->val = keyblock->keyvalue.data;
}

static void
key2enctype(rxgk_key key, afs_int32 *a_enctype)
{
    struct key_impl {
	krb5_context ctx;
	krb5_keyblock key;
    };
    krb5_keyblock *keyblock = &((struct key_impl *)key)->key;
    *a_enctype = krb5_keyblock_get_enctype(keyblock);
}

int
main(void)
{
    afs_int32 code;
    int tc_i;
    struct {
	afs_int32 k1_enctype;
	struct rx_opaque k1_keydata;
	afs_int32 combined_enctype;
	struct rx_opaque combined_keydata;

    } *tc, test_cases[] = {
	{
	    ETYPE_AES128_CTS_HMAC_SHA1_96,
	    OPAQUE("\xb1\xc2\xd1\xf3\xa5\x98\xfa\xb5\x77\x5f\x86\x9b\x04\x88\x88\xca"),
	    ETYPE_AES128_CTS_HMAC_SHA1_96,
	    OPAQUE("\x87\x01\xca\xa5\x18\x36\xf9\x1c\x2d\xe0\xb2\x0d\x41\xfe\x6c\x4c"),
	},
	{
	    ETYPE_AES128_CTS_HMAC_SHA1_96,
	    OPAQUE("\xb2\xc2\xd1\xf3\xa5\x98\xfa\xb5\x77\x5f\x86\x9b\x04\x88\x88\xca"),
	    ETYPE_AES256_CTS_HMAC_SHA1_96,
	    OPAQUE("\x13\x9c\x07\x7d\xdf\x68\xfe\x98\xe7\xc7\xf8\x27\xc1\xbe\xd7\x2d"
		   "\xeb\x1d\xba\xe0\xbb\xfe\xd3\x25\x1b\x30\xd7\x0d\x4b\xac\xe8\xdc"),
	},
    };
    afsUUID destination = { 0x8484c962, 0x5d1d, 0x4558, 0xb6, 0x52,
			    { 0x56, 0xa5, 0x1e, 0x44, 0x25, 0x70 } };

    plan(6*2);

    for (afstest_Scan(test_cases, tc, tc_i)) {
	rxgk_key k1 = NULL;
	rxgk_key got_key = NULL;
	afs_int32 got_enctype;
	struct rx_opaque got_keydata = RX_EMPTY_OPAQUE;

	code = rxgk_afscombine1_keydata(&got_keydata, tc->combined_enctype,
					&tc->k1_keydata, tc->k1_enctype,
					&destination);
	is_int(code, 0,
	       "[%d] rxgk_afscombine1_keydata() == 0", tc_i);
	is_opaque(&got_keydata, &tc->combined_keydata,
		  "[%d] rxgk_afscombine1_keydata() data", tc_i);

	rx_opaque_freeContents(&got_keydata);

	code = rxgk_make_key(&k1, tc->k1_keydata.val, tc->k1_keydata.len,
			     tc->k1_enctype);
	is_int(code, 0, "[%d] rxgk_make_key() == 0", tc_i);

	code = rxgk_afscombine1_key(&got_key, tc->combined_enctype, k1,
				    &destination);
	is_int(code, 0, "[%d] rxgk_afscombine1_key() == 0", tc_i);

	key2enctype(got_key, &got_enctype);
	key2data(got_key, &got_keydata);

	is_int(got_enctype, tc->combined_enctype,
	       "[%d] rxgk_afscombine1_key() type == %d",
	       tc_i, tc->combined_enctype);
	is_opaque(&got_keydata, &tc->combined_keydata,
		  "[%d] rxgk_afscombine1_key() data",
		  tc_i);

	rxgk_release_key(&k1);
	rxgk_release_key(&got_key);
    }

    return 0;
}
