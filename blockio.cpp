
#include <string.h>
#include <stdlib.h>
#include "bitchin.h"
#include "blockio.h"
#include "blockio_sg.h"
#include "blockio_packet.h"

JarchBlockIO::JarchBlockIO() {
}

JarchBlockIO::~JarchBlockIO() {
}

JarchBlockIO* blockiodefault(const char *drvname) {
	JarchBlockIO *drv = NULL;

	if (drvname == NULL)
		drvname = getenv("JDRIVER");
	if (drvname != NULL && *drvname == 0)
		drvname = NULL;

	if (drvname == NULL) {
#ifdef LINUX
		/* FIXME: On recent kernels linux_sg is unable to obtain CSS title keys. Why? */
		drvname = "linux_packet";
#endif
	}

	if (drvname == NULL) {
		bitch(BITCHERROR,"No driver name specified and no default available");
		return NULL;
	}

	if (!strcasecmp(drvname,"linux_sg"))
		drv = new JarchBlockIO_SG;
	else if (!strcasecmp(drvname,"linux_packet"))
		drv = new JarchBlockIO_PK;

	return drv;
}

int blockio_base_read(JarchBlockIO *bdev,juint64 sector,unsigned char *buf,int N) {
	unsigned char cmd[12];
	unsigned char *sense;
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
	err = bdev->scsi(cmd,12,buf,N*2048,1);

	if (err < 0)
		return -1;
	if (err & (2048 - 1))
		bitch(BITCHWARNING,"Read returned data not a multiple of sector size");

	sense = bdev->get_last_sense(NULL);
	if (sense != NULL) {
		unsigned char key = sense[2] & 0x0F;

		if (sense[2] & 0x20) {
			bitch(BITCHWARNING,"SCSI device says read failed because medium sector size does not match requested length");
			err = -1;
		}
		if (key >= 2) {
			bitch(BITCHWARNING,"SCSI command failed sense key %u",key);
			err = -1;
		}
	}

	return (err >= 0) ? (err >> 11) : err;
}

