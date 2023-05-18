#include <math.h>
#include "util.h"

#define GL_GLEXT_PROTOTYPES	// not the default?
#include <GL/freeglut.h>	// if missing: apt-get install freeglut3-dev
// Missing GLUT mouse defines?
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
//              Text x1 y1 "string"
//              Image x1 y1 "filename"
//
//              Layer n                 # Draw on layer n (n=1-12)
//              Color cr cg cb          # 0-255 for each color
//              Fill                    # Rectangle, Circle, Triangle are filled
//              Wire                    # Rectangle, Circle, Triangle are wire-frame
//              Width w                 # Line, Point, Arc, Text width is 'w' (min arc width is always 2)
//		Rotate angle		# Subsequent items are rotated by 'angle' (0, 90, 180, 270 only)
//		Scale factor		# text and images are scaled up by this factor
//
//      coordinates (x1,y1) (x2,y2) (x3,y3) in signed int range (+/-2000000000)
//      scale           text scale factor (when scale=N each letter fills an NxN unit square) max=1000000
//      start_angle     degree to start drawing arc
//      delta_angle     number of degrees to draw (+ccw, -cw)
//
//      Starting defaults:
//              Layer 1
//              Color 255 255 255
//              Wire
//              Width 1
//		Rotate 0
//		Scale 1
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

#define	LARGE		2000000000		// coordinates are limited to [-LARGE,+LARGE]
#define	MIN_TEXT_SCALE	0.00954			// scale that makes text fill a 1x1 unit (1/104.76)
#define	STROKE_FONT	GLUT_STROKE_MONO_ROMAN	// or GLUT_STROKE_ROMAN
#define	BITMAP_FONT	GLUT_BITMAP_9_BY_15	// or TIMES_ROMAN_24, HELVETICA_18
#define	ZOOM_MIN	0.0001
#define	ZOOM_MAX	100.0
#define	ZOOM_STEP	0.81			// gui zoom increment
#define	ZOOM_STEP_FINE	0.95			// gui zoom increment fine
#define	ROT_STEP	(360.0/(double)64)	// gui image rotation increment
#define	ROT_STEP_FINE	(ROT_STEP/4)		// gui image rotation increment fine
#define	INIT_MAX_WIDTH	1024			// gui initial window max width
#define	INIT_MAX_HEIGHT	1024			// gui initial window max height
#define	CIRCLE_STEPS	128			// number of line segments in a circle
#define	TWO_PI		(M_PI*2)
#define	MAX_LAYERS	12			// layer toggle mapped to keyboard F-keys
#define	LAYER_SEP	100			// z-axis layer separation
#define	MAX_POINTS	3			// max number of (x,y) points per object

// Viewing controls
double Zoom = 1.0;
double PanX = 0.0;
double PanY = 0.0;
double RotX = 0.0;
double RotY = 0.0;
double RotZ = 0.0;

double Zoom_min = ZOOM_MIN;

bool Layer[MAX_LAYERS + 1];

char *Title = "Viewer";

// Original Pan, Zoom values.  Rot[XYZ]_home are always 0
double Zoom_home;
double PanX_home;
double PanY_home;

// mouse drag
bool Moveactive = false;
int MoveX;
int MoveY;

// bounding rectangle
int Maxx = -(LARGE - 1);
int Maxy = -(LARGE - 1);
int Minx = LARGE + 1;
int Miny = LARGE + 1;

// Display object types
typedef enum otype {
	TYPE_NONE = 0,
	TYPE_LINE,
	TYPE_POINT,
	TYPE_RECT,
	TYPE_CIRCLE,
	TYPE_ARC,
	TYPE_TRIANGLE,
	TYPE_TEXT,
	TYPE_IMAGE,		//  x3,y3 hold image width,height
} otype_t;

typedef struct object
{
	struct object	*next;
	struct object	*prev;
	otype_t		type;
	int		x[MAX_POINTS];	// x,y for each point in object
	int		y[MAX_POINTS];
	int		cr,cg,cb;
	int		layer;
	int		z;		// layer converted to Z-axis value
	int		width;
	int		scale;
	bool		fill;
	int		rotate,radius,dstart,ddelta;
	const char	*text;		// text to display, filename when TYPE_IMAGE
	uint8_t		*image;		// raw RGB image bytes
	GLuint		texture;	// image converted to a texture object
} object_t;

object_t Root = {
	.next = &Root,
	.prev = &Root,
	.type = TYPE_NONE,
	.cr     = 255,
	.cg     = 255,
	.cb     = 255,
	.layer  = 0,
	.z	= 0,
	.width  = 1,
	.scale  = 1,
	.fill   = false,
	.rotate = 0,
	.radius = 1,
	.dstart = 0,
	.ddelta = 0,
	.text   = "",
};


