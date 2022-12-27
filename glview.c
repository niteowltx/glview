#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <values.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <GL/freeglut.h>		// if missing: apt-get install freeglut3-dev

// Missing mouse defines?
#define	GLUT_WHEEL_UP_BUTTON	3	// scroll wheel up
#define	GLUT_WHEEL_DOWN_BUTTON	4	// scroll wheel down
#define	GLUT_WHEEL_LEFT_BUTTON	5	// tilt scroll wheel to left
#define	GLUT_WHEEL_RIGHT_BUTTON	6	// tilt scroll wheel to right
#define	GLUT_WHEEL_PLUS		7	// mouse '+' button
#define	GLUT_WHEEL_MINUS	8	// mouse '-' button

//
//      glview --- simple OpenGL based 2d drawing viewer
//
//      Input is a text file of 2D drawing primitives:
//
//              Line x1 y1 x2 y2
//              Point x1 y1
//              Rectangle x1 y1 x2 y2
//              Circle x1 y1 radius
//              Arc x1 y1 radius start_angle delta_angle
//              Triangle x1 y1 x2 y2 x3 y3
//              Text x1 y1 angle scale "string"
//
//              Color cr cg cb          # 0-255 for each color
//              Fill                    # Rectangle, Circle, Triangle are filled
//              Wire                    # Rectangle, Circle, Triangle are wire-frame
//              Width w                 # Line, Point, Arc, Text width is 'w' (min arc width is always 2)
//              Layer n                 # Draw on layer n (n=1-12)
//
//      coordinates (x1,y1) (x2,y2) (x3,y3) in signed int range (+/-2000000000)
//      angle   degrees, for Text:
//               0      left to right
//               90     bottom to top
//               180    right to left
//               270    top to bottom
//      scale           text scale factor (when scale=N each letter fills an NxN unit square) max=1000000
//      start_angle     degree to start drawing arc
//      delta_angle     number of degrees to draw (+ccw, -cw)
//
//      Starting defaults:
//              Color 255 255 255
//              Wire
//              Width 1
//              Layer 1
//
//      When running:
//              click and drag to move the view
//              Zoom in/out with the mouse wheel (hold ctrl key for finer zoom)
//              'q'/ESC         quit
//              'a'             all layers on
//              F1-F12          toggle layer 1-12 (alternative: 1-9,0 for layers 1-10)
//              Home            return to original view
//              Left/Right      Rotate X (+Ctrl for finer change)
//              Up/Down         Rotate Y (+Ctrl for finer change)
//              PgUp/PgDn       Rotate Z (+Ctrl for finer change)
//

#define	MAXBUF		10240	// max input line length
#define	MAXTOKENS	100	// max tokens on any line
#define	MAXSTRING	1024	// max string length anywhere
#define	STRSAVE_SIZE	(1<<20)	// pool alloc size, refill when it gets below MAXSTRING

#define	LARGE		2000000000	// largest allowed coordinate value
#define	MIN_TEXT_SCALE	0.00954	// scale that makes text fill a 1x1 unit (1/104.76)
#define	STROKE_FONT	GLUT_STROKE_MONO_ROMAN	// or GLUT_STROKE_ROMAN
#define	BITMAP_FONT	GLUT_BITMAP_9_BY_15	// or TIMES_ROMAN_24, HELVETICA_18
#define	ZOOM_MIN	0.0001
#define	ZOOM_MAX	100.0
#define	ZOOM_STEP	0.81
#define	ZOOM_STEP_FINE	0.95
#define	MAX_WIDTH	1024
#define	MAX_HEIGHT	1024
#define	ROT_STEP	(360.0/(double)64)	// image rotation increment
#define	ROT_STEP_FINE	(ROT_STEP/4)	// image rotation increment fine
#define	CIRCLE_STEPS	128	// how many lines to draw a circle?
#define	TWO_PI		(M_PI*2)
#define	MAX_LAYERS	12
#define	LAYER_SEP	100

#define	DEF_LINE_WIDTH	1	// default line (and point) width
#define	DEF_RED		255
#define	DEF_GREEN	255
#define	DEF_BLUE	255
#define	DEF_POLY	GL_LINE	// GL_FILL or GL_LINE

#define	ltoz(l)		(l*(-LAYER_SEP))	// layer# to z depth
#define	dtor(angle)	((((double)(angle))/180)*M_PI)	// degrees to radians

// Viewing controls
double Zoom = 1.0;
double RotX = 0.0;
double RotY = 0.0;
double RotZ = 0.0;
double PanX = 0.0;
double PanY = 0.0;

