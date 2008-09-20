/* low-level blockio code for jarchdvd.
 * opens /dev/dvd then uses the CDROM_PACKET ioctl to send ATAPI READ commands
 * to the DVD-ROM drive. This allows finer control over ripping and better recovery
 * from bad sectors. */

#include "config.h"
#include <sys/stat.h>
#include <sys/types.h>
#ifdef LINUX
#include <linux/cdrom.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include "blockio.h"
#include "bitchin.h"
#include "config.h"

#if READ_METHOD == READ_METHOD_PACKET

#ifndef LINUX
#error READ_METHOD_PACKET is supported only by Linux!
#endif

class JarchBlockIO_PK : public JarchBlockIO
{
public:
					JarchBlockIO_PK();
					~JarchBlockIO_PK();
public:
        // required
	virtual int                     open(char *name);
	virtual int                     close();
	virtual unsigned char*          buffer();
	virtual int                     buffersize();
	virtual int                     seek(juint64 s);
	virtual int                     read(int n);
	// optional
	virtual int                     fd();
	virtual int                     scsi(unsigned char *cmd,int cmdlen,unsigned char *data,int datalen,int dir);
	// dir: 0=none, 1=read, 2=write
private:
	int				atapi_read(juint64 sector,unsigned char *buf,int N);
	int				dev_fd;
	int				alloc_sectors;
	unsigned char*			alloc_buffer;
	juint64				next_sector;
};

JarchBlockIO_PK::JarchBlockIO_PK()
{
	alloc_buffer = NULL;
	alloc_sectors = 0;
	next_sector = 0;
	dev_fd = -1;
}

JarchBlockIO_PK::~JarchBlockIO_PK()
{
	close();
}

int JarchBlockIO_PK::atapi_read(juint64 sector,unsigned char *buf,int N)
{
	unsigned char cmd[12],sense[60];
	int retr=1,err;

	memset(buf,0,N*2048);
	cmd[ 0] = 0xA8;		/* SCSI MMC-2 reference says to use READ(12) for DVD media */
	cmd[ 1] = 0x00;
	cmd[ 2] = sector >> 24;
	cmd[ 3] = sector >> 16;
	cmd[ 4] = sector >> 8;
	cmd[ 5] = sector;
	cmd[ 6] = N >> 24;
	cmd[ 7] = N >> 16;
	cmd[ 8] = N >> 8;
	cmd[ 9] = N;
	cmd[10] = 0;
	cmd[11] = 0;
	err = scsi(cmd,12,buf,N*2048,1);

	if (err < 0)
		return -1;

	return N;
}

int JarchBlockIO_PK::open(char *given_name)
{
	char *name;

	if (dev_fd >= 0)
		close();

	if (!given_name)
		return -1;

	if (!strlen(given_name))
		name = "/dev/dvd";		// default device
	else
		name = given_name;

	bitch(BITCHINFO,"JarchBlockIO_PK: Linux CDROM_PACKET ioctl driver");
	bitch(BITCHINFO,"JarchBlockIO_PK: Using device %s",name);

	dev_fd = ::open(name,O_RDONLY | O_LARGEFILE | O_NONBLOCK);
	if (dev_fd < 0) {
		bitch(BITCHERROR,"JarchBlockIO_PK: Unable to open block device %s (errno = %s)",name,strerror(errno));
		return -1;
	}

	/* we can do large transfers. the problem is that it really slows down the system! */
	alloc_sectors = 64;
	bitch(BITCHINFO,"Allocating memory to hold %d sectors",alloc_sectors);

	alloc_buffer = (unsigned char*)malloc(alloc_sectors*2048);
	if (!alloc_buffer) {
		bitch(BITCHINFO,"Unable to allocate sector buffer!");
		close();
		return -1;
	}

	next_sector = 0;
	return 0;
}

int JarchBlockIO_PK::close()
{
	if (alloc_buffer) free(alloc_buffer);
	alloc_buffer = NULL;

	if (dev_fd >= 0) ::close(dev_fd);
	dev_fd = -1;

	return 0;
}

unsigned char *JarchBlockIO_PK::buffer()
{
	if (dev_fd < 0) return NULL;
	return alloc_buffer;
}

int JarchBlockIO_PK::buffersize()
{
	if (dev_fd < 0) return 0;
	return alloc_sectors;
}

int JarchBlockIO_PK::read(int sectors)
{
	int rd;

	if (dev_fd < 0)
		return 0;

	if (sectors < 0)
		return 0;

	if (sectors > alloc_sectors)
		sectors = alloc_sectors;

	rd = atapi_read(next_sector,alloc_buffer,sectors);
	if (rd <= 0)		rd = 0;
	else			next_sector += rd;

	return rd;
}

int JarchBlockIO_PK::seek(juint64 s)
{
	next_sector = s;
	return 1;
}

int JarchBlockIO_PK::scsi(unsigned char *cmd,int cmdlen,unsigned char *data,int datalen,int dir)
{
	struct cdrom_generic_command pk;
	struct request_sense s;
	int length,returned;

	if (dev_fd < 0) return -1;

	// a way for the caller to determine if we support this method
	if (cmd == NULL || cmdlen <= 0)
		return 0;	// -1 would mean unsupported

	memset(&pk,0,sizeof(pk));
	memset(&s,0,sizeof(s));
	memcpy(pk.cmd,cmd,cmdlen > 12 ? 12 : cmdlen);
	pk.buffer = data;
	pk.buflen = datalen;
	pk.sense  = &s;
	if (dir == 1)           pk.data_direction = CGC_DATA_READ;
	else if (dir == 2)      pk.data_direction = CGC_DATA_WRITE;
	else                    pk.data_direction = CGC_DATA_UNKNOWN;
	pk.quiet  = 1;
	pk.timeout = 10000000;    /* you have 10 seconds to comply with this request */
	if (ioctl(dev_fd,CDROM_SEND_PACKET,(void*)(&pk)) < 0)
		return -1;
	if (s.sense_key != 0)
		return -1;

	return 0;
}

int JarchBlockIO_PK::fd()
{
	return dev_fd;
}

// interface to the rest of the program
JarchBlockIO* blockiodefault()
{
	return new JarchBlockIO_PK;
}

#endif //READ_METHOD
