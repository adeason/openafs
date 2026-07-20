

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

int
main(void)
{
    plan(123);

    test_ParseRights();
    test_StringifyRights();
}
