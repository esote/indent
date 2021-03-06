/*	$OpenBSD: indent.c,v 1.30 2015/11/11 01:12:09 deraadt Exp $	*/

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

#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "indent_globs.h"
#include "indent_codes.h"
#include <ctype.h>
#include <errno.h>
#include <err.h>

int
main(void)
{

    extern int  found_err;	/* flag set in diag() on error */
    int         dec_ind;	/* current indentation for declarations */
    int         di_stack[20];	/* a stack of structure indentation levels */
    int         flushed_nl;	/* used when buffering up comments to remember
				 * that a newline was passed over */
    int         force_nl;	/* when true, code must be broken */
    int         hd_type;	/* used to store type of stmt for if (...),
				 * for (...), etc */
    int 	i;		/* local loop counter */
    int         scase;		/* set to true when we see a case, so we will
				 * know what to do with the following colon */
    int         sp_sw;		/* when true, we are in the expressin of
				 * if(...), while(...), etc. */
    int         squest;		/* when this is positive, we have seen a ?
				 * without the matching : in a <c>?<s>:<s>
				 * construct */
    char 	*t_ptr;		/* used for copying tokens */
    int         tabs_to_var;	/* true if using tabs to indent to var name */
    int         type_code;	/* the type of token, returned by lexi */

    int         last_else = 0;	/* true iff last keyword was an else */

    if (pledge("stdio", NULL) == -1)
	err(1, "pledge");

    /*-----------------------------------------------*\
    |		      INITIALIZATION		      |
    \*-----------------------------------------------*/


    hd_type = 0;
    ps.p_stack[0] = stmt;	/* this is the parser's stack */
    ps.last_nl = true;		/* this is true if the last thing scanned was
				 * a newline */
    ps.last_token = semicolon;
    combuf = malloc(bufsize);
    labbuf = malloc(bufsize);
    codebuf = malloc(bufsize);
    tokenbuf = malloc(bufsize);
    if (combuf == NULL || labbuf == NULL || codebuf == NULL ||
        tokenbuf == NULL)
	    err(1, NULL);
    l_com = combuf + bufsize - 5;
    l_lab = labbuf + bufsize - 5;
    l_code = codebuf + bufsize - 5;
    l_token = tokenbuf + bufsize - 5;
    combuf[0] = codebuf[0] = labbuf[0] = ' ';	/* set up code, label, and
						 * comment buffers */
    combuf[1] = codebuf[1] = labbuf[1] = '\0';
    s_lab = e_lab = labbuf + 1;
    s_code = e_code = codebuf + 1;
    s_com = e_com = combuf + 1;
    s_token = e_token = tokenbuf + 1;

    in_buffer = malloc(10);
    if (in_buffer == NULL)
	    err(1, NULL);
    in_buffer_limit = in_buffer + 8;
    buf_ptr = buf_end = in_buffer;
    line_no = 1;
    had_eof = ps.in_decl = ps.decl_on_line = break_comma = false;
    sp_sw = force_nl = false;
    ps.in_or_st = false;
    ps.bl_line = true;
    dec_ind = 0;
    di_stack[ps.dec_nest = 0] = 0;
    ps.want_blank = ps.in_stmt = ps.ind_stmt = false;

    tabs_to_var = false;

    scase = ps.pcase = false;
    squest = 0;
    sc_end = 0;
    bp_save = 0;
    be_save = 0;

    ps.decl_com_ind = 0;
    ps.com_ind = 33;
    ps.decl_indent = 16;
    ps.unindent_displace = 0;
    ps.ind_size = 8;

    /*--------------------------------------------------*\
    |   		COMMAND LINE SCAN		 |
    \*--------------------------------------------------*/

    if (ps.com_ind <= 1)
	ps.com_ind = 2;		/* dont put normal comments before column 2 */
    if (ps.decl_com_ind <= 0)	/* if not specified by user, set this */
	ps.decl_com_ind = ps.com_ind;
    fill_buffer();	/* get first batch of stuff into input buffer */

    parse(semicolon);
    {
	char *p = buf_ptr;
	int   col = 1;

	while (1) {
	    if (*p == ' ')
		col++;
	    else if (*p == '\t')
		col = ((col - 1) & ~7) + 9;
	    else
		break;
	    p++;
	}
	if (col > ps.ind_size)
	    ps.ind_level = ps.i_l_follow = col / ps.ind_size;
    }

    /*
     * START OF MAIN LOOP
     */

    while (1) {			/* this is the main loop.  it will go until we
				 * reach eof */
	int         is_procname;

	type_code = lexi();	/* lexi reads one token.  The actual
				 * characters read are stored in "token". lexi
				 * returns a code indicating the type of token */
	is_procname = ps.procname[0];

	/*
	 * The following code moves everything following an if (), while (),
	 * else, etc. up to the start of the following stmt to a buffer. This
	 * allows proper handling of both kinds of brace placement.
	 */

	flushed_nl = false;
	while (ps.search_brace) {	/* if we scanned an if(), while(),
					 * etc., we might need to copy stuff
					 * into a buffer we must loop, copying
					 * stuff into save_com, until we find
					 * the start of the stmt which follows
					 * the if, or whatever */
	    switch (type_code) {
	    case newline:
		++line_no;
		flushed_nl = true;
	    case form_feed:
		break;		/* form feeds and newlines found here will be
				 * ignored */

	    case lbrace:	/* this is a brace that starts the compound
				 * stmt */
		if (sc_end == 0) {	/* ignore buffering if a comment wasnt
					 * stored up */
		    ps.search_brace = false;
		    goto check_type;
		}
		save_com[0] = '{';	/* we either want to put the brace
					 * right after the if */
		goto sw_buffer;	/* go to common code to get out of
					 * this loop */
	    case comment:	/* we have a comment, so we must copy it into
				 * the buffer */
		if (!flushed_nl || sc_end != 0) {
		    if (sc_end == 0) {	/* if this is the first comment, we
					 * must set up the buffer */
			save_com[0] = save_com[1] = ' ';
			sc_end = &(save_com[2]);
		    }
		    else {
			*sc_end++ = '\n';	/* add newline between
						 * comments */
			*sc_end++ = ' ';
			--line_no;
		    }
		    *sc_end++ = '/';	/* copy in start of comment */
		    *sc_end++ = '*';

		    for (;;) {	/* loop until we get to the end of the comment */
			*sc_end = *buf_ptr++;
			if (buf_ptr >= buf_end)
			    fill_buffer();

			if (*sc_end++ == '*' && *buf_ptr == '/')
			    break;	/* we are at end of comment */

			if (sc_end >= &(save_com[sc_size])) {	/* check for temp buffer
								 * overflow */
			    diag(1, "Internal buffer overflow - Move big comment from right after if, while, or whatever.");
			    fflush(stdout);
			    exit(1);
			}
		    }
		    *sc_end++ = '/';	/* add ending slash */
		    if (++buf_ptr >= buf_end)	/* get past / in buffer */
			fill_buffer();
		    break;
		}
	    default:		/* it is the start of a normal statment */
		if (flushed_nl)	/* if we flushed a newline, make sure it is
				 * put back */
		    force_nl = true;
		if ((type_code == sp_paren && *token == 'i'
		     && last_else) ||
		    (type_code == sp_nparen && *token == 'e'
		     && e_code != s_code && e_code[-1] == '}'))
			force_nl = false;

		if (sc_end == 0) {	/* ignore buffering if comment wasnt
					 * saved up */
		    ps.search_brace = false;
		    goto check_type;
		}
		if (force_nl) {	/* if we should insert a nl here, put it into
				 * the buffer */
		    force_nl = false;
		    --line_no;	/* this will be re-increased when the nl is
				 * read from the buffer */
		    *sc_end++ = '\n';
		    *sc_end++ = ' ';
		    flushed_nl = false;
		}
		for (t_ptr = token; *t_ptr; ++t_ptr)
		    *sc_end++ = *t_ptr;	/* copy token into temp buffer */
		ps.procname[0] = 0;

	sw_buffer:
		ps.search_brace = false;	/* stop looking for start of
						 * stmt */
		bp_save = buf_ptr;	/* save current input buffer */
		be_save = buf_end;
		buf_ptr = save_com;	/* fix so that subsequent calls to
					 * lexi will take tokens out of
					 * save_com */
		*sc_end++ = ' ';/* add trailing blank, just in case */
		buf_end = sc_end;
		sc_end = 0;
		break;
	    }			/* end of switch */
	    if (type_code != 0)	/* we must make this check, just in case there
				 * was an unexpected EOF */
		type_code = lexi();	/* read another token */
	    is_procname = ps.procname[0];
	}			/* end of while (search_brace) */
	last_else = 0;
check_type:
	if (type_code == 0) {	/* we got eof */
	    if (s_lab != e_lab || s_code != e_code
		    || s_com != e_com)	/* must dump end of line */
		dump_line();
	    if (ps.tos > 1)	/* check for balanced braces */
		diag(1, "Missing braces at end of file.");

	    fflush(stdout);
	    exit(found_err);
	}
	if (
		(type_code != comment) &&
		(type_code != newline) &&
		(type_code != preesc) &&
		(type_code != form_feed)) {
	    if (force_nl &&
		    (type_code != semicolon) &&
		    (type_code != lbrace)) {
		/* we should force a broken line here */
		flushed_nl = false;
		dump_line();
		ps.want_blank = false;	/* dont insert blank at line start */
		force_nl = false;
	    }
	    ps.in_stmt = true;	/* turn on flag which causes an extra level of
				 * indentation. this is turned off by a ; or
				 * '}' */
	    if (s_com != e_com) {	/* the turkey has embedded a comment
					 * in a line. fix it */
		*e_code++ = ' ';
		for (t_ptr = s_com; *t_ptr; ++t_ptr) {
		    CHECK_SIZE_CODE;
		    *e_code++ = *t_ptr;
		}
		*e_code++ = ' ';
		*e_code = '\0';	/* null terminate code sect */
		ps.want_blank = false;
		e_com = s_com;
	    }
	}
	else if (type_code != comment)	/* preserve force_nl thru a comment */
	    force_nl = false;	/* cancel forced newline after newline, form
				 * feed, etc */



	/*-----------------------------------------------------*\
	|	   do switch on type of token scanned		|
	\*-----------------------------------------------------*/
	CHECK_SIZE_CODE;
	switch (type_code) {	/* now, decide what to do with the token */

	case form_feed:	/* found a form feed in line */
	    ps.use_ff = true;	/* a form feed is treated much like a newline */
	    dump_line();
	    ps.want_blank = false;
	    break;

	case newline:
	    if (ps.last_token != comma || ps.p_l_follow > 0
		    || ps.block_init || !break_comma || s_com != e_com) {
		dump_line();
		ps.want_blank = false;
	    }
	    ++line_no;		/* keep track of input line number */
	    break;

	case lparen:		/* got a '(' or '[' */
	    ++ps.p_l_follow;	/* count parens to make Healy happy */
	    if (ps.want_blank && *token != '[' &&
		    (ps.last_token != ident
	      || (ps.its_a_keyword && !ps.sizeof_keyword)))
		*e_code++ = ' ';
	    if (ps.in_decl && !ps.block_init) {
		while ((e_code - s_code) < dec_ind) {
		    CHECK_SIZE_CODE;
		    *e_code++ = ' ';
		}
	    *e_code++ = token[0];
	    } else {
		*e_code++ = token[0];
	    }

	    ps.paren_indents[ps.p_l_follow - 1] = e_code - s_code;
	    ps.want_blank = false;
	    if (ps.in_or_st && *token == '(' && ps.tos <= 2) {
		/*
		 * this is a kluge to make sure that declarations will be
		 * aligned right if proc decl has an explicit type on it, i.e.
		 * "int a(x) {..."
		 */
		parse(semicolon);	/* I said this was a kluge... */
		ps.in_or_st = false;	/* turn off flag for structure decl or
					 * initialization */
	    }
	    if (ps.sizeof_keyword)
		ps.sizeof_mask |= 1 << ps.p_l_follow;
	    break;

	case rparen:		/* got a ')' or ']' */
	    rparen_count--;
	    if (ps.cast_mask & (1 << ps.p_l_follow) & ~ps.sizeof_mask) {
		ps.last_u_d = true;
		ps.cast_mask &= (1 << ps.p_l_follow) - 1;
	    }
	    ps.sizeof_mask &= (1 << ps.p_l_follow) - 1;
	    if (--ps.p_l_follow < 0) {
		ps.p_l_follow = 0;
		diag(0, "Extra %c", *token);
	    }
	    if (e_code == s_code)	/* if the paren starts the line */
		ps.paren_level = ps.p_l_follow;	/* then indent it */

	    *e_code++ = token[0];
	    ps.want_blank = true;

	    if (sp_sw && (ps.p_l_follow == 0)) {	/* check for end of if
							 * (...), or some such */
		sp_sw = false;
		force_nl = true;/* must force newline after if */
		ps.last_u_d = true;	/* inform lexi that a following
					 * operator is unary */
		ps.in_stmt = false;	/* dont use stmt continuation
					 * indentation */

		parse(hd_type);	/* let parser worry about if, or whatever */
	    }
	    ps.search_brace = true;	/* this should insure that constructs
					 * such as main(){...} and int[]{...}
					 * have their braces put in the right
					 * place */
	    break;

	case unary_op:		/* this could be any unary operation */
	    if (ps.want_blank)
		*e_code++ = ' ';
	    {
		char       *res = token;

		if (ps.in_decl && !ps.block_init) {	/* if this is a unary op
							 * in a declaration, we
							 * should indent this
							 * token */
		    for (i = 0; token[i]; ++i);	/* find length of token */
		    while ((e_code - s_code) < (dec_ind - i)) {
			CHECK_SIZE_CODE;
			*e_code++ = ' ';	/* pad it */
		    }
		}
		for (t_ptr = res; *t_ptr; ++t_ptr) {
		    CHECK_SIZE_CODE;
		    *e_code++ = *t_ptr;
		}
	    }
	    ps.want_blank = false;
	    break;

	case binary_op:	/* any binary operation */
	    if (ps.want_blank)
		*e_code++ = ' ';
	    {
		char       *res = token;

		for (t_ptr = res; *t_ptr; ++t_ptr) {
		    CHECK_SIZE_CODE;
		    *e_code++ = *t_ptr;	/* move the operator */
		}
	    }
	    ps.want_blank = true;
	    break;

	case postop:		/* got a trailing ++ or -- */
	    *e_code++ = token[0];
	    *e_code++ = token[1];
	    ps.want_blank = true;
	    break;

	case question:		/* got a ? */
	    squest++;		/* this will be used when a later colon
				 * appears so we can distinguish the
				 * <c>?<n>:<n> construct */
	    if (ps.want_blank)
		*e_code++ = ' ';
	    *e_code++ = '?';
	    ps.want_blank = true;
	    break;

	case casestmt:		/* got word 'case' or 'default' */
	    scase = true;	/* so we can process the later colon properly */
	    goto copy_id;

	case colon:		/* got a ':' */
	    if (squest > 0) {	/* it is part of the <c>?<n>: <n> construct */
		--squest;
		if (ps.want_blank)
		    *e_code++ = ' ';
		*e_code++ = ':';
		ps.want_blank = true;
		break;
	    }
	    if (ps.in_decl) {
		*e_code++ = ':';
		ps.want_blank = false;
		break;
	    }
	    ps.in_stmt = false;	/* seeing a label does not imply we are in a
				 * stmt */
	    for (t_ptr = s_code; *t_ptr; ++t_ptr)
		*e_lab++ = *t_ptr;	/* turn everything so far into a label */
	    e_code = s_code;
	    *e_lab++ = ':';
	    *e_lab++ = ' ';
	    *e_lab = '\0';

	    force_nl = ps.pcase = scase;	/* ps.pcase will be used by
						 * dump_line to decide how to
						 * indent the label. force_nl
						 * will force a case n: to be
						 * on a line by itself */
	    scase = false;
	    ps.want_blank = false;
	    break;

	case semicolon:	/* got a ';' */
	    ps.in_or_st = false;/* we are not in an initialization or
				 * structure declaration */
	    scase = false;	/* these will only need resetting in a error */
	    squest = 0;
	    if (ps.last_token == rparen && rparen_count == 0)
		ps.in_parameter_declaration = 0;
	    ps.cast_mask = 0;
	    ps.sizeof_mask = 0;
	    ps.block_init = 0;
	    ps.block_init_level = 0;
	    ps.just_saw_decl--;

	    if (ps.in_decl && s_code == e_code && !ps.block_init)
		while ((e_code - s_code) < (dec_ind - 1)) {
		    CHECK_SIZE_CODE;
		    *e_code++ = ' ';
		}

	    ps.in_decl = (ps.dec_nest > 0);	/* if we were in a first level
						 * structure declaration, we
						 * arent any more */

	    if ((!sp_sw || hd_type != forstmt) && ps.p_l_follow > 0) {

		/*
		 * This should be true iff there were unbalanced parens in the
		 * stmt.  It is a bit complicated, because the semicolon might
		 * be in a for stmt
		 */
		diag(1, "Unbalanced parens");
		ps.p_l_follow = 0;
		if (sp_sw) {	/* this is a check for a if, while, etc. with
				 * unbalanced parens */
		    sp_sw = false;
		    parse(hd_type);	/* dont lose the if, or whatever */
		}
	    }
	    *e_code++ = ';';
	    ps.want_blank = true;
	    ps.in_stmt = (ps.p_l_follow > 0);	/* we are no longer in the
						 * middle of a stmt */

	    if (!sp_sw) {	/* if not if for (;;) */
		parse(semicolon);	/* let parser know about end of stmt */
		force_nl = true;/* force newline after a end of stmt */
	    }
	    break;

	case lbrace:		/* got a '{' */
	    ps.in_stmt = false;	/* dont indent the {} */
	    if (!ps.block_init)
		force_nl = true;/* force other stuff on same line as '{' onto
				 * new line */
	    else if (ps.block_init_level <= 0)
		ps.block_init_level = 1;
	    else
		ps.block_init_level++;

	    if (s_code != e_code && !ps.block_init) {
		if (ps.in_parameter_declaration && !ps.in_or_st) {
		    ps.i_l_follow = 0;
		    dump_line();
		    ps.want_blank = false;
		}
	    }
	    if (ps.in_parameter_declaration)
		prefix_blankline_requested = 0;

	    if (ps.p_l_follow > 0) {	/* check for preceding unbalanced
					 * parens */
		diag(1, "Unbalanced parens");
		ps.p_l_follow = 0;
		if (sp_sw) {	/* check for unclosed if, for, etc. */
		    sp_sw = false;
		    parse(hd_type);
		    ps.ind_level = ps.i_l_follow;
		}
	    }
	    if (s_code == e_code)
		ps.ind_stmt = false;	/* dont put extra indentation on line
					 * with '{' */
	    if (ps.in_decl && ps.in_or_st) {	/* this is either a structure
						 * declaration or an init */
		di_stack[ps.dec_nest++] = dec_ind;
		/* ?		dec_ind = 0; */
	    }
	    else {
		ps.decl_on_line = false;
		ps.in_parameter_declaration = 0;
	    }
	    dec_ind = 0;
	    parse(lbrace);	/* let parser know about this */
	    if (ps.want_blank)	/* put a blank before '{' if '{' is not at
				 * start of line */
		*e_code++ = ' ';
	    ps.want_blank = false;
	    *e_code++ = '{';
	    ps.just_saw_decl = 0;
	    break;

	case rbrace:		/* got a '}' */
	    if (ps.p_stack[ps.tos] == decl && !ps.block_init)	/* semicolons can be
								 * omitted in
								 * declarations */
		parse(semicolon);
	    if (ps.p_l_follow) {/* check for unclosed if, for, else. */
		diag(1, "Unbalanced parens");
		ps.p_l_follow = 0;
		sp_sw = false;
	    }
	    ps.just_saw_decl = 0;
	    ps.block_init_level--;
	    if (s_code != e_code && !ps.block_init) {	/* '}' must be first on
							 * line */
		dump_line();
	    }
	    *e_code++ = '}';
	    ps.want_blank = true;
	    ps.in_stmt = ps.ind_stmt = false;
	    if (ps.dec_nest > 0) {	/* we are in multi-level structure
					 * declaration */
		dec_ind = di_stack[--ps.dec_nest];
		if (ps.dec_nest == 0 && !ps.in_parameter_declaration)
		    ps.just_saw_decl = 2;
		ps.in_decl = true;
	    }
	    prefix_blankline_requested = 0;
	    parse(rbrace);	/* let parser know about this */
	    ps.search_brace = ps.p_stack[ps.tos] == ifhead
		&& ps.il[ps.tos] >= ps.ind_level;
	    break;

	case swstmt:		/* got keyword "switch" */
	    sp_sw = true;
	    hd_type = swstmt;	/* keep this for when we have seen the
				 * expression */
	    goto copy_id;	/* go move the token into buffer */

	case sp_paren:		/* token is if, while, for */
	    sp_sw = true;	/* the interesting stuff is done after the
				 * expression is scanned */
	    hd_type = (*token == 'i' ? ifstmt :
		       (*token == 'w' ? whilestmt : forstmt));

	    /*
	     * remember the type of header for later use by parser
	     */
	    goto copy_id;	/* copy the token into line */

	case sp_nparen:	/* got else, do */
	    ps.in_stmt = false;
	    if (*token == 'e') {
		if (e_code != s_code && e_code[-1] != '}') {
		    dump_line();/* make sure this starts a line */
		    ps.want_blank = false;
		}
		force_nl = true;/* also, following stuff must go onto new line */
		last_else = 1;
		parse(elselit);
	    }
	    else {
		if (e_code != s_code) {	/* make sure this starts a line */
		    dump_line();
		    ps.want_blank = false;
		}
		force_nl = true;/* also, following stuff must go onto new line */
		last_else = 0;
		parse(dolit);
	    }
	    goto copy_id;	/* move the token into line */

	case decl:		/* we have a declaration type (int, register,
				 * etc.) */
	    parse(decl);	/* let parser worry about indentation */
	    if (ps.last_token == rparen && ps.tos <= 1) {
		ps.in_parameter_declaration = 1;
		if (s_code != e_code) {
		    dump_line();
		    ps.want_blank = 0;
		}
	    }
	    if (ps.in_parameter_declaration && ps.dec_nest == 0) {
		ps.ind_level = ps.i_l_follow = 1;
		ps.ind_stmt = 0;
	    }
	    ps.in_or_st = true;	/* this might be a structure or initialization
				 * declaration */
	    ps.in_decl = ps.decl_on_line = true;
	    if ( /* !ps.in_or_st && */ ps.dec_nest <= 0)
		ps.just_saw_decl = 2;
	    prefix_blankline_requested = 0;
	    for (i = 0; token[i++];);	/* get length of token */

	    /*
	     * dec_ind = e_code - s_code + (ps.decl_indent>i ? ps.decl_indent
	     * : i);
	     */
	    dec_ind = ps.decl_indent > 0 ? ps.decl_indent : i;
	    tabs_to_var = ps.decl_indent > 0;
	    goto copy_id;

	case ident:		/* got an identifier or constant */
	    if (ps.in_decl) {	/* if we are in a declaration, we must indent
				 * identifier */
		if (ps.want_blank)
		    *e_code++ = ' ';
		ps.want_blank = false;
		if (is_procname == 0) {
		    if (!ps.block_init) {
			int cur_dec_ind;
			int pos, startpos;

			/*
			 * in order to get the tab math right for
			 * indentations that are not multiples of 8 we
			 * need to modify both startpos and dec_ind
			 * (cur_dec_ind) here by eight minus the
			 * remainder of the current starting column
			 * divided by eight. This seems to be a
			 * properly working fix
			 */
			startpos = e_code - s_code;
			cur_dec_ind = dec_ind;
			pos = startpos;
			if ((ps.ind_level * ps.ind_size) % 8 != 0) {
			    pos += (ps.ind_level * ps.ind_size) % 8;
			    cur_dec_ind += (ps.ind_level * ps.ind_size) % 8;
			}

			if (tabs_to_var) {
			    while ((pos & ~7) + 8 <= cur_dec_ind) {
				CHECK_SIZE_CODE;
				*e_code++ = '\t';
				pos = (pos & ~7) + 8;
			    }
			}
			while (pos < cur_dec_ind) {
			    CHECK_SIZE_CODE;
			    *e_code++ = ' ';
			    pos++;
			}
			if (ps.want_blank && e_code - s_code == startpos)
			    *e_code++ = ' ';
			ps.want_blank = false;
		    }
		}
		else {
		    if (dec_ind && s_code != e_code)
			dump_line();
		    dec_ind = 0;
		    ps.want_blank = false;
		}
	    }
	    else if (sp_sw && ps.p_l_follow == 0) {
		sp_sw = false;
		force_nl = true;
		ps.last_u_d = true;
		ps.in_stmt = false;
		parse(hd_type);
	    }
    copy_id:
	    if (ps.want_blank)
		*e_code++ = ' ';

	    for (t_ptr = token; *t_ptr; ++t_ptr) {
		CHECK_SIZE_CODE;
		*e_code++ = *t_ptr;
	    }

	    ps.want_blank = true;
	    break;

	case period:		/* treat a period kind of like a binary
				 * operation */
	    *e_code++ = '.';	/* move the period into line */
	    ps.want_blank = false;	/* dont put a blank after a period */
	    break;

	case comma:
	    ps.want_blank = (s_code != e_code);	/* only put blank after comma
						 * if comma does not start the
						 * line */
	    if (ps.in_decl && is_procname == 0 && !ps.block_init)
		while ((e_code - s_code) < (dec_ind - 1)) {
		    CHECK_SIZE_CODE;
		    *e_code++ = ' ';
		}

	    *e_code++ = ',';
	    if (ps.p_l_follow == 0) {
		if (ps.block_init_level <= 0)
		    ps.block_init = 0;
		if (break_comma && (compute_code_target() + (e_code - s_code) > max_col - 8))
		    force_nl = true;
	    }
	    break;

	case preesc:		/* got the character '#' */
	    if ((s_com != e_com) ||
		    (s_lab != e_lab) ||
		    (s_code != e_code))
		dump_line();
	    *e_lab++ = '#';	/* move whole line to 'label' buffer */
	    {
		int         in_comment = 0;
		int         com_start = 0;
		char        quote = 0;
		int         com_end = 0;

		while (*buf_ptr == ' ' || *buf_ptr == '\t') {
		    buf_ptr++;
		    if (buf_ptr >= buf_end)
			fill_buffer();
		}
		while (*buf_ptr != '\n' || (in_comment && !had_eof)) {
		    CHECK_SIZE_LAB;
		    *e_lab = *buf_ptr++;
		    if (buf_ptr >= buf_end)
			fill_buffer();
		    switch (*e_lab++) {
		    case BACKSLASH:
			if (!in_comment) {
			    *e_lab++ = *buf_ptr++;
			    if (buf_ptr >= buf_end)
				fill_buffer();
			}
			break;
		    case '/':
			if (*buf_ptr == '*' && !in_comment && !quote) {
			    in_comment = 1;
			    *e_lab++ = *buf_ptr++;
			    com_start = e_lab - s_lab - 2;
			}
			break;
		    case '"':
			if (quote == '"')
			    quote = 0;
			break;
		    case '\'':
			if (quote == '\'')
			    quote = 0;
			break;
		    case '*':
			if (*buf_ptr == '/' && in_comment) {
			    in_comment = 0;
			    *e_lab++ = *buf_ptr++;
			    com_end = e_lab - s_lab;
			}
			break;
		    }
		}

		while (e_lab > s_lab && (e_lab[-1] == ' ' || e_lab[-1] == '\t'))
		    e_lab--;
		if (e_lab - s_lab == com_end && bp_save == 0) {	/* comment on
								 * preprocessor line */
		    if (sc_end == 0)	/* if this is the first comment, we
					 * must set up the buffer */
			sc_end = &(save_com[0]);
		    else {
			*sc_end++ = '\n';	/* add newline between
						 * comments */
			*sc_end++ = ' ';
			--line_no;
		    }
		    bcopy(s_lab + com_start, sc_end, com_end - com_start);
		    sc_end += com_end - com_start;
		    if (sc_end >= &save_com[sc_size])
			abort();
		    e_lab = s_lab + com_start;
		    while (e_lab > s_lab && (e_lab[-1] == ' ' || e_lab[-1] == '\t'))
			e_lab--;
		    bp_save = buf_ptr;	/* save current input buffer */
		    be_save = buf_end;
		    buf_ptr = save_com;	/* fix so that subsequent calls to
					 * lexi will take tokens out of
					 * save_com */
		    *sc_end++ = ' ';	/* add trailing blank, just in case */
		    buf_end = sc_end;
		    sc_end = 0;
		}
		*e_lab = '\0';	/* null terminate line */
		ps.pcase = false;
	    }

	    if (strncmp(s_lab, "#if", 3) == 0) {
		if (ifdef_level < sizeof state_stack / sizeof state_stack[0]) {
		    match_state[ifdef_level].tos = -1;
		    state_stack[ifdef_level++] = ps;
		}
		else
		    diag(1, "#if stack overflow");
	    }
	    else if (strncmp(s_lab, "#else", 5) == 0)
		if (ifdef_level <= 0)
		    diag(1, "Unmatched #else");
		else {
		    match_state[ifdef_level - 1] = ps;
		    ps = state_stack[ifdef_level - 1];
		}
	    else if (strncmp(s_lab, "#endif", 6) == 0) {
		if (ifdef_level <= 0)
		    diag(1, "Unmatched #endif");
		else {
		    ifdef_level--;
		}
	    }
	    break;		/* subsequent processing of the newline
				 * character will cause the line to be printed */

	case comment:		/* we have gotten a comment this is a biggie */
	    if (flushed_nl) {	/* we should force a broken line here */
		flushed_nl = false;
		dump_line();
		ps.want_blank = false;	/* dont insert blank at line start */
		force_nl = false;
	    }
	    pr_comment();
	    break;
	}			/* end of big switch stmt */

	*e_code = '\0';		/* make sure code section is null terminated */
	if (type_code != comment && type_code != newline && type_code != preesc)
	    ps.last_token = type_code;
    }				/* end of main while (1) loop */
}
