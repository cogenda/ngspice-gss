/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1987 Jeffrey M. Hsu
**********/

/*
    $Header: /cvsroot/ngspice/ngspice/ng-spice-rework/src/include/ftedbgra.h,v 1.1 2000/04/27 20:04:01 pnenzi Exp $

    External definitions for the graph database module.
*/

extern GRAPH *currentgraph;

extern GRAPH *NewGraph();

extern GRAPH *FindGraph();

extern GRAPH *CopyGraph();
