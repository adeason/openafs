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

#include "common.h"
#include <tests/tap/basic.h>

#include <opr/time.h>
#include <math.h>

#include <afs/afs_stats.h>
#include <afs/fs_stats.h>

static int
is_int64_approx_v(afs_int64 left, afs_int64 right, afs_int64 epsilon,
		  const char *format, va_list args)
{
    int success = 0;
    afs_int64 diff = left - right;

    opr_Assert(epsilon >= 0);

    if (left == right) {
	success = 1;
    } else if (diff > 0 && diff <= epsilon) {
	success = 1;
    } else if (diff < 0 && -diff <= epsilon) {
	success = 1;
    }

    if (!success) {
	diag(" left: %lld", (long long)left);
	diag("right: %lld", (long long)right);
	diag(" diff: %lld", (long long)diff);
    }
    return okv(success, format, args);
}

static int
is_time64(struct afs_time64 t1, afs_int64 t2_clunks, const char *format, ...)
{
    int success;
    va_list args;

    va_start(args, format);
    success = is_int64_v(opr_time64_toClunks(t1), t2_clunks, format, args);
    va_end(args);
    return success;
}

static int
is_time64_approx(struct afs_time64 t1, afs_int64 t2_clunks, afs_int64 epsilon,
		 const char *format, ...)
{
    int success;
    va_list args;

    va_start(args, format);
    success = is_int64_approx_v(opr_time64_toClunks(t1), t2_clunks, epsilon,
				format, args);
    va_end(args);

    return success;
}

static void
test_cmp(void)
{
    int tc_i;
    struct {
	afs_int64 clunks1;
	afs_int64 clunks2;
	int res_cmp;
	int res_lt;
	int res_lteq;
	int res_gt;
	int res_gteq;
	int res_eq;
    } *tc, test_cases[] = {
	/* clunks1, clunks2, cmp, <, <=, >, >=, == */
	{ 0, 0, 0, 0, 1, 0, 1, 1 },

	{  17508035496712980LL,  17508035496712980LL,  0, 0, 1, 0, 1, 1 },
	{  17508035496712985LL,  17508035496712980LL,  1, 0, 0, 1, 1, 0 },
	{  17508035496712975LL,  17508035496712980LL, -1, 1, 1, 0, 0, 0 },
	{ -17508035496712980LL, -17508035496712980LL,  0, 0, 1, 0, 1, 1 },
	{ -17508035496712985LL, -17508035496712980LL, -1, 1, 1, 0, 0, 0 },
	{ -17508035496712975LL, -17508035496712980LL,  1, 0, 0, 1, 1, 0 },
	{  17508035496712980LL, -17508035496712980LL,  1, 0, 0, 1, 1, 0 },
	{ -17508035496712980LL,  17508035496712980LL, -1, 1, 1, 0, 0, 0 },

	{ 9223372036854775807LL, -9223372036854775807LL - 1LL,  1, 0, 0, 1, 1, 0 },
	{ -9223372036854775807LL - 1LL, 9223372036854775807LL, -1, 1, 1, 0, 0, 0 },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct afs_time64 t1 = { tc->clunks1 };
	struct afs_time64 t2 = { tc->clunks2 };

	is_int(opr_time64_cmp(t1, t2), tc->res_cmp,
	       "opr_time64_cmp(%lld, %lld) == %d",
	       opr_time64_toClunksLL(t1),
	       opr_time64_toClunksLL(t2),
	       tc->res_cmp);

	is_int(opr_time64_lt(t1, t2), tc->res_lt,
	       "opr_time64_lt(%lld, %lld) == %d",
	       opr_time64_toClunksLL(t1),
	       opr_time64_toClunksLL(t2),
	       tc->res_lt);

	is_int(opr_time64_lteq(t1, t2), tc->res_lteq,
	       "opr_time64_lteq(%lld, %lld) == %d",
	       opr_time64_toClunksLL(t1),
	       opr_time64_toClunksLL(t2),
	       tc->res_lteq);

	is_int(opr_time64_gt(t1, t2), tc->res_gt,
	       "opr_time64_gt(%lld, %lld) == %d",
	       opr_time64_toClunksLL(t1),
	       opr_time64_toClunksLL(t2),
	       tc->res_gt);

	is_int(opr_time64_gteq(t1, t2), tc->res_gteq,
	       "opr_time64_gteq(%lld, %lld) == %d",
	       opr_time64_toClunksLL(t1),
	       opr_time64_toClunksLL(t2),
	       tc->res_gteq);

	is_int(opr_time64_eq(t1, t2), tc->res_eq,
	       "opr_time64_eq(%lld, %lld) == %d",
	       opr_time64_toClunksLL(t1),
	       opr_time64_toClunksLL(t2),
	       tc->res_eq);
    }
}

