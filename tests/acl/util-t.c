

/* For convenience */
#define ACL_r PRSFS_READ
#define ACL_w PRSFS_WRITE
#define ACL_i PRSFS_INSERT
#define ACL_l PRSFS_LOOKUP
#define ACL_d PRSFS_DELETE
#define ACL_k PRSFS_LOCK
#define ACL_a PRSFS_ADMINISTER

static void
test_ParseRights(void)
{
    int tc_i;
    struct {
	const char *str;
	int code;
	afs_uint32 mask;
	enum aclu_rights_rtype rtype;
	char bad_char;
	int bad_idx;

    } *tc, test_cases[] = {
	/* str code	      mask	     rtype */
	{ "rl",   0, ACL_r | ACL_l, ACLU_RTYPE_SET },
	/* or perhaps: */
	{ "rl",	  0,	      0x09, ACLU_RTYPE_SET },
	{ "rlia", 0,	      0x4D, ACLU_RTYPE_SET },

	{ "read", 0,	      0x09, ACLU_RTYPE_SET },

	{ "rl+",  0,	      0x09, ACLU_RTYPE_RELADD },
	{ "rl-",  0,	      0x09, ACLU_RTYPE_RELDEL },

	{ "rlbogus", EINVAL, 0, 0, 'b', 2 },
	{ NULL, EINVAL, 0, 0, 0, -1 },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	afs_uint32 mask = 0;
	enum aclu_rights_rtype rtype = 0;
	struct aclu_parse_error error;

	memset(&error, 0x0f, sizeof(error));

	is_int(aclu_ParseRightsAFS(tc->str, &mask, &rtype, &error), tc->code,
	       "aclu_ParseRightsAFS(%s) == %d",
	       tc->str ? tc->str : "(null)",
	       tc->code);

	if (tc->code == 0) {
	    is_hex(mask, tc->mask, "... mask matches");
	    is_hex(rtype, tc->type, "... rtype matches");

	} else {
	    is_hex(error.bad_char, tc->bad_char, "... bad_char matches");
	    is_int(error.bad_idx, tc->bad_idx, "... bad_idx matches");
	}
    }
}