// Walk every object, using o as the list item
#define	OBJECT_WALK(o)	for((o)=Root.next; (o) != (&Root); (o)=(o)->next)

// create a new object of 'type' and initialize it from the passed default object
static inline object_t *
object_new (const int type, const object_t *def)
{
	object_t *o = (object_t *)must_zalloc (sizeof (*o));

	o->next	= o->prev = o;
	o->type		= type;
	o->layer	= def->layer;
	o->z		= def->z;
	o->fill		= def->fill;
	o->scale	= def->scale;
	o->rotate	= def->rotate;
	o->width	= def->width;
	o->cr		= def->cr;
	o->cg		= def->cg;
	o->cb		= def->cb;
	return o;
}

// insert b just after a
static inline void
object_add (object_t *a, object_t *b)
{
	object_t *an = a->next;

	b->next = an;
	b->prev = a;
	a->next = an->prev = b;
}

// add object to front of global list
static inline void
object_add_front (object_t *o)
{
	object_add(&Root, o);
}

// add object to end of global list
static inline void
object_add_end (object_t *o)
{
	object_add(Root.prev, o);
}

static inline void
object_print(const object_t *o)
{
	char *tname = "???";
	unsigned int i;

	switch(o->type){
	case TYPE_NONE:		tname = "NONE"; break;
	case TYPE_LINE:		tname = "LINE"; break;
	case TYPE_POINT:	tname = "POINT"; break;
	case TYPE_RECT:		tname = "RECT"; break;
	case TYPE_CIRCLE:	tname = "CIRCLE"; break;
	case TYPE_ARC:		tname = "ARC"; break;
	case TYPE_TRIANGLE:	tname = "TRIANGLE"; break;
	case TYPE_TEXT:		tname = "TEXT"; break;
	case TYPE_IMAGE:	tname = "IMAGE"; break;
	}

	printf("%10s ",tname);
	for(i=0;i<MAX_POINTS;i++)
		printf("(%d,%d) ",o->x[i],o->y[i]);
	if(o->cr)     printf("Red:%d ",o->cr);
	if(o->cg)     printf("Grn:%d ",o->cg);
	if(o->cb)     printf("Blu:%d ",o->cb);
	if(o->layer)  printf("Lyr:%d ",o->layer);
	if(o->width)  printf("Wid:%d ",o->width);
	if(o->scale)  printf("Scl:%d ",o->scale);
	if(o->fill)   printf("Fil:%d ",o->fill);
	if(o->rotate) printf("Rot:%d ",o->rotate);
	if(o->radius) printf("Rad:%d ",o->radius);
	if(o->dstart) printf("Dst:%d ",o->dstart);
	if(o->ddelta) printf("Ddl:%d ",o->ddelta);
	if(o->text)   printf("<%s> ",o->text);
	if(o->texture)printf("Ttr:%d ",o->texture);
	printf("\n");
}

// Set one of the the (x,y) points in an object
static inline void
object_set_xy(object_t *o, const unsigned int idx, const int x, const int y)
{
	if( idx >= MAX_POINTS )
		fatal("%s idx:%d",__FUNCTION__, idx);
	o->x[idx] = x;
	o->y[idx] = y;
}

// layer# to z depth (higher layer #'s block lower)
static inline int
ltoz(const int l)
{
	return l * -LAYER_SEP;
}

// degrees to radians
static inline double
dtor(const int angle)
{
	return (((double)(angle))/180.0)*M_PI;
}

void
home_view (void)
{
	Zoom = Zoom_home;
	PanX = PanX_home;
	PanY = PanY_home;
	RotX = RotY = RotZ = 0.0;
}

// zoom in or out, keeping center at (x,y)
static inline void
set_zoom (const double zoom, const int x, const int y)
{
	if (zoom < Zoom_min || zoom > ZOOM_MAX)
		return;		// at the limit so ignore it
	PanX -= (x / Zoom) - (x / zoom);
	PanY += (y / Zoom) - (y / zoom);
	Zoom = zoom;
}

static inline double
range360 (double deg)
{
	while (deg < 0)
		deg += 360;
	while (deg >= 360)
		deg -= 360;
	return deg;
}

