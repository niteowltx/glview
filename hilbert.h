
// Find the x,y coordinates for a point on the hilbert curve of
// a specified order.  Order is the number of bits used for each
// axis.  When idx is zero, the returned coordinates are <0,0>
// Order should be no greater than half the number of bits in
// an unsigned int.
// Only the lower order*2 bits of idx are used.
static inline void
hilbert (const unsigned int idx, const unsigned int order, unsigned int *hx, unsigned int *hy)
{
	unsigned int i;
	unsigned int v;
	unsigned int tilt = 0;
	unsigned int x = 0;
	unsigned int y = 0;
	static unsigned int next_tilt[4][4] = {
		{3, 0, 0, 1},
		{2, 1, 1, 0},
		{1, 2, 2, 3},
		{0, 3, 3, 2},
	};
	static unsigned int next_x[4][4] = {
		{0, 1, 1, 0},
		{1, 1, 0, 0},
		{1, 0, 0, 1},
		{0, 0, 1, 1},
	};
	static unsigned int next_y[4][4] = {
		{0, 0, 1, 1},
		{1, 0, 0, 1},
		{1, 1, 0, 0},
		{0, 1, 1, 0},
	};

	for (i = order; i != 0; i--) {
		v = (idx >> ((i - 1) * 2)) & 3;
		x <<= 1;
		y <<= 1;
		x |= next_x[tilt][v];
		y |= next_y[tilt][v];
		tilt = next_tilt[tilt][v];
	}
	*hx = x;
	*hy = y;
}
