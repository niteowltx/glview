#ifndef __UTIL_H__
#define __UTIL_H__

//
// generally useful static inline functions
//
//	fatal/error/warn/usage
//	sleep/delay/cycle_count
//	must_* wrapper functions for system/libc functions: read/write/open/close/... malloc
//	generic hash function, lfsr
//	hexdump (bytes, uint32_t or uint64_t)
//	split strings into argc/argv style, simple CSV parser
//	base64 decoder
//	random numbers

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <x86intrin.h>

// Tunable
#define	MAXSTRING	1024
#define	MAXBUF		32768
#define	MAXTOKENS	2048					// tokenize/split_by/csv_get limit
#define	MIN_COLOR_BYTE	32					// rnd_color() min/max
#define	MAX_COLOR_BYTE	252
#define	LINELEN_UINT8	32					// hexdump uint8
#define LINELEN_UINT32	(LINELEN_UINT8/sizeof(uint32_t))	// hexdump uint32 words
#define LINELEN_UINT64	(LINELEN_UINT8/sizeof(uint64_t))	// hexdump uint64 words

// Probably not tunable
#define	BAD_VALUE	0xDDEEAADDBBEEEEFFlu
#define	CSV_SPLIT_CHAR	','				// split_by() for CSV input
#define	ENTIRE_ARRAY	UINT64_MAX			// array_subscript() returns this as the index when no [] was seen
#define	BASE64_PAD	'='

static inline uint64_t
cycles()
{
	return __rdtsc();	// __builtin_readcyclecounter() would be better
}

