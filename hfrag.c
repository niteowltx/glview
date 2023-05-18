#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <values.h>
#include <string.h>
#include <unistd.h>
#include "hilbert.h"
#include "util.h"

//
//	plot pairs of (addr,len) in hilbert space
//
//	In Hilbert space, a range or addresses can be displayed as connected squares
//
#define	SCALE	32
#define	ORDER	14	// total map size: (1u<<(ORDER*2) e.g. 13=64Mb
#define	MAXADDR	(1u<<(ORDER*2))

int Layer_base = 1;

static inline void
label_color()
{
	unsigned int color = rnd_color();

	printf("Color %d %d %d\n",(color>>16)&0xFF,(color>>8)&0xFF,color&0xFF);
}

// how many pairs of bits are set?
static inline unsigned int
maxorder(unsigned long int v)
{
	int order = 0;

	if(v==0)fatal("%s zero",__FUNCTION__);
	while(v){
		order++;
		v >>= 2;
		}
	return order-1;
}

// what is the maximum size square side length given addr and len
// returned value is log2 of the len.
static inline unsigned int
square_order(const unsigned long int addr, const unsigned long int len)
{
	unsigned long int order = maxorder(len);	// start with maximum possible order based on length

	// reduce order until it aligns with the given address
	while( (addr % (1u<<(2*(order))))!=0 )
		order--;
	return order;
}

static inline void
draw_range(unsigned int addr, unsigned int len)
{
	unsigned int x,y;
	int order;

	while(len){
		order = square_order(addr,len);
		hilbert(addr,ORDER,&x,&y);
		if( (x % (1u<<order))==0 )	// returned point may be lower left or upper right
			printf("Rectangle %u %u %u %u\n",x*SCALE,y*SCALE,(x+(1u<<order))*SCALE,(y+(1<<order))*SCALE);
		else
			printf("Rectangle %u %u %u %u\n",(x-(1u<<order)+1)*SCALE,(y-(1u<<order)+1)*SCALE,(x+1)*SCALE,(y+1)*SCALE);
		addr += 1u<<(2*order);
		len -= 1u<<(2*order);
		}
}

static inline void
draw_text(unsigned int addr, unsigned int len)
{
	unsigned int x,y;

	hilbert(addr,ORDER,&x,&y);
	x *= SCALE;
	x += SCALE/32;
	y *= SCALE;
	y += SCALE/32;

	printf("Text %d %d %x\n",    x,y+(SCALE/4),addr);
	printf("Text %d %d %x\n",    x,y+(SCALE/8),len);
}

static inline void
draw_area(unsigned int addr, unsigned int len)
{
	if( len==0 || len>=MAXADDR )return;
	if( addr >= MAXADDR ) return;
	if( (addr+len) >= MAXADDR ) return;

	printf("Layer %d\n",Layer_base);
	label_color();
	draw_range(addr,len);

	printf("Layer %d\n",Layer_base+1);
	printf("Color 255 255 255\n");
	draw_text(addr,len);

}

void
gen_map(FILE *fp)
{
	char buf[1024];
	unsigned long long int addr;
	unsigned long int len;

	while( fgets(buf,sizeof(buf),fp) != NULL ){
		if( buf[0]=='#' )
			continue;
		if( sscanf(buf,"%llx %lx",&addr,&len)==2 )
			draw_area(addr,len);
		}
}

int main(int argc,char **argv)
{
	FILE *fp;

	printf("Fill\n");
	printf("Width 2\n");
	printf("Point 0 0\n");
	printf("Point %d %d\n",(1<<ORDER)*SCALE,(1<<ORDER)*SCALE);
	printf("Scale %d\n",SCALE/8);
	if( argc==1 )
		gen_map(stdin);
	else{
		while(--argc){
			fp = fopen(*++argv,"r");
			gen_map(fp);
			fclose(fp);
			Layer_base += 2;
			}
		}
	return 0;
}
