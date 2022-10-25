/*
 *	Z80 - Macro - Assembler - Intel-like macro implementation
 *	Copyright (C) 2022 by Thomas Eberhardt
 *
 *	History:
 *	??-OCT-2022 Intel-like macros
 */

/*
 *	processing of all macro PSEUDO ops
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "z80a.h"
#include "z80aglb.h"

/* z80amain.c */
extern void fatal(int, const char *);
extern void process_line(char *);
extern char *next_arg(char *, int *);

/* z80anum.c */
extern int is_first_sym_char(char);
extern int is_sym_char(char);
extern int eval(char *);

/* z80aout.c */
extern void asmerr(int);

/* z80atab.c */
extern char *strsave(char *);

#define MACNEST	16				/* max. expansion nesting */

struct dum {					/* macro dummy */
	char *dum_name;				/* dummy name */
	struct dum *dum_next;
};

struct line {					/* macro source line */
	char *line_text;			/* source line */
	struct line *line_next;
};

struct expn;

struct mac {					/* macro */
	void (*mac_start)(struct expn *);	/* start expansion function */
	int (*mac_rept)(struct expn *);		/* repeat expansion function */
	char *mac_name;				/* macro name */
	int mac_count;				/* REPT count */
	char *mac_irp;				/* IRP, IRPC character list */
	struct dum *mac_dums, *mac_dums_last;	/* macro dummies */
	struct line *mac_lines, *mac_lines_last; /* macro body */
	struct mac *mac_next;
};

struct parm {					/* expansion parameter */
	char *parm_name;			/* dummy name */
	char *parm_val;				/* parameter value */
	struct parm *parm_next;
};

struct loc {					/* expansion local label */
	char *loc_name;				/* local label name */
	char loc_val[8];			/* local label value ??xxxx */
	struct loc *loc_next;
};

struct expn {					/* macro expansion */
	struct mac *expn_mac;			/* macro being expanded */
	struct parm *expn_parms, *expn_parms_last; /* macro parameters */
	struct loc *expn_locs;			/* expansion local labels */
	struct line *expn_line;			/* current expansion line */
	int expn_iflevel;			/* iflevel before expansion */
	int expn_iter;				/* curr. expansion iteration */
	char *expn_irp;				/* IRP, IRPC character list */
};

static struct mac *mac_table;			/* MACRO table */
static struct mac *mac_def;			/* macro being defined */
static struct mac *mac_found;			/* found macro */
static struct expn mac_expn[MACNEST];		/* macro expansion stack */
static int mac_loc_cnt;				/* counter for LOCAL labels */
static char tmp[MAXLINE + 1];			/* temporary buffer */

/*
 *	verify that p is a legal symbol
 *	return 1 if legal, otherwise 0
 */
int is_symbol(char *p)
{
	if (!is_sym_char(*p) || isdigit((unsigned char) *p))
		return(0);
	p++;
	while (is_sym_char(*p))
		p++;
	return(*p == '\0');
}

/*
 *	allocate a new macro with optional name and
 *	start/repeat expansion function
 */
struct mac *mac_new(char *name, void (*start)(struct expn *),
		    int (*rept)(struct expn *))
{
	struct mac *m;

	if ((m = (struct mac *) malloc(sizeof(struct mac))) == NULL)
		fatal(F_OUTMEM, "macro");
	if (name != NULL) {
		if ((m->mac_name = strsave(name)) == NULL)
			fatal(F_OUTMEM, "macro name");
	} else
		m->mac_name = NULL;
	m->mac_start = start;
	m->mac_rept = rept;
	m->mac_count = 0;
	m->mac_irp = NULL;
	m->mac_dums_last = m->mac_dums = NULL;
	m->mac_lines_last = m->mac_lines = NULL;
	m->mac_next = NULL;
	return(m);
}

/*
 *	delete a macro
 */
void mac_delete(struct mac *m)
{
	struct dum *d, *d1;
	struct line *ln, *ln1;

	for (d = m->mac_dums; d != NULL; d = d1) {
		d1 = d->dum_next;
		free(d->dum_name);
		free(d);
	}
	for (ln = m->mac_lines; ln != NULL; ln = ln1) {
		ln1 = ln->line_next;
		free(ln->line_text);
		free(ln);
	}
	if (m->mac_irp != NULL)
		free(m->mac_irp);
	if (m->mac_name != NULL)
		free(m->mac_name);
	free(m);
}