double Zoom_min = ZOOM_MIN;
int Layer[MAX_LAYERS + 1] = { 0 };

char *Title = "Viewer";

double Zoom_home;		// original Zoom
double PanX_home;		// original PanX
double PanY_home;		// original PanY

// mouse drag
int Moveactive = 0;
int Movex, Movey;

// bounding rectangle
int Maxx = -(LARGE - 1);
int Maxy = -(LARGE - 1);
int Minx = LARGE + 1;
int Miny = LARGE + 1;

int Width = DEF_LINE_WIDTH;	// current line width
int Fill = GL_FILL;		// current fill mode

// Display object types
#define	TYPE_NONE	0
#define	TYPE_LINE	1
#define	TYPE_POINT	2
#define	TYPE_RECT	3
#define	TYPE_CIRCLE	4
#define	TYPE_ARC	5
#define	TYPE_TRIANGLE	6
#define	TYPE_TEXT	7
#define	TYPE_COLOR	8
#define	TYPE_FILL	9
#define	TYPE_WIDTH	10

struct object
{
	struct object *next;
	struct object *prev;
	int type;
	int layer;
	int arg[6];		// maximum # of integer arguments
	char *text;		// at most, a single text argument
};
#define	X1	o->arg[0]
#define	Y1	o->arg[1]
#define	X2	o->arg[2]
#define	Y2	o->arg[3]
#define	X3	o->arg[4]
#define	Y3	o->arg[5]
#define	CR	o->arg[0]
#define	CG	o->arg[1]
#define	CB	o->arg[2]
#define	WIDTH	o->arg[0]
#define	ROTATE	o->arg[2]
#define	RADIUS	o->arg[2]
#define	DSTART	o->arg[3]
#define	DDELTA	o->arg[4]
#define	SCALE	o->arg[3]
#define	FILL	o->arg[0]
#define	TEXT	o->text
#define	LAYER	o->layer

struct object Objects = {
	.next = &Objects,
	.prev = &Objects,
	.type = TYPE_NONE,
};

// Utilities --------------------------------------------------------------------

//      fatal --- print error message and exit
void
fatal (char *format, ...)
{
	va_list args;

	printf ("FATAL ERROR: ");
	va_start (args, format);
	vprintf (format, args);
	va_end (args);
	printf ("\n");
	exit (1);
}

//      error --- print error message
void
error (char *format, ...)
{
	va_list args;

	printf ("ERROR: ");
	va_start (args, format);
	vprintf (format, args);
	va_end (args);
	printf ("\n");
}

// malloc or die
static inline void *
must_malloc (int size)
{
	void *vp = malloc (size);

	if (vp == NULL)
		fatal ("Can't malloc %d bytes", size);
	return vp;
}

// zalloc or die
static inline void *
must_zalloc (int size)
{
	void *vp = must_malloc (size);

	memset (vp, 0, size);
	return vp;
}

//
//      strsave --- save a string somewhere safe
//
//      A copy of the string is made in a malloc'ed area.  The return
//      pointer points to the copy.  The returned pointer CANNOT be
//      released via free().
//
char *
strsave (char *s)
{
	char *p;
	static int mleft = 0;
	static char *sbuf = NULL;

	if (mleft < (MAXSTRING + 1)) {
		sbuf = (char *) must_malloc (STRSAVE_SIZE);
		mleft = STRSAVE_SIZE;
	}

	p = sbuf;
	if (sbuf == NULL || s == NULL)
		return NULL;
	while ((*sbuf++ = *s++) != 0);
	mleft -= sbuf - p;
	return p;
}

static inline char *
skipwhite (char *s)
{
	while (*s) {
		if (!isspace (*s))
			break;
		s++;
	}
	return s;
}

//
//      tokenize --- split string into argv[] style array of pointers
//
//      Modifies 's' by inserting '\0'.  The tokens array will have a NULL
//      added at the end of the real tokens.
//
int
tokenize (char *s, char **tokens, int ntokens)
{
	char **itokens = tokens;

	ntokens--;		// use one for NULL at end
	s = skipwhite (s);
	while (*s != '\n' && *s != '\0') {
		if (s[0] == '/' && s[1] == '/')	// rest of line is comment
			break;
		if (--ntokens == 0)
			break;	// way too many tokens on this line
		if (*s == '"') {	// handle quoted string
			s++;
			*tokens++ = s;
			while (*s != '\n' && *s != '"')
				s++;
		}
		else {
			*tokens++ = s;
			while (!isspace (*s))
				s++;
		}
		*s++ = '\0';
		s = skipwhite (s);
	}
	*tokens = NULL;		// mark end of tokens
	return tokens - itokens;
}

