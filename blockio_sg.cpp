/* low-level blockio code for jarchdvd.
 * opens /dev/dvd then uses the SG_IO ioctl to send ATAPI READ commands
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
#include "blockio_sg.h"
#include "bitchin.h"
#include "config.h"

JarchBlockIO_SG::JarchBlockIO_SG()
{
	alloc_buffer = NULL;
	alloc_sectors = 0;
	next_sector = 0;
	sense_len = 0;
	dev_fd = -1;
}

JarchBlockIO_SG::~JarchBlockIO_SG()
{
	close();
}

int JarchBlockIO_SG::open(const char *given_name)
{
	int retry;
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

	retry = 3;
try_again:
	dev_fd = ::open(name,O_RDWR | O_LARGEFILE | O_NONBLOCK | O_EXCL);
	if (dev_fd < 0) {
		bitch(BITCHERROR,"JarchBlockIO_SG: Unable to open block device %s (errno = %s)",name,strerror(errno));
		if (errno == EBUSY && --retry > 0) {
			sleep(1);
			goto try_again;
		}
		return -1;
	}

	/* we can do large transfers. the problem is that it really slows down the system! */
	alloc_sectors = (31*1024) / 2048;
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

int JarchBlockIO_SG::seek(juint64 s)
{
	next_sector = s;
	return 1;
}

int JarchBlockIO_SG::scsi(unsigned char *cmd,int cmdlen,unsigned char *data,int datalen,int dir)
{
	struct sg_io_hdr sg;
	int length,returned;

	if (dev_fd < 0) {
		bitch(BITCHERROR,"SG_IO BUG: called when dev_fd < 0 (not open)");
		return -1;
	}

	// a way for the caller to determine if we support this method
	if (cmd == NULL || cmdlen <= 0)
		return 0;	// -1 would mean unsupported

	memset(sense,0,sizeof(sense));
	memset(&sg,0,sizeof(sg));
	sg.interface_id         = 'S';
	sg.cmd_len              = cmdlen;
	sg.mx_sb_len            = sizeof(sense);
	sg.sbp                  = sense;
	sg.dxfer_len            = datalen;
	sg.dxferp               = data;
	sg.cmdp                 = cmd;
	sg.timeout              = 30000;		/* 10 second */
	sg.flags                = SG_FLAG_DIRECT_IO;
	if (dir == 1)		sg.dxfer_direction = SG_DXFER_FROM_DEV;
	else if (dir == 2)	sg.dxfer_direction = SG_DXFER_TO_DEV;
	else			sg.dxfer_direction = SG_DXFER_NONE;

	if (ioctl(dev_fd,SG_IO,(void*)(&sg)) < 0) {
		sense_len = sg.sb_len_wr;
		bitch(BITCHWARNING,"SG_IO ioctl failed errno %s",strerror(errno));
		return -1;
	}

	sense_len = sg.sb_len_wr;
	if (sg.driver_status != 0) {
		bitch(BITCHWARNING,"SG_IO OK but driver status 0x%02X",sg.driver_status);
		return -1;
	}
	if (sg.masked_status != 0) {
		bitch(BITCHWARNING,"SG_IO OK but mask status 0x%02X masked 0x%02X",sg.status,sg.masked_status);
		return -1;
	}
	if (sg.host_status != 0) {
		bitch(BITCHWARNING,"SG_IO OK but host status 0x%02X",sg.host_status);
		return -1;
	}

	/* FIXME: I'm not getting very good sense data for sector read failures from my
	 *        LG Bluray reader USB drive. This results in sector read failures getting
	 *        counted as successfuly reads and causing portions of the DVD rip to have
	 *        garbage data. */
	if ((sg.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
		if (sense_len == 0) {
			struct sg_io_hdr sg2;
			unsigned char scmd[6] = {0x03,0x00,0x00,0x00,sizeof(sense)/*allocation length*/,0x00};

			bitch(BITCHWARNING,"SG ioctl did not return sense data, fetching");

			memset(&sg2,0,sizeof(sg2));
			memset(sense,0,sizeof(sense));
			sg2.interface_id         = 'S';
			sg2.cmd_len              = 6;
			sg2.sbp                  = NULL;
			sg2.dxfer_len            = sizeof(sense);
			sg2.dxferp               = sense;
			sg2.cmdp                 = scmd;
			sg2.timeout              = 30000;
			sg2.flags                = SG_FLAG_DIRECT_IO;
			sg2.dxfer_direction      = SG_DXFER_FROM_DEV;

			if (ioctl(dev_fd,SG_IO,(void*)(&sg2)) < 0) {
				bitch(BITCHWARNING,"SG ioctl did not return sense data either");
			}
			else if (sg2.resid != 0) {
				sense_len = sg2.dxfer_len - sg2.resid;
			}
			else {
				/* They aren't returning residual either */
				sense_len = 7 + sense[7]; /* <- FIXME: Is this right? */
			}
		}
	}

	if (sg.resid > sg.dxfer_len) {
		bitch(BITCHWARNING,"SG ioctl residual is greater than total data transfer");
		sg.resid = sg.dxfer_len;
	}

	return sg.dxfer_len - sg.resid;
}

int JarchBlockIO_SG::fd()
{
	return dev_fd;
}

unsigned char* JarchBlockIO_SG::get_last_sense(size_t *size) {
	if (size != NULL) *size = sense_len;
	return sense;
}

