/*
 * Copyright (c) 2021 Sine Nomine Associates.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <afsconfig.h>
#include <afs/param.h>

#include <roken.h>

#include <afs/cellconfig.h>
#include <afs/cmd.h>
#include <ubik_np.h>

static struct ubikctl_info ubik_suites[] = {
    {
	.cmd_prefix = "pt",
	.db_descr = "ptdb",
	.server_type = AFSCONF_PROTPORT,
    },
    {
	.cmd_prefix = "vl",
	.db_descr = "vldb",
	.server_type = AFSCONF_VLDBPORT,
    },
    {0}
};

int
main(int argc, char *argv[])
{
    int code;
    struct ubikctl_info *suite;

    setprogname(argv[0]);

    initialize_CMD_error_table();
    initialize_U_error_table();

    for (suite = ubik_suites; suite->cmd_prefix != NULL; suite++) {
	code = ubikctl_CreateSyntax(suite);
	if (code != 0) {
	    return code;
	}
    }

    return cmd_Dispatch(argc, argv);
}
