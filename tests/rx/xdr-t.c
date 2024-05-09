/*
 * Copyright (c) 2024 Sine Nomine Associates. All rights reserved.
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

#include <rx/rx_opaque.h>
#include <rx/xdr.h>

#include <tests/tap/basic.h>
#include <ctype.h>

#include "common.h"

/* A buffer big enough to be larger than our normal buffer size of 4096. */
char bigbuf[40960];

/*
 * Given an opaque buffer, allocate and return a string of the hexdump of that
 * buffer. Caller must free the returned string.
 */
static char *
opaque2hex(struct rx_opaque *buf)
{
    size_t hex_len = buf->len * 2 + 1;
    char *hex = bcalloc_type(hex_len, char);
    int cur_i;
    unsigned char *buf_c = buf->val;

    for (cur_i = 0; cur_i < buf->len; cur_i++) {
	size_t remain = hex_len - cur_i * 2;
	int nbytes;

	nbytes = snprintf(hex + cur_i * 2, remain, "%02x", buf_c[cur_i]);
	opr_Assert(nbytes == 2);
	opr_Assert(nbytes < remain);
    }

    return hex;
}

/* '0' -> 0, 'f' -> 15, etc */
static unsigned char
hexchar(char hex)
{
    hex = tolower(hex);
    if (hex >= '0' && hex <= '9') {
	return hex - '0';
    }
    if (hex >= 'a' && hex <= 'f') {
	return hex - 'a' + 10;
    }
    bail("bad hex 0x%x", (unsigned int)hex);
    return 0;
}

/*
 * Given a hexdump string, alloc and return an opaque buffer of the actual
 * data. Caller must free the returned opaque.
 */
static struct rx_opaque *
hex2opaque(char *hex)
{
    struct rx_opaque *buf;
    size_t hex_len = strlen(hex);
    int cur_i;

    if (hex_len % 2 != 0) {
	bail("bad hex string: %s", hex);
    }

    buf = rx_opaque_new(NULL, 0);
    opr_Assert(buf != NULL);

    opr_Verify(rx_opaque_alloc(buf, hex_len / 2) == 0);

    for (cur_i = 0; cur_i < buf->len; cur_i++) {
	unsigned char *buf_cur = &((unsigned char *)buf->val)[cur_i];
	char *hex_cur = &hex[cur_i * 2];

	*buf_cur = hexchar(hex_cur[0]) * 16 + hexchar(hex_cur[1]);
    }

    return buf;
}

/* A buffer and XDR for the current buffer we're encoding to. */
static struct rx_opaque encode_buf;
static XDR encode_xdrs;

static XDR *
xdrs_encode(void)
{
    static char buf[4096];

    memset(&encode_xdrs, 0, sizeof(encode_xdrs));
    encode_buf.len = sizeof(buf);
    encode_buf.val = buf;

    xdrmem_create(&encode_xdrs, encode_buf.val, encode_buf.len, XDR_ENCODE);
    return &encode_xdrs;
}

/* Check that the buffer we encoded to matches the given hex. */
static void
check_encode(char *expected_hex)
{
    struct rx_opaque buf;
    char *got_hex;

    buf = encode_buf;
    buf.len = xdr_getpos(&encode_xdrs);
    got_hex = opaque2hex(&buf);

    is_string(got_hex, expected_hex, "... encoding matches");

    free(got_hex);
}

static XDR *
xdrs_decode(char *hex)
{
    static struct rx_opaque *buf;
    static XDR xdrs;

    rx_opaque_free(&buf);
    buf = hex2opaque(hex);

    xdrmem_create(&xdrs, buf->val, buf->len, XDR_DECODE);

    return &xdrs;
}

/*
 * Checks that xdr_int() encodes the given value into a buffer that matches the
 * given hexdump. Also checks that xdr_int() decodes the given hexdump into the
 * given value.
 *
 * All other t_xdr_* functions are the same, but just for other xdr primitives.
 */