// cpuid is not privileged
static inline void
cpuid (unsigned int *eax, unsigned int *ebx, unsigned int *ecx, unsigned int *edx)
{
	// ecx is often an input as well as an output
	asm volatile ("cpuid":"=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx) :"0" (*eax), "2" (*ecx) :"memory");
}

static inline int
simple_min(const int a, const int b)
{
	return a<b ? a:b;
}

static inline int
simple_max(const int a, const int b)
{
	return a>b ? a:b;
}

static inline int
simple_lmin(const long int a, const long int b)
{
	return a<b ? a:b;
}

static inline int
simple_lmax(const long int a, const long int b)
{
	return a>b ? a:b;
}

static inline unsigned int
simple_umin(const unsigned int a, const unsigned int b)
{
	return a<b ? a:b;
}

static inline unsigned int
simple_umax(const unsigned int a, const unsigned int b)
{
	return a>b ? a:b;
}

static inline unsigned int
simple_ulmin(const unsigned long int a, const unsigned long int b)
{
	return a<b ? a:b;
}

static inline unsigned int
simple_ulmax(const unsigned long int a, const unsigned long int b)
{
	return a>b ? a:b;
}

// given 2 lo-hi ranges, find the overlap and return the number of elements in the range
// if there is no overlap, return 0 and set *hi < *lo.
static inline uint64_t
range_overlap(const uint64_t lo1, const uint64_t hi1, const uint64_t lo2, const uint64_t hi2, uint64_t *lo, uint64_t *hi)
{
	uint64_t	l,h;

	if( lo1>hi1 || lo2>hi2 || hi1<lo2 ){	// invalid ranges or no overlap
		*lo = 1;
		*hi = 0;
		return 0;
		}
	h = simple_ulmin(hi1,hi2);
	l = simple_ulmin(h,lo2);

	*lo = l;
	*hi = h;
	return (h-l)+1;
}

// word align memory address by rounding down if necessary
static inline uint64_t
round_down(const uint64_t addr)
{
	return addr & ~(sizeof(uint64_t)-1);
}

// extract a uint64_t from a uint8_t array (little endian)
static inline uint64_t
get_uint64(const uint8_t *p)
{
	uint64_t v;

	v  = (uint64_t)p[0]<<0;
	v |= (uint64_t)p[1]<<8;
	v |= (uint64_t)p[2]<<16;
	v |= (uint64_t)p[3]<<24;
	v |= (uint64_t)p[4]<<32;
	v |= (uint64_t)p[5]<<40;
	v |= (uint64_t)p[6]<<48;
	v |= (uint64_t)p[7]<<56;
	return v;
}

// extract a uint32_t from a uint8_t array (little endian)
static inline uint64_t
get_uint32(const uint8_t *p)
{
	return p[0]<<0 | p[1]<<8 | p[2]<<16 | p[3]<<24;
}

// extract a uint16_t from a uint8_t array (little endian)
static inline uint64_t
get_uint16(const uint8_t *p)
{
	return p[0]<<0 | p[1]<<8;
}

// swap 4-bit nybbles of a uint8_t
static inline uint8_t
swap_4(const uint8_t n)
{
	return (n>>4)|(n<<4);
}

// swap bytes of a uint16_t
static inline uint16_t
swap_8(const uint16_t n)
{
	return (n>>8)|(n<<8);
}

// swap words of a uint32_t
static inline uint32_t
swap_16(const uint32_t n)
{
	return (n>>16)|(n<<16);
}

// swap uint32_t of a uint64_t
static inline uint64_t
swap_32(const uint64_t n)
{
	return (n>>32)|(n<<32);
}

// a simple 8-bit lfsr generator
// valid: 8E 95 96 A6 AF B1 B2 B4 B8 C3 C6 D4 E1 E7 F3 FA
static inline uint8_t
lfsr_next_8(const uint8_t lfsr)
{
        uint8_t msb = lfsr>>1u;

        if( lfsr & 1u )
                msb ^= 0x95u;
        return msb;
}

// a simple 16-bit lfsr generator
// valid: 8016 801C 801F 8029 805E 806B 8097 809E 80A7 80AE 80CB 80D0 80D6 80DF 80E3 810A ...
static inline uint16_t
lfsr_next_16(const uint16_t lfsr)
{
        uint16_t msb = lfsr>>1u;

        if( lfsr & 1u )
                msb ^= 0x801Cu;
        return msb;
}

// a simple 32-bit lfsr generator
// valid: 80000057 80000062 8000007A 80000092 800000B9 800000BA 80000106 80000114
//        8000012D 8000014E 8000016C 8000019F 800001A6 800001F3 8000020F 800002CC ...
static inline uint32_t
lfsr_next_32(const uint32_t lfsr)
{
        uint32_t msb = lfsr>>1u;

        if( lfsr & 1u )
                msb ^= 0x80000062u;
        return msb;
}

// a simple 64-bit lfsr generator
// valid: 800000000000000D 800000000000000E 800000000000007A 80000000000000BA
//        80000000000000D0 80000000000000EF 8000000000000128 8000000000000165
//        80000000000001A3 80000000000001E4 80000000000001E7 80000000000001F9
//        8000000000000212 8000000000000299 80000000000003BC 80000000000003BF ...
static inline uint64_t
lfsr_next_64(const uint64_t lfsr)
{
        uint64_t msb = lfsr>>1u;

        if( lfsr & 1u )
                msb ^= 0x800000000000000Eu;
        return msb;
}

#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL
// Return 64-bit FNV-1a hash for string
// See: https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function
static inline uint64_t
hash_key (const char *s)
{
	uint64_t hash = FNV_OFFSET;

	for( ; *s; s++ ){
		hash ^= (uint64_t) (unsigned char) *s;
		hash *= FNV_PRIME;
	}
	return hash;
}

// print error message and exit
static inline void
fatal (const char *format, ...)
{
	va_list args;

	printf ("FATAL: ");
	va_start (args, format);
	vprintf (format, args);
	va_end (args);
	printf ("\n");
	exit (1);
}

// print error message
static inline void
error (const char *format, ...)
{
	va_list args;

	printf ("ERROR: ");
	va_start (args, format);
	vprintf (format, args);
	va_end (args);
	printf ("\n");
}

// print warning message
static inline void
warning (const char *format, ...)
{
	va_list args;

	printf ("Warning: ");
	va_start (args, format);
	vprintf (format, args);
	va_end (args);
	printf ("\n");
}

// print usage message and exit
static inline void
usage (const char *format, ...)
{
	va_list args;

	printf ("Usage: ");
	va_start (args, format);
	vprintf (format, args);
	va_end (args);
	printf ("\n");
	exit (1);
}

// malloc or die
static inline void *
must_malloc (const size_t size)
{
	void *vp = malloc (size);

	if (vp == NULL && size != 0)
		fatal ("%s size:%u",__FUNCTION__, size);
	return vp;
}

// realloc or die
static inline void *
must_realloc (void *ptr, const size_t size)
{
	void *vp = realloc (ptr, size);

	if (vp == NULL && size != 0)
		fatal ("%s ptr:%lX size:%u",__FUNCTION__, (unsigned long)ptr, size);
	return vp;
}

// zalloc or die
static inline void *
must_zalloc (const size_t size)
{
	void *vp = must_malloc (size);

	if(vp != NULL )
		memset (vp, 0, size);
	return vp;
}

static inline char *
must_strdup(const char *s)
{
	char *dup = strdup(s);

	if( dup==NULL )
		fatal ("%s s:%s errno:%s", __FUNCTION__, s, strerror (errno));
	return dup;
}

// readlink or die
static inline ssize_t
must_readlink(const char *name, char *buf, ssize_t bufsize)
{
	ssize_t lsize = readlink(name,buf,bufsize);

	if( lsize<0 || lsize >= (bufsize-1) )	// error or truncation
		fatal ("%s name:%s errno:%s", __FUNCTION__, name, strerror (errno));
	buf[lsize] = '\0';			// TODO: always a good idea?
	return lsize;
}

// does file exist?
static inline bool
file_exists(const char *filename)
{
	struct stat s;

	return stat(filename, &s)==0;
}

// read or die (a short read or EOF counts as a failure)
static inline void
must_read(int fd, uint8_t *buf, ssize_t count)
{
	ssize_t actual = read(fd,buf,count);

	if( actual != count )
		fatal("%s fd:%d count:%ld actual:%ld errno:%s", __FUNCTION__, fd,count,actual,strerror(errno));
}

// pread or die (a short read or EOF counts as a failure)
static inline void
must_pread(int fd, uint8_t *buf, ssize_t count, off_t offset)
{
	ssize_t actual = pread(fd,buf,count,offset);

	if( actual != count )
		fatal("%s fd:%d count:%ld offset:%ld actual:%ld errno:%s", __FUNCTION__, fd,count,offset,actual,strerror(errno));
}

// write or die
static inline void
must_write(int fd, const uint8_t *buf, ssize_t count)
{
	ssize_t actual = write(fd,buf,count);

	if( actual != count )
		fatal("%s fd:%d count:%ld actual:%ld errno:%s", __FUNCTION__, fd,count,actual,strerror(errno));
}

// pwrite or die
static inline void
must_pwrite(int fd, const uint8_t *buf, ssize_t count, off_t offset)
{
	ssize_t actual = pwrite(fd,buf,count,offset);

	if( actual != count )
		fatal("%s fd:%d count:%ld offset:%ld actual:%ld errno:%s", __FUNCTION__, fd,count,offset,actual,strerror(errno));
}

// open or die
static inline int
must_open (const char *name, const int flags, const int mode)
{
	int fd = open (name, flags, mode);

	if (fd < 0)
		fatal ("%s name:%s errno:%s", __FUNCTION__, name, strerror (errno));
	return fd;
}

// open read-opnly or fail
static inline int
must_open_ro(const char *file)
{
	return must_open(file,O_RDONLY,0);
}

// open read-write or fail
static inline int
must_open_rw(const char *file)
{
	return must_open(file,O_RDWR,0);
}

// close or die
static inline void
must_close (int fd)
{
	int ret = close (fd);

	if (ret != 0)
		fatal ("%s fd:%d errno:%s", __FUNCTION__, fd, strerror (errno));
}

// seek or die
static inline off_t
must_lseek(int fd, off_t offset, int whence)
{
	off_t stat =  lseek(fd, offset, whence);

	if( stat == -1 )
		fatal ("%s fd:%d offset:%ld whence:%d errno:%s", __FUNCTION__, fd, offset, whence, strerror (errno));

	return stat;
}

// fopen or die
static inline FILE *
must_fopen (const char *name, const char *mode)
{
	FILE *fp = fopen (name, mode);

	if (fp==NULL)
		fatal ("%s name:%s mode:%s", __FUNCTION__, name, mode);
	return fp;
}

// popen or die
static inline FILE *
must_popen (const char *command, const char *mode)
{
	FILE *fp = popen (command, mode);

	if (fp==NULL)
		fatal ("%s command:%s mode:%s", __FUNCTION__, command, mode);
	return fp;
}

// size of fd or fail
static inline ssize_t
must_size_fd (const int fd)
{
	struct stat s;

	if (fstat (fd, &s) < 0)
		fatal ("%s fd:%d errno:%s", __FUNCTION__, fd, strerror (errno));
	return s.st_size;
}

// size of filename or fail
static inline ssize_t
must_size_name (const char *filename)
{
	struct stat s;

	if (stat (filename, &s) < 0)
		fatal ("%s filename:%s errno:%s", __FUNCTION__, filename, strerror (errno));
	return s.st_size;
}

// mmap file or fail
static inline void *
must_mmap (char *file, size_t s, int oflags, int mflags)
{
	int fd = must_open (file, oflags, 0666);
	void *vp = mmap (NULL, s, mflags, MAP_PRIVATE | MAP_NORESERVE, fd, 0);

	if (vp == MAP_FAILED)
		fatal ("%s s:%ld errno:%s", __FUNCTION__, s, strerror (errno));
	close (fd);
	return vp;
}

// mmap a chunk of private, zeroed memory or fail
static inline void *
must_mmap_zero (const size_t size)
{
	return must_mmap("/dev/zero", size, O_RDWR, PROT_READ|PROT_WRITE);
}

// mmap whole file read-only or fail
static inline void *
must_mmap_ro (char *file, size_t * length)
{
	size_t s = must_size_name (file);
	if(length)
		*length = s;
	return must_mmap (file, s, O_RDONLY, PROT_READ);
}

// mmap whole file read-write or fail
static inline void *
must_mmap_rw (char *file, size_t * length)
{
	size_t s = must_size_name (file);
	if(length)
		*length = s;
	return must_mmap (file, s, O_RDWR, PROT_READ | PROT_WRITE);
}

// mmap anonymous memory or fail
static inline void *
must_mmap_anon (size_t s, int mflags)
{
	void *vp = mmap (NULL, s, mflags, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	if (vp == MAP_FAILED)
		fatal ("%s s:%ld errno:%s", __FUNCTION__, s, strerror (errno));
	return vp;
}

// unmap or fail
static inline void
must_munmap (char *buf, size_t length)
{
	if (munmap (buf, length) != 0)
		fatal ("%s buf:0x%lX length:%ld errno:%s", __FUNCTION__, (unsigned long) buf, length, strerror (errno));
}

// system or fail
static inline void
must_system(const char *buf)
{
	int rval = system(buf);

	if(rval)
		fatal("%s rval:%d buf:%s",__FUNCTION__,rval,buf);
}

// generate a random integer in the range [0,n-1]
// fatal if n is not [1-RAND_MAX]
static inline unsigned int
rnd(const int n)
{
	if( n < 1 || n > RAND_MAX)
		fatal("%s n:%d",__FUNCTION__,n);
	return random()%n;
}

// random integer in the range [from,to-1]
static inline unsigned int
rnd_range(const int from, const int to)
{
	return rnd(to-from)+from;
}

// random 24-bit RGB color value with each channel in the range [MIN_COLOR_BYTE,MAX_COLOR_BYTE]
static inline unsigned int
rnd_color(void)
{
	unsigned int cr = rnd_range(MIN_COLOR_BYTE,MAX_COLOR_BYTE);
	unsigned int cg = rnd_range(MIN_COLOR_BYTE,MAX_COLOR_BYTE);
	unsigned int cb = rnd_range(MIN_COLOR_BYTE,MAX_COLOR_BYTE);

	return (cr<<16)|(cg<<8)|cb;
}

// seed the random number generator from /dev/urandom
// TODO: use initstate()?
static inline void
rnd_init()
{
	unsigned int seed = 1;
	unsigned int fd = must_open_ro("/dev/urandom");

	if( read(fd,&seed,sizeof(seed)) != sizeof(seed) )
		fatal("%s",__FUNCTION__);
	srandom(seed);
	close(fd);
}

static inline char *
skipwhite(char *s)
{
	while(*s){
		if( !isspace(*s) )
			break;
		s++;
		}
	return s;
}

//	tokenize --- split string into argv[] style array of pointers
//
// Modifies 's' by inserting '\0'.  The tokens array will have a NULL
// added at the end of the real tokens.
// Allows "string with blanks" and also stops when // is seen
static inline unsigned int
tokenize(char *s, char **tokens, unsigned int ntokens)
{
	char **itokens = tokens;

	if(ntokens<1)
		fatal("%s ntokens:%d",__FUNCTION__,ntokens);
	ntokens--;	// use one for NULL at end
	s = skipwhite(s);
	while( *s != '\n' && *s != '\0'){
		if( s[0]=='/' && s[1]=='/' )	// rest of line is comment
			break;
		if( --ntokens <= 0 )
			break;		// way too many tokens on this line
		if( *s == '"' ){	// handle quoted string
			s++;
			*tokens++ = s;
			while( *s != '\n' && *s != '\0' && *s != '"' )s++;
			*s = ' ';	// replace end of quoted string with space so it will be marked correctly
			}
		else{
			*tokens++ = s;
			while( *s && !isspace(*s) )s++;
			}
		if( isspace(*s) )
			*s++ = '\0';
		s = skipwhite(s);
		}
	*tokens = NULL;	// mark end of tokens
	return tokens-itokens;	// number of tokens not including NULL at end
}

//	split_by --- split string into argv[] style array of pointers by a 'split' character
//
// Simpler version of tokenize() that does not check for comments or quoted characters
// and allows the split character to be specified
// NOTE: if the return value is zero, *tokens is guaranteed to point to a NULL
static inline unsigned int
split_by(const char split, char *s, char **tokens, unsigned int ntokens)
{
	char **itokens = tokens;

	if(ntokens<1)
		fatal("%s ntokens:%d",__FUNCTION__,ntokens);
	ntokens--;	// use one for NULL at end
	while( *s != '\0'){
		if( --ntokens <= 0 )
			break;		// way too many tokens on this line
		*tokens++ = s;
		while( *s != '\0' && *s != '\n' && *s != split )
			s++;
		if( *s == '\n' )
			*s = '\0';
		if( *s == '\0' )
			break;
		*s++ = '\0';
		}
	*tokens = NULL;	// mark end of tokens
	return tokens-itokens;	// number of tokens not including NULL at end
}

// get a line of csv data from file, split into argv[] strings
// Note that the returned argv[] points to internal static data that
// will be overwritten on the next call to csv_get() so the caller
// must fully process the strings before the next call to this function
static inline unsigned int
csv_get(FILE *fp, char **csv, const unsigned int maxtokens)
{
	static char	buf[MAXBUF];

	if( fgets(buf,sizeof(buf),fp) == NULL )
		return 0;
	return split_by(CSV_SPLIT_CHAR,buf,csv,maxtokens);
}

// display a range of memory as uint8_t, hex and ascii, zeroes print as __ for readability
// Specifying 'base' as UINT64_MAX supresses printing base
static inline void
hexdump_uint8(const uint64_t base, const uint8_t *buf, uint64_t size)
{
	uint64_t off;
	uint64_t i;
	uint64_t printlen;

	for(off=0;off<size;off += simple_min(size-off,LINELEN_UINT8)){
		printlen = simple_min(size-off,LINELEN_UINT8);
		if(base==UINT64_MAX)
			printf("\t");
		else
			printf("%016lX |",base+off);
		for(i=0;i<printlen;i++){
			if( buf[off+i] != 0 )
				printf(" %02X",buf[off+i]);
			else
				printf(" __");
			}
		for( ;i<LINELEN_UINT8;i++)
			printf("   ");
		printf("  |");
		for(i=0;i<printlen;i++)
			printf("%c",isprint(buf[off+i])? buf[off+i]:'.');
		for( ;i<LINELEN_UINT8;i++)
			printf(" ");
		printf("|\n");
		}
}

// display a range of memory as uint32_t, zeros print as ________ for readability
// Specifying 'base' as UINT32_MAX supresses printing base
static inline void
hexdump_uint32(const uint64_t base, const uint32_t *buf, const uint64_t size)
{
	uint32_t off;
	uint32_t i;
	uint32_t printlen;

	for(off=0;off<size;off += simple_min(size-off,LINELEN_UINT32)){
		printlen = simple_min(size-off,LINELEN_UINT32);
		if(base==UINT64_MAX)
			printf("\t");
		else
			printf("%016lX |",base+(off*sizeof(uint32_t)));
		for(i=0;i<printlen;i++){
			if( buf[off+i] != 0 )
				printf(" %08X",buf[off+i]);
			else
				printf(" ________");
			}
		printf("\n");
		}
}

// display a range of memory as uint64_t, zeros print as ________________ for readability
// Specifying 'base' as UINT64_MAX supresses printing base
static inline void
hexdump_uint64(const uint64_t base, const uint64_t *buf, const uint64_t size)
{
	uint64_t off;
	uint64_t i;
	uint64_t printlen;

	for(off=0;off<size;off += simple_min(size-off,LINELEN_UINT64)){
		printlen = simple_min(size-off,LINELEN_UINT64);
		if(base==UINT64_MAX)
			printf("\t");
		else
			printf("%016lX |",base+(off*sizeof(uint64_t)));
		for(i=0;i<printlen;i++){
			if( buf[off+i] != 0 )
				printf(" %016lX",buf[off+i]);
			else
				printf(" ________________");
			}
		printf("\n");
		}
}

// hexdump_uint8 with named header, and trim size to skip all trailing zero bytes (rounded to sizeof(uint64_t))
static inline void
hexdump_tag (const char *tag, const uint64_t base, const uint8_t *buf, const unsigned int size)
{
	unsigned int max_used;

	for (max_used = size; max_used; max_used--)
		if (buf[max_used - 1])
			break;
	// round up to nearest int
	max_used = (max_used + sizeof (uint64_t) - 1) / sizeof (uint64_t);
	max_used *= sizeof (uint64_t);
	if (max_used) {
		printf ("======= %s ======\n", tag);
		hexdump_uint8 (base, buf, max_used);
	}
}

// wait for nanoseconds
static inline void
ns_pause(const uint64_t ns)
{
	struct timespec t;

	t.tv_sec = ns/1000000000ul;
	t.tv_nsec = (unsigned long)(ns%1000000000ul);
	(void)nanosleep(&t,NULL);
}

// wait for microseconds
static inline void
us_pause(const uint64_t us)
{
	ns_pause(us*1000);
}

// wait for milliseconds
static inline void
ms_pause(const uint64_t ms)
{
	ns_pause(ms*1000000);
}

// raw time in ns
static inline uint64_t
ns()
{
	struct timespec t;

	if( clock_gettime(CLOCK_MONOTONIC_RAW,&t) < 0 )
		fatal("%s",__FUNCTION__);
	return (t.tv_sec*1000000000ul) + (t.tv_nsec);
}

// raw time in us
static inline uint64_t
us()
{
	return ns()/1000;
}

// raw time in ms
static inline uint64_t
ms()
{
	return ns()/1000000;
}

// current time in seconds since 1/1/1970
static inline time_t
now()
{
	return time(NULL);
}

// decode a base64 string, return number of bytes decoded, out can hold up to outmax bytes
// Base64 converts [A-Za-z0-9+/] to 0-63, 1 or 2 BASE64_PAD are padding chars at the end if necessary
// Allow - and . as alternates for +, and , as an alternate for /
// groups of 4 characters convert to 3 decoded bytes
static inline uint64_t
base64_decode (const unsigned char *in, uint8_t *out, uint64_t outmax)
{
	uint64_t v;
	uint64_t decoded = 0;
	static const uint8_t base64dec[256] = {
	//      _0  _1  _2  _3  _4  _5  _6  _7  _8  _9  _A  _B  _C  _D  _E  _F
		 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,	// 0_
		 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,	// 1_
		 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62, 63, 62, 62, 63,	// 2_
		52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,	// 3_
		 0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,	// 4_
		15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0, 63,	// 5_
		 0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,	// 6_
		41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  0,  0,  0,  0,  0,	// 7_
		 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,	// 8_
		 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,	// 9_
		 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62, 63, 62, 62, 63,	// A_
		52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,	// B_
		 0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,	// C_
		15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0, 63,	// D_
		 0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,	// E_
		41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  0,  0,  0,  0,  0,	// F_
		};

	while(in[0] && in[1] && in[2] && in[3] && in[0]!=BASE64_PAD && in[1]!=BASE64_PAD && in[2]!=BASE64_PAD && in[3]!=BASE64_PAD){
		if((decoded+3)>outmax)
			fatal("%s overflow3 decoded:%u outmax:%u",__FUNCTION__,decoded,outmax);
		v = base64dec[in[0]] << 18 | base64dec[in[1]] << 12 | base64dec[in[2]] << 6 | base64dec[in[3]];
		out[0] = v>>16;
		out[1] = (v>>8)&0xFF;
		out[2] = v & 0xFF;
		in += 4;
		out += 3;
		decoded += 3;
		}
	if( in[0]=='\0' || in[0]==BASE64_PAD )
		return decoded;
	if( in[1]=='\0' || in[1]==BASE64_PAD){
		fatal("%s invalid in:%s",__FUNCTION__,in);	// a single char only encodes 6-bits, not enough for a whole byte
		return 0;	// NOTREACHED
		}
	if( in[2]=='\0' || in[2]==BASE64_PAD){
		if((decoded+1)>outmax)
			fatal("%s overflow1 decoded:%u outmax:%u in:%s",__FUNCTION__,decoded,outmax,in);
		v = base64dec[in[0]] << 18 | base64dec[in[1]] << 12;
		out[0] = v>>16;
		return decoded+1;
		}
	if( in[3]=='\0' || in[3]==BASE64_PAD){
		if((decoded+2)>outmax)
			fatal("%s overflow2 decoded:%u outmax:%u in:%s",__FUNCTION__,decoded,outmax,in);
		v = base64dec[in[0]] << 18 | base64dec[in[1]] << 12 | base64dec[in[2]] << 6;
		out[0] = v>>16;
		out[1] = (v>>8)&0xFF;
		return decoded+2;
		}
	fatal("%s",__FUNCTION__);
	return 0;	// NOTREACHED
}

#endif
#define	CMAX	251
