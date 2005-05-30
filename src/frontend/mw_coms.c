/* Michael Widlok 		2 Jun 1999 */
/* $Id: mw_coms.c,v 1.4 2005/05/30 20:28:30 sjborley Exp $ */
/* New commands for unloading circuits */

#include "ngspice.h"
#include "cpdefs.h"
#include "ftedefs.h"
#include "ftedev.h"
#include "ftedebug.h"
#include "dvec.h"

#include "circuits.h"
#include "mw_coms.h"
#include "variable.h"
#include "runcoms.h"

/* Clears ckt and removes current circ. form database */
 
void
com_removecirc(wordlist *wl)
{
    struct variable *v, *next;
    struct circ *ct;
    
    if (ft_curckt == NULL) {
        fprintf(cp_err, "Error: there is no circuit loaded.\n");
        return;
    }

	ct = ft_curckt; 
    
    if_cktfree(ct->ci_ckt, ct->ci_symtab);
    for (v = ct->ci_vars; v; v = next) {
	next = v->va_next;
	tfree(v);
    }
    
    ct->ci_vars = NULL;


    return;
}

