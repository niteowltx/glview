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

#define	SCALE	32

#define	ORDER	13	/* (2^(13*2) = 64Mb */

int Layer_base = 1;

static inline void
label_color()
{
	unsigned int color = rnd_color();

	printf("Color %d %d %d\n",(color>>16)&0xFF,(color>>8)&0xFF,color&0xFF);
}

static inline int
maxorder(unsigned int v)
{
	int order = 0;

	while(v){
		order++;
		v >>= 2;
		}
	return order-1;
}

static inline void
draw_range(unsigned int addr, unsigned int len)
{
	unsigned int x,y;
	int order;

	while(len){
		for( order = maxorder(len); order >= 0; order-- ){
			if( (addr % (1<<(2*order)))==0 )
				break;
			}
		hilbert(addr,ORDER,&x,&y);
		if( (x % (1<<order))==0 )	/* returned point may be lower left or upper right */
			printf("Rectangle %d %d %d %d\n",x*SCALE,y*SCALE,(x+(1<<order))*SCALE,(y+(1<<order))*SCALE);
		else
			printf("Rectangle %d %d %d %d\n",(x-(1<<order)+1)*SCALE,(y-(1<<order)+1)*SCALE,(x+1)*SCALE,(y+1)*SCALE);
		addr += 1<<(2*order);
		len -= 1<<(2*order);
		}
}

static inline void
draw_text(unsigned int addr, unsigned int len)
{
	unsigned int x,y;

	hilbert(addr,ORDER,&x,&y);

	printf("Text %d %d 0 %d %x\n",    (x*SCALE)+(SCALE/32),(y*SCALE)+(SCALE/32)+(SCALE/8), SCALE/16,addr);
	printf("Text %d %d 0 %d %x\n",    (x*SCALE)+(SCALE/32),(y*SCALE)+(SCALE/32)+(SCALE/16),SCALE/16,len);
}

static inline void
draw_area(unsigned int addr, unsigned int len)
{
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
		sscanf(buf,"%llx %lx",&addr,&len);
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
	if( argc==1 )
		gen_map(stdin);
	else{
		while(--argc){
			fp = fopen(*++argv,"r");
			gen_map(fp);
			fclose(fp);
			Layer_base += 3;
			}
		}
	return 0;
}
