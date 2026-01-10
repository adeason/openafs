/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 *
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
 *
 * DFBSD OSI header file. Extends afs_osi.h.
 *
 * afs_osi.h includes this file, which is the only way this file should
 * be included in a source file. This file can redefine macros declared in
 * afs_osi.h.
 */

#ifndef _OSI_MACHDEP_H_
#define _OSI_MACHDEP_H_

#include <opr/time.h>

static_inline void
osi_GetTime(osi_timeval32_t *atv)
{
    struct timeval now;
    microtime(&now);
    atv->tv_sec = now.tv_sec;
    atv->tv_usec = now.tv_usec;
}

static_inline int
opr_time64_now_safe(struct afs_time64 *out)
{
    struct timeval now;
    microtime(&now);
    return opr_time64_fromTimeval_safe(now.tv_sec, now.tv_usec, out);
}

#endif /* _OSI_MACHDEP_H_ */
