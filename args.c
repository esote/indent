/*	$OpenBSD: args.c,v 1.18 2014/05/20 01:25:23 guenther Exp $	*/

/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.
 * Copyright (c) 1976 Board of Trustees of the University of Illinois.
 * Copyright (c) 1985 Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Argument scanning and profile reading code.  Default parameters are set
 * here as well.
 */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "indent_globs.h"
#include <err.h>

/* profile types */
#define	PRO_SPECIAL	1	/* special case */
#define	PRO_BOOL	2	/* boolean */
#define	PRO_INT		3	/* integer */
#define	PRO_FONT	4	/* troff font */

/* profile specials for booleans */
#define	ON		1	/* turn it on */
#define	OFF		0	/* turn it off */

/* profile specials for specials */
#define	IGN		1	/* ignore it */
#define	CLI		2	/* case label indent (float) */
#define	STDIN		3	/* use stdin */
#define	KEY		4	/* type (keyword) */

char *option_source = "?";

/*
 * N.B.: because of the way the table here is scanned, options whose names are
 * substrings of other options must occur later; that is, with -lp vs -l, -lp
 * must be first.  Also, while (most) booleans occur more than once, the last
 * default value is the one actually assigned.
 */
struct pro {
    char       *p_name;		/* name, eg -bl, -cli */
    int         p_type;		/* type (int, bool, special) */
    int         p_default;	/* the default value (if int) */
    int         p_special;	/* depends on type */
    int        *p_obj;		/* the associated variable */
}           pro[] = {
	{"bc", PRO_BOOL, true, OFF, &ps.leave_comma },
	{"cd", PRO_INT, 0, 0, &ps.decl_com_ind },
	{"ci", PRO_INT, 0, 0, &continuation_indent },
	{"cli", PRO_SPECIAL, 0, CLI, 0 },
	{"c", PRO_INT, 33, 0, &ps.com_ind },
	{"di", PRO_INT, 16, 0, &ps.decl_indent },
	{"d", PRO_INT, 0, 0, &ps.unindent_displace },
	{"ei", PRO_BOOL, true, ON, &ps.else_if },
	{"fbc", PRO_FONT, 0, 0, (int *) &blkcomf },
	{"fbx", PRO_FONT, 0, 0, (int *) &boxcomf },
	{"fb", PRO_FONT, 0, 0, (int *) &bodyf },
	{"fc1", PRO_BOOL, true, ON, &format_col1_comments },
	{"fc", PRO_FONT, 0, 0, (int *) &scomf },
	{"fk", PRO_FONT, 0, 0, (int *) &keywordf },
	{"fs", PRO_FONT, 0, 0, (int *) &stringf },
	{"ip", PRO_BOOL, true, ON, &ps.indent_parameters },
	{"i", PRO_INT, 8, 0, &ps.ind_size },
	{"lc", PRO_INT, 0, 0, &block_comment_max_col },
	{"lp", PRO_BOOL, true, ON, &lineup_to_parens },
	{"l", PRO_INT, 78, 0, &max_col },
	{"nbc", PRO_BOOL, true, ON, &ps.leave_comma },
	{"nei", PRO_BOOL, true, OFF, &ps.else_if },
	{"nfc1", PRO_BOOL, true, OFF, &format_col1_comments },
	{"nip", PRO_BOOL, true, OFF, &ps.indent_parameters },
	{"nlp", PRO_BOOL, true, OFF, &lineup_to_parens },
	{"npcs", PRO_BOOL, false, OFF, &proc_calls_space },
	{"npro", PRO_SPECIAL, 0, IGN, 0 },
	{"npsl", PRO_BOOL, true, OFF, &procnames_start_line },
	{"nps", PRO_BOOL, false, OFF, &pointer_as_binop },
	{"nsc", PRO_BOOL, true, OFF, &star_comment_cont },
	{"nsob", PRO_BOOL, false, OFF, &swallow_optional_blanklines },
	{"nut", PRO_BOOL, true, OFF, &use_tabs},
	{"pcs", PRO_BOOL, false, ON, &proc_calls_space },
	{"psl", PRO_BOOL, true, ON, &procnames_start_line },
	{"ps", PRO_BOOL, false, ON, &pointer_as_binop },
	{"sc", PRO_BOOL, true, ON, &star_comment_cont },
	{"sob", PRO_BOOL, false, ON, &swallow_optional_blanklines },
	{"st", PRO_SPECIAL, 0, STDIN, 0 },
	{"troff", PRO_BOOL, false, ON, &troff },
	{"ut", PRO_BOOL, true, ON, &use_tabs},
	/* whew! */
	{ 0, 0, 0, 0, 0 }
};

void scan_profile(FILE *);
void set_option(char *);

void
scan_profile(FILE *f)
{
    int i;
    char *p;
    char        buf[BUFSIZ];

    while (1) {
	for (p = buf;
	    (i = getc(f)) != EOF && (*p = i) > ' ' && p + 1 - buf < BUFSIZ;
	    ++p)
		;
	if (p != buf) {
	    *p = 0;
	    set_option(buf);
	}
	else if (i == EOF)
	    return;
    }
}

char       *param_start;

int
eqin(char *s1, char *s2)
{
    while (*s1) {
	if (*s1++ != *s2++)
	    return (false);
    }
    param_start = s2;
    return (true);
}

/*
 * Set the defaults.
 */
void
set_defaults(void)
{
    struct pro *p;

    /*
     * Because ps.case_indent is a float, we can't initialize it from the
     * table:
     */
    ps.case_indent = 0.0;	/* -cli0.0 */
    for (p = pro; p->p_name; p++)
	if (p->p_type != PRO_SPECIAL && p->p_type != PRO_FONT)
	    *p->p_obj = p->p_default;
}

void
set_option(char *arg)
{
    struct pro *p;

    arg++;			/* ignore leading "-" */
    for (p = pro; p->p_name; p++)
	if (*p->p_name == *arg && eqin(p->p_name, arg))
	    goto found;
    errx(1, "%s: unknown parameter \"%s\"", option_source, arg - 1);
found:
    switch (p->p_type) {

    case PRO_SPECIAL:
	switch (p->p_special) {

	case IGN:
	    break;

	case CLI:
	    if (*param_start == 0)
		goto need_param;
	    ps.case_indent = atof(param_start);
	    break;

	case STDIN:
	    if (input == 0)
		input = stdin;
	    if (output == 0)
		output = stdout;
	    break;

	case KEY:
	    if (*param_start == 0)
		goto need_param;
	    {
		char *str;
		if ((str = strdup(param_start)) == NULL)
			err(1, NULL);
		addkey(str, 4);
	    }
	    break;

	default:
	    errx(1, "set_option: internal error: p_special %d", p->p_special);
	}
	break;

    case PRO_BOOL:
	if (p->p_special == OFF)
	    *p->p_obj = false;
	else
	    *p->p_obj = true;
	break;

    case PRO_INT:
	if (!isdigit((unsigned char)*param_start)) {
    need_param:
	    errx(1, "%s: ``%s'' requires a parameter", option_source, arg - 1);
	}
	*p->p_obj = atoi(param_start);
	if (*p->p_name == 'i' && *p->p_obj <= 0)
		errx(1, "%s: ``%s must be greater of zero''",
		    option_source, arg - 1);
	break;

    case PRO_FONT:
	parsefont((struct fstate *) p->p_obj, param_start);
	break;

    default:
	errx(1, "set_option: internal error: p_type %d", p->p_type);
    }
}