static void
test_attrs(void)
{
    int tc_i;
    struct {
	afs_int64 clunks;
	int res_ismaxtime;
	int res_iszero;
	int res_isnegative;
	int res_ispositive;
    } *tc, test_cases[] = {
	{  0, 0, 1, 0, 0 },
	{  5, 0, 0, 0, 1 },
	{ -5, 0, 0, 1, 0 },

	{  0x7FFFFFFF, 0, 0, 0, 1 },
	{  0x7FFFFFFFFFFFFFFELL, 0, 0, 0, 1 },
	{  0x7FFFFFFFFFFFFFFFLL, 1, 0, 0, 1 },
	{ -0x7FFFFFFFFFFFFFFFLL - 1LL, 0, 0, 1, 0 },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct afs_time64 val = { tc->clunks };

	is_int(opr_time64_isMaxtime(val), tc->res_ismaxtime,
	       "opr_time64_isMaxtime(%lld) == %d",
	       opr_time64_toClunksLL(val), tc->res_ismaxtime);

	is_int(opr_time64_isZero(val), tc->res_iszero,
	       "opr_time64_isZero(%lld) == %d",
	       opr_time64_toClunksLL(val), tc->res_iszero);

	is_int(opr_time64_isNegative(val), tc->res_isnegative,
	       "opr_time64_isNegative(%lld) == %d",
	       opr_time64_toClunksLL(val), tc->res_isnegative);

	is_int(opr_time64_isPositive(val), tc->res_ispositive,
	       "opr_time64_isPositive(%lld) == %d",
	       opr_time64_toClunksLL(val),
	       tc->res_ispositive);
    }
}

static void
test_constants(void)
{
    int tc_i;
    struct {
	const char *descr;
	struct afs_time64 got;
	afs_int64 expected;
    } *tc, test_cases[] = {
	{ "maxTime", opr_time64_maxTime(), 0x7FFFFFFFFFFFFFFFLL },
	{ "zero",    opr_time64_zero(),	   0 },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	is_time64(tc->got, tc->expected,
		  "%s == %lld",
		  tc->descr, (long long)tc->expected);
    }
}

static void
test_add(void)
{
    int tc_i;
    struct {
	afs_int64 clunks;
	afs_int64 add;

	/* Expected code from opr_time64_add_safe().*/
	int code;

	/*
	 * Expected result from opr_time64_add_wrap() (and from
	 * opr_time64_add_safe() if code==0).
	 */
	afs_int64 expected;
    } *tc, test_cases[] = {
	{ 0, 0, 0, 0 },

	{  17508035496712980LL,  5,  0,  17508035496712985LL },
	{  17508035496712980LL, -5,  0,  17508035496712975LL },
	{ -17508035496712980LL,  5,  0, -17508035496712975LL },
	{ -17508035496712980LL, -5,  0, -17508035496712985LL },

	{ 0x7FFFFFFFFFFFFFF0LL, 15,      0,  0x7FFFFFFFFFFFFFFFLL },
	{ 0x7FFFFFFFFFFFFFF0LL, 16, ERANGE, -0x7FFFFFFFFFFFFFFFLL - 1LL },
	{ 0x7FFFFFFFFFFFFFF0LL, 20, ERANGE, -0x7FFFFFFFFFFFFFFCLL },

	{ -0x7FFFFFFFFFFFFFF0LL, -16,      0, -0x7FFFFFFFFFFFFFFFLL - 1LL },
	{ -0x7FFFFFFFFFFFFFF0LL, -17, ERANGE,  0x7FFFFFFFFFFFFFFFLL },
	{ -0x7FFFFFFFFFFFFFF0LL, -20, ERANGE,  0x7FFFFFFFFFFFFFFCLL },

	{  0x7FFFFFFFFFFFFFFFLL,        0x7FFFFFFFFFFFFFFFLL,       ERANGE, -2 },
	{ -0x7FFFFFFFFFFFFFFFLL - 1LL, -0x7FFFFFFFFFFFFFFFLL - 1LL, ERANGE,  0 },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct afs_time64 in = { tc->clunks };
	struct afs_time64 add = { tc->add };
	struct afs_time64 got = {0};

	is_int(opr_time64_add_safe(in, add, &got), tc->code,
	       "opr_time64_add_safe(%lld, %lld) == %d",
	       opr_time64_toClunksLL(in),
	       opr_time64_toClunksLL(add),
	       tc->code);

	if (tc->code == 0) {
	    is_time64(got, tc->expected,
		      "... result matches %lld",
		      (long long)tc->expected);
	}
    }
}

