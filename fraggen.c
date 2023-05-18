#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <values.h>
#include <string.h>
#include <unistd.h>
#include "util.h"

//	alloc/free random memory sizes and report final allocated values

typedef struct frag{
	void	*addr;
	unsigned long	len;
} frag_t;

#define	MAXFRAGS (1<<16)	// number of fragments to show

frag_t	Frags[MAXFRAGS];

#define	FRAG_COUNT	1000000		// try to malloc this many times
unsigned long int	Minfrag = 0;
unsigned long int	Maxfrag = 19;

void	*Minaddr = (void *)~0;
void	*Maxaddr = 0;

static inline void
frag_minmax(const frag_t *f)
{
	if( f->addr==NULL || f->len==0 )
		return;
	if( f->addr < Minaddr )
		Minaddr = f->addr;
	if( f->addr > Maxaddr )
		Maxaddr = f->addr;
}

static inline void
frag_fill(frag_t *f)
{
	if(f->addr)	// replacing a previous allocation
		free(f->addr);

	f->len = 1<<rnd_range(Minfrag,Maxfrag);
	f->addr = must_malloc(f->len);
}

// print 'address len' pairs.  Subtract Minaddr from all addresses
static inline void
frag_print(const frag_t *f)
{
	if( f->addr && f->len )
		printf("%llx %lx\n",(unsigned long long int)f->addr-(unsigned long long int)Minaddr,f->len);
}

int
main(int argc, char **argv)
{
	unsigned int i;

	(void)argc; (void)argv;
	for(i=0;i<FRAG_COUNT;i++)	// replace random slot
		frag_fill(&Frags[rnd(MAXFRAGS)]);
	for(i=0;i<MAXFRAGS;i++)		// establish min/max
		frag_minmax(&Frags[i]);
	printf("# Minaddr:%llx Maxaddr:%llx Range:%llx\n",(unsigned long long)Minaddr,(unsigned long long)Maxaddr, (unsigned long long)(Maxaddr-Minaddr));
	for(i=0;i<MAXFRAGS;i++)		// print all slots
		frag_print(&Frags[i]);
	return 0;
}
