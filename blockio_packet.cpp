/* low-level blockio code for jarchdvd.
 * opens /dev/dvd then uses the CDROM_PACKET ioctl to send ATAPI READ commands
 * to the DVD-ROM drive. This allows finer control over ripping and better recovery
 * from bad sectors. */

#include "config.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include "blockio.h"
#include "blockio_packet.h"
#include "bitchin.h"
#include "config.h"

JarchBlockIO_PK::JarchBlockIO_PK() : JarchBlockIO()
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

int JarchBlockIO_PK::open(const char *given_name)
{
	const char *name;

	if (dev_fd >= 0)
		close();

	if (!given_name)
		return -1;

	if (*given_name == 0)
		name = "/dev/dvd";		// default device
	else
		name = given_name;

	bitch(BITCHINFO,"JarchBlockIO_PK: Linux CDROM_PACKET ioctl driver");
	bitch(BITCHINFO,"JarchBlockIO_PK: Using device %s",name);

	dev_fd = ::open(name,O_RDWR | O_LARGEFILE | O_NONBLOCK | O_EXCL);
	if (dev_fd < 0) {
		bitch(BITCHERROR,"JarchBlockIO_PK: Unable to open block device %s (errno = %s)",name,strerror(errno));
		return -1;
	}

	/* we can do large transfers. the problem is that it really slows down the system! */
	alloc_sectors = (31*1024) / 2048; /* 31KB */
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

int JarchBlockIO_PK::seek(juint64 s)
{
	next_sector = s;
	return 1;
}

int JarchBlockIO_PK::scsi(unsigned char *cmd,int cmdlen,unsigned char *data,int datalen,int dir)
{
	struct cdrom_generic_command pk;
	int length,returned;

	if (dev_fd < 0) return -1;

	// a way for the caller to determine if we support this method
	last_sense_len = 0;
	if (cmd == NULL || cmdlen <= 0)
		return 0;	// -1 would mean unsupported

	memset(&pk,0,sizeof(pk));
	memset(&last_sense,0,sizeof(last_sense));
	memcpy(pk.cmd,cmd,cmdlen > 12 ? 12 : cmdlen);
	pk.buffer = data;
	pk.buflen = datalen;
	pk.sense  = &last_sense; /* <- NTS: The kernel doesn't fill in sense data, unless CDROM_SEND_PACKET fails (returns errno=EIO) */
	if (dir == 1)           pk.data_direction = CGC_DATA_READ;
	else if (dir == 2)      pk.data_direction = CGC_DATA_WRITE;
	else                    pk.data_direction = CGC_DATA_UNKNOWN;
	pk.quiet = 0;
	pk.timeout = 30000000;    /* you have 30 seconds to comply with this request */
	if (ioctl(dev_fd,CDROM_SEND_PACKET,(void*)(&pk)) < 0) {
		if (errno != EIO) {
			bitch(BITCHWARNING,"CDROM_SEND_PACKET errno %s",strerror(errno));
			return -1;
		}

		pk.buflen = datalen;
	}

	if (last_sense.error_code != 0)
		last_sense_len = 7 + last_sense.add_sense_len;

	/* NOBODY documents this... but the ioctl() updates the structure we passed back
	 * with the actual amount of data transferred (SG_IO residual). But you would
	 * only know this by reading the Linux kernel source block/scsi_ioctl.c >:( */
	if (pk.buflen > datalen) {
		bitch(BITCHWARNING,"SG ioctl residual is greater than total data transfer");
		pk.buflen = datalen;
	}

	return datalen - pk.buflen;
}

int JarchBlockIO_PK::fd()
{
	return dev_fd;
}

unsigned char* JarchBlockIO_PK::get_last_sense(size_t *size) {
	if (size != NULL) *size = last_sense_len;
	/* WARNING: This happens to work because Linux defines the fields in such a way they line up with the raw data */
	return (unsigned char*)(&last_sense);
}

int JarchBlockIO_PK::read(int sectors)
{
	int rd,ret=0,dord;

	if (dev_fd < 0)
		return 0;

	if (sectors <= 0)
		return 0;

	do {
		if (sectors > alloc_sectors)
			dord = alloc_sectors;
		else
			dord = sectors;

		rd = blockio_base_read(this,next_sector,alloc_buffer,dord);
		if (rd < dord) break;
		next_sector += rd;
		sectors -= rd;
		ret += rd;
	} while (sectors > 0);

	return ret;
}