static void
t_xdr_int(int val, char *hex)
{
    XDR *xdrs;
    int success;
    int got;

    xdrs = xdrs_encode();
    success = xdr_int(xdrs, &val);
    ok(success, "xdr_int(%d) encode success", val);
    check_encode(hex);

    xdrs = xdrs_decode(hex);
    success = xdr_int(xdrs, &got);
    ok(success, "xdr_int(%s) decode success", hex);
    is_int(got, val, "... value matches");
}

static void
t_xdr_u_int(u_int val, char *hex)
{
    XDR *xdrs;
    int success;
    u_int got;

    xdrs = xdrs_encode();
    success = xdr_u_int(xdrs, &val);
    ok(success, "xdr_u_int(%u) encode success", val);
    check_encode(hex);

    xdrs = xdrs_decode(hex);
    success = xdr_u_int(xdrs, &got);
    ok(success, "xdr_u_int(%s) decode success", hex);
    is_hex(got, val, "... value matches");
}

static void
t_xdr_long(long val, char *hex)
{
    XDR *xdrs;
    int success;
    long got;

    xdrs = xdrs_encode();
    success = xdr_long(xdrs, &val);
    ok(success, "xdr_long(%ld) encode success", val);
    check_encode(hex);

    xdrs = xdrs_decode(hex);
    success = xdr_long(xdrs, &got);
    ok(success, "xdr_long(%s) decode success", hex);
    is_int(got, val, "... value matches");
}

static void
t_xdr_u_long(u_long val, char *hex)
{
    XDR *xdrs;
    int success;
    u_long got;

    xdrs = xdrs_encode();
    success = xdr_u_long(xdrs, &val);
    ok(success, "xdr_u_long(%lu) encode success", val);
    check_encode(hex);

    xdrs = xdrs_decode(hex);
    success = xdr_u_long(xdrs, &got);
    ok(success, "xdr_u_long(%s) decode success", hex);
    is_hex(got, val, "... value matches");
}

/* Checks just the decoding part of t_xdr_char(). */
static void
t_xdr_char_dec(char val, char *hex)
{
    XDR *xdrs;
    int success;
    char got;

    xdrs = xdrs_decode(hex);
    success = xdr_char(xdrs, &got);
    ok(success, "xdr_char(%s) decode success", hex);
    is_hex(got, val, "... value matches");
}

static void
t_xdr_char(char val, char *hex)
{
    XDR *xdrs;
    int success;

    xdrs = xdrs_encode();
    success = xdr_char(xdrs, &val);
    ok(success, "xdr_char(%d) encode success", (int)val);
    check_encode(hex);

    t_xdr_char_dec(val, hex);
}

static void
t_xdr_u_char_dec(u_char val, char *hex)
{
    XDR *xdrs;
    int success;
    u_char got;

    xdrs = xdrs_decode(hex);
    success = xdr_u_char(xdrs, &got);
    ok(success, "xdr_u_char(%s) decode success", hex);
    is_hex(got, val, "... value matches");
}

static void
t_xdr_u_char(u_char val, char *hex)
{
    XDR *xdrs;
    int success;

    xdrs = xdrs_encode();
    success = xdr_u_char(xdrs, &val);
    ok(success, "xdr_u_char(%u) encode success", (unsigned)val);
    check_encode(hex);

    t_xdr_u_char_dec(val, hex);
}

static void
t_xdr_short_dec(short val, char *hex)
{
    XDR *xdrs;
    int success;
    short got;

    xdrs = xdrs_decode(hex);
    success = xdr_short(xdrs, &got);
    ok(success, "xdr_short(%s) decode success", hex);
    is_hex(got, val, "... value matches");
}

static void
t_xdr_short(short val, char *hex)
{
    XDR *xdrs;
    int success;

    xdrs = xdrs_encode();
    success = xdr_short(xdrs, &val);
    ok(success, "xdr_short(%d) encode success", (int)val);
    check_encode(hex);

    t_xdr_short_dec(val, hex);
}

