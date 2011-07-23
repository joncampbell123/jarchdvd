
#include "lsimage.h"
#include "bitchin.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#include <string>

#define NO_FRAGSIZE	0xFFFFFFFF

static const char *ImgDirHeader = "JDVD2IMGD";

#if defined(__i386__) || defined(__powerpc__)
long long lseek64(int fd,long long where,int whence);
int ftruncate64(int fd,long long where);
#endif

LargeSplitImage::frag::frag()
{
	fd = -1;
}

LargeSplitImage::frag::~frag()
{
	if (fd >= 0) {
		::close(fd);
		fd = -1;
	}
}

LargeSplitImage::LargeSplitImage()
{
	iso_fd = -1;
	fragsize = 0;
	bname = NULL;
	sitrks = 0;
	sitr = NULL;
	fd = -1;
}

LargeSplitImage::~LargeSplitImage()
{
	close();
}

int LargeSplitImage::open(const char *basename)
{
	unsigned char tmp[280];
	int l;

	if (close() < 0) return -1;

	bitch(BITCHINFO,"Opening Large Split Image basename %s",basename);
	bitch_indent();

	sitrks = 4;
	sitr = (frag**)malloc(sizeof(frag*) * sitrks);
	if (!sitr) {
		bitch(BITCHERROR,"Not enough memory");
		bitch_unindent();
		return -1;
	}

	for (l=0;l < sitrks;l++)
		sitr[l] = NULL;

	l = strlen(basename);
	bname = (char*)malloc(l+1);
	if (!bname) {
		bitch(BITCHERROR,"Not enough memory");
		bitch_unindent();
		return -1;
	}
	memcpy(bname,basename,l+1);

	sprintf((char*)tmp,"%s.img-dir",basename);
	bitch(BITCHINFO,"file %s is the directory",(char*)tmp);
	fd = ::open((char*)tmp,O_RDWR | O_BINARY,0644);
	if (fd < 0) {
		if (errno == ENOENT) {
			bitch(BITCHINFO,"File does not exist, creating %s",tmp);
			fd = ::open((char*)tmp,O_CREAT | O_RDWR | O_TRUNC | O_BINARY,0644);
			if (fd < 0) {
				bitch(BITCHERROR,"Cannot create file!");
				bitch_unindent();
				close();
				return -1;
			}

			lseek(fd,0,SEEK_SET);
			::write(fd,ImgDirHeader,9);
#if 0
			::write(fd,LeHe32bin(512<<20),4);	// fragment size is 512MB
#else
			::write(fd,LeHe32bin(NO_FRAGSIZE),4);	// no fragments
#endif
			::write(fd,LeHe32bin(0),4);		// current max size is 0
			::write(fd,LeHe32bin(0),4);		// stored as 64-bit int
		}
		else {
			bitch_unindent();
			close();
			return -1;
		}
	}

	if (lseek(fd,0,SEEK_SET) != 0) {
		bitch_unindent();
		close();
		return -1;
	}

	if (::read(fd,tmp,21) < 21) {
		bitch_unindent();
		close();
		return -1;
	}

	if (memcmp(tmp,ImgDirHeader,9)) {
		bitch(BITCHERROR,"Bad directory header");
		bitch_unindent();
		close();
		return -1;
	}

	fragsize = binLeHe32((unsigned char*)(tmp+9));
	no_fragments = (fragsize == NO_FRAGSIZE);
	if (no_fragments) {
		bitch(BITCHINFO,"No fragments");

		if (!strcmp(basename,"dvdrom-image")) {
			if ((iso_fd = ::open64("dvd-rip-image.iso",O_RDWR | O_CREAT | O_BINARY,0644)) < 0) {
				bitch(BITCHERROR,"Cannot open rip image");
				bitch_unindent();
				close();
				return -1;
			}
		}
		else {
			std::string nam = std::string(basename) + ".bin";
			if ((iso_fd = ::open64(nam.c_str(),O_RDWR | O_CREAT | O_BINARY,0644)) < 0) {
				bitch(BITCHERROR,"Cannot open rip image");
				bitch_unindent();
				close();
				return -1;
			}
		}
	}
	else {
		bitch(BITCHINFO,"Fragment size is %u bytes (%uMB)",fragsize,fragsize>>20);
		if (fragsize < 1024 || fragsize > (512<<20)) {
			bitch_unindent();
			close();
			return -1;
		}
	}

	max_pos  =  (juint64)binLeHe32(tmp + 13);
	max_pos |= ((juint64)binLeHe32(tmp + 17)) << ((juint64)32);

	current_pos = 0;
	bitch(BITCHINFO,"Maximum byte offset is " PRINT64F,max_pos);
	bitch_unindent();
	return 0;
}

