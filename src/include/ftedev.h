/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1987 Jeffrey M. Hsu
**********/

/*
    $Header: /cvsroot/ngspice/ngspice/ng-spice-rework/src/include/ftedev.h,v 1.1.2.2 2003/12/05 13:56:46 stefanjones Exp $

    The display device structure.
*/

#ifndef FTEDEV_H_INCLUDED
#define FTEDEV_H_INCLUDED


typedef struct {
    char *name;
    int minx, miny;
    int width, height;      /* in screen coordinate system */
    int numlinestyles, numcolors;   /* number supported */
    int (*Init)();
    int (*NewViewport)();
    int (*Close)();
    int (*Clear)();
    int (*DrawLine)();
    int (*Arc)();
    int (*Text)();
    int (*DefineColor)();
    int (*DefineLinestyle)();
    int (*SetLinestyle)();
    int (*SetColor)();
    int (*Update)();
/*  int (*NDCtoScreen)(); */
    int (*Track)();
    int (*MakeMenu)();
    int (*MakeDialog)();
    int (*Input)();
    void (*DatatoScreen)();
} DISPDEVICE;

extern DISPDEVICE *dispdev;


#endif
