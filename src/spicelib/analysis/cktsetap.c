/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 Thomas L. Quarles
**********/

#include "ngspice.h"
#include "tskdefs.h"
#include "jobdefs.h"
#include "ifsim.h"
#include "iferrmsg.h"
#include "cktdefs.h"

#include "analysis.h"

extern SPICEanalysis *analInfo[];

/* ARGSUSED */
int
CKTsetAnalPm(void *ckt, void *analPtr, int parm, IFvalue *value, IFvalue *selector)
{
    int type = ((JOB *)analPtr)->JOBtype;
    if((analInfo[type]->setParm)==NULL) return(E_BADPARM);
    return( (*(analInfo[type]->setParm))(ckt,analPtr,parm,value) );
}
