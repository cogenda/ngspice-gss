/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1987 Gary W. Ng
Modified: Alan Gillespie
**********/

#include "ngspice.h"
#include <stdio.h>
#include "mos9defs.h"
#include "cktdefs.h"
#include "fteconst.h"
#include "iferrmsg.h"
#include "noisedef.h"
#include "suffix.h"

/*
 * MOS9noise (mode, operation, firstModel, ckt, data, OnDens)
 *    This routine names and evaluates all of the noise sources
 *    associated with MOSFET's.  It starts with the model *firstModel and
 *    traverses all of its insts.  It then proceeds to any other models
 *    on the linked list.  The total output noise density generated by
 *    all of the MOSFET's is summed with the variable "OnDens".
 */

extern void   NevalSrc();
extern double Nintegrate();

int
MOS9noise (mode, operation, genmodel, ckt, data, OnDens)
    int mode;
    int operation;
    GENmodel *genmodel;
    CKTcircuit *ckt;
    Ndata *data;
    double *OnDens;
{
    MOS9model *firstModel = (MOS9model *) genmodel;
    MOS9model *model;
    MOS9instance *inst;
    char name[N_MXVLNTH];
    double tempOnoise;
    double tempInoise;
    double noizDens[MOS9NSRCS];
    double lnNdens[MOS9NSRCS];
    int error;
    int i;

    /* define the names of the noise sources */

    static char *MOS9nNames[MOS9NSRCS] = {       /* Note that we have to keep the order */
	"_rd",              /* noise due to rd */        /* consistent with the index definitions */
	"_rs",              /* noise due to rs */        /* in MOS9defs.h */
	"_id",              /* noise due to id */
	"_1overf",          /* flicker (1/f) noise */
	""                  /* total transistor noise */
    };

    for (model=firstModel; model != NULL; model=model->MOS9nextModel) {
	for (inst=model->MOS9instances; inst != NULL; inst=inst->MOS9nextInstance) {
	    switch (operation) {

	    case N_OPEN:

		/* see if we have to to produce a summary report */
		/* if so, name all the noise generators */

		if (((NOISEAN*)ckt->CKTcurJob)->NStpsSm != 0) {
		    switch (mode) {

		    case N_DENS:
			for (i=0; i < MOS9NSRCS; i++) {
			    (void)sprintf(name,"onoise_%s%s",inst->MOS9name,MOS9nNames[i]);


data->namelist = (IFuid *)trealloc((char *)data->namelist,(data->numPlots + 1)*sizeof(IFuid));
if (!data->namelist) return(E_NOMEM);
		(*(SPfrontEnd->IFnewUid))(ckt,
			&(data->namelist[data->numPlots++]),
			(IFuid)NULL,name,UID_OTHER,(GENERIC **)NULL);
				/* we've added one more plot */


			}
			break;

		    case INT_NOIZ:
			for (i=0; i < MOS9NSRCS; i++) {
			    (void)sprintf(name,"onoise_total_%s%s",inst->MOS9name,MOS9nNames[i]);


data->namelist = (IFuid *)trealloc((char *)data->namelist,(data->numPlots + 1)*sizeof(IFuid));
if (!data->namelist) return(E_NOMEM);
		(*(SPfrontEnd->IFnewUid))(ckt,
			&(data->namelist[data->numPlots++]),
			(IFuid)NULL,name,UID_OTHER,(GENERIC **)NULL);
				/* we've added one more plot */


			    (void)sprintf(name,"inoise_total_%s%s",inst->MOS9name,MOS9nNames[i]);


data->namelist = (IFuid *)trealloc((char *)data->namelist,(data->numPlots + 1)*sizeof(IFuid));
if (!data->namelist) return(E_NOMEM);
		(*(SPfrontEnd->IFnewUid))(ckt,
			&(data->namelist[data->numPlots++]),
			(IFuid)NULL,name,UID_OTHER,(GENERIC **)NULL);
				/* we've added one more plot */



			}
			break;
		    }
		}
		break;

	    case N_CALC:
		switch (mode) {

		case N_DENS:
		    NevalSrc(&noizDens[MOS9RDNOIZ],&lnNdens[MOS9RDNOIZ],
				 ckt,THERMNOISE,inst->MOS9dNodePrime,inst->MOS9dNode,
				 inst->MOS9drainConductance);

		    NevalSrc(&noizDens[MOS9RSNOIZ],&lnNdens[MOS9RSNOIZ],
				 ckt,THERMNOISE,inst->MOS9sNodePrime,inst->MOS9sNode,
				 inst->MOS9sourceConductance);

		    NevalSrc(&noizDens[MOS9IDNOIZ],&lnNdens[MOS9IDNOIZ],
				 ckt,THERMNOISE,inst->MOS9dNodePrime,inst->MOS9sNodePrime,
                                 (2.0/3.0 * fabs(inst->MOS9gm)));

		    NevalSrc(&noizDens[MOS9FLNOIZ],(double*)NULL,ckt,
				 N_GAIN,inst->MOS9dNodePrime, inst->MOS9sNodePrime,
				 (double)0.0);
		    noizDens[MOS9FLNOIZ] *= model->MOS9fNcoef * 
				 exp(model->MOS9fNexp *
				 log(MAX(fabs(inst->MOS9cd),N_MINLOG))) /
				 (data->freq *
				 (inst->MOS9w - 2*model->MOS9widthNarrow) *
                                 inst->MOS9m *
				 (inst->MOS9l - 2*model->MOS9latDiff) *
				 model->MOS9oxideCapFactor * model->MOS9oxideCapFactor);
		    lnNdens[MOS9FLNOIZ] = 
				 log(MAX(noizDens[MOS9FLNOIZ],N_MINLOG));

		    noizDens[MOS9TOTNOIZ] = noizDens[MOS9RDNOIZ] +
						     noizDens[MOS9RSNOIZ] +
						     noizDens[MOS9IDNOIZ] +
						     noizDens[MOS9FLNOIZ];
		    lnNdens[MOS9TOTNOIZ] = 
				 log(MAX(noizDens[MOS9TOTNOIZ], N_MINLOG));

		    *OnDens += noizDens[MOS9TOTNOIZ];

		    if (data->delFreq == 0.0) { 

			/* if we haven't done any previous integration, we need to */
			/* initialize our "history" variables                      */

			for (i=0; i < MOS9NSRCS; i++) {
			    inst->MOS9nVar[LNLSTDENS][i] = lnNdens[i];
			}

			/* clear out our integration variables if it's the first pass */

			if (data->freq == ((NOISEAN*)ckt->CKTcurJob)->NstartFreq) {
			    for (i=0; i < MOS9NSRCS; i++) {
				inst->MOS9nVar[OUTNOIZ][i] = 0.0;
				inst->MOS9nVar[INNOIZ][i] = 0.0;
			    }
			}
		    } else {   /* data->delFreq != 0.0 (we have to integrate) */
			for (i=0; i < MOS9NSRCS; i++) {
			    if (i != MOS9TOTNOIZ) {
				tempOnoise = Nintegrate(noizDens[i], lnNdens[i],
				      inst->MOS9nVar[LNLSTDENS][i], data);
				tempInoise = Nintegrate(noizDens[i] * data->GainSqInv ,
				      lnNdens[i] + data->lnGainInv,
				      inst->MOS9nVar[LNLSTDENS][i] + data->lnGainInv,
				      data);
				inst->MOS9nVar[LNLSTDENS][i] = lnNdens[i];
				data->outNoiz += tempOnoise;
				data->inNoise += tempInoise;
				if (((NOISEAN*)ckt->CKTcurJob)->NStpsSm != 0) {
				    inst->MOS9nVar[OUTNOIZ][i] += tempOnoise;
				    inst->MOS9nVar[OUTNOIZ][MOS9TOTNOIZ] += tempOnoise;
				    inst->MOS9nVar[INNOIZ][i] += tempInoise;
				    inst->MOS9nVar[INNOIZ][MOS9TOTNOIZ] += tempInoise;
                                }
			    }
			}
		    }
		    if (data->prtSummary) {
			for (i=0; i < MOS9NSRCS; i++) {     /* print a summary report */
			    data->outpVector[data->outNumber++] = noizDens[i];
			}
		    }
		    break;

		case INT_NOIZ:        /* already calculated, just output */
		    if (((NOISEAN*)ckt->CKTcurJob)->NStpsSm != 0) {
			for (i=0; i < MOS9NSRCS; i++) {
			    data->outpVector[data->outNumber++] = inst->MOS9nVar[OUTNOIZ][i];
			    data->outpVector[data->outNumber++] = inst->MOS9nVar[INNOIZ][i];
			}
		    }    /* if */
		    break;
		}    /* switch (mode) */
		break;

	    case N_CLOSE:
		return (OK);         /* do nothing, the main calling routine will close */
		break;               /* the plots */
	    }    /* switch (operation) */
	}    /* for inst */
    }    /* for model */

return(OK);
}