// Doubly linked lists --------------------------------------------------------------------

// Walk every element in a list from the head, using o as the list item
#define	OBJECT_WALK(head,o)	for((o)=(head)->next; (o) != (head); (o)=(o)->next)

static inline struct object *
object_alloc (void)
{
	struct object *o = must_zalloc (sizeof (*o));

	o->next = o->prev = o;
	o->type = TYPE_NONE;
	o->text = NULL;
	return o;
}

static inline struct object *
object_new (int type, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, char *text, int layer)
{
	struct object *o = object_alloc ();

	o->type = type;
	o->arg[0] = arg1;
	o->arg[1] = arg2;
	o->arg[2] = arg3;
	o->arg[3] = arg4;
	o->arg[4] = arg5;
	o->arg[5] = arg6;
	o->text = text;
	o->layer = layer;
	return o;
}

static inline void
object_add (struct object *a, struct object *b)
{
	struct object *an = a->next;

	b->next = an;
	b->prev = a;
	a->next = an->prev = b;
}

static inline void
object_remove (struct object *o)
{
	o->next->prev = o->prev;
	o->prev->next = o->next;
}

// --------------------------------------------------------------------

void
home_view (void)
{
	Zoom = Zoom_home;
	PanX = PanX_home;
	PanY = PanY_home;
	RotX = RotY = RotZ = 0.0;
}

// zoom in or out, keeping center at (x,y)
static void
set_zoom (double zoom, int x, int y)
{
	if (zoom < Zoom_min || zoom > ZOOM_MAX)
		return;		// at the limit so ignore it
	PanX -= (x / Zoom) - (x / zoom);
	PanY += (y / Zoom) - (y / zoom);
	Zoom = zoom;
}

static double
range360 (double deg)
{
	while (deg < 0)
		deg += 360;
	while (deg >= 360)
		deg -= 360;
	return deg;
}

// adjust rotation by delta degrees (rotx,roty,rotz), keeping center at (x,y)
static void
set_rot (int rotx, int roty, int rotz, int x, int y)
{
	double oldx = (x / Zoom) - PanX;			// convert from screen point to model <x,y>
	double oldy = (y / Zoom) + PanY;
	double dist = sqrt ((oldx * oldx) + (oldy * oldy));	// convert from rectangular to polar
	double angle = atan2 (oldy, oldx);
	double newx = cos (angle + dtor (rotz)) * (dist);	// find new x,y after rotation
	double newy = sin (angle + dtor (rotz)) * (dist);
	double dx = oldx - newx;	// how far did it move
	double dy = oldy - newy;

	PanX -= dx;
	PanY += dy;

	if (rotx)
		RotX = range360 (RotX + rotx);
	if (roty)
		RotY = range360 (RotY + roty);
	if (rotz)
		RotZ = range360 (RotZ + rotz);
}

static inline int
clamp (int v, int minv, int maxv)
{
	if (v < minv)
		return minv;
	if (v > maxv)
		return maxv;
	return v;
}

// limit x coordinates to -LARGE,LARGE range
static inline int
xcoord (char *s)
{
	return clamp (atoi (s), -LARGE, LARGE);
}

// limit y coordinates to -LARGE,LARGE range
static inline int
ycoord (char *s)
{
	return clamp (atoi (s), -LARGE, LARGE);
}

// limit colors to 0-255
static inline int
color (char *s)
{
	return clamp (atoi (s), 0, 255);
}

// limit scale
static inline int
scale (char *s)
{
	return clamp (atoi (s), 1, 1 << 20);
}

// limit radius to 1-LARGE
static inline int
radius (char *s)
{
	return clamp (atoi (s), 1, LARGE);
}

// limit angles to 0-360
static inline int
angle (char *s)
{
	return clamp (atoi (s), 0, 359);
}

// limit delta angles to +/-360
static inline int
dangle (char *s)
{
	return clamp (atoi (s), -360, 360);
}

// limit layer to 1-MAX_LAYERS
static inline int
layer (char *s)
{
	return clamp (atoi (s), 1, MAX_LAYERS);
}

static inline void
min_max_point (int x, int y)
{
	if (x < Minx)
		Minx = x;
	if (x > Maxx)
		Maxx = x;
	if (y < Miny)
		Miny = y;
	if (y > Maxy)
		Maxy = y;
}