static void
t_xdr_u_short_dec(u_short val, char *hex)
{
    XDR *xdrs;
    int success;
    u_short got;

    xdrs = xdrs_decode(hex);
    success = xdr_u_short(xdrs, &got);
    ok(success, "xdr_u_short(%s) decode success", hex);
    is_hex(got, val, "... value matches");
}

static void
t_xdr_u_short(u_short val, char *hex)
{
    XDR *xdrs;
    int success;

    xdrs = xdrs_encode();
    success = xdr_u_short(xdrs, &val);
    ok(success, "xdr_u_short(%u) encode success", (unsigned)val);
    check_encode(hex);

    t_xdr_u_short_dec(val, hex);
}

/* Checks just the encoding part of t_xdr_bool(). */
static void
t_xdr_bool_enc(bool_t val, char *hex)
{
    XDR *xdrs;
    int success;

    xdrs = xdrs_encode();
    success = xdr_bool(xdrs, &val);
    ok(success, "xdr_bool(%d) encode success", (int)val);
    check_encode(hex);
}

static void
t_xdr_bool_dec(bool_t val, char *hex)
{
    XDR *xdrs;
    int success;
    bool_t got;

    xdrs = xdrs_decode(hex);
    success = xdr_bool(xdrs, &got);
    ok(success, "xdr_bool(%s) decode success", hex);
    is_hex(got, val, "... value matches");
}

static void
t_xdr_bool(bool_t val, char *hex)
{
    t_xdr_bool_enc(val, hex);
    t_xdr_bool_dec(val, hex);
}

static void
t_xdr_enum(enum_t val, char *hex)
{
    XDR *xdrs;
    int success;
    enum_t got;

    xdrs = xdrs_encode();
    success = xdr_enum(xdrs, &val);
    ok(success, "xdr_enum(%d) encode success", val);
    check_encode(hex);

    xdrs = xdrs_decode(hex);
    success = xdr_enum(xdrs, &got);
    ok(success, "xdr_enum(%s) decode success", hex);
    is_int(got, val, "... value matches");
}

static void
t_xdr_opaque_dec_common(int should_succeed, u_int size, void *val, char *hex)
{
    XDR *xdrs;
    int success;
    struct rx_opaque got;
    struct rx_opaque exp;

    opr_Verify(rx_opaque_alloc(&got, size) == 0);

    xdrs = xdrs_decode(hex);
    success = xdr_opaque(xdrs, got.val, got.len);

    if (should_succeed) {
	ok(success, "xdr_opaque(%s) decode success", hex);
    } else {
	ok(!success, "xdr_opaque(%s) decode fail", hex);
	goto done;
    }

    exp.val = val;
    exp.len = size;

    is_opaque(&got, &exp, "... value matches");

 done:
    rx_opaque_freeContents(&got);
}

static void
t_xdr_opaque_dec(u_int size, void *val, char *hex)
{
    t_xdr_opaque_dec_common(1, size, val, hex);
}

/* Like t_xdr_opaque_dec(), but checks that xdr_opaque() fails. */
static void
t_xdr_opaque_dec_fail(u_int size, char *hex)
{
    t_xdr_opaque_dec_common(0, size, NULL, hex);
}

static void
t_xdr_opaque_common(int should_succeed, u_int size, void *val, char *hex)
{
    XDR *xdrs;
    int success;

    xdrs = xdrs_encode();
    success = xdr_opaque(xdrs, val, size);
    if (should_succeed) {
	ok(success, "xdr_opaque(%s) encode success", hex);
	check_encode(hex);
    } else {
	ok(!success, "xdr_opaque(%s) encode fail", hex);
    }

    t_xdr_opaque_dec_common(should_succeed, size, val, hex);
}

