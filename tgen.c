#include <stdio.h>
#include <string.h>

#define	SCALE	10
#define	COL	400

#define	CMAX	251
void
rnd_color (void)
{
	static int cr = 128;
	static int cg = 128;
	static int cb = 128;

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
	printf ("Color %d %d %d\n", cr, cg, cb);
}

void
rnd_layer (void)
{
	static int l = 0;

	printf ("Layer %d\n", l + 1);
	l += 31;
	l %= 12;
}

int
main (int argc, char **argv)
{
	char buf[1024];
	int x = 0, y = 0;

	(void) argc; (void) argv;
	printf("Scale %d\n",SCALE);
	while (fgets (buf, sizeof (buf), stdin) != NULL) {
		buf[strlen (buf) - 1] = ' ';
		rnd_color ();
		rnd_layer ();
		printf ("Text %d %d \"%s\"\n", x * SCALE, y * (SCALE + (SCALE / 2)), buf);
		x += strlen (buf);
		if (x >= COL) {
			y--;
			x = 0;
		}
	}
	return 0;
}
