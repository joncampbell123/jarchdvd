
#ifndef __INCLUDE_BLOCKIO_H
#define __INCLUDE_BLOCKIO_H

#include "config.h"

class JarchBlockIO
{
public:
	// required
	virtual int			open(char *name) = 0;
	virtual int			close() = 0;
	virtual unsigned char*		buffer() = 0;
	virtual int			buffersize() = 0;
	virtual int			seek(juint64 s) = 0;
	virtual int			read(int n) = 0;
	// optional
	virtual int			fd() = 0;
	virtual int			scsi(unsigned char *cmd,int cmdlen,unsigned char *data,int datalen,int dir) = 0;
	// dir: 0=none, 1=read, 2=write
};

JarchBlockIO* blockiodefault();

#endif //__INCLUDE_BLOCKIO_H