static void
test_addSecs(void)
{
    int tc_i;
    struct {
	afs_int64 clunks;
	afs_int64 add_secs;
	int code;
	afs_int64 expected;

    } *tc, test_cases[] = {
	{ 17508035490123456LL, 5, 0, 17508035540123456LL },

	{  9223372036844775807LL,  1, 0, 0x7FFFFFFFFFFFFFFFLL },
	{  9223372036844775808LL,  1, ERANGE },

	{ -9223372036844775808LL, -1, 0, -0x7FFFFFFFFFFFFFFFLL - 1LL },
	{ -9223372036844775809LL, -1, ERANGE },

	{ 0x7FFFFFFFFFFFFFFFLL, -922337203685LL, 0, 4775807 },
	{ 0x7FFFFFFFFFFFFFFFLL, -922337203686LL, ERANGE },

	{ -0x7FFFFFFFFFFFFFFFLL - 1LL, 922337203685LL, 0, -4775808 },
	{ -0x7FFFFFFFFFFFFFFFLL - 1LL, 922337203686LL, ERANGE },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct afs_time64 in = { tc->clunks };
	struct afs_time64 got = { 0 };

	is_int(opr_time64_addSecs_safe(in, tc->add_secs, &got), tc->code,
	       "opr_time64_addSecs_safe(%lld, %lld) == %d",
	       opr_time64_toClunksLL(in),
	       (long long)tc->add_secs,
	       tc->code);
	if (tc->code == 0) {
	    is_time64(got, tc->expected,
		      " ... returns time %lld",
		      (long long)tc->expected);
	}
    }
}

static void
test_fromSecs(void)
{
    int tc_i;
    struct {
	afs_int64 secs;
	int code;
	afs_int64 expected;
    } *tc, test_cases[] = {
	{ 0, 0, 0 },

	{ 1750881673, 0, 17508816730000000LL },
	{ -36, 0, -360000000LL },

	{ 922337203685LL,  0, 9223372036850000000LL },
	{ 922337203686LL,  ERANGE },
	{ 1000000000000LL, ERANGE },

	{ -922337203685LL,  0, -9223372036850000000LL },
	{ -922337203686LL,  ERANGE },
	{ -1000000000000LL, ERANGE },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct afs_time64 got;

	is_int(opr_time64_fromSecs_safe(tc->secs, &got), tc->code,
	       "opr_time64_fromSecs_safe(%lld) == %d",
	       (long long)tc->secs,
	       tc->code);
	if (tc->code == 0) {
	    is_time64(got, tc->expected,
		      " ... result matches %lld",
		      (long long)tc->expected);
	}
    }
}

static void
test_fromMicrosecs(void)
{
    int tc_i;
    struct {
	afs_int64 usec;
	int code;
	afs_int64 expected;
    } *tc, test_cases[] = {
	{ 922337203685477580LL, 0, 9223372036854775800LL },
	{ 922337203685477581LL, ERANGE },

	{ -922337203685477580LL, 0, -9223372036854775800LL },
	{ -922337203685477581LL, ERANGE },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct afs_time64 got = {0};

	is_int(opr_time64_fromMicrosecs_safe(tc->usec, &got), tc->code,
	       "opr_time64_fromMicrosecs_safe(%lld) == %d",
	       (long long)tc->usec,
	       tc->code);
	if (tc->code == 0) {
	    is_time64(got, tc->expected,
		      " ... result matches %lld",
		      (long long)tc->expected);
	}
    }
}

static void
test_fromTimeval(void)
{
    int tc_i;
    struct {
	afs_int64 secs;
	afs_int64 usec;
	int code;
	afs_int64 expected;
    } *tc, test_cases[] = {
	{  0, 0, 0, 0 },
	{  4, 5, 0,  40000050 },
	{ -4, 5, 0, -39999950 },

	{  4, 1000005, 0, 50000050 }, /* weird; usec over 10^6 */
	{  5,       5, 0, 50000050 },

	{  4,     -5, 0, 39999950 },
	{  3, 999995, 0, 39999950 },

	{ -4,     -5, 0, -40000050 },
	{ -5, 999995, 0, -40000050 },

	{ 922337203685LL,  477580, 0, 9223372036854775800LL },
	{ 922337203685LL,  477581, ERANGE },
	{ 922337203685LL, -477581, 0, 9223372036845224190LL },

	{ 922337203686LL, 0, ERANGE },
	{ 922337203686LL,  999999, ERANGE },
	{ 922337203686LL, -999999, ERANGE },

	{ -922337203685LL, -477580, 0, -9223372036854775800LL },
	{ -922337203685LL, -477581, ERANGE },
	{ -922337203685LL,  477581, 0, -9223372036845224190LL },

	{ -922337203686LL, 0, ERANGE },
	{ -922337203686LL,  999999, ERANGE },
	{ -922337203686LL, -999999, ERANGE },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct afs_time64 got;
	is_int(opr_time64_fromTimeval_safe(tc->secs, tc->usec, &got), tc->code,
	       "opr_time64_fromTimeval_safe(%lld, %lld) == %d",
	       (long long)tc->secs,
	       (long long)tc->usec,
	       tc->code);
	if (tc->code == 0) {
	    is_time64(got, tc->expected,
		      " ... result matches %lld",
		      (long long)tc->expected);
	}
    }
}