/*
 *	initialize variables at start of pass
 */
void mac_start_pass(void)
{
	mac_loc_cnt = 0;
}

/*
 *	clean up at end of pass
 */
void mac_end_pass(void)
{
	register struct mac *m;

	while (mac_table != NULL) {
		m = mac_table->mac_next;
		mac_delete(mac_table);
		mac_table = m;
	}
}

/*
 * 	add a dummy to a macro
 */
void mac_add_dum(struct mac *m, char *name)
{
	register struct dum *d;

	for (d = m->mac_dums; d; d = d->dum_next)
		if (strcmp(d->dum_name, name) == 0) {
			asmerr(E_MULSYM);
			return;
		}
	d = (struct dum *) malloc(sizeof(struct dum));
	if (d == NULL || (d->dum_name = strsave(name)) == NULL)
		fatal(F_OUTMEM, "macro dummy");
	d->dum_next = NULL;
	if (m->mac_dums == NULL)
		m->mac_dums = d;
	else
		m->mac_dums_last->dum_next = d;
	m->mac_dums_last = d;
}

/*
 * 	add a local to a macro expansion
 */
void expn_add_loc(struct expn *e, char *name)
{
	register struct loc *l;
	register struct dum *d;

	for (l = e->expn_locs; l; l = l->loc_next)
		if (strcmp(l->loc_name, name) == 0) {
			asmerr(E_MULSYM);
			return;
		}
	for (d = e->expn_mac->mac_dums; d; d = d->dum_next)
		if (strcmp(d->dum_name, name) == 0) {
			asmerr(E_MULSYM);
			return;
		}
	l = (struct loc *) malloc(sizeof(struct loc));
	if (l == NULL || (l->loc_name = strsave(name)) == NULL)
		fatal(F_OUTMEM, "macro local label");
	l->loc_next = e->expn_locs;
	e->expn_locs = l;
}

/*
 *	start macro expansion
 *	assign values to parameters, save iflevel
 */
void mac_start_expn(struct mac *m)
{
	register struct expn *e;
	register struct dum *d;
	register struct parm *p;

	if (mac_exp_nest == MACNEST) {
		/* abort macro expansion */
		mac_exp_nest = 0;
		if ((iflevel = mac_expn[0].expn_iflevel) > 0)
			gencode = condnest[iflevel - 1];
		else
			gencode = pass;
		asmerr(E_MACNEST);
		return;
	}
	if (m->mac_lines != NULL) {
		e = &mac_expn[mac_exp_nest];
		e->expn_mac = m;
		e->expn_parms_last = e->expn_parms = NULL;
		for (d = m->mac_dums; d; d = d->dum_next) {
			p = (struct parm *) malloc(sizeof(struct parm));
			if (p == NULL)
				fatal(F_OUTMEM, "macro parameter");
			p->parm_name = d->dum_name;
			p->parm_val = NULL;
			p->parm_next = NULL;
			if (e->expn_parms == NULL)
				e->expn_parms = p;
			else
				e->expn_parms_last->parm_next = p;
			e->expn_parms_last = p;
		}
		e->expn_locs = NULL;
		e->expn_line = m->mac_lines;
		e->expn_iflevel = iflevel;
		e->expn_iter = 0;
		e->expn_irp = m->mac_irp;
		(*m->mac_start)(e);
		mac_exp_nest++;
	}
}

/*
 *	end macro expansion
 *	delete parameters and local labels, restore iflevel
 */
void mac_end_expn(void)
{
	register struct expn *e;
	register struct parm *p, *p1;
	register struct loc *l, *l1;
	register struct mac *m;

	e = &mac_expn[mac_exp_nest - 1];
	for (p = e->expn_parms; p; p = p1) {
		p1 = p->parm_next;
		if (p->parm_val != NULL)
			free(p->parm_val);
		free(p);
	}
	for (l = e->expn_locs; l; l = l1) {
		l1 = l->loc_next;
		free(l);
	}
	if ((iflevel = e->expn_iflevel) > 0)
		gencode = condnest[iflevel - 1];
	else
		gencode = pass;
	m = e->expn_mac;
	mac_exp_nest--;
	/* delete unnamed macros (IRP, IRPC, REPT) */
	if (m->mac_name == NULL)
		mac_delete(m);
}

