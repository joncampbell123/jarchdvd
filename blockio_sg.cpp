/* low-level blockio code for jarchdvd.
 * opens /dev/dvd then uses the SG_IO ioctl to send ATAPI READ commands
 * to the DVD-ROM drive. This allows finer control over ripping and better recovery
 * from bad sectors. */

#include "config.h"
#include <sys/stat.h>
#include <sys/types.h>
#ifdef LINUX
#include <sys/ioctl.h>
#include <scsi/sg.h>
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

#if READ_METHOD == READ_METHOD_SG

#ifndef LINUX
#error READ_METHOD_PACKET is supported only by Linux!
#endif

class JarchBlockIO_SG : public JarchBlockIO
{
public:
					JarchBlockIO_SG();
					~JarchBlockIO_SG();
public:
        // required
	virtual int                     open(const char *name);
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

JarchBlockIO_SG::JarchBlockIO_SG()
{
	alloc_buffer = NULL;
	alloc_sectors = 0;
	next_sector = 0;
	dev_fd = -1;
}

JarchBlockIO_SG::~JarchBlockIO_SG()
{
	close();
}

int JarchBlockIO_SG::atapi_read(juint64 sector,unsigned char *buf,int N)
{
	unsigned char cmd[12],sense[60];
	struct sg_io_hdr sg;
	int retr=1,err;

	memset(buf,0,N*2048);
	memset(&sg,0,sizeof(sg));
	sg.interface_id         = 'S';
	sg.dxfer_direction      = SG_DXFER_FROM_DEV;
	sg.cmd_len              = 12;
	sg.mx_sb_len            = sizeof(sense);
	sg.sbp                  = sense;
	sg.sb_len_wr            = 0;
	sg.iovec_count          = 0;
	sg.dxfer_len            = N*2048;
	sg.dxferp               = buf;
	sg.cmdp                 = cmd;
	sg.timeout              = 10000;		/* 10 second */
	sg.flags                = 0;
	sg.resid                = 0;
	sg.duration             = 0;
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
	err = ioctl(dev_fd,SG_IO,(void*)(&sg));

	if (err < 0)
		return -1;

	if (!(sg.masked_status == 0 || sg.masked_status == 2))
		return -1;

	if (sg.resid > 0) {
		N = sg.dxfer_len - sg.resid;
		if (N < 0) N = 0;
		N >>= 11;
	}

	return N;
}

int JarchBlockIO_SG::open(const char *given_name)
{
	const char *name;

	if (dev_fd >= 0)
		close();

	if (!given_name)
		return -1;

	if (!strlen(given_name))
		name = "/dev/dvd";		// default device
	else
		name = given_name;

	bitch(BITCHINFO,"JarchBlockIO_SG: Linux SG_IO ioctl driver");
	bitch(BITCHINFO,"JarchBlockIO_SG: Using device %s",name);

	dev_fd = ::open(name,O_RDONLY | O_LARGEFILE | O_NONBLOCK);
	if (dev_fd < 0) {
		bitch(BITCHERROR,"JarchBlockIO_SG: Unable to open block device %s (errno = %s)",name,strerror(errno));
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

int JarchBlockIO_SG::close()
{
	if (alloc_buffer) free(alloc_buffer);
	alloc_buffer = NULL;

	if (dev_fd >= 0) ::close(dev_fd);
	dev_fd = -1;

	return 0;
}

unsigned char *JarchBlockIO_SG::buffer()
{
	if (dev_fd < 0) return NULL;
	return alloc_buffer;
}

int JarchBlockIO_SG::buffersize()
{
	if (dev_fd < 0) return 0;
	return alloc_sectors;
}

int JarchBlockIO_SG::read(int sectors)
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

int JarchBlockIO_SG::seek(juint64 s)
{
	next_sector = s;
	return 1;
}

int JarchBlockIO_SG::scsi(unsigned char *cmd,int cmdlen,unsigned char *data,int datalen,int dir)
{
	unsigned char sense[32];
	struct sg_io_hdr sg;
	int length,returned;

	if (dev_fd < 0) return -1;

	// a way for the caller to determine if we support this method
	if (cmd == NULL || cmdlen <= 0)
		return 0;	// -1 would mean unsupported

	memset(sense,0,sizeof(sense));
	memset(&sg,0,sizeof(sg));
	sg.interface_id         = 'S';
	sg.cmd_len              = cmdlen;
	sg.mx_sb_len            = sizeof(sense);
	sg.sbp                  = sense;
	sg.sb_len_wr            = 0;
	sg.iovec_count          = 0;
	sg.dxfer_len            = datalen;
	sg.dxferp               = data;
	sg.cmdp                 = cmd;
	sg.timeout              = 10000;		/* 10 second */
	sg.flags                = 0;
	sg.resid                = 0;
	sg.duration             = 0;
	if (dir == 1)		sg.dxfer_direction = SG_DXFER_FROM_DEV;
	else if (dir == 2)	sg.dxfer_direction = SG_DXFER_TO_DEV;
	else			sg.dxfer_direction = SG_DXFER_NONE;

	if (ioctl(dev_fd,SG_IO,(void*)(&sg)) < 0)
		return -1;
	if (sense[2] & 0xF)
		return -1;

	return 0;
}

int JarchBlockIO_SG::fd()
{
	return dev_fd;
}

// interface to the rest of the program
JarchBlockIO* blockiodefault()
{
	return new JarchBlockIO_SG;
}

#endif //READ_METHOD
