
#include "config.h"
#include "rippedmap.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include "bitchin.h"

RippedMap::RippedMap()
{
	bitmap_size = 0;
	fd = -1;
}

RippedMap::~RippedMap()
{
	close();
}

static const char* RipMapSig = "JRipMap1";

int RippedMap::open(const char *name)
{
	unsigned char buf[16];

	close();

	fd = ::open(name,O_RDWR | O_BINARY,0644);
	if (fd < 0) {
		if (errno == ENOENT) {
			fd = ::open(name,O_RDWR | O_CREAT | O_TRUNC | O_BINARY,0644);
			if (fd < 0) {
				bitch(BITCHERROR,"Cannot create ripmap file %s",name);
				return -1;
			}

			if (lseek(fd,0,SEEK_SET) != 0) {
				bitch(BITCHERROR,"Cannot lseek(0) for file %s",name);
				::close(fd);
				return -1;
			}

			write(fd,RipMapSig,8);
			write(fd,LeHe32bin(0),4);
		}
		else {
			bitch(BITCHERROR,"Cannot open ripmap %s (%s)",name,strerror(errno));
		}
	}

// format of a ripmap
//
// char[8]		signature "JRipMap1"
// unsigned long	number of entries (bits)
// char[...]		bitmap, LSB first

	if (lseek(fd,0,SEEK_SET) != 0) {
		bitch(BITCHERROR,"Cannot lseek(0) for file %s",name);
		::close(fd);
		return -1;
	}

	file_size = lseek(fd,0,SEEK_END);
	if (file_size < 12) {
		bitch(BITCHERROR,"Cannot open ripmap %s, file too small",name);
		::close(fd);
		return -1;
	}

	lseek(fd,0,SEEK_SET);
	if (read(fd,buf,12) < 12) {
		bitch(BITCHERROR,"ripmap %s file read error",name);
		::close(fd);
		return -1;
	}

	if (memcmp(buf,RipMapSig,8)) {
		bitch(BITCHERROR,"bad signature in ripmap file %s",name);
		::close(fd);
		return -1;
	}

	bitmap_size = binLeHe32(buf+8);
	return 0;
}

int RippedMap::close()
{
	if (fd >= 0) {
		::close(fd);
		fd = -1;
	}

	return 0;
}

int RippedMap::getsize()
{
	return bitmap_size;
}

int RippedMap::set(int N,int val)
{
	unsigned long x;
	char c,shf;

	if (fd < 0) return 0;

	x = 12 + (N >> 3);
	shf = N & 7;

	if (x >= file_size) {
		file_size = lseek(fd,0,SEEK_END);

		c = 0;
		while (x >= file_size) {
			if (write(fd,&c,1) < 1) {
				bitch(BITCHERROR,"RippedMap::set() write failed");
				return -1;
			}
			file_size++;
		}
	}

	if (N >= bitmap_size) {
		bitmap_size = N+1;
		if (lseek(fd,8,SEEK_SET) != 8) {
			bitch(BITCHERROR,"RippedMap::set() lseek(8) failed");
			return -1;
		}
		if (write(fd,LeHe32bin(bitmap_size),4) < 4) {
			bitch(BITCHERROR,"RippedMap::set() write() failed to update header bitmap size");
			return -1;
		}
	}

	c = 0;
	if (lseek(fd,x,SEEK_SET) != x) {
		bitch(BITCHERROR,"RippedMap::set() lseek(%u) failed",x);
		return -1;
	}

	if (read(fd,&c,1) < 1) {
		bitch(BITCHERROR,"RippedMap::set() unable to read() data before modification of bit %u",N);
		return -1;
	}

	if (val)				c |=   1<<shf;
	else					c &= ~(1<<shf);

	if (lseek(fd,x,SEEK_SET) != x) {
		bitch(BITCHERROR,"RippedMap::set() lseek(%u) failed",x);
		return -1;
	}

	if (write(fd,&c,1) < 1) {
		bitch(BITCHERROR,"RippedMap::set() write() failed to update bit %u",N);
		return -1;
	}

	return 0;
}

int RippedMap::get(int N)
{
	unsigned long x;
	char c,shf;

	if (fd < 0) return 0;
	if (N >= bitmap_size) return 0;

	x = 12 + (N >> 3);
	shf = N & 7;
	if (lseek(fd,x,SEEK_SET) != x) return 0;
	if (read(fd,&c,1) < 1) return 0;
	return (int)((c >> shf)&1);
}

void DoTestRippedMap()
{
	RippedMap *m = NULL;
	int r;

	bitch(BITCHINFO,"*** RippedMap test ***");
	remove("test.ripmap");

	bitch(BITCHINFO,"Allocating object");
	m = new RippedMap;

	bitch(BITCHINFO,"Opening test file");
	if (m->open("test.ripmap") < 0) {
		bitch(BITCHINFO,"...failed");
		goto finish;
	}

	r = m->getsize();
	bitch(BITCHINFO,"Size should be 0. Got %d",r);
	if (r != 0) goto finish;

	for (r=0;r < 512;r += 4) {
		if (m->set(r,1) < 0) {
			bitch(BITCHERROR,"Unable to set bit %u",r);
			goto finish;
		}
	}

	bitch(BITCHINFO,"Set every 4th bit to 1");

	r = m->getsize();
	bitch(BITCHINFO,"Size should be 509. Got %d",r);
	if (r != 509) goto finish;

	for (r=0;r < 512;r += 4) {
		if (m->get(r) != 1)
			goto finish;
		if (m->get(r+1) != 0)
			goto finish;
		if (m->get(r+2) != 0)
			goto finish;
		if (m->get(r+3) != 0)
			goto finish;
	}

	bitch(BITCHINFO,"Passed pattern test");
	m->close();
	remove("test.ripmap");

finish:
	if (m) {
		bitch(BITCHINFO,"Closing object");
		m->close();
		bitch(BITCHINFO,"Deallocating object");
		delete m;
	}
}