int LargeSplitImage::close()
{
	int i;

	update_max();
	if (sitr != NULL && sitrks) {
		for (i=0;i < sitrks;i++) {
			if (sitr[i]) delete sitr[i];
			sitr[i] = NULL;
		}

		sitrks = 0;
		free(sitr);
		sitr = NULL;
	}

	if (bname) {
		free(bname);
		bname = NULL;
	}

	if (iso_fd >= 0) {
		::close(iso_fd);
		iso_fd = -1;
	}

	if (fd >= 0) {
		::close(fd);
		fd = -1;
	}

	fragsize = 0;
	return 0;
}

juint64 LargeSplitImage::seek(juint64 ofs)
{
	current_pos = ofs;
	return ofs;
}

int LargeSplitImage::read(unsigned char *buf,int l)
{
	unsigned long no;
	char tmp[280];
	juint64 n64;
	int N,nor,p;
	int rr;

	if (fragsize == 0 || fd < 0 || l < 0) return 0;
	if (current_pos >= max_pos) return 0;

	if (no_fragments) {
		long long p = ::lseek64(iso_fd,current_pos,SEEK_SET);
		if (p != current_pos) return 0;
		int rd = ::read(iso_fd,buf,l);
		if (rd < 0) rd = 0;
		current_pos += rd;
		return rd;
	}

	n64 = ((juint64)max_pos) - ((juint64)current_pos);
	if (n64 >= ((juint64)l)) n64 = (juint64)l;
	l = (int)n64;
	p = l;
	while (p > 0) {
		/* which fragment?
		 * offset into fragment?
		 * number of bytes we can read from fragment? */
		N = (int)(current_pos / ((juint64)fragsize));
		no = (int)(current_pos % ((juint64)fragsize));
		nor = fragsize - no;
		if (nor > p) nor = p;

		/* do we need to expand the array of fragment objects? */
		/* NOTE: C++ purists need to shut the hell up about the use of C malloc/free/realloc.
		 *       "new" and "delete" are nice and all but provide nothing for resizing an allocated array.
		 *       Don't give me that "but you can allocate a new array and copy it", that's just wasteful!
		 *       Secondly, I know of several runtime C++ libraries (--ahem--MICROSOFT--) where using
		 *       malloc/free on something allocated using "new" will confuse the hell out of the runtime
		 *       library and cause problems. I don't know if Linux/GCC has this problem but I'd rather
		 *       not risk it. */
		if (N >= sitrks) {
			int os = sitrks,k;
			void *t;

			sitrks = N+1;
			t = realloc((void*)sitr,sizeof(frag*) * sitrks);
			if (!t) {
				bitch(BITCHERROR,"LargeSplitImage::read() unable to extend array of file objects");
				return (l-p);
			}

			sitr = (frag**)t;
			for (k=os;k < sitrks;k++) sitr[k] = NULL;
		}

		/* do we need to allocate the fragment object? */
		if (!sitr[N]) {
			sitr[N] = new frag;
			if (!sitr[N]) {
				bitch(BITCHERROR,"LargeSplitImage::read() unable to alloc frag object");
				return (l-p);
			}

			sprintf(tmp,"%s.img-%04u",bname,N);
			sitr[N]->fd = ::open(tmp,O_RDWR | O_BINARY,0644);
			if (sitr[N]->fd < 0) {
				if (errno == ENOENT) {
					/* this is to be expected. if the file doesn't exist
					 * then the data isn't there. No error message needed. */
					return (l-p);
				}
				bitch(BITCHERROR,"LargeSplitImage::read() error opening fragment %u (%s)",N,strerror(errno));
				return (l-p);
			}
		}

		/* if the fragment has an open file attached, use it! (duh) */
		if (sitr[N]->fd >= 0) {
			/* Linux:     This will fail if it goes beyond the EOF
			 * 
			 * Windows:   For some strange reason seeking beyond EOF is OK.
			 *            read() farther down will fail though. */
			if (lseek(sitr[N]->fd,no,SEEK_SET) != no) {
				return (l-p);
			}
			else {
				rr = ::read(sitr[N]->fd,buf,nor);
				if (rr < 0) rr = 0;
				current_pos += rr;
				buf += rr;
				p -= rr;
				/* if the read was incomplete, return now. */
				if (rr < nor) return (l-p);
			}
		}
		else {
			return (l-p);
		}
	}

	return l;
}

