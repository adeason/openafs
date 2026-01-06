/*
 * Copyright (c) 2012 Your File System Inc. All rights reserved.
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

/*
 * This header provides routines for dealing with the 100ns based AFS time
 * type, afs_time64. With this type, time is represented in units of 100ns,
 * sometimes called "clunks". Absolute time is the same as with unix time (time
 * since the unix epoch of 1 Jan 1970 UTC, skipping leap seconds), except
 * represented in clunks instead of seconds. The count of clunks is always
 * recorded in a signed 64-bit integer.
 *
 * The actual integer is hidden inside a structure, so that accidental
 * assignment like this will fail during compilation:
 *
 *     time_t ourTime;
 *     struct afs_time64 theirTime;
 *
 *     ourTime = theirTime;
 *
 * But any callers can still easily access the underlying raw int64 by just
 * looking at "theirTime.clunks". The name of the internal field is a bit
 * obnoxious, which mildly discourages callers from interacting with the raw
 * value.
 */

#ifndef OPENAFS_OPR_TIME_H
#define OPENAFS_OPR_TIME_H

#include <afs/opr.h>
#if defined(KERNEL) && !defined(UKERNEL)
# include "afs/sysincludes.h"
#else
# include <errno.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <sys/time.h>
# include <time.h>
#endif

#define OPR_TIME64_CLUNKS_PER_US    (10LL)
#define OPR_TIME64_CLUNKS_PER_MS    (OPR_TIME64_CLUNKS_PER_US * 1000LL)
#define OPR_TIME64_CLUNKS_PER_SEC   (OPR_TIME64_CLUNKS_PER_MS * 1000LL)

#define OPR_TIME64_MAX_SECS  (922337203685LL)
#define OPR_TIME64_MIN_SECS (-922337203685LL)

#define OPR_TIME64_MAX_MICROSECS  (922337203685477580LL)
#define OPR_TIME64_MIN_MICROSECS (-922337203685477580LL)

#define OPR_TIME64_MAX_CLUNKS (0x7FFFFFFFFFFFFFFFLL)
#define OPR_TIME64_MIN_CLUNKS (-OPR_TIME64_MAX_CLUNKS - 1)

static_inline int
opr_time64_cmp(struct afs_time64 t1, struct afs_time64 t2)
{
    if (t1.clunks > t2.clunks) {
	return 1;
    }
    if (t1.clunks < t2.clunks) {
	return -1;
    }
    return 0;
}

static_inline int
opr_time64_lt(struct afs_time64 t1, struct afs_time64 t2)
{
    return opr_time64_cmp(t1, t2) < 0;
}

static_inline int
opr_time64_lteq(struct afs_time64 t1, struct afs_time64 t2)
{
    return opr_time64_cmp(t1, t2) <= 0;
}

static_inline int
opr_time64_gt(struct afs_time64 t1, struct afs_time64 t2)
{
    return opr_time64_cmp(t1, t2) > 0;
}

static_inline int
opr_time64_gteq(struct afs_time64 t1, struct afs_time64 t2)
{
    return opr_time64_cmp(t1, t2) >= 0;
}
static_inline int
opr_time64_eq(struct afs_time64 t1, struct afs_time64 t2)
{
    return opr_time64_cmp(t1, t2) == 0;
}

/* Return the maximum time representable by a struct afs_time64. */
static_inline struct afs_time64
opr_time64_maxTime(void)
{
    struct afs_time64 out = { OPR_TIME64_MAX_CLUNKS };
    return out;
}

/* Is the given time the maximum representable time? */
static_inline int
opr_time64_isMaxtime(struct afs_time64 in)
{
    return opr_time64_eq(in, opr_time64_maxTime());
}

static_inline struct afs_time64
opr_time64_zero(void)
{
    struct afs_time64 out = { 0 };
    return out;
}

static_inline int
opr_time64_isZero(struct afs_time64 in)
{
    return opr_time64_eq(in, opr_time64_zero());
}

static_inline int
opr_time64_isNegative(struct afs_time64 in)
{
    return opr_time64_lt(in, opr_time64_zero());
}

static_inline int
opr_time64_isPositive(struct afs_time64 in)
{
    return opr_time64_gt(in, opr_time64_zero());
}

/*
 * *out = in + add
 *
 * If the result cannot be represented, return ERANGE.
 */
