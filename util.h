#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <fcntl.h>
#include <errno.h>

#define	MAXWORD	1024			// max string length anywhere
#define	STRSAVE_SIZE	(1<<20)		// pool alloc size, refill when it gets below MAXWORD

//	fatal --- print error message and exit with error
static inline void
fatal(char *format, ...)
{
	va_list args;

	printf("FATAL: ");
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf("\n");
	exit(1);
}


//	error --- print error message
static inline void
error(char *format, ...)
{
	va_list args;

	printf("ERROR: ");
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf("\n");
}

// ====================== library function must work or fatal()

// size of fd or fail
ssize_t
must_size_fd(const int fd)
{
	struct stat s;

	if( fstat(fd,&s) < 0 )
		fatal("%s fd:%d errno:%s",__FUNCTION__,fd,strerror(errno));
	return s.st_size;
}

// size of filename or fail
ssize_t
must_size_name(const char *file)
{
	struct stat s;

	if( stat(file,&s) < 0 )
		fatal("%s file:%s errno:%s",__FUNCTION__,file,strerror(errno));
	return s.st_size;
}

// fopen or fail
static inline FILE *
must_fopen(const char *file, const char *mode)
{
	FILE *fp = fopen(file,mode);

	if( fp==NULL )
		fatal("%s file:%s mode:%s erno:%s",__FUNCTION__,file,mode,strerror(errno));
	return fp;
}

