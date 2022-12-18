#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <values.h>
#include <string.h>
#include <unistd.h>
#include "hilbert.h"

// generate hilbert curves

#define	MINORDER	1u
#define	MAXORDER	8u

#define CMAX    251
void
rnd_color (void)
{
	static unsigned int cr = 128;
	static unsigned int cg = 128;
	static unsigned int cb = 128;

	cr += 23;
	if (cr > CMAX) {
		cr -= CMAX;
		cg += 27;
		if (cg > CMAX) {
			cg -= CMAX;
			cb += 29;
			if (cb > CMAX)
				cb -= CMAX;
		}
	}
	printf ("Color %u %u %u\n", cr, cg, cb);
}

static inline int
point (int idx, int scale)
{
	return (idx * scale) + (scale / 2);
}

void
plot_hilbert (unsigned int order, int scale)
{
	unsigned int i;
	unsigned int x1, y1, x2, y2;

	printf ("Layer %u\n", (order - MINORDER) + 1);
	rnd_color ();
	hilbert (0, order, &x1, &y1);
	//printf("Text %d %d 0 %d %x\n",point(x1,scale),point(y1,scale),scale/8,0);
	for (i = 1u; i < (1u << order) * (1u << order); i++) {
		hilbert (i, order, &x2, &y2);
		printf ("Line %d %d %d %d\n", point (x1, scale), point (y1, scale), point (x2, scale), point (y2, scale));
		//printf("Text %d %d 0 %d %x\n",point(x2,scale),point(y2,scale),scale/8,i);
		x1 = x2;
		y1 = y2;
	}
}

int
main (int argc, char **argv)
{
	unsigned int order;
	int scale = 1 << (MAXORDER + 3);

	(void) argc;
	(void) argv;
	printf ("Width 1\n");
	for (order = MINORDER; order <= MAXORDER; order++, scale >>= 1)
		plot_hilbert (order, scale);
	return 0;
}