void
all_layers_on (void)
{
	int i;

	for (i = 1; i <= MAX_LAYERS; i++)
		Layer[i] = 1;
}

// read input file, build display object list
static void
Init (FILE * fp)
{
	char buf[MAXBUF];
	char *tokens[MAXTOKENS];
	struct object *o;
	int w, h;
	int cur_layer = 1;

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		o = NULL;
		switch (tokenize (buf, tokens, MAXTOKENS)) {
		case 1:
			if (strcasecmp (tokens[0], "fill") == 0)
				o = object_new (TYPE_FILL, GL_FILL, 0, 0, 0, 0, 0, NULL, cur_layer);
			else if (strcasecmp (tokens[0], "wire") == 0)
				o = object_new (TYPE_FILL, GL_LINE, 0, 0, 0, 0, 0, NULL, cur_layer);
			break;
		case 2:
			if (strcasecmp (tokens[0], "width") == 0) {
				o = object_new (TYPE_WIDTH, scale (tokens[1]), 0, 0, 0, 0, 0, NULL, cur_layer);
			}
			if (strcasecmp (tokens[0], "layer") == 0) {
				cur_layer = layer (tokens[1]);
			}
			break;
		case 3:
			if (strcasecmp (tokens[0], "point") == 0)
				o = object_new (TYPE_POINT, xcoord (tokens[1]), ycoord (tokens[2]), 0, 0, 0, 0, NULL, cur_layer);
			break;
		case 4:
			if (strcasecmp (tokens[0], "circle") == 0)
				o = object_new (TYPE_CIRCLE, xcoord (tokens[1]), ycoord (tokens[2]), radius (tokens[3]), 0, 0, 0, NULL, cur_layer);
			else if (strcasecmp (tokens[0], "color") == 0) {
				o = object_new (TYPE_COLOR, color (tokens[1]), color (tokens[2]), color (tokens[3]), 0, 0, 0, NULL, cur_layer);
			}
			break;
		case 5:
			if (strcasecmp (tokens[0], "rectangle") == 0)
				o = object_new (TYPE_RECT, xcoord (tokens[1]), ycoord (tokens[2]), xcoord (tokens[3]), ycoord (tokens[4]), 0, 0, NULL, cur_layer);
			else if (strcasecmp (tokens[0], "line") == 0)
				o = object_new (TYPE_LINE, xcoord (tokens[1]), ycoord (tokens[2]), xcoord (tokens[3]), ycoord (tokens[4]), 0, 0, NULL, cur_layer);
			break;
		case 6:
			if (strcasecmp (tokens[0], "text") == 0)
				o = object_new (TYPE_TEXT, xcoord (tokens[1]), ycoord (tokens[2]), angle (tokens[3]), scale (tokens[4]), 0, 0, strsave (tokens[5]), cur_layer);
			else if (strcasecmp (tokens[0], "arc") == 0)
				o = object_new (TYPE_ARC, xcoord (tokens[1]), ycoord (tokens[2]), radius (tokens[3]), angle (tokens[4]), dangle (tokens[5]), 0, NULL, cur_layer);
			break;
		case 7:
			if (strcasecmp (tokens[0], "triangle") == 0)
				o = object_new (TYPE_TRIANGLE, xcoord (tokens[1]), ycoord (tokens[2]), xcoord (tokens[3]), ycoord (tokens[4]), xcoord (tokens[5]), ycoord (tokens[6]), NULL, cur_layer);
			break;
		default:
			break;
		}
		if (o != NULL)
			object_add (Objects.prev, o);
	}

	// find bounding rectangle for all primitives
	OBJECT_WALK (&Objects, o) {
		switch (o->type) {
		case TYPE_LINE:
		case TYPE_RECT:
			min_max_point (X1, Y1);
			min_max_point (X2, Y2);
			break;
		case TYPE_CIRCLE:
		case TYPE_ARC:
			min_max_point (X1 - RADIUS, Y1 - RADIUS);
			min_max_point (X1 + RADIUS, Y1 + RADIUS);
			break;
		case TYPE_TRIANGLE:
			min_max_point (X1, Y1);
			min_max_point (X2, Y2);
			min_max_point (X3, Y3);
			break;
		case TYPE_POINT:
			min_max_point (X1, Y1);
			break;
		case TYPE_TEXT:
			min_max_point (X1, Y1);
			w = strlen (TEXT) * SCALE;
			h = SCALE;
			switch (ROTATE) {
			case 0:
				min_max_point (X1 + w, Y1 + h);
				break;
			case 90:
				min_max_point (X1 - h, Y1 + w);
				break;
			case 180:
				min_max_point (X1 - w, Y1 - h);
				break;
			case 270:
				min_max_point (X1 + h, Y1 - w);
				break;
			}
			break;
		}
	}
	if (Maxx < Minx || Maxy < Miny)
		exit (0);	// nothing to draw

	// Increase boundary by 10%
	Minx -= (Maxx - Minx) / 20;
	Miny -= (Maxy - Miny) / 20;
	Maxx += (Maxx - Minx) / 20;
	Maxy += (Maxy - Miny) / 20;

	// Add a gray background rectangle
	o = object_new (TYPE_RECT, Minx, Miny, Maxx, Maxy, 0, 0, NULL, 1);
	object_add (&Objects, o);	// insert at front of list so this becomes the background rectangle

	all_layers_on ();
}

