## Process this file with automake to produce Makefile.in
#
# JW 3/9/01 - had a go and makeing an   autoconf script.

noinst_LIBRARIES = libipcxsp.a

libipcxsp_a_SOURCES = \
ipcaegis.c   \
ipc.c        \
ipcsockets.c \
ipcstdio.c   \
ipctiein.c

INCLUDES = -I$(top_srcdir)/src/include -I$(top_srcdir)/src/spicelib/devices

MAINTAINERCLEANFILES = Makefile.in
