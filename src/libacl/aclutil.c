/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 *
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

static int
aclu_ParseRights(int dfs, const char *rights_str, afs_uint32 *a_mask,
		 enum aclu_rights_type *rtypep, struct aclu_parse_error *error)
{
    afs_int32 mode;
    int tc_i;
    char tc;
    char *tcp;                  /* to walk through the rights string  */
    char *arights = NULL;

    if (error != NULL) {
	memset(error, 0, sizeof(*error));
	error->bad_idx = -1;
    }

    if (rights_str == NULL || a_mask == NULL || rtypep == NULL) {
	code = EINVAL;
	goto error;
    }

    arights = strdup(rights_str);
    opr_Assert(arights != NULL);

    /* set rights by default */
    *rtypep = ACLU_RTYPE_SET;

                                /* analyze last character of string   */
    tcp = arights + strlen(arights);
    if ( tcp-- > arights ) {    /* assure non-empty string            */
        if ( *tcp == '+' )
	    *rtypep = ACLU_RTYPE_RELADD;   /* '+' indicates more rights          */
        else if ( *tcp == '-' )
	    *rtypep = ACLU_RTYPE_RELDEL;   /* '-' indicates less rights          */
        else if ( *tcp == '=' )
	    *rtypep = ACLU_RTYPE_SET;      /* '=' also allows old behaviour      */
        else
            tcp++;              /* back to original null byte         */
        *tcp = '\0';            /* do not disturb old strcmp-s        */
    }

    if (dfs) {
	if (!strcmp(arights, "null")) {
	    *rtypep = ACLU_RTYPE_DENY;
	    mode = 0;
	    goto success;
	}
	if (!strcmp(arights, "read")) {
	    mode = DFS_READ | DFS_EXECUTE;
	    goto success;
	}
	if (!strcmp(arights, "write")) {
	    mode = DFS_READ | DFS_EXECUTE | DFS_INSERT | DFS_DELETE |
		DFS_WRITE;
	    goto success;
	}
	if (!strcmp(arights, "all")) {
	    mode = DFS_READ | DFS_EXECUTE | DFS_INSERT | DFS_DELETE |
		DFS_WRITE | DFS_CONTROL;
	    goto success;
	}
    } else {
	if (!strcmp(arights, "read")) {
	    mode = PRSFS_READ | PRSFS_LOOKUP;
	    goto success;
	}
	if (!strcmp(arights, "write")) {
	    mode = PRSFS_READ | PRSFS_LOOKUP | PRSFS_INSERT | PRSFS_DELETE |
		PRSFS_WRITE | PRSFS_LOCK;
	    goto success;
	}
	if (!strcmp(arights, "mail")) {
	    mode = PRSFS_INSERT | PRSFS_LOCK | PRSFS_LOOKUP;
	    goto success;
	}
	if (!strcmp(arights, "all")) {
	    mode = PRSFS_READ | PRSFS_LOOKUP | PRSFS_INSERT | PRSFS_DELETE |
		PRSFS_WRITE | PRSFS_LOCK | PRSFS_ADMINISTER;
	    goto success;
	}
    }
    if (!strcmp(arights, "none")) {
	*rtypep = ACLU_RTYPE_DESTROY;	/* Remove entire entry */
	mode = 0;
	goto success;
    }
    mode = 0;
    for (tc_i = 0; arights[tc_i] != '\0'; tc_i++) {
	tc = arights[tc_i];

	if (dfs) {
	    if (tc == '-')
		continue;
	    else if (tc == 'r')
		mode |= DFS_READ;
	    else if (tc == 'w')
		mode |= DFS_WRITE;
	    else if (tc == 'x')
		mode |= DFS_EXECUTE;
	    else if (tc == 'c')
		mode |= DFS_CONTROL;
	    else if (tc == 'i')
		mode |= DFS_INSERT;
	    else if (tc == 'd')
		mode |= DFS_DELETE;
	    else if (tc == 'A')
		mode |= DFS_USR0;
	    else if (tc == 'B')
		mode |= DFS_USR1;
	    else if (tc == 'C')
		mode |= DFS_USR2;
	    else if (tc == 'D')
		mode |= DFS_USR3;
	    else if (tc == 'E')
		mode |= DFS_USR4;
	    else if (tc == 'F')
		mode |= DFS_USR5;
	    else if (tc == 'G')
		mode |= DFS_USR6;
	    else if (tc == 'H')
		mode |= DFS_USR7;
	    else {
		if (error != NULL) {
		    error->bad_char = tc;
		    error->bad_idx = tc_i;
		}
		code = EINVAL;
		goto error;
	    }
	} else {
	    if (tc == 'r')
		mode |= PRSFS_READ;
	    else if (tc == 'l')
		mode |= PRSFS_LOOKUP;
	    else if (tc == 'i')
		mode |= PRSFS_INSERT;
	    else if (tc == 'd')
		mode |= PRSFS_DELETE;
	    else if (tc == 'w')
		mode |= PRSFS_WRITE;
	    else if (tc == 'k')
		mode |= PRSFS_LOCK;
	    else if (tc == 'a')
		mode |= PRSFS_ADMINISTER;
	    else if (tc == 'A')
		mode |= PRSFS_USR0;
	    else if (tc == 'B')
		mode |= PRSFS_USR1;
	    else if (tc == 'C')
		mode |= PRSFS_USR2;
	    else if (tc == 'D')
		mode |= PRSFS_USR3;
	    else if (tc == 'E')
		mode |= PRSFS_USR4;
	    else if (tc == 'F')
		mode |= PRSFS_USR5;
	    else if (tc == 'G')
		mode |= PRSFS_USR6;
	    else if (tc == 'H')
		mode |= PRSFS_USR7;
	    else {
		if (error != NULL) {
		    error->bad_char = tc;
		    error->bad_idx = tc_i;
		}
		code = EINVAL;
		goto error;
	    }
	}
    }