/*
 *	repeat macro for IRP, IRPC, REPT when end reached
 *	end expansion for MACRO
 */
int mac_rept_expn(void)
{
	register struct expn *e;
	register struct mac *m;
	register struct loc *l, *l1;

	e = &mac_expn[mac_exp_nest - 1];
	e->expn_iter++;
	m = e->expn_mac;
	if (*m->mac_rept != NULL && (*m->mac_rept)(e)) {
		for (l = e->expn_locs; l; l = l1) {
			l1 = l->loc_next;
			free(l);
		}
		if ((iflevel = e->expn_iflevel) > 0)
			gencode = condnest[iflevel - 1];
		else
			gencode = pass;
		e->expn_locs = NULL;
		e->expn_line = m->mac_lines;
		return(1);
	} else {
		mac_end_expn();
		return(0);
	}
}

/*
 *	add source line l to current macro definition
 */
void mac_add_line(struct opc *op, char *line)
{
	register struct line *l;
	register struct mac *m;

	a_mode = A_NONE;
	l = (struct line *) malloc(sizeof(struct line));
	if (l == NULL || (l->line_text = strsave(line)) == NULL)
		fatal(F_OUTMEM, "macro body line");
	l->line_next = NULL;
	m = mac_def;
	if (m->mac_lines == NULL)
		m->mac_lines = l;
	else
		m->mac_lines_last->line_next = l;
	m->mac_lines_last = l;
	if (op != NULL) {
		if (op->op_flags & OP_MDEF)
			mac_def_nest++;
		else if (op->op_flags & OP_MEND) {
			if (--mac_def_nest == 0) {
				m = mac_def;
				mac_def = NULL;
				/* start expansion for IRP, IRPC, REPT */
				if (m->mac_name == NULL)
					mac_start_expn(m);
			}
		}
	}
}

/*
 *	return value of dummy or local s, NULL if not found
 */
const char *mac_get_dumloc(struct expn *e, char *s)
{
	register struct parm *p;
	register struct loc *l;

	for (p = e->expn_parms; p; p = p->parm_next)
		if (strcmp(p->parm_name, s) == 0)
			return(p->parm_val == NULL ? "" : p->parm_val);
	for (l = e->expn_locs; l; l = l->loc_next)
		if (strcmp(l->loc_name, s) == 0)
			return(l->loc_val);
	return(NULL);
}

/*
 *	substitute dummies and locals with actual values
 *	in current expansion source line and return the
 *	result
 */
char *mac_subst_dumloc(struct expn *e)
{
	register char *p, *q, *q1;
	register const char *v;
	register char c;
	register int n, amp_flag;

	p = e->expn_line->line_text;
	if (*p == LINCOM)
		return(p);
	q = tmp;
	*q++ = ' ';
	n = 0;
	while (*p != '\n' && *p != '\0') {
		if (is_first_sym_char(*p)) {
			q1 = q;
			*q++ = toupper((unsigned char) *p++);
			while (is_sym_char(*p))
				*q++ = toupper((unsigned char) *p++);
			if (*(q1 - 1) != '^') {
				*q = '\0';
				v = mac_get_dumloc(e, q1);
				if (v != NULL) {
					if (*(q1 - 1) == '&')
						q1--;
					q = q1;
					while (*v != '\0')
						*q++ = *v++;
					if (*p == '&')
						p++;
				}
			}
		} else if (*p == STRDEL || *p == STRDEL2) {
			*q++ = c = *p++;
			amp_flag = 0;
			while (1) {
				if (*p == '\n' || *p == '\0') {
					asmerr(E_MISDEL);
					goto done;
				} else if (*p == c) {
					amp_flag = 0;
					*q++ = *p++;
					if (*p != c)
						break;
					else {
						*q++ = *p++;
						continue;
					}
				} else if (is_first_sym_char(*p)) {
					q1 = q;
					*q++ = *p++;
					while (is_sym_char(*p))
						*q++ = *p++;
					*q = '\0';
					if (*(q1 - 1) == '&' || *p == '&'
							     || amp_flag) {
						amp_flag = 0;
						v = mac_get_dumloc(e, q1);
						if (v != NULL) {
							if (*(q1 - 1) == '&')
								q1--;
							q = q1;
							while (*v != '\0')
								*q++ = *v++;
							if (*p == '&') {
								amp_flag = 1;
								p++;
							}
						}
					}
				} else {
					amp_flag = 0;
					*q++ = *p++;
				}
			}
		} else if (*p == '^') {
			*q++ = *p++;
			if (*p != '\n' && *p != '\0')
				*q++ = toupper((unsigned char) *p++);
			else {
				asmerr(E_ILLOPE);
				goto done;
			}
		} else if (*p == '<') {
			n++;
			*q++ = *p++;
		} else if (*p == '>') {
			n--;
			*q++ = *p++;
		} else if (n == 0 && *p == COMMENT) {
			if (*(p + 1) != COMMENT)
				while (*p != '\n' && *p != '\0')
					*q++ = *p++;
			goto done;
		} else
			*q++ = toupper((unsigned char) *p++);
	}
	if (n > 0)
		asmerr(E_MISDEL);
done:
	*q++ = '\n';
	*q = '\0';
	return(tmp + 1);
}