static inline int
is_shift_pressed (void)
{
	return glutGetModifiers () & GLUT_ACTIVE_SHIFT;
}

static inline int
is_ctrl_pressed (void)
{
	return glutGetModifiers () & GLUT_ACTIVE_CTRL;
}

static inline int
is_alt_pressed (void)
{
	return glutGetModifiers () & GLUT_ACTIVE_ALT;
}

static inline void
bitmap_output (char *s)
{
	glRasterPos2f (0, 0);
	while (*s)
		glutBitmapCharacter (BITMAP_FONT, *s++);
}

static inline void
stroke_output (char *s)
{
	while (*s)
		glutStrokeCharacter (STROKE_FONT, *s++);
}

static void
glPrintf (int x, int y, int rot, int scale, int z, char *format, ...)
{
	va_list args;
	static char buffer[20000];

	va_start (args, format);
	vsprintf (buffer, format, args);
	va_end (args);

	glPushMatrix ();
	glTranslatef ((float) x, (float) y, (float) z);
	glRotatef ((float) rot, 0, 0, 1);
	glScalef (MIN_TEXT_SCALE * scale, MIN_TEXT_SCALE * scale, MIN_TEXT_SCALE * scale);
	stroke_output (buffer);
	glPopMatrix ();
}

static void
glLinei (int x1, int y1, int x2, int y2, int width, int z)
{
	width /= 2;

	if (width <= 0) {
		glBegin (GL_LINES);
		glVertex3i (x1, y1, z);
		glVertex3i (x2, y2, z);
		glEnd ();
		return;
	}
	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);	// Lines are always filled
	if ((x1 <= x2 && y1 == y2) || (x1 == x2 && y1 < y2)) {
		glBegin (GL_POLYGON);
		glVertex3i (x1 - width, y1 - width, z);
		glVertex3i (x1 - width, y1 + width, z);
		glVertex3i (x2 + width, y2 + width, z);
		glVertex3i (x2 + width, y2 - width, z);
		glEnd ();
	}
	else if ((x1 > x2 && y1 == y2) || (x1 == x2 && y1 > y2)) {
		glBegin (GL_POLYGON);
		glVertex3i (x2 - width, y2 - width, z);
		glVertex3i (x2 - width, y2 + width, z);
		glVertex3i (x1 + width, y1 + width, z);
		glVertex3i (x1 + width, y1 - width, z);
		glEnd ();
	}
	else {
		double angle = atan2 ((double) (y2 - y1), (double) (x2 - x1));
		int t2sina = (int) (width * sin (angle));
		int t2cosa = (int) (width * cos (angle));

		glBegin (GL_TRIANGLES);
		glVertex3i (x1 + t2sina, y1 - t2cosa, z);
		glVertex3i (x2 + t2sina, y2 - t2cosa, z);
		glVertex3i (x2 - t2sina, y2 + t2cosa, z);
		glVertex3i (x2 - t2sina, y2 + t2cosa, z);
		glVertex3i (x1 - t2sina, y1 + t2cosa, z);
		glVertex3i (x1 + t2sina, y1 - t2cosa, z);
		glEnd ();
	}
	glPolygonMode (GL_FRONT_AND_BACK, Fill);
}

