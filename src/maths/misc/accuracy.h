/**********
Copyright 1991 Regents of the University of California. All rights reserved.
Authors: 1987 Karti Mayaram, 1991 David Gates
$Id: accuracy.h,v 1.2 2005/05/31 16:37:22 sjborley Exp $
**********/

#ifndef ACCURACY_H
#define ACCURACY_H

/*
 * Definitions of Globals for Machine Accuracy Limits
 */

extern double BMin;          /* lower limit for B(x) */
extern double BMax;          /* upper limit for B(x) */
extern double ExpLim;        /* limit for exponential */
extern double Accuracy;      /* accuracy of the machine */

extern void evalAccLimits(void);

#endif /* ACCURACY_H */