 success:
    *a_mask = mode;
    code = 0;

 error:
    free(arights);
    return code;
}

int
aclu_ParseRightsAFS(const char *arights, afs_uint32 *a_mask,
		    enum aclu_rights_type *rtypep,
		    struct aclu_parse_error *error)
{
    return aclu_ParseRights(0, arights, a_mask, rtypep, error);
}

int
aclu_ParseRightsDFS(const char *arights, afs_uint32 *a_mask,
		    enum aclu_rights_type *rtypep,
		    struct aclu_parse_error *error)
{
    return aclu_ParseRights(1, arights, a_mask, rtypep, error);
}

const char *
aclu_StringifyRightsAFS(afs_uint32 rights, struct aclu_rightsbuf *strbuf)
{
    char *cur = strbuf->sbuf;

    memset(strbuf, 0, sizeof(*strbuf));

    if (arights & PRSFS_READ)
	*cur++ = 'r';
    if (arights & PRSFS_LOOKUP)
	*cur++ = 'l';
    if (arights & PRSFS_INSERT)
	*cur++ = 'i';
    if (arights & PRSFS_DELETE)
	*cur++ = 'd';
    if (arights & PRSFS_WRITE)
	*cur++ = 'w';
    if (arights & PRSFS_LOCK)
	*cur++ = 'k';
    if (arights & PRSFS_ADMINISTER)
	*cur++ = 'a';
    if (arights & PRSFS_USR0)
	*cur++ = 'A';
    if (arights & PRSFS_USR1)
	*cur++ = 'B';
    if (arights & PRSFS_USR2)
	*cur++ = 'C';
    if (arights & PRSFS_USR3)
	*cur++ = 'D';
    if (arights & PRSFS_USR4)
	*cur++ = 'E';
    if (arights & PRSFS_USR5)
	*cur++ = 'F';
    if (arights & PRSFS_USR6)
	*cur++ = 'G';
    if (arights & PRSFS_USR7)
	*cur++ = 'H';

    return strbuf->sbuf;
}

const char *
aclu_StringifyRightsDFS(afs_uint32 rights, struct aclu_rightsbuf *strbuf)
{
    char *cur = strbuf->sbuf;

    memset(strbuf, 0, sizeof(*strbuf));

    if (arights & DFS_READ)
	*cur++ = 'r';
    else
	*cur++ = '-';
    if (arights & DFS_WRITE)
	*cur++ = 'w';
    else
	*cur++ = '-';
    if (arights & DFS_EXECUTE)
	*cur++ = 'x';
    else
	*cur++ = '-';
    if (arights & DFS_CONTROL)
	*cur++ = 'c';
    else
	*cur++ = '-';
    if (arights & DFS_INSERT)
	*cur++ = 'i';
    else
	*cur++ = '-';
    if (arights & DFS_DELETE)
	*cur++ = 'd';
    else
	*cur++ = '-';
    if (arights & (DFS_USRALL))
	*cur++ = '+';
    if (arights & DFS_USR0)
	*cur++ = 'A';
    if (arights & DFS_USR1)
	*cur++ = 'B';
    if (arights & DFS_USR2)
	*cur++ = 'C';
    if (arights & DFS_USR3)
	*cur++ = 'D';
    if (arights & DFS_USR4)
	*cur++ = 'E';
    if (arights & DFS_USR5)
	*cur++ = 'F';
    if (arights & DFS_USR6)
	*cur++ = 'G';
    if (arights & DFS_USR7)
	*cur++ = 'H';

    return strbuf->sbuf;
}