static void
t_xdr_opaque(u_int size, void *val, char *hex)
{
    t_xdr_opaque_common(1, size, val, hex);
}

static void
t_xdr_opaque_fail(u_int size, void *val, char *hex)
{
    t_xdr_opaque_common(0, size, val, hex);
}

static void
t_xdr_bytes_dec_common(int should_succeed, u_int maxsize, u_int size, void *val, char *hex)
{
    XDR *xdrs;
    int success;
    char *got_val = NULL;
    u_int got_len = 0;
    struct rx_opaque got;
    struct rx_opaque exp;

    xdrs = xdrs_decode(hex);
    success = xdr_bytes(xdrs, &got_val, &got_len, maxsize);

    if (should_succeed) {
	ok(success, "xdr_bytes(%u, %s) decode success", maxsize, hex);
    } else {
	ok(!success, "xdr_bytes(%u, %s) decode fail", maxsize, hex);
	goto done;
    }

    got.val = got_val;
    got.len = got_len;

    exp.val = val;
    exp.len = size;

    is_opaque(&got, &exp, "... value matches");

 done:
    xdrs->x_op = XDR_FREE;
    xdr_bytes(xdrs, &got_val, &got_len, maxsize);
}

static void
t_xdr_bytes_dec(u_int maxsize, u_int size, void *val, char *hex)
{
    t_xdr_bytes_dec_common(1, maxsize, size, val, hex);
}

static void
t_xdr_bytes_dec_fail(u_int maxsize, char *hex)
{
    t_xdr_bytes_dec_common(0, maxsize, 0, NULL, hex);
}

static void
t_xdr_bytes_common(int should_succeed, u_int maxsize, u_int size, void *val, char *hex)
{
    XDR *xdrs;
    int success;

    xdrs = xdrs_encode();
    success = xdr_bytes(xdrs, (char**)&val, &size, maxsize);
    if (should_succeed) {
	ok(success, "xdr_bytes(%u, %s) encode success", maxsize, hex);
	check_encode(hex);
    } else {
	ok(!success, "xdr_bytes(%u, %s) encode fail", maxsize, hex);
    }

    t_xdr_bytes_dec_common(should_succeed, maxsize, size, val, hex);
}

static void
t_xdr_bytes(u_int maxsize, u_int size, void *val, char *hex)
{
    t_xdr_bytes_common(1, maxsize, size, val, hex);
}

static void
t_xdr_bytes_fail(u_int maxsize, u_int size, void *val, char *hex)
{
    t_xdr_bytes_common(0, maxsize, size, val, hex);
}

static void
t_xdr_string_dec_common(int should_succeed, u_int maxsize, u_int val_size, char *val, char *hex)
{
    XDR *xdrs;
    int success;
    char *got = NULL;

    xdrs = xdrs_decode(hex);
    success = xdr_string(xdrs, &got, maxsize);

    if (should_succeed) {
	ok(success, "xdr_string(%u, %s) decode success", maxsize, hex);
    } else {
	ok(!success, "xdr_string(%u, %s) decode fail", maxsize, hex);
	goto done;
    }

    if (val_size == 0) {
	is_string(got, val, "... value matches");
    } else {
	is_blob(got, val, val_size, "... value matches");
    }

 done:
    xdrs->x_op = XDR_FREE;
    xdr_string(xdrs, &got, maxsize);
}

static void
t_xdr_string_dec(u_int maxsize, char *val, char *hex)
{
    t_xdr_string_dec_common(1, maxsize, 0, val, hex);
}

static void
t_xdr_string_dec_fail(u_int maxsize, char *hex)
{
    t_xdr_string_dec_common(0, maxsize, 0, NULL, hex);
}

/*
 * Like t_xdr_string_dec(), but the resulting decoded string we match against
 * is an opaque with a length, not a NUL-terminated string.
 */
