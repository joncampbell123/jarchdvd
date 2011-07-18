
#ifdef LINUX
#include <sys/ioctl.h>
#include <scsi/sg.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

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
	virtual unsigned char*		get_last_sense(size_t *size);
private:
	int				atapi_read(juint64 sector,unsigned char *buf,int N);
	int				dev_fd;
	int				alloc_sectors;
	unsigned char*			alloc_buffer;
	juint64				next_sector;
};

#endif