static void
test_toSecs(void)
{
    int tc_i;
    struct {
	afs_int64 clunks;
	afs_int64 secs;
    } *tc, test_cases[] = {
	{ 0 },

	/*  clunks  secs */
	{  51234567,   5 },
	{ -51234567,  -5 },

	/*           clunks          secs */
	{ 21474836479999999LL, 2147483647 },
	{ 21474836480000000LL, 2147483648LL },
	{ 21474836491234567LL, 2147483649LL },
	{ 42949672959999999LL, 4294967295LL },

	/*           clunks             secs */
	{ 42949672960000000LL,    4294967296LL },
	{ 42949672971234567LL,    4294967297LL },
	{ 0x7fffffffffffffffLL, 0xd6bf94d5e5LL },

	/*            clunks           secs */
	{ -21474836479999999LL, -2147483647 },
	{ -21474836480000000LL, -2147483648 },
	{ -21474836491234567LL, -2147483649 },

	{ -42949672959999999LL, -4294967295 },
	{ -42949672960000000LL, -4294967296 },
	{ -42949672970123456LL, -4294967297 },

	{ -0x8000000000000000LL,
			      -0xd6bf94d5e5LL },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct afs_time64 val = { tc->clunks };

	is_int64(opr_time64_toSecs(val), tc->secs,
		 "opr_time64_toSecs(%lld) == %lld",
		 opr_time64_toClunksLL(val),
		 (long long)tc->secs);

    }
}

static void
test_ctime(void)
{
    int tc_i;
    struct {
	afs_int64 clunks;

	/* opr_time64_ctime() can match either 'ctime' or 'ctime2' (if set). */
	const char *ctime;
	const char *ctime2;

    } *tc, test_cases[] = {
	{ 12336521450123456LL, "Tue Feb  3 04:09:05 2009" },

	/* Around the 2^31 limit. */
	{ 21474836470123456LL, "Mon Jan 18 22:14:07 2038" },
	{ 21474836480123456LL, "Mon Jan 18 22:14:08 2038" },

	/* Around the 2^32 limit. */
	{ 42949672950123456LL, "Sun Feb  7 01:28:15 2106" },
	{ 42949672960123456LL, "Sun Feb  7 01:28:16 2106" },

	/* Arbitrary far-future date. */
	{ 82949672960123456LL, "Fri Nov  9 08:34:56 2232" },

	/*
	 * Arbitrary negative dates. Don't go too far in the past; some systems
	 * may interpret far-past dates differently.
	 */
	{    -3153600009999999LL, "Sun Jan  3 19:00:00 1960" },
	{   -22089708009999999LL, "Mon Jan  1 00:00:00 1900" },

	/*
	 * Biggest/smallest possible dates. For times after year 9999 or before
	 * year 0, ctime() on some sytems will return an error, but success on
	 * others. Accept either result here.
	 */
	{  9223372036854775807LL, "Sat Sep 13 21:48:05 31197",
				  "[922337203685]" },
	{ -9223372036854775807LL - 1LL,
				  "Sun Apr 19 16:11:55 -27258",
				  "[-922337203685]" },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct afs_time64 val = { tc->clunks };
	char *got = opr_time64_ctime(val);
	const char *exp_ctime = tc->ctime;

	if (tc->ctime2 != NULL && strcmp(got, tc->ctime2) == 0) {
	    exp_ctime = tc->ctime2;
	}

	is_string(got, exp_ctime,
		  "opr_time64_ctime(%lld) == '%s'",
		  opr_time64_toClunksLL(val),
		  exp_ctime);
    }
}

static void
test_now(void)
{
    struct afs_time64 now = opr_time64_now();
    time_t now_t = time(NULL);
    afs_int64 epsilon = 2 * OPR_TIME64_CLUNKS_PER_SEC;

    is_time64_approx(now, now_t * OPR_TIME64_CLUNKS_PER_SEC, epsilon,
		    "opr_time64_now() (%lld) returns time close to time() (%ld)",
		    opr_time64_toClunksLL(now), (long)now_t);
}

int
main(int argc, char **argv)
{
    plan(212);

    /* Assume EST timezone. */
    putenv("TZ=EST+5");

    test_cmp();
    test_attrs();
    test_constants();

    test_add();
    test_addSecs();

    test_fromSecs();
    test_fromMicrosecs();
    test_fromTimeval();

    test_toSecs();

    test_ctime();

    test_now();

    return 0;
}