static void
t_xdr_string_dec_opaque(u_int maxsize, u_int size, char *val, char *hex)
{
    t_xdr_string_dec_common(1, maxsize, size, val, hex);
}

static void
t_xdr_string_common(int should_succeed, u_int maxsize, char *val, char *hex)
{
    XDR *xdrs;
    int success;

    xdrs = xdrs_encode();
    success = xdr_string(xdrs, &val, maxsize);
    if (should_succeed) {
	ok(success, "xdr_string(%u, %s) encode success", maxsize, hex);
	check_encode(hex);
    } else {
	ok(!success, "xdr_string(%u, %s) encode fail", maxsize, hex);
    }

    t_xdr_string_dec_common(should_succeed, maxsize, 0, val, hex);
}

static void
t_xdr_string(u_int maxsize, char *val, char *hex)
{
    t_xdr_string_common(1, maxsize, val, hex);
}

static void
t_xdr_string_fail(u_int maxsize, char *val, char *hex)
{
    t_xdr_string_common(0, maxsize, val, hex);
}

int
main(void)
{
    /* Make 'bigbuf' full of 'x's, except for the last char, so it's a
     * terminated string. */
    memset(bigbuf, 'x', sizeof(bigbuf) - 1);

    plan(278);

    ok(xdr_void(), "xdr_void success");

    /*
     * Check that xdr_int() encodes a 1 into 0x00000001, and decodes 0x00000001
     * into a 1.
     */
    t_xdr_int(1,  "00000001");
    t_xdr_int(-1, "ffffffff");
    t_xdr_int(0,  "00000000");
    t_xdr_int(11259375,    "00abcdef");
    t_xdr_int(2147483647,  "7fffffff");
    t_xdr_int(-2147483648, "80000000");

    /* Same as above, but for xdr_u_int(). */
    t_xdr_u_int(0, "00000000");
    t_xdr_u_int(1, "00000001");
    t_xdr_u_int(4294967295, "ffffffff");

    t_xdr_long(0,  "00000000");
    t_xdr_long(1,  "00000001");
    t_xdr_long(-1, "ffffffff");
    t_xdr_long(2147483647,  "7fffffff");
    t_xdr_long(-2147483648, "80000000");

    t_xdr_u_long(0, "00000000");
    t_xdr_u_long(1, "00000001");
    t_xdr_u_long(4294967295, "ffffffff");

    t_xdr_char(0,  "00000000");
    t_xdr_char(1,  "00000001");
    t_xdr_char(-1, "ffffffff");
    t_xdr_char(127,  "0000007f");
    t_xdr_char(-128, "ffffff80");

    /* The high bits shouldn't matter when decoding. */
    t_xdr_char_dec(1,  "deadbe01");
    t_xdr_char_dec(-1, "000000ff");

    t_xdr_u_char(0, "00000000");
    t_xdr_u_char(1, "00000001");
    t_xdr_u_char(255, "000000ff");
    t_xdr_u_char_dec(1, "deadbe01");

    t_xdr_short(0,  "00000000");
    t_xdr_short(1,  "00000001");
    t_xdr_short(-1, "ffffffff");
    t_xdr_short(32767,  "00007fff");
    t_xdr_short(-32768, "ffff8000");
    t_xdr_short_dec(1,  "dead0001");
    t_xdr_short_dec(-1, "0000ffff");

    t_xdr_u_short(0, "00000000");
    t_xdr_u_short(1, "00000001");
    t_xdr_u_short(65535, "0000ffff");
    t_xdr_u_short_dec(1, "dead0001");

    t_xdr_bool(0, "00000000");
    t_xdr_bool(1, "00000001");
    /*
     * Encoding other values as a bool will just result in a 1 in the encoded
     * stream. Decoding other values will also result in getting a 1 back.
     */
    t_xdr_bool_enc(-1, "00000001");
    t_xdr_bool_enc(2147483647, "00000001");
    t_xdr_bool_enc(-2147483648, "00000001");
    t_xdr_bool_dec(1, "7fffffff");
    t_xdr_bool_dec(1, "ffffffff");
    t_xdr_bool_dec(1, "80000000");

    t_xdr_enum(0, "00000000");
    t_xdr_enum(1, "00000001");
    t_xdr_enum(-1, "ffffffff");
    t_xdr_enum(2147483647,  "7fffffff");
    t_xdr_enum(-2147483648, "80000000");

    t_xdr_opaque(0, NULL, "");
    t_xdr_opaque(4, "a\0cd", "61006364");
    /* Opaques with a length not divisible by 4 should result in padding. */
    t_xdr_opaque(5, "a\0cde",   "6100636465000000");
    t_xdr_opaque(6, "a\0cdef",  "6100636465660000");
    t_xdr_opaque(7, "a\0cdefg", "6100636465666700");
    /* Padding should be ignored on decode. */
    t_xdr_opaque_dec(5, "a\0cde",   "6100636465deadbe");
    t_xdr_opaque_dec(6, "a\0cdef",  "610063646566dead");
    t_xdr_opaque_dec(7, "a\0cdefg", "61006364656667de");

    /* xdr_opaque() should fail for a somewhat large value, since our buffer is
     * not that big. */
    t_xdr_opaque_fail(sizeof(bigbuf), bigbuf, "00000000");

    /* This should fail because it lacks padding. */
    t_xdr_opaque_dec_fail(5, "0102030405");

    /*
     * Like xdr_opaque(), but note that xdr_bytes() needs a maxsize in addition
     * to a size. We give a maxsize of 4096 for most of these.
     */
    t_xdr_bytes(4096, 0, NULL, "00000000");
    t_xdr_bytes(4096, 4, "a\0cd",    "0000000461006364");
    t_xdr_bytes(4096, 5, "a\0cde",   "000000056100636465000000");
    t_xdr_bytes(4096, 6, "a\0cdef",  "000000066100636465660000");
    t_xdr_bytes(4096, 7, "a\0cdefg", "000000076100636465666700");
    t_xdr_bytes_dec(4096, 5, "a\0cde",   "000000056100636465deadbe");
    t_xdr_bytes_dec(4096, 6, "a\0cdef",  "00000006610063646566dead");
    t_xdr_bytes_dec(4096, 7, "a\0cdefg", "0000000761006364656667de");

    /* If we try to encode or decode more than maxsize bytes, we should fail. */
    t_xdr_bytes_fail(3, 4, "a\0cd", "0000000461006364");

    /* We should fail for a large number of bytes, since our buffer is not that big. */
    t_xdr_bytes_fail(sizeof(bigbuf), sizeof(bigbuf) - 1, bigbuf, "0000000801020304");

    /* Lacks padding. */
    t_xdr_bytes_dec_fail(4096, "000000050102030405");

    t_xdr_string(4096, "", "00000000");
    t_xdr_string(4096, "abcd",    "0000000461626364");
    t_xdr_string(4096, "abcde",   "000000056162636465000000");
    t_xdr_string(4096, "abcdef",  "000000066162636465660000");
    t_xdr_string(4096, "abcdefg", "000000076162636465666700");
    t_xdr_string_dec(4096, "abcde",   "000000056162636465deadbe");
    t_xdr_string_dec(4096, "abcdef",  "00000006616263646566dead");
    t_xdr_string_dec(4096, "abcdefg", "0000000761626364656667de");
    t_xdr_string_fail(3, "abcd", "0000000461626364");
    t_xdr_string_fail(sizeof(bigbuf), bigbuf, "00FFFFFE61626364");

    /* This works, but it's weird because there's a NUL byte in the string. */
    t_xdr_string_dec_opaque(4096, 4, "a\0cd", "0000000461006364");

    t_xdr_string_dec_fail(4096, "000000056162636465");

    /* TODO: xdr_union(). */

    return 0;
}