static_inline int
opr_time64_add_safe(struct afs_time64 in, struct afs_time64 add,
		    struct afs_time64 *out)
{
    if (in.clunks > 0 && add.clunks > 0) {
	if (in.clunks > OPR_TIME64_MAX_CLUNKS - add.clunks) {
	    return ERANGE;
	}
    }
    if (in.clunks < 0 && add.clunks < 0) {
	if (in.clunks < OPR_TIME64_MIN_CLUNKS - add.clunks) {
	    return ERANGE;
	}
    }
    out->clunks = in.clunks + add.clunks;
    return 0;
}

/*
 * Initialize an afs_time64 from the given number of clunks. This should not
 * usually be necessary, except when decoding an afs_time64 from the wire or
 * the net, etc. It is preferred to use this function instead of setting an
 * afs_time64's clunks directly, to make it easier to track who is doing this
 * if needed.
 */
static_inline struct afs_time64
opr_time64_fromClunks(afs_int64 clunks)
{
    struct afs_time64 val;
    val.clunks = clunks;
    return val;
}

/*
 * Initialize an afs_time64 from the given number of seconds. If the result
 * cannot be represented as an afs_time64, return ERANGE.
 */
static_inline int
opr_time64_fromSecs_safe(afs_int64 in, struct afs_time64 *out)
{
    if (in < OPR_TIME64_MIN_SECS || in > OPR_TIME64_MAX_SECS) {
	return ERANGE;
    }
    out->clunks = in * OPR_TIME64_CLUNKS_PER_SEC;
    return 0;
}

/*
 * Same as opr_time64_fromSecs_safe(), but asserts on error. Do NOT use with
 * untrusted data!
 */
static_inline struct afs_time64
opr_time64_fromSecs(afs_int64 in)
{
    struct afs_time64 val;
    opr_Verify(opr_time64_fromSecs_safe(in, &val) == 0);
    return val;
}

/*
 * Same as opr_time64_fromSecs(), but using an unsigned int32. This is okay to
 * use with untrusted data, since all seconds in the 32-bit int range can be
 * represented as an afs_time64.
 */
static_inline struct afs_time64
opr_time64_fromUint32(const afs_uint32 *in)
{
    return opr_time64_fromSecs(*in);
}

static_inline struct afs_time64
opr_time64_fromInt32(const afs_int32 *in)
{
    return opr_time64_fromSecs(*in);
}

static_inline int
opr_time64_fromMicrosecs_safe(afs_int64 in, struct afs_time64 *out)
{
    if (in < OPR_TIME64_MIN_MICROSECS || in > OPR_TIME64_MAX_MICROSECS) {
	return ERANGE;
    }
    out->clunks = in * OPR_TIME64_CLUNKS_PER_US;
    return 0;
}

/*
 * Similar to opr_time64_fromSecs_safe(), but the given time is given in
 * seconds and microseconds.
 *
 * This doesn't take a struct timeval directly for convenience when we're
 * dealing with timeval-like structs that aren't literally struct timeval
 * (e.g., struct rx_clock, or some KERNEL environments).
 */
static_inline int
opr_time64_fromTimeval_safe(afs_int64 sec, afs_int64 microsec,
			    struct afs_time64 *out)
{
    int code;
    struct afs_time64 val;
    struct afs_time64 val_usec;

    code = opr_time64_fromSecs_safe(sec, &val);
    if (code != 0) {
	return code;
    }

    code = opr_time64_fromMicrosecs_safe(microsec, &val_usec);
    if (code != 0) {
	return code;
    }

    return opr_time64_add_safe(val, val_usec, out);
}

/*
 * Get the raw 'clunks' value from the given afs_time64. This should not
 * usually be necessary, but it is preferred to use this over directly
 * referencing the 'clunks' field, to make it easier to track who is using this
 * and make it less likely to accidentally modify the 'clunks' field.
 */
static_inline afs_int64
opr_time64_toClunks(struct afs_time64 in)
{
    return in.clunks;
}

/* 'long long' version of opr_time64_toClunks() for printf() convenience. */
static_inline long long
opr_time64_toClunksLL(struct afs_time64 in)
{
    return opr_time64_toClunks(in);
}

/*
 * Convert the given afs_time64 time into whole seconds. This does not return
 * errors, but is "safer" than opr_time64_toSecs() because the output argument
 * prevents us from accidentally truncating the result in a 32-bit int.
 */