/*
 *	get next macro expansion line
 */
char *mac_expand(void)
{
	register struct expn *e;
	register char *p;

	e = &mac_expn[mac_exp_nest - 1];
	if (e->expn_line == NULL && !mac_rept_expn())
		return(NULL);
	p = mac_subst_dumloc(e);
	e->expn_line = e->expn_line->line_next;
	return(p);
}

/*
 *	macro lookup
 */
int mac_lookup(char *opcode)
{
	register struct mac *m;

	mac_found = NULL;
	for (m = mac_table; m; m = m->mac_next)
		if (strcmp(m->mac_name, opcode) == 0) {
			mac_found = m;
			return(1);
		}
	return(0);
}

/*
 *	MACRO invocation, to be called after successful mac_lookup()
 */
void mac_call(void)
{
	if (mac_found != NULL)
		mac_start_expn(mac_found);
	else
		fatal(F_INTERN, "mac_call with no macro");
}

/*
 *	get next MACRO or IRP parameter
 */
char *mac_next_parm(char *s)
{
	register char *t, c;
	register int n;

	t = tmp;
	while (isspace((unsigned char) *s))
		s++;
	if (*s == STRDEL || *s == STRDEL2) {
		*t++ = c = *s++;
		while (1) {
			if (*s == '\n' || *s == '\0') {
				asmerr(E_MISDEL);
				return(NULL);
			} else if (*s == c) {
				if (*(s + 1) != c)
					break;
				else
					s++;
			}
			*t++ = *s++;
		}
		*t++ = *s++;
	} else if (*s == '<') {
		s++;
		n = 0;
		while (1) {
			if (*s == '\n' || *s == '\0'
				       || *s == COMMENT) {
				asmerr(E_MISDEL);
				return(NULL);
			} else if (*s == '<')
				n++;
			else if (*s == '>') {
				if (n == 0)
					break;
				else
					n--;
			} else if (*s == '^') {
				s++;
				if (*s == '\n' || *s == '\0') {
					asmerr(E_ILLOPE);
					return(NULL);
				}
			} else if (*s == STRDEL || *s == STRDEL2) {
				*t++ = c = *s++;
				while (1) {
					if (*s == '\n' || *s == '\0') {
						asmerr(E_MISDEL);
						return(NULL);
					} else if (*s == c) {
						if (*(s + 1) != c)
							break;
						else
							s++;
					}
					*t++ = *s++;
				}
			}
			*t++ = toupper((unsigned char) *s++);
		}
		s++;
#if 0
	} else if (*s == '^') {
		s++;
		if (*s == '\n' || *s == '\0') {
			asmerr(E_ILLOPE);
			return(NULL);
		}
		*t++ = toupper((unsigned char) *s++);
#endif
	} else if (*s == '%') {
		s++;
		while (*s != '\n' && *s != '\0'
				  && *s != ',' && *s != COMMENT) {
			*t++ = c = toupper((unsigned char) *s++);
			if (c == STRDEL || c == STRDEL2) {
				while (1) {
					if (*s == '\n' || *s == '\0') {
						asmerr(E_MISDEL);
						return(NULL);
					} else if (*s == c) {
						*t++ = *s++;
						if (*s != c)
							break;
					}
					*t++ = *s++;
				}
			}
		}
		*t = '\0';
		sprintf(tmp, "%d", eval(tmp));
		t = tmp + strlen(tmp);
	} else
		while (!isspace((unsigned char) *s) && *s != '\n'
						    && *s != '\0'
						    && *s != ','
						    && *s != COMMENT)
			*t++ = toupper((unsigned char) *s++);
	while (isspace((unsigned char) *s))
		s++;
	*t = '\0';
	return s;
}

