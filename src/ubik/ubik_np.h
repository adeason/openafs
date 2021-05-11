/*
 * Copyright (c) 2020 Sine Nomine Associates. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef OPENAFS_UBIK_UBIK_NP_H
#define OPENAFS_UBIK_UBIK_NP_H

/*
 * ubik_np.h - Header for ubik-related items that are not stable public
 * interfaces exported to outside the OpenAFS tree, but are public to other
 * subsystems in the OpenAFS tree.
 */

#include <ubik.h>

/*** ubik.c ***/

struct ubik_rawinit_opts {
    int r_create;
    int r_rw;
};

int ubik_RawDbase(struct ubik_dbase *dbase);
int ubik_RawTrans(struct ubik_trans *trans);
int ubik_RawHandle(struct ubik_trans *trans, FILE **a_fh);

int ubik_RawInit(char *path, struct ubik_rawinit_opts *ropts,
		 struct ubik_dbase **dbase);
void ubik_RawClose(struct ubik_dbase **a_dbase);
int ubik_RawGetHeader(struct ubik_trans *trans, struct ubik_hdr *hdr);
int ubik_RawGetVersion(struct ubik_trans *trans, struct ubik_version *version);
int ubik_RawSetVersion(struct ubik_trans *trans, struct ubik_version *version);

typedef void (*ubik_writehook_func)(struct ubik_dbase *tdb, afs_int32 fno,
				    void *bp, afs_int32 pos, afs_int32 count);
int ubik_InstallWriteHook(ubik_writehook_func func);

#endif /* OPENAFS_UBIK_UBIK_NP_H */