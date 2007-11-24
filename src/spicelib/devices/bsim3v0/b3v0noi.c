/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1995 Gary W. Ng and Min-Chie Jeng.
File:  b3v0noi.c
**********/

#include "ngspice.h"
#include "bsim3v0def.h"
#include "cktdefs.h"
#include "iferrmsg.h"
#include "noisedef.h"
#include "suffix.h"
#include "const.h"  /* jwan */

/*
 * BSIM3v0noise (mode, operation, firstModel, ckt, data, OnDens)
 *    This routine names and evaluates all of the noise sources
 *    associated with MOSFET's.  It starts with the model *firstModel and
 *    traverses all of its insts.  It then proceeds to any other models
 *    on the linked list.  The total output noise density generated by
 *    all of the MOSFET's is summed with the variable "OnDens".
 */

extern void   NevalSrc();
extern double Nintegrate();


double
StrongInversionNoiseEval3v0(double vgs, double vds, BSIM3v0model *model, 
                         BSIM3v0instance *here, double freq, double temp)
{
struct bsim3v0SizeDependParam *pParam;
double cd, esat, DelClm, EffFreq, N0, Nl, Vgst;
double T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, Ssi;

    pParam = here->pParam;
    cd = fabs(here->BSIM3v0cd) * here->BSIM3v0m;
    if (vds > here->BSIM3v0vdsat)
    {   esat = 2.0 * pParam->BSIM3v0vsattemp / here->BSIM3v0ueff;
	T0 = ((((vds - here->BSIM3v0vdsat) / pParam->BSIM3v0litl) + model->BSIM3v0em)
	   / esat);
        DelClm = pParam->BSIM3v0litl * log (MAX(T0, N_MINLOG));
    }
    else 
        DelClm = 0.0;
    EffFreq = pow(freq, model->BSIM3v0ef);
    T1 = CHARGE * CHARGE * 8.62e-5 * cd * (temp + CONSTCtoK) * here->BSIM3v0ueff;
    T2 = 1.0e8 * EffFreq * model->BSIM3v0cox
       * pParam->BSIM3v0leff * pParam->BSIM3v0leff;
    Vgst = vgs - here->BSIM3v0von;
    N0 = model->BSIM3v0cox * Vgst / CHARGE;
    if (N0 < 0.0)
	N0 = 0.0;
    Nl = model->BSIM3v0cox * (Vgst - MIN(vds, here->BSIM3v0vdsat)) / CHARGE;
    if (Nl < 0.0)
	Nl = 0.0;

    T3 = model->BSIM3v0oxideTrapDensityA
       * log(MAX(((N0 + 2.0e14) / (Nl + 2.0e14)), N_MINLOG));
    T4 = model->BSIM3v0oxideTrapDensityB * (N0 - Nl);
    T5 = model->BSIM3v0oxideTrapDensityC * 0.5 * (N0 * N0 - Nl * Nl);

    T6 = 8.62e-5 * (temp + CONSTCtoK) * cd * cd;
    T7 = 1.0e8 * EffFreq * pParam->BSIM3v0leff
       * pParam->BSIM3v0leff * pParam->BSIM3v0weff * here->BSIM3v0m;
    T8 = model->BSIM3v0oxideTrapDensityA + model->BSIM3v0oxideTrapDensityB * Nl
       + model->BSIM3v0oxideTrapDensityC * Nl * Nl;
    T9 = (Nl + 2.0e14) * (Nl + 2.0e14);

    Ssi = T1 / T2 * (T3 + T4 + T5) + T6 / T7 * DelClm * T8 / T9;
    return Ssi;
}