/*
 *	start IRP macro expansion
 */
void mac_start_irp(struct expn *e)
{
	register char *s;

	if (*e->expn_irp != '\0') {
		if ((s = mac_next_parm(e->expn_irp)) != NULL) {
			e->expn_irp = s;
			if ((s = strsave(tmp)) == NULL)
				fatal(F_OUTMEM, "IRP character list");
			e->expn_parms->parm_val = s;
		}
	}
}

/*
 *	repeat IRP macro expansion
 */
int mac_rept_irp(struct expn *e)
{
	register char *s;

	s = e->expn_irp;
	if (*s == '\0')
		return(0);
	else if (*s != ',') {
		asmerr(E_ILLOPE);
		return(0);
	} else {
		if ((s = mac_next_parm(s + 1)) == NULL)
			return(0);
		e->expn_irp = s;
		if ((s = strsave(tmp)) == NULL)
			fatal(F_OUTMEM, "IRP character list");
		if (e->expn_parms->parm_val != NULL)
			free(e->expn_parms->parm_val);
		e->expn_parms->parm_val = s;
		return(1);
	}
}

/*
 *	start IRPC macro expansion
 */
void mac_start_irpc(struct expn *e)
{
	register char *s;

	if (*e->expn_irp != '\0') {
		if ((s = (char *) malloc(sizeof(char) * 2)) == NULL)
			fatal(F_OUTMEM, "IRPC character");
		*s = *e->expn_irp++;
		*(s + 1) = '\0';
		e->expn_parms->parm_val = s;
	}
}

/*
 *	repeat IRPC macro expansion
 */
int mac_rept_irpc(struct expn *e)
{
	if (*e->expn_irp != '\0') {
		e->expn_parms->parm_val[0] = *e->expn_irp++;
		return(1);
	} else
		return(0);
}

/*
 *	start MACRO macro expansion
 */
void mac_start_macro(struct expn *e)
{
	register char *s;
	register struct parm *p;

	s = operand;
	p = e->expn_parms;
	while (p != NULL && *s != '\n' && *s != '\0' && *s != COMMENT) {
		if ((s = mac_next_parm(s)) == NULL)
			return;
		if (*s == ',') {
			s++;
			while (isspace((unsigned char) *s))
				s++;
		} else if (*s != '\0' && *s != COMMENT) {
			asmerr(E_ILLOPE);
			return;
		}
		if ((p->parm_val = strsave(tmp)) == NULL)
			fatal(F_OUTMEM, "parameter assignment");
		p = p->parm_next;
	}
}

/*
 *	start REPT macro expansion
 */
void mac_start_rept(struct expn *e)
{
	if (e->expn_mac->mac_count <= 0)
		e->expn_line = NULL;
}

/*
 *	repeat REPT macro expansion
 */
int mac_rept_rept(struct expn *e)
{
	return(e->expn_iter < e->expn_mac->mac_count);
}

/*
 *	ENDM
 */
int op_endm(int dummy1, int dummy2)
{
	UNUSED(dummy1);
	UNUSED(dummy2);

	a_mode = A_NONE;
	if (mac_exp_nest == 0)
		asmerr(E_NIMEXP);
	else
		(void) mac_rept_expn();
	return(0);
}

/*
 *	EXITM
 */
int op_exitm(int dummy1, int dummy2)
{
	UNUSED(dummy1);
	UNUSED(dummy2);

	a_mode = A_NONE;
	if (mac_exp_nest == 0)
		asmerr(E_NIMEXP);
	else
		mac_end_expn();
	return(0);
}

/*
 *	IFB, IFNB, IFIDN, IFDIF
 */