static void
test_StringifyRights(void)
{
    int tc_i;
    struct {
	afs_uint32 rights;
	const char *str;

    } *tc, test_cases[] = {
	{ 0x9, "rl" },
	{ 0xffffffff, "rlidwkaABCDEFGH" },
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct aclu_rightsbuf buf;

	is_string(aclu_ParseRightsAFS(tc->rights, &buf), tc->str,
		  "aclu_ParseRightsAFS(0x%x) == %s",
		  tc->rights, tc->str);
}

struct test_aclu_AclEntry {
    char *name;
    afs_uint32 rights;
};
struct test_aclu_Acl {
    int nplus;
    int nminus;
    struct test_aclu_AclEntry pluslist[20];
    struct test_aclu_AclEntry minuslist[20];

    int dfs;
    char *cell;
};

static int
check_AclEntry(const char *list, int idx, struct aclu_AclEntry *got, struct test_aclu_AclEntry *exp)
{
    if (strcmp(got->name, exp->name) != 0) {
	diag(" left AclEntry %s[%d] name: %s", got->name);
	diag("right AclEntry %s[%d] name: %s", exp->name);
	return 0;
    }

    if (got->rights != exp->rights) {
	diag(" left AclEntry %s[%d] rights: 0x%x", got->rights);
	diag("right AclEntry %s[%d] rights: 0x%x", exp->rights);
	return 0;
    }

    return 1;
}

static int
is_acl_v(struct aclu_Acl *got, struct test_aclu_Acl *exp, const char *fmt,
	 va_list ap)
{
    int success;
    const char *exp_cell;

    opr_Assert(exp != NULL);

    if (got == NULL) {
	diag(" left: NULL");
	diag("right: not NULL");
	goto fail;
    }

    if (got->nplus != exp->nplus) {
	diag(" left nplus: %d", got->nplus);
	diag("right nplus: %d", exp->nplus);
	goto fail
    }

    if (got->nminus != exp->nminus) {
	diag(" left nminus: %d", got->nminus);
	diag("right nminus: %d", exp->nminus);
	goto fail
    }

    for (entry_i = 0; entry_i < got->nplus; entry_i++) {
	success = check_AclEntry("pluslist", entry_i,
				 got->pluslist[entry_i],
				 exp->pluslist[entry_i]);
	if (!success) {
	    goto fail;
	}
    }

    for (entry_i = 0; entry_i < got->nplus; entry_i++) {
	success = check_AclEntry("minuslist", entry_i,
				 got->minuslist[entry_i],
				 exp->minuslist[entry_i]);
	if (!success) {
	    goto fail;
	}
    }

    if (got->dfs != exp->dfs) {
	diag(" left dfs: %d", got->dfs);
	diag("right dfs: %d", exp->dfs);
	goto fail;
    }

    exp_cell = exp->cell;
    if (exp_cell == NULL) {
	exp_cell = "";
    }
    if (strcmp(got->cell, exp_cell) != 0) {
	diag(" left cell: %s", got->cell);
	diag("right cell: %s", exp->cell);
	goto fail;
    }

    success = 1;

 done:
    okv(success, fmt, ap);

    return success;

 fail:
    success = 0;
    goto done;
}

static int
is_acl(struct aclu_Acl *got, struct test_aclu_Acl *exp, const char *fmt, ...)
{
    int success;
    va_list args;

    va_start(args, fmt);
    success = is_acl_v(got, exp, fmt, args);
    va_end(args);

    return success;
}

static void
test_ParseAcl(void)
{
    int tc_i;
    struct {
	const char *str;
	int code;
	struct test_aclu_Acl acl;

    } *tc, test_cases[] = {
	{ "2\n0\nsystem:administrators 127\nreaders 9\n", 0,
	    {	2, 0,
		{   { NULL, "system:administators", 127 },
		    { NULL, "readers", 9 },
		},
	    },
	},

	{ "1\n1\nsystem:administrators 127\nbadusers 9\n", 0,
	    {	1, 1,
		{{ NULL, "system:administators", 127 }},
		{{ NULL, "badusers", 9 }},
	    },
	},
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct aclu_acl acl;

	memset(&acl, 0, sizeof(acl));

	is_code(aclu_ParseAcl(tc->str, &acl), tc->code,
		"[%d] aclu_ParseAcl() == %d",
		tc_i, tc->code);
	if (tc->code == 0) {
	    is_acl(acl, &tc->acl, "... acl matches");
	}
    }
}

static void
test_AclToNetstring(void)
{
    int tc_i;

    struct aclu_AclEntry readers = { NULL, "readers", 9 };
    struct aclu_AclEntry pluslist_2 = { &readers, "system:administrators", 127 };

    struct aclu_AclEntry pluslist_1 = {	    NULL, "system:administrators", 127 };
    struct aclu_AclEntry minuslist_1 = { NULL, "baduser", 9 };

    struct {
	struct aclu_Acl acl;
	const char *netstr;

    } *tc, test_cases[] = {
	{
	    { 0, "", 2, 0, &pluslist_2, NULL },
	    "2\n0\nsystem:administrators 127\nreaders 9\n",
	},
	{
	    { 0, "", 1, 1, &pluslist_1, &minuslist_1 },
	    "1\n1\nsystem:administrators 127\nbaduser 9\n",
	},
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct aclu_aclbuf buf;

	memset(&buf, 0, sizeof(buf));

	is_string(aclu_AclToNetstring(&tc->acl, &buf), tc->netstr,
		  "[%d] aclu_AclToNetstring() matches");
    }
}

static int
TestFilterBadName(struct aclu_Acl *acl, int neg, const char *name,
		  afs_uint32 rights, void *rock, int *a_remove)
{
    int *a_changed = rock;

    for (nm = aname; (tc = *nm); nm++) {
	/* all must be '-' or digit to be bad */
	if (tc != '-' && (tc < '0' || tc > '9'))
	    return 0;
    }

    /* Assume all numerical names are bad */
    *a_remove = 1;
    *a_changed += 1;

    return 0;
}

static void
test_CleanAcl(void)
{
    int tc_i;

    struct {
	const char *acl;
	int code;
	int n_changes;
	const char *result;

    } *tc, test_cases[] = {
	{
	    "2\n0\nnsystem:administrators 127\nreaders 9\n",
	    0, 0,
	    "2\n0\nnsystem:administrators 127\nreaders 9\n",
	},
	{
	    "2\n0\nnsystem:administrators 127\n1234 9\n",
	    0, 1,
	    "1\n0\nnsystem:administrators 127\n",
	},
	{
	    "2\n0\nnsystem:administrators 127\n-1234 9\n",
	    0, 1,
	    "1\n0\nnsystem:administrators 127\n",
	},
	{
	    "2\n0\n567890 127\n-1234 9\n",
	    0, 2,
	    "0\n0\n",
	},
	{
	    "0\n2\nnsystem:administrators 127\nreaders 9\n",
	    0, 0,
	    "0\n2\nnsystem:administrators 127\nreaders 9\n",
	},
	{
	    "0\n2\nnsystem:administrators 127\n1234 9\n",
	    0, 1,
	    "0\n1\nnsystem:administrators 127\n",
	},
	{
	    "0\n2\nnsystem:administrators 127\n-1234 9\n",
	    0, 1,
	    "0\n1\nnsystem:administrators 127\n",
	},
	{
	    "0\n2\n567890 127\n-1234 9\n",
	    0, 2,
	    "0\n0\n",
	},
    };

    for (afstest_Scan(test_cases, tc, tc_i)) {
	struct aclu_acl *acl;
	struct aclu_aclbuf buf;
	int changed = 0;
	int code;

	memset(&acl, 0, sizeof(acl));
	memset(&buf, 0, sizeof(buf));

	code = aclu_ParseAcl(tc->acl, &acl);
	opr_Assert(code == 0);

	code = aclu_FilterAcl(acl, TestFilterBadName, &changed);
	is_int(code, tc->code,
	       "[%d] aclu_FilterAcl() == %d",
	       tc_i, tc->code);

	is_int(changed, tc->n_changed,
	       "... changed == %d", tc->n_changed);

	is_string(aclu_AclToNetstring(acl, &buf), tc->result,
		  "... filtered acl matches");

	aclu_FreeAcl(&acl);
    }
}

int
main(void)
{
    plan(123);

    test_ParseRights();
    test_StringifyRights();
    test_ParseAcl();
    test_AclToNetstring();
    test_CleanAcl();
}
