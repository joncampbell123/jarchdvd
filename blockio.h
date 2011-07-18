
#ifndef __INCLUDE_BLOCKIO_H
#define __INCLUDE_BLOCKIO_H

#include "config.h"

class JarchBlockIO
{
public:
					JarchBlockIO();
	virtual				~JarchBlockIO();
public:
	// required
	virtual int			open(const char *name) = 0;
	virtual int			close() = 0;
	virtual unsigned char*		buffer() = 0;
	virtual int			buffersize() = 0;
	virtual int			seek(juint64 s) = 0;
	virtual int			read(int n) = 0; /* we provide one that issues generic 2048-byte READs */
	// optional
	virtual int			fd() = 0;
	virtual int			scsi(unsigned char *cmd,int cmdlen,unsigned char *data,int datalen,int dir) = 0;
	// dir: 0=none, 1=read, 2=write
	virtual unsigned char*		get_last_sense(size_t *size) = 0;
public: /* predefined */
};

int blockio_base_read(JarchBlockIO *bdev,juint64 sector,unsigned char *buf,int N);

JarchBlockIO* blockiodefault(const char *drv);

#endif //__INCLUDE_BLOCKIO_H