int op_mcond(int op_code, int dummy)
{
	register char *p, *p1, *p2;

	UNUSED(dummy);

	a_mode = A_NONE;
	if (iflevel >= IFNEST) {
		asmerr(E_IFNEST);
		return(0);
	}
	condnest[iflevel++] = gencode;
	if (gencode < 0)
		return(0);
	switch(op_code) {
	case 1:				/* IFB */
	case 2:				/* IFNB */
	case 3:				/* IFIDN */
	case 4:				/* IFDIF */
		p = operand;
		if (*p == '\0' || *p == COMMENT) {
			asmerr(E_MISOPE);
			return(0);
		}
		if (*p++ != '<') {
			asmerr(E_ILLOPE);
			return(0);
		}
		while (*p != '>' && *p != '\0' && *p != COMMENT)
			p++;
		if (*p++ != '>') {
			asmerr(E_MISPAR);
			return(0);
		}
		p1 = p;
		while (isspace((unsigned char) *p1))
			p1++;
		if (op_code == 1 || op_code == 2) { /* IFB and IFNB */
			if (*p1 != '\0' && *p1 != COMMENT) {
				asmerr(E_ILLOPE);
				return(0);
			}
			gencode = ((p - operand) == 2) ? pass : -pass;
		} else {		/* IFIDN and IFDIF */
			if (*p1 != ',') {
				asmerr(E_MISOPE);
				return(0);
			}
			*p = '\0';
			while (isspace((unsigned char) *p1))
				p1++;
			p = p1;
			if (*p1++ != '<') {
				asmerr(E_ILLOPE);
				return(0);
			}
			while (*p1 != '>' && *p1 != '\0' && *p1 != COMMENT)
				p1++;
			if (*p1++ != '>') {
				asmerr(E_MISPAR);
				return(0);
			}
			p2 = p1;
			while (isspace((unsigned char) *p2))
				p2++;
			if (*p2 != '\0' && *p2 != COMMENT) {
				asmerr(E_ILLOPE);
				return(0);
			}
			*p1 = '\0';
			gencode = (strcmp(operand, p) == 0) ? pass : -pass;
		}
		break;
	default:
		fatal(F_INTERN, "invalid opcode for function op_mcond");
		break;
	}
	if ((op_code & 1) == 0)		/* negate for inverse IF */
		gencode = -gencode;
	return(0);
}

/*
 *	IRP
 */
int op_irp(int dummy1, int dummy2)
{
	register char *s, *t, c;
	register struct mac *m;
	register int n;

	UNUSED(dummy1);
	UNUSED(dummy2);

	a_mode = A_NONE;
	m = mac_new(NULL, mac_start_irp, mac_rept_irp);
	s = operand;
	t = tmp;
	if (!is_first_sym_char(*s)) {
		asmerr(E_ILLOPE);
		return(0);
	}
	*t++ = toupper((unsigned char) *s++);
	while (is_sym_char(*s))
		*t++ = toupper((unsigned char) *s++);
	*t = '\0';
	mac_add_dum(m, tmp);
	while (isspace((unsigned char) *s))
		s++;
	if (*s++ != ',') {
		asmerr(E_ILLOPE);
		return(0);
	}
	while (isspace((unsigned char) *s))
		s++;
	t = tmp;
	n = 0;
	if (*s++ != '<') {
		asmerr(E_ILLOPE);
		return(0);
	}
	while (1) {
		if (*s  == '\n' || *s == '\0' || *s == COMMENT) {
			asmerr(E_MISDEL);
			return(0);
		} else if (*s == STRDEL || *s == STRDEL2) {
			*t++ = c = *s++;
			while (1) {
				if (*s == '\n' || *s == '\0') {
					asmerr(E_MISDEL);
					return(0);
				} else if (*s == c) {
					*t++ = *s++;
					if (*s != c)
						break;
					else {
						*t++ = *s++;
						continue;
					}
				} else
					*t++ = *s++;
			}
		} else if (*s == '^') {
			*t++ = *s++;
			if (*s != '\n' || *s != '\0')
				*t++ = toupper((unsigned char) *s++);
			else {
				asmerr(E_ILLOPE);
				return(0);
			}
		} else if (*s == '<') {
			n++;
			*t++ = *s++;
		} else if (*s == '>') {
			if (n == 0)
				break;
			else {
				n--;
				*t++ = *s++;
			}
		} else
			*t++ = toupper((unsigned char) *s++);
	}
	*t = '\0';
	if ((m->mac_irp = strsave(tmp)) == NULL)
		fatal(F_OUTMEM, "IRP character lists");
	mac_def = m;
	mac_def_nest++;
	return(0);
}

