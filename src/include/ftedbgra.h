/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1987 Jeffrey M. Hsu
**********/

/*
    $Header: /cvsroot/ngspice/ngspice/ng-spice-rework/src/include/ftedbgra.h,v 1.1.2.1 2002/11/26 11:39:40 stefanjones Exp $

    External definitions for the graph database module.
*/

extern GRAPH *currentgraph;

extern GRAPH *NewGraph();

extern GRAPH *FindGraph();

extern GRAPH *CopyGraph();
