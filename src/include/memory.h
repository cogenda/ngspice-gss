#ifndef _MEMORY_H
#define _MEMORY_H

#ifndef HAVE_LIBGC
extern void *tmalloc(size_t num);
extern void *trealloc(void *str, size_t num);
extern void txfree(void *ptr);

#define tfree(x) (txfree(x), x = 0)

#else
#include <gc/gc.h>

#define tmalloc(m) GC_malloc(m)
#define trealloc(m,n) GC_realloc((m),(n))
#define tfree(m)
#define txfree(m)
#endif

#define alloc(TYPE) ((TYPE *) tmalloc(sizeof(TYPE)))
#define MALLOC(x) tmalloc((unsigned)(x))
#define FREE(x) {if (x) {txfree((char *)(x));(x) = 0;}}
#define REALLOC(x,y) trealloc((char *)(x),(unsigned)(y))
#define ZERO(PTR,TYPE)	(bzero((PTR),sizeof(TYPE)))

#endif