// adjust rotation by delta degrees (rotx,roty,rotz), keeping center at (x,y)
static inline void
set_rot (const int rotx, const int roty, const int rotz, const int x, const int y)
{
	double oldx = (x / Zoom) - PanX;			// convert from screen point to model <x,y>
	double oldy = (y / Zoom) + PanY;
	double dist = sqrt ((oldx * oldx) + (oldy * oldy));	// convert from rectangular to polar
	double angle = atan2 (oldy, oldx);
	double newx = cos (angle + dtor (rotz)) * (dist);	// find new x,y after rotation
	double newy = sin (angle + dtor (rotz)) * (dist);
	double dx = oldx - newx;				// how far did it move
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

// force v to be in the range [minv,maxv]
static inline int
clamp (const int v, const int minv, const int maxv)
{
	if (v < minv)
		return minv;
	if (v > maxv)
		return maxv;
	return v;
}

// limit coordinates to -LARGE,LARGE range
static inline int
coord (const char *s)
{
	return clamp (atoi (s), -LARGE, LARGE);
}

// limit colors to 0-255
static inline int
color (const char *s)
{
	return clamp (atoi (s), 0, 255);
}

// limit scale
static inline int
scale (const char *s)
{
	return clamp (atoi (s), 1, 1 << 20);
}

// limit radius to 1-LARGE
static inline int
radius (const char *s)
{
	return clamp (atoi (s), 1, LARGE);
}

// limit angles to whole degrees
static inline int
angle (const char *s)
{
	return clamp (atoi (s), 0, 359);
}

// limit delta angles to +/-360
static inline int
dangle (const char *s)
{
	return clamp (atoi (s), -360, 360);
}

// limit layer to 1-MAX_LAYERS
static inline int
layer (const char *s)
{
	return clamp (atoi (s), 1, MAX_LAYERS);
}

static inline void
one_layer(const unsigned int layer, const bool val)
{
	if( layer <= MAX_LAYERS )
		Layer[layer] = val;
}

static inline void
all_layers (const bool val)
{
	unsigned int i;

	for (i = 0; i <= MAX_LAYERS; i++)
		one_layer(i,val);
}

static inline void
layer_toggle(const int l)
{
	if( l>=1 && l <= MAX_LAYERS )
		Layer[l] = !Layer[l];
}

static inline bool
layer_visible(const object_t *o)
{
	return Layer[o->layer];
}

// check for OpenGL error conditions
static inline void
err_check(const char *tag)
{
	int err = glGetError();
	char *s;

	switch(err){
	case GL_NO_ERROR:			return;
	case GL_INVALID_ENUM:			s="INVALID_ENUM"; break;
	case GL_INVALID_VALUE:			s="INVALID_VALUE"; break;
	case GL_INVALID_OPERATION:		s="INVALID_OPERATION"; break;
	case GL_INVALID_FRAMEBUFFER_OPERATION:	s="INVALID_FRAMEBUFFER_OPERATION"; break;
	case GL_OUT_OF_MEMORY:			s="OUT_OF_MEMORY"; break;
	case GL_STACK_UNDERFLOW:		s="STACK_UNDERFLOW"; break;
	case GL_STACK_OVERFLOW:			s="STACK_OVERFLOW"; break;
	default:				s="UNKNOWN"; break;
	}
	
	fatal("%s: %x %s\n",tag,err,s);
}

// load image from file
// uses ImageMagick identify and convert
static inline void
image_load(object_t *o)
{
	FILE	*fp;
	char	tmp[MAXBUF];
	int	w=0,h=0;
	int	img_bytes = 0;
	uint8_t	*img;
	uint8_t	*p;
	size_t	nread;
	int	remaining;

	o->image = NULL;
	sprintf(tmp,"identify -format \"%%w %%h\\n\" %s 2>/dev/null\n",o->text);
	fp = popen(tmp,"r");
	if( fp==NULL )
		fatal("identify open image %s\n",o->text);
	if( fgets(tmp,sizeof(tmp),fp) == NULL )
		fatal("identify read image %s\n",o->text);
	pclose(fp);
	if( sscanf(tmp,"%d %d",&w,&h) != 2 )
		fatal("identify scan image %s\n",o->text);
	if( w<=0 || h<=0 )
		fatal("invalid image size %s  w:%d h:%d" ,o->text,w,h);

	img_bytes = w*h*3;
	img = must_malloc(img_bytes);

	// use ImageMagick to convert to raw RGB and also flip the image vertically
	sprintf(tmp,"convert %s -size 3 -depth 8 -flip RGB:- 2>/dev/null",o->text);
	fp = popen(tmp,"r");
	for(p=img, remaining=img_bytes; remaining; remaining -= nread, p += nread){
		nread = fread(p,1,remaining,fp);
		if( nread == 0 )
			break;
		}
	pclose(fp);
	if( remaining != 0 )
		fatal("read image %s, remaining:%d",o->text,remaining);

	object_set_xy(o,2,w,h);	// keep actual image size here
	o->image = img;
}

// convert image bytes into a texture (must be called after glutInit and glutCreateWindow)
static inline void
image_finalize (object_t *o)
{
	glGenTextures(1, &o->texture);
	glBindTexture(GL_TEXTURE_2D, o->texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, o->x[2], o->y[2], 0, GL_RGB, GL_UNSIGNED_BYTE, o->image);
	glGenerateMipmap(GL_TEXTURE_2D);
	free(o->image);
	o->image = NULL;
}

// update global boundary to include this point
static inline void
min_max_point (const int x, const int y)
{
	if (x < Minx) Minx = x;
	if (x > Maxx) Maxx = x;
	if (y < Miny) Miny = y;
	if (y > Maxy) Maxy = y;
}

// set x[1],y[1] for rectangular object at x[0],y[0]
// object should have x[2] and y[2] set to the unscaled width and height of the object
static inline void
set_xy2(object_t *o)
{
	int sw  = o->x[2] * o->scale;		// apply scaling factor
	int sh  = o->y[2] * o->scale;

	switch (o->rotate) {
	case 0:
		object_set_xy(o,1,o->x[0]+sw,o->y[0]+sh);
		break;
	case 90:
		object_set_xy(o,1,o->x[0]-sh,o->y[0]+sw);
		break;
	case 180:
		object_set_xy(o,1,o->x[0]-sw,o->y[0]-sh);
		break;
	case 270:
		object_set_xy(o,1,o->x[0]+sh,o->y[0]-sw);
		break;
	}
}

// read input file, add to object list
static inline void
object_scan (FILE * fp)
{
	object_t *o;
	static object_t cur = {
		.type = TYPE_NONE,
		.layer = 1,
		.cr = 255,
		.cg = 255,
		.cb = 255,
		.rotate = 0,
		.scale = 1,
		.width = 1,
		.fill = false,
	};
	char buf[MAXBUF];
	char *tokens[MAXTOKENS];

	while (fgets (buf, sizeof (buf), fp) != NULL) {
		o = NULL;
		switch( tokenize (buf, tokens, MAXTOKENS) ){
		case 1:
			if (strcasecmp (tokens[0], "fill") == 0){
				cur.fill = true;
				}
			else if (strcasecmp (tokens[0], "wire") == 0){
				cur.fill = false;
				}
			break;
		case 2:
			if (strcasecmp (tokens[0], "width") == 0) {
				cur.width = scale(tokens[1]);
				}
			else if (strcasecmp (tokens[0], "layer") == 0) {
				cur.layer = layer (tokens[1]);
				}
			else if (strcasecmp (tokens[0], "rotate") == 0) {
				cur.rotate = angle (tokens[1]);
				}
			else if (strcasecmp (tokens[0], "scale") == 0) {
				cur.scale = scale (tokens[1]);
				}
			break;
		case 3:
			if (strcasecmp (tokens[0], "point") == 0){
				o = object_new (TYPE_POINT, &cur);
				object_set_xy(o,0,coord(tokens[1]),coord(tokens[2]));
				}
			break;
		case 4:
			if (strcasecmp (tokens[0], "circle") == 0){
				o = object_new (TYPE_CIRCLE, &cur);
				object_set_xy(o,0,coord(tokens[1]),coord(tokens[2]));
				o->radius = radius(tokens[3]);
				object_set_xy(o,1,o->x[0] - o->radius, o->y[0] - o->radius);	// lower left of bounding square
				object_set_xy(o,2,o->x[0] + o->radius, o->y[0] + o->radius);	// upper right of bounding square
				}
			else if (strcasecmp (tokens[0], "color") == 0) {
				cur.cr = color(tokens[1]);
				cur.cg = color(tokens[2]);
				cur.cb = color(tokens[3]);
				}
			else if (strcasecmp (tokens[0], "image") == 0){
				o = object_new (TYPE_IMAGE, &cur);
				o->fill = true;			// images are always filled
				object_set_xy(o,0,coord(tokens[1]),coord(tokens[2]));
				o->text = strdup(tokens[3]);	// filename
				image_load(o);			// sets x[2] and y[2] to width, height of image
				set_xy2(o);
				}
			else if (strcasecmp (tokens[0], "text") == 0){
				o = object_new (TYPE_TEXT, &cur);
				object_set_xy(o,0,coord(tokens[1]),coord(tokens[2]));
				o->text = strdup(tokens[3]);
				object_set_xy(o,2,strlen(o->text),1);
				set_xy2(o);
				}
			break;
		case 5:
			if (strcasecmp (tokens[0], "rectangle") == 0){
				o = object_new (TYPE_RECT, &cur);
				object_set_xy(o,0,coord(tokens[1]),coord(tokens[2]));
				object_set_xy(o,1,coord(tokens[3]),coord(tokens[4]));
				}
			else if (strcasecmp (tokens[0], "line") == 0){
				o = object_new (TYPE_LINE, &cur);
				o->fill = true;			// lines are always filled
				object_set_xy(o,0,coord(tokens[1]),coord(tokens[2]));
				object_set_xy(o,1,coord(tokens[3]),coord(tokens[4]));
				}
			break;
		case 6:
			if (strcasecmp (tokens[0], "arc") == 0){
				o = object_new (TYPE_ARC, &cur);
				o->fill = true;		// arcs are always filled
				object_set_xy(o,0,coord(tokens[1]),coord(tokens[2]));
				o->radius = radius(tokens[3]);
				o->dstart = angle(tokens[4]);
				o->ddelta = dangle(tokens[5]);
				object_set_xy(o,1,o->x[0] - o->radius, o->y[0] - o->radius);	// lower left of bounding square
				object_set_xy(o,2,o->x[0] + o->radius, o->y[0] + o->radius);	// upper right of bounding square
				}
			break;
		case 7:
			if (strcasecmp (tokens[0], "triangle") == 0){
				o = object_new (TYPE_TRIANGLE, &cur);
				object_set_xy(o,0,coord(tokens[1]),coord(tokens[2]));
				object_set_xy(o,1,coord(tokens[3]),coord(tokens[4]));
				object_set_xy(o,2,coord(tokens[5]),coord(tokens[6]));
				}
			break;
		default:
			break;
		}
		if (o != NULL)
			object_add_end (o);
	}
}

// finalize objects
static inline void
object_wrapup()
{
	object_t *o;
	object_t cur = {
		.layer = 0,	// NOTE: objects drawn on layer 0 are always visible
		.z = ltoz(0),
		.cr = 255,
		.cg = 255,
		.cb = 255,
		.rotate = 0,
		.scale = 1,
		.width = 1,
		.fill = false,
	};
	char buf[MAXBUF];

	// find bounding rectangle for all primitives
	OBJECT_WALK (o) {
		o->z = ltoz(o->layer);
		switch (o->type) {
		case TYPE_NONE:
			break;
		case TYPE_POINT:
			min_max_point (o->x[0], o->y[0]);
			break;
		case TYPE_LINE:
		case TYPE_RECT:
		case TYPE_TEXT:
		case TYPE_IMAGE:
			min_max_point (o->x[0], o->y[0]);
			min_max_point (o->x[1], o->y[1]);
			break;
		case TYPE_CIRCLE:	// 1=center, 2=ll, 3=ur
		case TYPE_ARC:
		case TYPE_TRIANGLE:
			min_max_point (o->x[0], o->y[0]);
			min_max_point (o->x[1], o->y[1]);
			min_max_point (o->x[2], o->y[2]);
			break;
		}
	}
	if (Maxx < Minx || Maxy < Miny){
		printf("Empty display list\n");
		exit (0);	// nothing to draw
	}

	// Increase boundary by 10%
	Minx -= (Maxx - Minx) / 20;
	Miny -= (Maxy - Miny) / 20;
	Maxx += (Maxx - Minx) / 20;
	Maxy += (Maxy - Miny) / 20;

	// Show dimensions of the box on layer zero
	o = object_new(TYPE_TEXT,&cur);
	sprintf(buf,"%d x %d",Maxx-Minx,Maxy-Miny);
	o->scale = clamp((Maxx-Minx)/50, 10, 10000);
	o->text = strdup(buf);
	object_set_xy(o,0,Minx,Miny-(o->scale*2));
	object_add_front (o);

	// Show border rectangle on layer zero
	o = object_new (TYPE_RECT, &cur);
	object_set_xy(o,0,Minx,Miny);
	object_set_xy(o,1,Maxx,Maxy);
	object_add_front (o);

	all_layers(true);
}

static inline bool
is_shift_pressed (void)
{
	return !!(glutGetModifiers () & GLUT_ACTIVE_SHIFT);
}

static inline bool
is_ctrl_pressed (void)
{
	return !!(glutGetModifiers () & GLUT_ACTIVE_CTRL);
}

static inline bool
is_alt_pressed (void)
{
	return !!(glutGetModifiers () & GLUT_ACTIVE_ALT);
}

static inline void
render_line (const object_t *o)
{
	const int w = o->width/2;

	if (w <= 0) {
		glBegin (GL_LINES);
		glVertex3i (o->x[0], o->y[0], o->z);
		glVertex3i (o->x[1], o->y[1], o->z);
		glEnd ();
		return;
	}
	if ((o->x[0] <= o->x[1] && o->y[0] == o->y[1]) || (o->x[0] == o->x[1] && o->y[0] < o->y[1])) {
		glBegin (GL_POLYGON);
		glVertex3i (o->x[0] - w, o->y[0] - w, o->z);
		glVertex3i (o->x[0] - w, o->y[0] + w, o->z);
		glVertex3i (o->x[1] + w, o->y[1] + w, o->z);
		glVertex3i (o->x[1] + w, o->y[1] - w, o->z);
		glEnd ();
	}
	else if ((o->x[0] > o->x[1] && o->y[0] == o->y[1]) || (o->x[0] == o->x[1] && o->y[0] > o->y[1])) {
		glBegin (GL_POLYGON);
		glVertex3i (o->x[1] - w, o->y[1] - w, o->z);
		glVertex3i (o->x[1] - w, o->y[1] + w, o->z);
		glVertex3i (o->x[0] + w, o->y[0] + w, o->z);
		glVertex3i (o->x[0] + w, o->y[0] - w, o->z);
		glEnd ();
	}
	else {
		double angle = atan2 ((double) (o->y[1] - o->y[0]), (double) (o->x[1] - o->x[0]));
		int t2sina = (int) (w * sin (angle));
		int t2cosa = (int) (w * cos (angle));

		glBegin (GL_TRIANGLES);
		glVertex3i (o->x[0] + t2sina, o->y[0] - t2cosa, o->z);
		glVertex3i (o->x[1] + t2sina, o->y[1] - t2cosa, o->z);
		glVertex3i (o->x[1] - t2sina, o->y[1] + t2cosa, o->z);
		glVertex3i (o->x[1] - t2sina, o->y[1] + t2cosa, o->z);
		glVertex3i (o->x[0] - t2sina, o->y[0] + t2cosa, o->z);
		glVertex3i (o->x[0] + t2sina, o->y[0] - t2cosa, o->z);
		glEnd ();
	}
}

static inline void
render_rectangle (const object_t *o)
{
	glBegin (GL_POLYGON);
	glVertex3i (o->x[0], o->y[0], o->z);
	glVertex3i (o->x[1], o->y[0], o->z);
	glVertex3i (o->x[1], o->y[1], o->z);
	glVertex3i (o->x[0], o->y[1], o->z);
	glEnd ();
}

static inline void
bitmap_output (const char *s)
{
	glRasterPos2f (0, 0);
	while (*s)
		glutBitmapCharacter (BITMAP_FONT, *s++);
}

static inline void
stroke_output (const char *s)
{
	while (*s)
		glutStrokeCharacter (STROKE_FONT, *s++);
}

static inline void
render_text (const object_t *o)
{
	glPushMatrix ();
	glTranslatef ((float) o->x[0], (float) o->y[0], (float) o->z);
	glScalef (MIN_TEXT_SCALE * o->scale, MIN_TEXT_SCALE * o->scale, MIN_TEXT_SCALE * o->scale);
	stroke_output (o->text);	// alternate: bitmap_output()
	glPopMatrix ();
}

static inline void
render_image (const object_t *o)
{
	glPushMatrix ();
	glBindTexture(GL_TEXTURE_2D, o->texture);
	glEnable(GL_TEXTURE_2D);
//	glRotatef (o->rotate, 0, 0, 1);
//	glTranslatef ((float) o->x[0], (float) o->y[0], (float) o->z);

	glBegin(GL_TRIANGLES);
	glTexCoord2f(0,0);	glVertex3i(o->x[0],o->y[0],o->z);	// lower right triangle
	glTexCoord2f(1,0);	glVertex3i(o->x[1],o->y[0],o->z);
	glTexCoord2f(0,1);	glVertex3i(o->x[0],o->y[1],o->z);
	glTexCoord2f(1,0);	glVertex3i(o->x[1],o->y[0],o->z);	// upper left triangle
	glTexCoord2f(1,1);	glVertex3i(o->x[1],o->y[1],o->z);
	glTexCoord2f(0,1);	glVertex3i(o->x[0],o->y[1],o->z);

#if 0
	switch(o->rotate){
	case 0:
		glTexCoord2f(0,0);	glVertex3i(o->x[0],o->y[0],o->z);	// lower right triangle
		glTexCoord2f(1,0);	glVertex3i(o->x[1],o->y[0],o->z);
		glTexCoord2f(0,1);	glVertex3i(o->x[0],o->y[1],o->z);
		glTexCoord2f(1,0);	glVertex3i(o->x[1],o->y[0],o->z);	// upper left triangle
		glTexCoord2f(1,1);	glVertex3i(o->x[1],o->y[1],o->z);
		glTexCoord2f(0,1);	glVertex3i(o->x[0],o->y[1],o->z);
		break;
	case 90:
		glTexCoord2f(0,0);	glVertex3i(o->x[0],o->y[0],o->z);	// lower right triangle
		glTexCoord2f(1,0);	glVertex3i(o->x[0],o->y[1],o->z);
		glTexCoord2f(0,1);	glVertex3i(o->x[1],o->y[0],o->z);
		glTexCoord2f(1,0);	glVertex3i(o->x[0],o->y[1],o->z);	// upper left triangle
		glTexCoord2f(1,1);	glVertex3i(o->x[1],o->y[1],o->z);
		glTexCoord2f(0,1);	glVertex3i(o->x[1],o->y[0],o->z);
		break;
	case 180:
		glTexCoord2f(0,0);	glVertex3i(o->x[0],o->y[0],o->z);	// lower right triangle
		glTexCoord2f(1,0);	glVertex3i(o->x[1],o->y[0],o->z);
		glTexCoord2f(0,1);	glVertex3i(o->x[0],o->y[1],o->z);
		glTexCoord2f(1,0);	glVertex3i(o->x[1],o->y[0],o->z);	// upper left triangle
		glTexCoord2f(1,1);	glVertex3i(o->x[1],o->y[1],o->z);
		glTexCoord2f(0,1);	glVertex3i(o->x[0],o->y[1],o->z);
		break;
	case 270:
		glTexCoord2f(0,0);	glVertex3i(o->x[0],o->y[0],o->z);	// lower right triangle
		glTexCoord2f(1,0);	glVertex3i(o->x[1],o->y[0],o->z);
		glTexCoord2f(0,1);	glVertex3i(o->x[0],o->y[1],o->z);
		glTexCoord2f(1,0);	glVertex3i(o->x[1],o->y[0],o->z);	// upper left triangle
		glTexCoord2f(1,1);	glVertex3i(o->x[1],o->y[1],o->z);
		glTexCoord2f(0,1);	glVertex3i(o->x[0],o->y[1],o->z);
		break;
	}
#endif
	glEnd();

	glDisable(GL_TEXTURE_2D);
	glPopMatrix ();
}

static inline void
render_triangle (const object_t *o)
{
	glBegin (GL_TRIANGLES);
	glVertex3i (o->x[0], o->y[0], o->z);
	glVertex3i (o->x[1], o->y[1], o->z);
	glVertex3i (o->x[2], o->y[2], o->z);
	glEnd ();
}

static inline void
render_circle (const object_t *o)
{
	double angle;
	int x,y;

	glBegin (o->fill ? GL_POLYGON : GL_LINE_LOOP);
	for (angle = 0; angle < TWO_PI; angle += (TWO_PI / CIRCLE_STEPS)){
		x = o->x[0] + (int) (sin (angle) * o->radius);
		y = o->y[0] + (int) (cos (angle) * o->radius);
		glVertex3i (x,y,o->z);
		}
	glEnd ();
}

static inline void
render_arc (const object_t *o)
{
	double angle_start = dtor ((o->dstart - 90) % 360);
	double angle_delta = dtor (o->ddelta);
	double angle_end = angle_start;
	double angle;
	int arcx_outer[CIRCLE_STEPS + 1];
	int arcy_outer[CIRCLE_STEPS + 1];
	int arcx_inner[CIRCLE_STEPS + 1];
	int arcy_inner[CIRCLE_STEPS + 1];
	int step = 0;
	int i;
	const int w = (o->width >= 2) ? o->width : 2;	// arcs of width <2 would be invisible

	if (angle_delta >= 0)
		angle_end += angle_delta;
	else
		angle_start += angle_delta;

	for (angle = angle_start; angle < angle_end; angle += (TWO_PI / CIRCLE_STEPS), step++) {
		arcx_outer[step] = (int) (sin (angle) * (o->radius + (w / 2)));
		arcy_outer[step] = (int) (cos (angle) * (o->radius + (w / 2)));
		arcx_inner[step] = (int) (sin (angle) * (o->radius - (w / 2)));
		arcy_inner[step] = (int) (cos (angle) * (o->radius - (w / 2)));
	}
	arcx_outer[step] = (int) (sin (angle_end) * (o->radius + (w / 2)));
	arcy_outer[step] = (int) (cos (angle_end) * (o->radius + (w / 2)));
	arcx_inner[step] = (int) (sin (angle_end) * (o->radius - (w / 2)));
	arcy_inner[step] = (int) (cos (angle_end) * (o->radius - (w / 2)));

	if (step > 0 && arcx_outer[step] == arcx_outer[step - 1] && arcy_outer[step] == arcy_outer[step - 1])
		step--;

	for (i = 0; i < step; i++) {
		glBegin (GL_POLYGON);
		glVertex3i (o->x[0] + arcx_outer[i + 0], o->y[0] + arcy_outer[i + 0], o->z);
		glVertex3i (o->x[0] + arcx_outer[i + 1], o->y[0] + arcy_outer[i + 1], o->z);
		glVertex3i (o->x[0] + arcx_inner[i + 1], o->y[0] + arcy_inner[i + 1], o->z);
		glVertex3i (o->x[0] + arcx_inner[i + 0], o->y[0] + arcy_inner[i + 0], o->z);
		glEnd ();
	}
}

static inline void
render_params(const object_t *o)
{
	glLineWidth ((float) o->width);
	glPointSize ((float) o->width);
	glColor3ub (o->cr, o->cg, o->cb);
	glPolygonMode (GL_FRONT_AND_BACK, o->fill ? GL_FILL : GL_LINE);
	glRotatef ((float) o->rotate, 0, 0, 1);
}

// walk the object primitive list and put them into the display buffer
static void
Render (void)
{
	object_t *o;

	render_params(&Root); // set defaults
	OBJECT_WALK (o) {
		if( !layer_visible(o) )
			continue;
		render_params(o);
		switch (o->type) {
		case TYPE_NONE:					break;
		case TYPE_LINE:		render_line (o);	break;
		case TYPE_POINT:	render_circle (o);	break;
		case TYPE_RECT:		render_rectangle (o);	break;
		case TYPE_TEXT:		render_text (o);	break;
		case TYPE_IMAGE:	render_image (o);	break;
		case TYPE_TRIANGLE:	render_triangle (o);	break;
		case TYPE_CIRCLE:	render_circle (o);	break;
		case TYPE_ARC:		render_arc (o);		break;
		}
	}
}

static void
Motion (const int x, const int y)
{
	if (!Moveactive)
		return;
	PanX += (x - MoveX) / Zoom;
	PanY += (MoveY - y) / Zoom;
	MoveX = x;
	MoveY = y;
	glutPostRedisplay ();
}

static void
Mouse (const int button, const int state, const int x, const int y)
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
			MoveX = x;
			MoveY = y;
			Moveactive = true;
		}
		else {
			Moveactive = false;
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
	glClear (GL_COLOR_BUFFER_BIT);	// and maybe GL_DEPTH_BUFFER_BIT ?
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
Key (const unsigned char key, const int x, const int y)
{
	(void) x;
	(void) y;
	switch (key) {
	case 'a': all_layers(true); break;
	case '1': layer_toggle(1); break;
	case '2': layer_toggle(2); break;
	case '3': layer_toggle(3); break;
	case '4': layer_toggle(4); break;
	case '5': layer_toggle(5); break;
	case '6': layer_toggle(6); break;
	case '7': layer_toggle(7); break;
	case '8': layer_toggle(8); break;
	case '9': layer_toggle(9); break;
	case '0': layer_toggle(10); break;
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
SpecialKey (const int key, const int x, const int y)
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

	case GLUT_KEY_F1:	layer_toggle(1); break;
	case GLUT_KEY_F2:	layer_toggle(2); break;
	case GLUT_KEY_F3:	layer_toggle(3); break;
	case GLUT_KEY_F4:	layer_toggle(4); break;
	case GLUT_KEY_F5:	layer_toggle(5); break;
	case GLUT_KEY_F6:	layer_toggle(6); break;
	case GLUT_KEY_F7:	layer_toggle(7); break;
	case GLUT_KEY_F8:	layer_toggle(8); break;
	case GLUT_KEY_F9:	layer_toggle(9); break;
	case GLUT_KEY_F10:	layer_toggle(10); break;
	case GLUT_KEY_F11:	layer_toggle(11); break;
	case GLUT_KEY_F12:	layer_toggle(12); break;

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
Reshape (const int width, const int height)
{
	glViewport (0, 0, width, height);
	glMatrixMode (GL_PROJECTION);		// Start modifying the projection matrix.
	glLoadIdentity ();			// Reset project matrix.

	// Map abstract coords directly to window coords.
	glOrtho (0, width, 0, height, -((MAX_LAYERS + 1) * LAYER_SEP * 100), (MAX_LAYERS + 1) * LAYER_SEP * 100);

	//glScalef(1, -1, 1);                   // Invert Y axis so increasing Y goes down.
	glTranslatef (0, height, 0);		// Shift origin up to upper-left corner.
}

static void
WindowSetup (void)
{
	object_t	*o;
	double		zx, zy;			// zoom required to fit in x and y directions
	int		w = Maxx - Minx;
	int		h = Maxy - Miny;

	// if the image is too big for the initial window size, adjust Zoom to make it fit
	if (w > INIT_MAX_WIDTH || h > INIT_MAX_HEIGHT) {
		zx = INIT_MAX_WIDTH / (double) w;
		zy = INIT_MAX_HEIGHT / (double) h;
		Zoom = zx < zy ? zx : zy;
		if (Zoom < ZOOM_MIN)
			Zoom = ZOOM_MIN;
		w *= Zoom;
		h *= Zoom;
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

	glutInitWindowSize (w, h);
	glutCreateWindow (Title);

	OBJECT_WALK (o) {	// finalize the list
		if( o->image != NULL )
			image_finalize(o);	// convert raw image bytes to texture form
		//object_print(o);		// DEBUG
		}
}

int
main (int argc, char **argv)
{
	FILE *fp;

	glutInit (&argc, argv);
	if (argc > 1) {
		Title = argv[1];
		if ((fp = fopen (argv[1], "r")) != NULL) {
			object_scan (fp);
			fclose (fp);
		}
	}
	else
		object_scan (stdin);

	object_wrapup();
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