int LargeSplitImage::write(unsigned char *buf,int l)
{
	unsigned long no;
	char tmp[280];
	int N,nor,p;
	int rr;

	if (fragsize == 0 || fd < 0 || l < 0) return 0;

	if (no_fragments) {
		long long p = ::lseek64(iso_fd,current_pos,SEEK_SET);
		if (p < current_pos) {
			if (::ftruncate64(iso_fd,current_pos) < 0) return 0;
			p = ::lseek64(iso_fd,current_pos,SEEK_SET);
		}
		if (p != current_pos) return 0;
		int rd = ::write(iso_fd,buf,l);
		if (rd < 0) rd = 0;
		current_pos += rd;
		return rd;
	}

	p = l;
	while (p > 0) {
		/* which fragment? offset into fragment? how many bytes available to write? */
		N = (int)(current_pos / ((juint64)fragsize));
		no = (int)(current_pos % ((juint64)fragsize));
		nor = fragsize - no;
		if (nor > p) nor = p;

		/* do we need to expand the frag array?
		 * NOTE: See comments in LargeSplitImage::read() */
		if (N >= sitrks) {
			int os = sitrks,k;
			void *t;

			sitrks = N+1;
			t = realloc((void*)sitr,sizeof(frag*) * sitrks);
			if (!t) {
				bitch(BITCHERROR,"LargeSplitImage::write() unable to extend array of file objects");
				update_max();
				return (l-p);
			}

			sitr = (frag**)t;
			for (k=os;k < sitrks;k++) sitr[k] = NULL;
		}

		/* alloc frag object if necessary */
		if (!sitr[N]) {
			sitr[N] = new frag;
			if (!sitr[N]) {
				bitch(BITCHERROR,"LargeSplitImage::write() unable to alloc frag object");
				update_max();
				return (l-p);
			}

			sprintf(tmp,"%s.img-%04u",bname,N);
			sitr[N]->fd = ::open(tmp,O_CREAT | O_RDWR | O_BINARY,0644);
			if (sitr[N]->fd < 0) {
				bitch(BITCHERROR,"LargeSplitImage::write() unable to open/create fragment %u",N);
				update_max();
				return (l-p);
			}
		}

		/* if open file handle, write the data! (duh) */
		if (sitr[N]->fd >= 0) {
			/* Linux does not permit seeking beyond the EOF. So to write the data
			 * where needed we need to ftruncate() the file out there, then lseek().
			 * Windows does not need this. */
			if (lseek(sitr[N]->fd,no,SEEK_SET) != no) {
#ifdef WIN32
				/* If Windows fails this request there must be something seriously wrong! */
				return (l-p);
#else				
				/* Linux needs us to ftruncate() to that
				 * point before allowing us to seek there */
				if (ftruncate(sitr[N]->fd,no) < 0) {
					bitch(BITCHERROR,"LargeSplitImage::write() unable to extend file (fragment %u)",N);
					update_max();
					return (l-p);
				}
				if (lseek(sitr[N]->fd,no,SEEK_SET) != no) {
					bitch(BITCHERROR,"LargeSplitImage::write() unable to lseek(%u)",no);
					update_max();
					return (l-p);
				}
#endif
			}

			rr = ::write(sitr[N]->fd,buf,nor);
			if (rr < 0) rr = 0;
			current_pos += rr;
			buf += rr;
			p -= rr;
			if (rr < nor) {
				update_max();
				bitch(BITCHERROR,"LargeSplitImage::write() unable to write complete data, fragment %u",N);
				return (l-p);
			}
		}
	}

	update_max();
	return l;
}

int LargeSplitImage::update_max()
{
	if (fd < 0) return -1;

	if (current_pos > max_pos) {
		/* update header in directory file */
		if (lseek(fd,13,SEEK_SET) != 13)
			return -1;

		max_pos = current_pos;
		::write(fd,LeHe32bin((unsigned long)(max_pos                 )),4);
		::write(fd,LeHe32bin((unsigned long)(max_pos >> ((juint64)32))),4);
	}

	return 0;
}

