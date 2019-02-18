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
#define	PRO_INT		3	/* integer */

/* profile specials for booleans */
#define	ON		1	/* turn it on */
#define	OFF		0	/* turn it off */

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
	{"cd", PRO_INT, 0, 0, &ps.decl_com_ind },
	{"ci", PRO_INT, 0, 0, &continuation_indent },
	{"c", PRO_INT, 33, 0, &ps.com_ind },
	{"di", PRO_INT, 16, 0, &ps.decl_indent },
	{"d", PRO_INT, 0, 0, &ps.unindent_displace },
	{"i", PRO_INT, 8, 0, &ps.ind_size },
	{"lc", PRO_INT, 0, 0, &block_comment_max_col },
	{"l", PRO_INT, 78, 0, &max_col },
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

    case PRO_INT:
	if (!isdigit((unsigned char)*param_start)) {
	    errx(1, "%s: ``%s'' requires a parameter", option_source, arg - 1);
	}
	*p->p_obj = atoi(param_start);
	if (*p->p_name == 'i' && *p->p_obj <= 0)
		errx(1, "%s: ``%s must be greater of zero''",
		    option_source, arg - 1);
	break;

    default:
	errx(1, "set_option: internal error: p_type %d", p->p_type);
    }
}