static_inline void
opr_time64_toSecs_safe(struct afs_time64 in, afs_int64 *out)
{
    *out = in.clunks / OPR_TIME64_CLUNKS_PER_SEC;
}

/*
 * More convenient form of opr_time64_toSecs_safe(), when we're sure we're not
 * truncating a value, and using opr_time64_toSecs_safe() is very cumbersome.
 */
static_inline afs_int64
opr_time64_toSecs(struct afs_time64 in)
{
    afs_int64 val;
    opr_time64_toSecs_safe(in, &val);
    return val;
}

/*
 * Same as opr_time64_toSecs_safe(), but converts the time into seconds
 * represented by an afs_uint32. If the result cannot be represented as an
 * afs_uint32, the returned value wraps around.
 *
 * That is: 2^32   -> 0
 *	    2^32+1 -> 1
 *	    -1	   -> 2^32-1
 */
static_inline void
opr_time64_toUint32_wrap(struct afs_time64 in, afs_uint32 *out)
{
    static const afs_int64 limit = MAX_AFS_UINT32 + 1LL;
    afs_int64 secs;
    opr_time64_toSecs_safe(in, &secs);
    *out = (secs % limit + limit) % limit;
}

#if !defined(KERNEL) || defined(UKERNEL) || !defined(AFS_LINUX_ENV)
/*
 * Version of opr_time64_toSecs() that converts to a system time_t
 * specifically. Only use this when you need to actually use a time_t (for
 * example, when dealing with libc functions).
 */
static_inline void
opr_time64_toTimeT(struct afs_time64 in, time_t *out)
{
    opr_StaticAssert(sizeof(time_t) >= sizeof(afs_int64));
    *out = opr_time64_toSecs(in);
}
#endif

#if !defined(KERNEL) || defined(UKERNEL)
/*
 * Similar to ctime(3), but we take an afs_time64, and we trim off the trailing
 * newline of the returned string.
 *
 * Like ctime(3), this returns a pointer to a static buffer, so try not to use
 * this in multithreaded code.
 */
static_inline char *
opr_time64_ctime(struct afs_time64 in)
{
    static char buf[26];
    time_t secs;
    char *str;
    char *nl;

    opr_time64_toTimeT(in, &secs);
    str = ctime(&secs);
    if (str == NULL) {
	snprintf(buf, sizeof(buf), "[%ld]", (long)secs);
	return buf;
    }

    /* Trim off trailing \n. */
    nl = strchr(str, '\n');
    if (nl != NULL) {
	*nl = '\0';
    }

    return str;
}

static_inline int
opr_time64_now_safe(struct afs_time64 *out)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
	/*
	 * Even for _safe(), don't return an error. The only possible error is
	 * maybe EFAULT; if that happens, that's basically a segfault, so act
	 * like a segfault happened and crash.
	 *
	 * Do not call opr_Verify/opr_Assert, since opr_Assert calls this.
	 */
	opr_abort();
	return EIO;
    }
    return opr_time64_fromTimeval_safe(tv.tv_sec, tv.tv_usec, out);
}

#if defined(__GNUC__) && __GNUC__ <= 4
/*
 * gcc 4.8.8 has been seen to erroneously flag the return value from
 * opr_time64_now() as maybe uninitialized. Work around it by always
 * initializing the return value up front for older gcc, without sacrificing
 * compiler uninitialized warnings on platforms or newer gcc.
 */
# define WORKAROUND_WUNINITIALIZED
#endif

static_inline struct afs_time64
opr_time64_now(void)
{
    struct afs_time64 now;
#ifdef WORKAROUND_WUNINITIALIZED
    now.clunks = 0;
#endif

    opr_Verify(opr_time64_now_safe(&now) == 0);
    opr_Assert(now.clunks != 0);
    return now;
}
#endif /* !KERNEL || UKERNEL */

/*
 * Same as opr_time64_add_safe(), but we add whole seconds to 'in', instead of
 * another afs_time64.
 */
static_inline int
opr_time64_addSecs_safe(struct afs_time64 in, afs_int64 add_sec,
			struct afs_time64 *out)
{
    int code;
    struct afs_time64 add;

    code = opr_time64_fromSecs_safe(add_sec, &add);
    if (code != 0) {
	return code;
    }

    return opr_time64_add_safe(in, add, out);
}

#endif /* OPENAFS_OPR_TIME_H */