static void
glArci (int x, int y, int radius, int dstart, int ddelta, int z)
{
	double angle_start = dtor ((dstart - 90) % 360);
	double angle_delta = dtor (ddelta);
	double angle_end = angle_start;
	double angle;
	int arcx_outer[CIRCLE_STEPS + 1];
	int arcy_outer[CIRCLE_STEPS + 1];
	int arcx_inner[CIRCLE_STEPS + 1];
	int arcy_inner[CIRCLE_STEPS + 1];
	int step = 0;
	int i;
	int w = (Width >= 2) ? Width : 2;	// arcs of width <2 would be invisible

	if (angle_delta >= 0)
		angle_end += angle_delta;
	else
		angle_start += angle_delta;

	for (angle = angle_start; angle < angle_end; angle += (TWO_PI / CIRCLE_STEPS), step++) {
		arcx_outer[step] = (int) (sin (angle) * (radius + (w / 2)));
		arcy_outer[step] = (int) (cos (angle) * (radius + (w / 2)));
		arcx_inner[step] = (int) (sin (angle) * (radius - (w / 2)));
		arcy_inner[step] = (int) (cos (angle) * (radius - (w / 2)));
	}
	arcx_outer[step] = (int) (sin (angle_end) * (radius + (w / 2)));
	arcy_outer[step] = (int) (cos (angle_end) * (radius + (w / 2)));
	arcx_inner[step] = (int) (sin (angle_end) * (radius - (w / 2)));
	arcy_inner[step] = (int) (cos (angle_end) * (radius - (w / 2)));

	if (step > 0 && arcx_outer[step] == arcx_outer[step - 1] && arcy_outer[step] == arcy_outer[step - 1])
		step--;

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);	// arcs are always filled
	for (i = 0; i < step; i++) {
		glBegin (GL_POLYGON);
		glVertex3i (x + arcx_outer[i + 0], y + arcy_outer[i + 0], z);
		glVertex3i (x + arcx_outer[i + 1], y + arcy_outer[i + 1], z);
		glVertex3i (x + arcx_inner[i + 1], y + arcy_inner[i + 1], z);
		glVertex3i (x + arcx_inner[i + 0], y + arcy_inner[i + 0], z);
		glEnd ();
	}
	glPolygonMode (GL_FRONT_AND_BACK, Fill);
}

static void
glCirclei (int x, int y, int radius, int z)
{
	double angle;

	glBegin (GL_POLYGON);
	for (angle = 0; angle < TWO_PI; angle += (TWO_PI / CIRCLE_STEPS))
		glVertex3i (x + (int) (sin (angle) * radius), y + (int) (cos (angle) * radius), z);
	glEnd ();
}

static void
glTrianglei (int x1, int y1, int x2, int y2, int x3, int y3, int z)
{
	glBegin (GL_TRIANGLES);
	glVertex3i (x1, y1, z);
	glVertex3i (x2, y2, z);
	glVertex3i (x3, y3, z);
	glEnd ();
}

static void
my_glRecti (int x1, int y1, int x2, int y2, int z)
{
	glBegin (GL_POLYGON);
	glVertex3i (x1, y1, z);
	glVertex3i (x2, y1, z);
	glVertex3i (x2, y2, z);
	glVertex3i (x1, y2, z);
	glEnd ();
}


// walk the object primitive list and put them into the display buffer
static void
Render (void)
{
	struct object *o;

	glLineWidth ((float) DEF_LINE_WIDTH);
	glPointSize ((float) DEF_LINE_WIDTH);
	Width = DEF_LINE_WIDTH;
	glColor3ub (DEF_RED, DEF_GREEN, DEF_BLUE);
	glPolygonMode (GL_FRONT_AND_BACK, DEF_POLY);

	OBJECT_WALK (&Objects, o) {
		switch (o->type) {
		case TYPE_LINE:
			if (Layer[LAYER] == 0)
				continue;
			glLinei (X1, Y1, X2, Y2, Width, ltoz (LAYER));
			break;
		case TYPE_POINT:
			if (Layer[LAYER] == 0)
				continue;
			glCirclei (X1, Y1, Width / 2, ltoz (LAYER));
			break;
		case TYPE_RECT:
			if (Layer[LAYER] == 0)
				continue;
			my_glRecti (X1, Y1, X2, Y2, ltoz (LAYER));
			break;
		case TYPE_TEXT:
			if (Layer[LAYER] == 0)
				continue;
			glPrintf (X1, Y1, ROTATE, SCALE, ltoz (LAYER), TEXT);
			break;
		case TYPE_TRIANGLE:
			if (Layer[LAYER] == 0)
				continue;
			glTrianglei (X1, Y1, X2, Y2, X3, Y3, ltoz (LAYER));
			break;
		case TYPE_CIRCLE:
			if (Layer[LAYER] == 0)
				continue;
			glCirclei (X1, Y1, RADIUS, ltoz (LAYER));
			break;
		case TYPE_ARC:
			if (Layer[LAYER] == 0)
				continue;
			glArci (X1, Y1, RADIUS, DSTART, DDELTA, ltoz (LAYER));
			break;
		case TYPE_COLOR:
			glColor3ub (CR, CG, CB);
			break;
		case TYPE_WIDTH:
			glLineWidth ((float) WIDTH);
			glPointSize ((float) WIDTH);
			Width = WIDTH;
			break;
		case TYPE_FILL:
			Fill = FILL;
			glPolygonMode (GL_FRONT_AND_BACK, FILL);
			break;
		}
	}
}

