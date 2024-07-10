/*
 *	Z80/8080-Macro-Assembler
 *	Copyright (C) 1987-2022 by Udo Munk
 *	Copyright (C) 2022-2024 by Thomas Eberhardt
 */

/*
 *	this module contains all global variables
 */

#include <stdio.h>

#include "z80a.h"
#include "z80aglb.h"
#include "z80aout.h"

char **infiles,			/* source filenames */
     *srcfn,			/* filename of current processed source file */
     *objfn,			/* object filename */
     *lstfn,			/* listing filename */
     line[MAXLINE + 2],		/* buffer for one line of source */
     label[MAXLINE + 1],	/* buffer for label */
     opcode[MAXLINE + 1],	/* buffer for opcode */
     operand[MAXLINE + 1],	/* buffer for working with operand */
     title[MAXLINE + 1];	/* buffer for title of source */

BYTE ops[OPCARRAY];		/* buffer for generated object code */

WORD rpc,			/* real program counter */
     pc,			/* logical program counter, normally equal */
				/* to rpc, except when inside a .PHASE block */
     hexlen = MAXHEX,		/* HEX record length */
     carylen = CARYLEN;		/* C array bytes per line */

int  list_flag,			/* flag for option -l */
     nodate_flag,		/* flag for option -T */
     sym_flag,			/* flag for option -s */
     undoc_flag,		/* flag for option -u */
     ver_flag,			/* flag for option -v */
     nofill_flag,		/* flag for option -x */
     upcase_flag,		/* flag for option -U */
     mac_list_flag,		/* flag for option -m */
     i8080_flag,		/* flag for option -8 */
     nfiles,			/* number of input files */
     radix = 10,		/* current radix, set to 10 at start of pass */
     phs_flag,			/* flag for being inside a .PHASE block */
     pass,			/* processed pass */
     iflevel,			/* IF nesting level */
     act_iflevel,		/* active IF nesting level */
     act_elselevel,		/* active ELSE nesting level */
     gencode = TRUE,		/* flag for conditional code */
     nofalselist,		/* flag for false conditional listing */
     mac_def_nest,		/* macro definition nesting level */
     mac_exp_nest,		/* macro expansion nesting level */
     mac_symmax,		/* max. macro symbol length encountered */
     errors,			/* error counter */
     errnum,			/* error number in pass 2 */
     load_flag,			/* flag for load_addr already set */
     obj_fmt = OBJ_HEX,		/* format of object file (default Intel HEX) */
     symlen = SYMLEN,		/* significant characters in symbols */
     symmax,			/* max. symbol name length encountered */
     p_line,			/* no. printed lines on page (can be < 0) */
     ppl = PLENGTH,		/* page length */
     page;			/* no. of pages for listing */

unsigned long
     c_line;			/* current line no. in current source */

FILE *srcfp,			/* file pointer for current source */
     *objfp,			/* file pointer for object code */
     *lstfp,			/* file pointer for listing */
     *errfp;			/* file pointer for error output */