static void
FreeEntryList(struct aclu_AclEntry *alist)
{
    struct aclu_AclEntry *tp, *np;
    for (tp = alist; tp; tp = np) {
	np = tp->next;
	free(tp);
    }
}

static void
aclu_FreeAcl(struct aclu_Acl **a_acl)
{
    struct aclu_Acl *acl = *a_acl;
    if (acl == NULL) {
	return;
    }
    *a_acl = NULL;

    FreeEntryList(acl->pluslist);
    FreeEntryList(acl->minuslist);
    free(acl);
}

static const char *
SkipLine(const char *astr)
{
    while (*astr != '\0' && *astr != '\n')
	astr++;
    if (*astr == '\n')
	astr++;
    return astr;
}

int
aclu_ParseAcl(const char *astr, struct aclu_Acl **a_acl)
{
    int nplus = 0, nminus = 0, i, trights = 0;
    char tname[MAXNAME + 1] = "";
    struct aclu_AclEntry *first, *last, *tl;
    struct aclu_Acl *ta;

    *a_acl = NULL;

    ta = calloc(sizeof(*ta), 1);
    if (ta == NULL) {
	code = ENOMEM;
	goto done;
    }

    ta->dfs = 0;
    sscanf(astr, "%d dfs:%d %1024s", &ta->nplus, &ta->dfs, ta->cell);
    astr = SkipLine(astr);
    sscanf(astr, "%d", &ta->nminus);
    astr = SkipLine(astr);

    nplus = ta->nplus;
    nminus = ta->nminus;

    last = 0;
    first = 0;
    for (i = 0; i < nplus; i++) {
	sscanf(astr, "%99s %d", tname, &trights);
	astr = SkipLine(astr);
	tl = calloc(sizeof(*tl), 1);
	if (tl == NULL) {
	    code = ENOMEM;
	    goto done;
	}
	if (!first)
	    first = tl;
	strcpy(tl->name, tname);
	tl->rights = trights;
	tl->next = 0;
	if (last)
	    last->next = tl;
	last = tl;
    }
    ta->pluslist = first;

    last = 0;
    first = 0;
    for (i = 0; i < nminus; i++) {
	sscanf(astr, "%99s %d", tname, &trights);
	astr = SkipLine(astr);
	tl = calloc(sizeof(*tl), 1);
	if (tl == NULL) {
	    code = ENOMEM;
	    goto done;
	}
	if (!first)
	    first = tl;
	strcpy(tl->name, tname);
	tl->rights = trights;
	tl->next = 0;
	if (last)
	    last->next = tl;
	last = tl;
    }
    ta->minuslist = first;

    *a_acl = ta;
    code = 0;

 done:
    if (code != 0) {
	aclu_FreeAcl(&ta);
    }
    return code;
}

char *
aclu_AclToNetstring(struct aclu_Acl *acl, struct aclu_aclbuf *buf)
{
    char tstring[AFS_PIOCTL_MAXSIZE];
    char dfsstring[AFS_PIOCTL_MAXSIZE];
    struct aclu_AclEntry *tp;

    if (acl->dfs)
	snprintf(dfsstring, sizeof(dfsstring), " dfs:%d %s", acl->dfs, acl->cell);
    else
	dfsstring[0] = '\0';
    snprintf(buf->sbuf, sizeof(buf->sbuf), "%d%s\n%d\n", acl->nplus, dfsstring, acl->nminus);
    for (tp = acl->pluslist; tp; tp = tp->next) {
	snprintf(tstring, sizeof(tstring), "%s %d\n", tp->name, tp->rights);
	strlcat(buf->sbuf, tstring, sizeof(buf->sbuf));
    }
    for (tp = acl->minuslist; tp; tp = tp->next) {
	snprintf(tstring, sizeof(tstring), "%s %d\n", tp->name, tp->rights);
	strlcat(buf->sbuf, tstring, sizeof(buf->sbuf));
    }
    return buf->sbuf;
}