static void
Motion (int x, int y)
{
	if (!Moveactive)
		return;

	PanX += (x - Movex) / Zoom;
	PanY += (Movey - y) / Zoom;
	Movex = x;
	Movey = y;
	glutPostRedisplay ();
}

static void
Mouse (int button, int state, int x, int y)
{
	switch (button) {
	case GLUT_WHEEL_UP_BUTTON:
		if (state == GLUT_UP)
			return;
		set_zoom (Zoom / (is_ctrl_pressed ()? ZOOM_STEP_FINE : ZOOM_STEP), x, y);
		break;
	case GLUT_WHEEL_DOWN_BUTTON:
		if (state == GLUT_UP)
			return;
		set_zoom (Zoom * (is_ctrl_pressed ()? ZOOM_STEP_FINE : ZOOM_STEP), x, y);
		break;
	case GLUT_LEFT_BUTTON:
		if (state == GLUT_DOWN) {
			Movex = x;
			Movey = y;
			Moveactive = 1;
		}
		else {
			Moveactive = 0;
		}
		break;
	case GLUT_MIDDLE_BUTTON:
		if (state == GLUT_UP)
			return;
		break;
	case GLUT_RIGHT_BUTTON:
		if (state == GLUT_UP)
			return;
		break;
	default:
		printf ("Mouse? button:%d state:%d x:%d y:%d\n", button, state, x, y);
		break;
	}
	glutPostRedisplay ();
}

static void
Draw (void)
{
	glPushMatrix ();
	glMatrixMode (GL_PROJECTION);
	glClearColor (0.0, 0.0, 0.0, 0.0);
	glClear (GL_COLOR_BUFFER_BIT);
	glScalef (Zoom, Zoom, Zoom);
	glTranslatef (PanX, PanY, 0.0);
	glRotatef (RotX, 1, 0, 0);
	glRotatef (RotY, 0, 1, 0);
	glRotatef (RotZ, 0, 0, 1);
	Render ();
	glPopMatrix ();
	glutSwapBuffers ();
}

static void
Key (unsigned char key, int x, int y)
{
	(void) x;
	(void) y;
	switch (key) {
	case 'a':
		all_layers_on ();
		break;
	case '1':
		Layer[1] = !Layer[1];
		break;
	case '2':
		Layer[2] = !Layer[2];
		break;
	case '3':
		Layer[3] = !Layer[3];
		break;
	case '4':
		Layer[4] = !Layer[4];
		break;
	case '5':
		Layer[5] = !Layer[5];
		break;
	case '6':
		Layer[6] = !Layer[6];
		break;
	case '7':
		Layer[7] = !Layer[7];
		break;
	case '8':
		Layer[8] = !Layer[8];
		break;
	case '9':
		Layer[9] = !Layer[9];
		break;
	case '0':
		Layer[10] = !Layer[10];
		break;
	case 'q':
	case 27:		// ESC
		exit (0);
	default:
		printf ("Key? key:%d x:%d y:%d\n", key,x,y);
		break;
	}
	glutPostRedisplay ();
}

