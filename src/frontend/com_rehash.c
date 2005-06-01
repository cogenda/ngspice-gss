/*************
* com_rehash.c
* $Id: com_rehash.c,v 1.3 2005/06/01 11:32:38 sjborley Exp $
************/

#include <config.h>
#include <ngspice.h>

#include <wordlist.h>
#include "com_rehash.h"
#include "streams.h"
#include "control.h"
#include "parser/unixcom.h"

void
com_rehash(wordlist *wl)
{
    char *s;

    if (!cp_dounixcom) {
        fprintf(cp_err, "Error: unixcom not set.\n");
        return;
    }
    s = getenv("PATH");
    if (s)
        cp_rehash(s, TRUE);
    else
        fprintf(cp_err, "Error: no PATH in environment.\n");
    return;
}
