/*
 * Copyright (c) 2020 Sine Nomine Associates. All rights reserved.
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

/*!
 * Common file-related functions for test programs
 */

#include <afsconfig.h>
#include <afs/param.h>
#include <roken.h>

#include <afs/opr.h>
#include <afs/afsutil.h>

#include <tests/tap/basic.h>

#include "common.h"

char *
afstest_mkdtemp(void)
{
    char *template;

    template = afstest_asprintf("%s/afs_XXXXXX", gettmpdir());

#if defined(HAVE_MKDTEMP)
    return mkdtemp(template);
#else
    /*
     * Note that using the following is not a robust replacement
     * for mkdtemp as there is a possible race condition between
     * creating the name and creating the directory itself.  The
     * use of this routine is limited to running tests.
     */
    if (mktemp(template) == NULL)
	return NULL;
    if (mkdir(template, 0700))
	return NULL;
    return template;
#endif
}

void
afstest_rmdtemp(char *path)
{
    int code;
    struct stat st;

    /* Sanity check, only zap directories that look like ours */
    opr_Assert(strstr(path, "afs_") != NULL);
    if (getenv("MAKECHECK") == NULL) {
	/* Don't delete tmp dirs if we're not running under 'make check'. */
	return;
    }
    code = lstat(path, &st);
    if (code != 0) {
	/* Given path doesn't exist (or we can't access it?) */
	return;
    }
    if (!S_ISDIR(st.st_mode)) {
	/* Path isn't a dir; that's weird. Bail out to be on the safe side. */
	return;
    }
    afstest_systemlp("rm", "-rf", path, (char*)NULL);
}

static char *
path_from_tdir(char *env_var, char *filename)
{
    char *tdir;

    /* C_TAP_SOURCE/C_TAP_BUILD in the env points to 'tests/' in the
     * srcdir/objdir. */

    tdir = getenv(env_var);
    if (tdir == NULL) {
	/*
	 * If C_TAP_SOURCE/C_TAP_BUILD isn't set, we assume we're running from
	 * the same cwd as one of the test programs (e.g. 'tests/foo/'). So to
	 * get to 'tests/', just go up one level.
	 */
	tdir = "..";
    }

    /*
     * The given 'filename' is specified relative to the top srcdir/objdir.
     * Since 'tdir' points to 'tests/', go up one level before following
     * 'filename'.
     */
    return afstest_asprintf("%s/../%s", tdir, filename);
}

char *
afstest_src_path(char *path)
{
    return path_from_tdir("C_TAP_SOURCE", path);
}

char *
afstest_obj_path(char *path)
{
    return path_from_tdir("C_TAP_BUILD", path);
}

int
afstest_file_contains(char *path, char *target)
{
    FILE *fh;
    char line[128];

    fh = fopen(path, "r");
    if (fh == NULL) {
	diag("%s: Failed to open %s", __func__, path);
	goto failure;
    }

    while (fgets(line, sizeof(line), fh) != NULL) {
	if (strstr(line, target) != NULL) {
	    goto success;
	}
    }

 failure:
    if (fh != NULL) {
	fclose(fh);
    }
    return 0;

 success:
    if (fh != NULL) {
	fclose(fh);
    }
    return 1;
}

int
afstest_cp(char *src_path, char *dest_path)
{
    int code;

    /* Instead of needing to reimplement the logic for copying a file, just run
     * 'cp'. */
    code = afstest_systemlp("cp", src_path, dest_path, (char*)NULL);
    if (code != 0) {
	return EIO;
    }
    return 0;
}
