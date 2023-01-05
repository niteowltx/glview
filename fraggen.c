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

#define	MAXFRAGS (1<<16)
frag_t	Frags[MAXFRAGS];

#define	FRAG_COUNT	1000000
unsigned long int	Minfrag = 1<<0;
unsigned long int	Maxfrag = 1<<12;

void	*Minaddr = (void *)~0;
void	*Maxaddr = 0;

static inline void
frags_slot(const unsigned int i)
{
	if(Frags[i].addr)
		free(Frags[i].addr);

	Frags[i].len = rnd_range(Minfrag,Maxfrag);
	Frags[i].addr = must_malloc(Frags[i].len);
	if( Frags[i].addr < Minaddr ){
		Minaddr = Frags[i].addr;
		printf("# New Min:%llx Len:%lx\n",(unsigned long long int)Minaddr,Frags[i].len);
		}
	if( Frags[i].addr > Maxaddr ){
		Maxaddr = Frags[i].addr;
		printf("# New Max:%llx Len:%lx\n",(unsigned long long int)Maxaddr,Frags[i].len);
		}
}

static inline void
frags_print(const unsigned int i)
{
	printf("%llx %lx\n",(unsigned long long int)Frags[i].addr,Frags[i].len);
}

int
main(int argc, char **argv)
{
	unsigned int i;

	(void)argc; (void)argv;
	for(i=0;i<MAXFRAGS;i++)	// fill all slots
		frags_slot(i);
	for(i=0;i<FRAG_COUNT;i++)	// replace random slot
		frags_slot(rnd(MAXFRAGS));
	for(i=0;i<MAXFRAGS;i++)	// print all slots
		frags_print(i);
	printf("# Min:%llx Max:%llx\n",(unsigned long long)Minaddr,(unsigned long long)Maxaddr);
	return 0;
}
