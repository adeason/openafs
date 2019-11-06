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

#ifndef OPENAFS_AUTH_AUTHCON_H
#define OPENAFS_AUTH_AUTHCON_H

/*
 * authcon.h - Header for authcon.c-related items that are not public
 * interfaces exported to outside the OpenAFS tree.
 */

#include <afs/cellconfig.h>

enum afsconf_bsso_type {
    AFSCONF_BSSO_DEFAULT = 0,
    AFSCONF_BSSO_VLSERVER = 1,
    AFSCONF_BSSO_BOSSERVER = 2,
};

struct afsconf_bsso_info {
    enum afsconf_bsso_type type;

    struct afsconf_dir *dir;
    void (*logger)(const char *format, ...);

    afs_uint32 host;
    rxgk_getfskey_func getfskey;
    void *getfskey_rock;
    afsUUID *server_uuid;
};

int afsconf_BuildServerSecurityObjects_int(struct afsconf_bsso_info *info,
					   struct rx_securityClass ***classes,
					   afs_int32 *numClasses);

#endif /* OPENAFS_AUTH_AUTHCON_H */