// open or fail
static inline int
must_open(const char *file, const int flags, const mode_t mode)
{
	int fd = open(file,flags,mode);

	if( fd<0 )
		fatal("%s file:%s flags:0x%x mode:%o errno:%s",__FUNCTION__,file,flags,mode,strerror(errno));
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

// read or fail
static inline ssize_t
must_read(const int fd, void *buf, const size_t count)
{
	ssize_t rval = read(fd,buf,count);

	if( rval < 0 )
		fatal("%s fd:%d buf:0x%lx count:%ld errno:%s",__FUNCTION__,fd,(unsigned long)buf,count,strerror(errno));
	return rval;
}

// read exactly count or fail
static inline void
must_read_all(const int fd, void *buf, const size_t count)
{
	ssize_t rval = read(fd,buf,count);

	if( rval != (ssize_t)count )
		fatal("%s fd:%d buf:0x%lx count:%ld rval:%ld errno:%s",__FUNCTION__,fd,(unsigned long)buf,count,rval,strerror(errno));
}

// write or fail
static inline ssize_t
must_write(const int fd, void *buf, const size_t count)
{
	ssize_t rval = write(fd,buf,count);

	if( rval < 0 )
		fatal("%s fd:%d buf:0x%lx count:%ld rval:%ld errno:%s",__FUNCTION__,fd,(unsigned long)buf,count,rval,strerror(errno));
	return rval;
}

// write exactly count or fail
static inline void
must_write_all(const int fd, void *buf, const size_t count)
{
	ssize_t rval = write(fd,buf,count);

	if( rval != (ssize_t)count )
		fatal("%s fd:%d buf:0x%lx count:%ld rval:%ld errno:%s",__FUNCTION__,fd,(unsigned long)buf,count,rval,strerror(errno));
}

// malloc or fail
static inline void *
must_malloc(const size_t size)
{
	void *vp = malloc(size);

	if(vp==NULL)
		fatal("%s size:%ld errno:%s",__FUNCTION__,size,strerror(errno));
	return vp;
}

// realloc or fail
static inline void *
must_realloc(void *p, const size_t size)
{
	void *vp = realloc(p,size);

	if(vp==NULL)
		fatal("%s size:%ld errno:%s",__FUNCTION__,size,strerror(errno));
	return vp;
}

// malloc and zero or fail
static inline void *
must_zalloc(const size_t size)
{
	void *vp = must_malloc(size);

	memset(vp,0,size);
	return vp;
}

// mmap a chunk of private, zeroed memory or fail
static inline void *
must_mmap_zero(const size_t size)
{
	int fd = must_open("/dev/zero",O_RDWR,0);
	void *vp = mmap(0,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_NORESERVE,fd,0);

	if (vp == MAP_FAILED)
		fatal("%s size:%ld errno:%s",__FUNCTION__,size,strerror(errno));
	close(fd);
	return vp;
}

// mmap or fail
static inline void *
must_mmap(char *file, size_t *length, int oflags, int mflags)
{
	int fd = must_open(file, oflags, 0);
	size_t size = must_size_fd(fd);
	void *vp = mmap(NULL, size, mflags, MAP_PRIVATE|MAP_NORESERVE, fd, 0);

	if (vp == MAP_FAILED)
		fatal("%s size:%ld errno:%s",__FUNCTION__,size,strerror(errno));
	close(fd);
	if(length)
		*length = size;
	return vp;
}

// mmap read-only or fail
static inline void *
must_mmap_ro(char *file, size_t *length)
{
	return must_mmap(file,length,O_RDONLY,PROT_READ);
}

// mmap read-write or fail
static inline void *
must_mmap_rw(char *file, size_t *length)
{
	return must_mmap(file,length,O_RDWR,PROT_READ|PROT_WRITE);
}

// unmap or fail
static inline void
must_munmap(char *buf, size_t length)
{
	if( munmap(buf,length) != 0 )
		fatal("%s buf:0x%lx length:%ld errno:%s",__FUNCTION__,(unsigned long)buf,length,strerror(errno));
}

// strtol or fail
static inline long int
must_strtol(const char *nptr, char **endptr, int base)
{
	long int n;

	errno = 0;
	n = strtol(nptr,endptr,base);
	if ( errno )
		fatal("%s nptr:%s n:%ld errno:%s",__FUNCTION__,nptr,n,strerror(errno));
	return n;
}

// strtoul or fail
static inline unsigned long
must_strtoul(const char *nptr, char **endptr, int base)
{
	unsigned long n;

	errno = 0;
	n = strtoul(nptr,endptr,base);
	if ( errno )
		fatal("%s nptr:%s n:%ld errno:%s",__FUNCTION__,nptr,n,strerror(errno));
	return n;
}

// total available ram or fail
static inline unsigned long
must_totalram()
{
	struct sysinfo info;

	if( sysinfo(&info) != 0 )
		fatal("%s errno:%s",__FUNCTION__,strerror(errno));
	return info.totalram * info.mem_unit;
}

static inline void
must_system(char *cmd)
{
	if( system(cmd) != 0 )
		fatal("%s cmd:%s",__FUNCTION__,cmd);

}

// ====================== random numbers
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


// ====================== fast malloc and strings
#define	FAST_MALLOC_CHUNK	2000000
static unsigned int Fast_malloc_left = 0;
static char *Fast_malloc_next = NULL;

// how much space (maximum) is needed?
static inline char *
fast_malloc_need(const unsigned int n)
{
	if( Fast_malloc_left<n){
		Fast_malloc_next = (char *)must_malloc(FAST_MALLOC_CHUNK);
		Fast_malloc_left = FAST_MALLOC_CHUNK;
		}
	return Fast_malloc_next;
}

// how much space (actual) was used?
static inline void
fast_malloc_use(const unsigned int n)
{
	Fast_malloc_next += n;
	Fast_malloc_left -= n;
}

// quickly allocate less than FAST_MALLOC_CHUNK bytes that CANNOT be free()'d
static inline char *
fast_malloc(const unsigned int n)
{
	char *p = fast_malloc_need(n);

	fast_malloc_use(n);
	return p;
}

static inline void
fast_memcpy(void *dst, const void *src, const unsigned int n)
{
	__builtin_memcpy(dst,src,n);
}

static inline void
fast_memset(void *dst, const int val, const unsigned int n)
{
	__builtin_memset(dst,val,n);
}

//	string helper functions

//	strsave --- save a string somewhere safe
//
//	A copy of the string is made in a fast_malloc'ed area.  The return
//	pointer points to the copy.
//
//	IMPORTANT: For speed, the returned string CANNOT be free()'d
//		and the length of the string must be <= MAXWORD
//
static inline char *
strsave(const char *s)
{
	char *p,*q;

	p = q = fast_malloc_need(MAXWORD+1);
	while( (*q++ = *s++) != 0 )
		;
	fast_malloc_use(q-p);
	return p;
}

//	strnodup --- remove duplicate letters in string
//
//	The input string is edited to remove all duplicates.
static inline void
strnodup(char *str)
{
	char	tmp[MAXWORD];
	char	*p = tmp;
	char	*s;

	if( str==NULL )
		fatal("%s NULL",__FUNCTION__);
	*p  = '\0';
	for( s=str; *s; s++)
		if( strchr(tmp,*s) == NULL ){
			*p++ = *s;
			*p = '\0';
			}
	strcpy(str,tmp);
}

//	strcommon --- count letters in common between 2 strings
static inline int
strcommon(const char *s1,const char *s2)
{
	int common = 0;

	while(*s1){
		if( strchr(s2,*s1) != NULL )
			common++;
		s1++;
		}
	return common;
}

//	strmerge --- combine 2 strings and eliminate duplicates
static inline char *
strmerge(const char *s1,const char *s2)
{
	char	tmp[MAXWORD];

	strcpy(tmp,s1);
	strcat(tmp,s2);
	strnodup(tmp);
	return strsave(tmp);
}

//	strsort --- sort string
static inline char *
strsort(char *s1, char *s2)
{
	(void)s2;
	return s1;	// TODO
}

//
//	strnorm --- convert string to normal form, in place
//
//	Remove all non-alpha characters, convert to lower case
//
static inline void
strnorm(char *word)
{
	char *p = word;
	char *p2 = word;

	while( *p ){
		if( islower(*p) )
			*p2++ = *p;
		else if( isupper(*p) )
			*p2++ = tolower(*p);
		p++;
		}
	*p2 = '\0';
}

// ====================== misc
#define	CMAX	251
static inline unsigned int
rnd_color(void)
{
	static unsigned int cr = 128;
	static unsigned int cg = 128;
	static unsigned int cb = 128;

	cr += 31;
	if( cr > CMAX ){
		cr -= CMAX;
		cg += 29;
		if( cg > CMAX ){
			cg -= CMAX;
			cb += 27;
			if( cb > CMAX )
				cb -= CMAX;
			}
		}
	return (cr<<16)|(cg<<8)|cb;
}

