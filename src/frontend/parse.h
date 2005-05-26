/*************
 * Header file for parse.c
 * 1999 E. Rouat
 * $Id: parse.h,v 1.3 2005/05/26 19:29:52 sjborley Exp $
 ************/

#ifndef _PARSE_H
#define _PARSE_H

#include <pnode.h>
#include <wordlist.h>

struct pnode * ft_getpnames(wordlist *wl, bool check);
#define free_pnode(ptr)  free_pnode_x(ptr); ptr=NULL;
void free_pnode_x(struct pnode *t);


#endif