int
BSIM3v0noise (int mode, int operation, GENmodel *inModel, CKTcircuit *ckt, 
              Ndata *data, double *OnDens)
{
BSIM3v0model *model = (BSIM3v0model *)inModel;
BSIM3v0instance *here;
struct bsim3v0SizeDependParam *pParam;
char name[N_MXVLNTH];
double tempOnoise;
double tempInoise;
double noizDens[BSIM3v0NSRCS];
double lnNdens[BSIM3v0NSRCS];

double vgs, vds, Slimit;
double T1, T10, T11;
double Ssi, Swi;

int i;

    /* define the names of the noise sources */
    static char *BSIM3v0nNames[BSIM3v0NSRCS] =
    {   /* Note that we have to keep the order */
	".rd",              /* noise due to rd */
			    /* consistent with the index definitions */
	".rs",              /* noise due to rs */
			    /* in BSIM3v0defs.h */
	".id",              /* noise due to id */
	".1overf",          /* flicker (1/f) noise */
	""                  /* total transistor noise */
    };

    for (; model != NULL; model = model->BSIM3v0nextModel)
    {    for (here = model->BSIM3v0instances; here != NULL;
	      here = here->BSIM3v0nextInstance)
	 {    
	 
              if (here->BSIM3v0owner != ARCHme)
                      continue;

	      pParam = here->pParam;
	      switch (operation)
	      {  case N_OPEN:
		     /* see if we have to to produce a summary report */
		     /* if so, name all the noise generators */

		      if (((NOISEAN*)ckt->CKTcurJob)->NStpsSm != 0)
		      {   switch (mode)
			  {  case N_DENS:
			          for (i = 0; i < BSIM3v0NSRCS; i++)
				  {    (void) sprintf(name, "onoise.%s%s",
					              here->BSIM3v0name,
						      BSIM3v0nNames[i]);
                                       data->namelist = (IFuid *) trealloc(
					     (char *) data->namelist,
					     (data->numPlots + 1)
					     * sizeof(IFuid));
                                       if (!data->namelist)
					   return(E_NOMEM);
		                       (*(SPfrontEnd->IFnewUid)) (ckt,
			                  &(data->namelist[data->numPlots++]),
			                  (IFuid) NULL, name, UID_OTHER,
					  (void **) NULL);
				       /* we've added one more plot */
			          }
			          break;
		             case INT_NOIZ:
			          for (i = 0; i < BSIM3v0NSRCS; i++)
				  {    (void) sprintf(name, "onoise_total.%s%s",
						      here->BSIM3v0name,
						      BSIM3v0nNames[i]);
                                       data->namelist = (IFuid *) trealloc(
					     (char *) data->namelist,
					     (data->numPlots + 1)
					     * sizeof(IFuid));
                                       if (!data->namelist)
					   return(E_NOMEM);
		                       (*(SPfrontEnd->IFnewUid)) (ckt,
			                  &(data->namelist[data->numPlots++]),
			                  (IFuid) NULL, name, UID_OTHER,
					  (void **) NULL);
				       /* we've added one more plot */

			               (void) sprintf(name, "inoise_total.%s%s",
						      here->BSIM3v0name,
						      BSIM3v0nNames[i]);
                                       data->namelist = (IFuid *) trealloc(
					     (char *) data->namelist,
					     (data->numPlots + 1)
					     * sizeof(IFuid));
                                       if (!data->namelist)
					   return(E_NOMEM);
		                       (*(SPfrontEnd->IFnewUid)) (ckt,
			                  &(data->namelist[data->numPlots++]),
			                  (IFuid) NULL, name, UID_OTHER,
					  (void **)NULL);
				       /* we've added one more plot */
			          }
			          break;
		          }
		      }
		      break;
	         case N_CALC:
		      switch (mode)
		      {  case N_DENS:
		              NevalSrc(&noizDens[BSIM3v0RDNOIZ],
				       &lnNdens[BSIM3v0RDNOIZ], ckt, THERMNOISE,
				       here->BSIM3v0dNodePrime, here->BSIM3v0dNode,
				       here->BSIM3v0drainConductance * here->BSIM3v0m);

		              NevalSrc(&noizDens[BSIM3v0RSNOIZ],
				       &lnNdens[BSIM3v0RSNOIZ], ckt, THERMNOISE,
				       here->BSIM3v0sNodePrime, here->BSIM3v0sNode,
				       here->BSIM3v0sourceConductance * here->BSIM3v0m);

                              if (model->BSIM3v0noiMod == 2)
		              {   NevalSrc(&noizDens[BSIM3v0IDNOIZ],
				         &lnNdens[BSIM3v0IDNOIZ], ckt, THERMNOISE,
				         here->BSIM3v0dNodePrime,
                                         here->BSIM3v0sNodePrime, (here->BSIM3v0ueff
					 * fabs((here->BSIM3v0qinv * here->BSIM3v0m)
					 / (pParam->BSIM3v0leff
					 *  pParam->BSIM3v0leff))));
		              }
                              else
			      {   NevalSrc(&noizDens[BSIM3v0IDNOIZ],
				       &lnNdens[BSIM3v0IDNOIZ], ckt, THERMNOISE,
				       here->BSIM3v0dNodePrime,
				       here->BSIM3v0sNodePrime,
                                       (2.0 / 3.0 * fabs(here->BSIM3v0gm
				       + here->BSIM3v0gds) * here->BSIM3v0m));

			      }
		              NevalSrc(&noizDens[BSIM3v0FLNOIZ], (double*) NULL,
				       ckt, N_GAIN, here->BSIM3v0dNodePrime,
				       here->BSIM3v0sNodePrime, (double) 0.0);

                              if (model->BSIM3v0noiMod == 2)
			      {   vgs = *(ckt->CKTstates[0] + here->BSIM3v0vgs);
		                  vds = *(ckt->CKTstates[0] + here->BSIM3v0vds);
			          if (vds < 0.0)
			          {   vds = -vds;
				      vgs = vgs + vds;
			          }
                                  if (vgs >= here->BSIM3v0von + 0.1)
			          {   Ssi = StrongInversionNoiseEval3v0(vgs, vds,
					    model, here, data->freq,
					    ckt->CKTtemp);
                                      noizDens[BSIM3v0FLNOIZ] *= Ssi;
			          }
                                  else 
			          {   pParam = here->pParam;
				      T10 = model->BSIM3v0oxideTrapDensityA
					  * 8.62e-5 * (ckt->CKTtemp + CONSTCtoK);
		                      T11 = pParam->BSIM3v0weff * here->BSIM3v0m * pParam->BSIM3v0leff
				          * pow(data->freq, model->BSIM3v0ef)
				          * 4.0e36;
		                      Swi = T10 / T11 * here->BSIM3v0cd * here->BSIM3v0m
				          * here->BSIM3v0cd * here->BSIM3v0m;
                                      Slimit = StrongInversionNoiseEval3v0(
				           here->BSIM3v0von + 0.1,
				           vds, model, here,
				           data->freq, ckt->CKTtemp);
				      T1 = Swi + Slimit;
				      if (T1 > 0.0)
                                          noizDens[BSIM3v0FLNOIZ] *= (Slimit * Swi)
							        / T1; 
				      else
                                          noizDens[BSIM3v0FLNOIZ] *= 0.0;
			          }
		              }
                              else
			      {    noizDens[BSIM3v0FLNOIZ] *= model->BSIM3v0kf * 
				            exp(model->BSIM3v0af
					    * log(MAX(fabs(here->BSIM3v0cd * here->BSIM3v0m),
					    N_MINLOG)))
					    / (pow(data->freq, model->BSIM3v0ef)
					    * pParam->BSIM3v0leff
				            * pParam->BSIM3v0leff
					    * model->BSIM3v0cox);
			      }

		              lnNdens[BSIM3v0FLNOIZ] =
				     log(MAX(noizDens[BSIM3v0FLNOIZ], N_MINLOG));

		              noizDens[BSIM3v0TOTNOIZ] = noizDens[BSIM3v0RDNOIZ]
						     + noizDens[BSIM3v0RSNOIZ]
						     + noizDens[BSIM3v0IDNOIZ]
						     + noizDens[BSIM3v0FLNOIZ];
		              lnNdens[BSIM3v0TOTNOIZ] = 
				     log(MAX(noizDens[BSIM3v0TOTNOIZ], N_MINLOG));

		              *OnDens += noizDens[BSIM3v0TOTNOIZ];

		              if (data->delFreq == 0.0)
			      {   /* if we haven't done any previous 
				     integration, we need to initialize our
				     "history" variables.
				    */

			          for (i = 0; i < BSIM3v0NSRCS; i++)
				  {    here->BSIM3v0nVar[LNLSTDENS][i] =
					     lnNdens[i];
			          }

			          /* clear out our integration variables
				     if it's the first pass
				   */
			          if (data->freq ==
				      ((NOISEAN*) ckt->CKTcurJob)->NstartFreq)
				  {   for (i = 0; i < BSIM3v0NSRCS; i++)
				      {    here->BSIM3v0nVar[OUTNOIZ][i] = 0.0;
				           here->BSIM3v0nVar[INNOIZ][i] = 0.0;
			              }
			          }
		              }
			      else
			      {   /* data->delFreq != 0.0,
				     we have to integrate.
				   */
			          for (i = 0; i < BSIM3v0NSRCS; i++)
				  {    if (i != BSIM3v0TOTNOIZ)
				       {   tempOnoise = Nintegrate(noizDens[i],
						lnNdens[i],
				                here->BSIM3v0nVar[LNLSTDENS][i],
						data);
				           tempInoise = Nintegrate(noizDens[i]
						* data->GainSqInv, lnNdens[i]
						+ data->lnGainInv,
				                here->BSIM3v0nVar[LNLSTDENS][i]
						+ data->lnGainInv, data);
				           here->BSIM3v0nVar[LNLSTDENS][i] =
						lnNdens[i];
				           data->outNoiz += tempOnoise;
				           data->inNoise += tempInoise;
				           if (((NOISEAN*)
					       ckt->CKTcurJob)->NStpsSm != 0)
					   {   here->BSIM3v0nVar[OUTNOIZ][i]
						     += tempOnoise;
				               here->BSIM3v0nVar[OUTNOIZ][BSIM3v0TOTNOIZ]
						     += tempOnoise;
				               here->BSIM3v0nVar[INNOIZ][i]
						     += tempInoise;
				               here->BSIM3v0nVar[INNOIZ][BSIM3v0TOTNOIZ]
						     += tempInoise;
                                           }
			               }
			          }
		              }
		              if (data->prtSummary)
			      {   for (i = 0; i < BSIM3v0NSRCS; i++)
				  {    /* print a summary report */
			               data->outpVector[data->outNumber++]
					     = noizDens[i];
			          }
		              }
		              break;
		         case INT_NOIZ:
			      /* already calculated, just output */
		              if (((NOISEAN*)ckt->CKTcurJob)->NStpsSm != 0)
			      {   for (i = 0; i < BSIM3v0NSRCS; i++)
				  {    data->outpVector[data->outNumber++]
					     = here->BSIM3v0nVar[OUTNOIZ][i];
			               data->outpVector[data->outNumber++]
					     = here->BSIM3v0nVar[INNOIZ][i];
			          }
		              }
		              break;
		      }
		      break;
	         case N_CLOSE:
		      /* do nothing, the main calling routine will close */
		      return (OK);
		      break;   /* the plots */
	      }       /* switch (operation) */
	 }    /* for here */
    }    /* for model */

    return(OK);
}