static void
SpecialKey (int key, int x, int y)
{
	switch (key) {
	case GLUT_KEY_LEFT:
		set_rot (0, is_ctrl_pressed ()? ROT_STEP_FINE : ROT_STEP, 0, x, y);
		break;
	case GLUT_KEY_RIGHT:
		set_rot (0, is_ctrl_pressed ()? -ROT_STEP_FINE : -ROT_STEP, 0, x, y);
		break;
	case GLUT_KEY_UP:
		set_rot (is_ctrl_pressed ()? ROT_STEP_FINE : ROT_STEP, 0, 0, x, y);
		break;
	case GLUT_KEY_DOWN:
		set_rot (is_ctrl_pressed ()? -ROT_STEP_FINE : -ROT_STEP, 0, 0, x, y);
		break;
	case GLUT_KEY_F1:
		Layer[1] = !Layer[1];
		break;
	case GLUT_KEY_F2:
		Layer[2] = !Layer[2];
		break;
	case GLUT_KEY_F3:
		Layer[3] = !Layer[3];
		break;
	case GLUT_KEY_F4:
		Layer[4] = !Layer[4];
		break;
	case GLUT_KEY_F5:
		Layer[5] = !Layer[5];
		break;
	case GLUT_KEY_F6:
		Layer[6] = !Layer[6];
		break;
	case GLUT_KEY_F7:
		Layer[7] = !Layer[7];
		break;
	case GLUT_KEY_F8:
		Layer[8] = !Layer[8];
		break;
	case GLUT_KEY_F9:
		Layer[9] = !Layer[9];
		break;
	case GLUT_KEY_F10:
		Layer[10] = !Layer[10];
		break;
	case GLUT_KEY_F11:
		Layer[11] = !Layer[11];
		break;
	case GLUT_KEY_F12:
		Layer[12] = !Layer[12];
		break;
	case GLUT_KEY_PAGE_UP:
		set_rot (0, 0, is_ctrl_pressed ()? ROT_STEP_FINE : ROT_STEP, x, y);
		break;
	case GLUT_KEY_PAGE_DOWN:
		set_rot (0, 0, is_ctrl_pressed ()? -ROT_STEP_FINE : -ROT_STEP, x, y);
		break;
	case GLUT_KEY_HOME:
		home_view ();
		break;
	case GLUT_KEY_END:
		break;
	case GLUT_KEY_INSERT:
		break;
	case GLUT_KEY_SHIFT_L:
	case GLUT_KEY_SHIFT_R:
	case GLUT_KEY_CTRL_L:
	case GLUT_KEY_CTRL_R:
		break;
	default:
		printf ("SpecialKey? key:%d x:%d y:%d\n", key,x,y);
		break;
	}
	glutPostRedisplay ();
}

static void
Reshape (int width, int height)
{
	glViewport (0, 0, width, height);
	glMatrixMode (GL_PROJECTION);	// Start modifying the projection matrix.
	glLoadIdentity ();	// Reset project matrix.
	glOrtho (0, width, 0, height, -((MAX_LAYERS + 1) * LAYER_SEP * 100), (MAX_LAYERS + 1) * LAYER_SEP * 100);	// Map abstract coords directly to window coords.
	//glScalef(1, -1, 1);                   // Invert Y axis so increasing Y goes down.
	glTranslatef (0, height, 0);	// Shift origin up to upper-left corner.
}

static void
WindowSetup (void)
{
	int width = Maxx - Minx;
	int height = Maxy - Miny;
	double zx, zy;		// zoom required to fit in x and y directions

	// if the image is too big for a maximum window, adjust Zoom to make it initially fit
	if (width > MAX_WIDTH || height > MAX_HEIGHT) {
		zx = MAX_WIDTH / (double) width;
		zy = MAX_HEIGHT / (double) height;
		Zoom = zx < zy ? zx : zy;
		if (Zoom < ZOOM_MIN)
			Zoom = ZOOM_MIN;
		width *= Zoom;
		height *= Zoom;
	}
	Zoom_min = Zoom / 2;	// limit zoom out to half the initial window size

	PanX_home = PanX = -Minx;
	PanY_home = PanY = -Maxy;
	Zoom_home = Zoom;
	RotX = RotY = RotZ = 0.0;

	glEnable (GL_LINE_SMOOTH);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glutInitDisplayMode (GLUT_RGB | GLUT_DOUBLE);
	glutInitWindowSize (width, height);
	glutCreateWindow (Title);
}

int
main (int argc, char **argv)
{
	FILE *fp;

	glutInit (&argc, argv);
	if (argc > 1) {
		Title = argv[1];
		if ((fp = fopen (argv[1], "r")) != NULL) {
			Init (fp);
			fclose (fp);
		}
	}
	else
		Init (stdin);

	WindowSetup ();

	glutReshapeFunc (Reshape);
	glutKeyboardFunc (Key);
	glutSpecialFunc (SpecialKey);
	glutMouseFunc (Mouse);
	glutMotionFunc (Motion);
	glutDisplayFunc (Draw);
	glutMainLoop ();
	return 0;
}