/*
 *	IPRC
 */
int op_irpc(int dummy1, int dummy2)
{
	register char *s, *t;
	register struct mac *m;
	register int n;

	UNUSED(dummy1);
	UNUSED(dummy2);

	a_mode = A_NONE;
	m = mac_new(NULL, mac_start_irpc, mac_rept_irpc);
	s = operand;
	t = tmp;
	if (!is_first_sym_char(*s)) {
		asmerr(E_ILLOPE);
		return(0);
	}
	*t++ = toupper((unsigned char) *s++);
	while (is_sym_char(*s))
		*t++ = toupper((unsigned char) *s++);
	*t = '\0';
	mac_add_dum(m, tmp);
	while (isspace((unsigned char) *s))
		s++;
	if (*s++ != ',') {
		asmerr(E_ILLOPE);
		return(0);
	}
	while (isspace((unsigned char) *s))
		s++;
	t = tmp;
	n = 0;
	if (*s == '<') {
		s++;
		n++;
	}
	while (!isspace((unsigned char) *s) && *s != '\n' && *s != '\0'
					    && *s != COMMENT) {
		if (*s == '>' && n > 0) {
			s++;
			break;
		} else if (*s == '^') {
			s++;
			if (*s == '\n' || *s == '\0') {
				asmerr(E_ILLOPE);
				return(0);
			}
		}
		*t++ = toupper((unsigned char) *s++);
	}
	while (isspace((unsigned char) *s))
		s++;
	if (*s != '\0' && *s != COMMENT) {
		asmerr(E_ILLOPE);
		return(0);
	}
	*t = '\0';
	if ((m->mac_irp = strsave(tmp)) == NULL)
		fatal(F_OUTMEM, "IRPC character list");
	mac_def = m;
	mac_def_nest++;
	return(0);
}

/*
 *	LOCAL
 */
int op_local(int dummy1, int dummy2)
{
	register char *p, *p1;
	register struct expn *e;

	UNUSED(dummy1);
	UNUSED(dummy2);

	a_mode = A_NONE;
	if (mac_exp_nest == 0) {
		asmerr(E_NIMEXP);
		return(0);
	}
	e = &mac_expn[mac_exp_nest - 1];
	p = operand;
	/* XXX loop does (n-1)*n/2+n*m strcmp's for n locals and m dummies */
	while (p != NULL) {
		p1 = next_arg(p, NULL);
		if (*p != '\0') {
			if (is_symbol(p)) {
				expn_add_loc(e, p);
				if (mac_loc_cnt == 10000)
					asmerr(E_OUTLCL);
				else
					mac_loc_cnt++;
				sprintf(e->expn_locs->loc_val,
					"??%04d", mac_loc_cnt);
			} else
				asmerr(E_ILLOPE);
		}
		p = p1;
	}
	return(0);
}

/*
 *	MACRO
 */
int op_macro(int dummy1, int dummy2)
{
	register char *p, *p1;
	register struct mac *m;

	UNUSED(dummy1);
	UNUSED(dummy2);

	a_mode = A_NONE;
	m = mac_new(label, mac_start_macro, NULL);
	m->mac_next = mac_table;
	mac_table = m;
	p = operand;
	/* XXX loop does (n-1)*n/2 strcmp's for n dummies */
	while (p != NULL) {
		p1 = next_arg(p, NULL);
		if (*p != '\0') {
			if (is_symbol(p))
				mac_add_dum(m, p);
			else
				asmerr(E_ILLOPE);
		}
		p = p1;
	}
	mac_def = m;
	mac_def_nest++;
	return(0);
}

/*
 *	REPT
 */
int op_rept(int dummy1, int dummy2)
{
	register struct mac *m;

	UNUSED(dummy1);
	UNUSED(dummy2);

	a_mode = A_NONE;
	m = mac_new(NULL, mac_start_rept, mac_rept_rept);
	m->mac_count = eval(operand);
	mac_def = m;
	mac_def_nest++;
	return(0);
}
