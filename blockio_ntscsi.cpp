/* low-level blockio code for jarchdvd.
 * opens the first CD/DVD-ROM drive in the system and uses the Windows NT SCSI ioctls */

#ifdef WIN32
#include <windows.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include "blockio.h"
#include "bitchin.h"
#include "config.h"

#if READ_METHOD == READ_METHOD_NTSCSI

#ifndef WIN32
#error READ_METHOD set to NTSCSI which requires Microsoft Windows
#endif

#define CTL_CODE( DevType, Function, Method, Access ) (                 \
    ((DevType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method) \
)

#define METHOD_BUFFERED     0
#define METHOD_IN_DIRECT    1
#define METHOD_OUT_DIRECT   2
#define METHOD_NEITHER      3
#define IOCTL_SCSI_BASE		0x00000004
#define FILE_ANY_ACCESS      0
#define FILE_READ_ACCESS     (0x0001)
#define FILE_WRITE_ACCESS    (0x0002)

#define IOCTL_SCSI_PASS_THROUGH_DIRECT  CTL_CODE( IOCTL_SCSI_BASE, 0x0405, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS )

#define  SCSI_IOCTL_DATA_OUT          0
#define  SCSI_IOCTL_DATA_IN           1
#define  SCSI_IOCTL_DATA_UNSPECIFIED  2

typedef struct {
  USHORT Length;
  UCHAR  ScsiStatus;
  UCHAR  PathId;
  UCHAR  TargetId;
  UCHAR  Lun;
  UCHAR  CdbLength;
  UCHAR  SenseInfoLength;
  UCHAR  DataIn;
  ULONG  DataTransferLength;
  ULONG  TimeOutValue;
  ULONG  DataBufferOffset;
  ULONG  SenseInfoOffset;
  UCHAR  Cdb[16];
} SCSI_PASS_THROUGH;

typedef struct {
  USHORT Length;
  UCHAR  ScsiStatus;
  UCHAR  PathId;
  UCHAR  TargetId;
  UCHAR  Lun;
  UCHAR  CdbLength;
  UCHAR  SenseInfoLength;
  UCHAR  DataIn;
  ULONG  DataTransferLength;
  ULONG  TimeOutValue;
  PVOID  DataBuffer;
  ULONG  SenseInfoOffset;
  UCHAR  Cdb[16];
} SCSI_PASS_THROUGH_DIRECT;

typedef struct {
  SCSI_PASS_THROUGH_DIRECT spt;
  ULONG Filler;
  UCHAR ucSenseBuf[32];
} SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER;

/* disallow any major problems we might cause allowing the user to
   specify device names like \\.\SOFTICE or \\.\ACPI when we're
   specifically designed to talk to devices like \\.\D or \\.\F */
static int IsNTDriveLetterName(char *n)
{
	if (n[0] != '\\') return 0;
	if (n[1] != '\\') return 0;
	if (n[2] != '.') return 0;
	if (n[3] != '\\') return 0;
	if (n[5] != ':') return 0;
	if (!isalpha(n[4])) return 0;
	return 1;
}

class JarchBlockIO_NTSCSI : public JarchBlockIO
{
public:
					JarchBlockIO_NTSCSI();
					~JarchBlockIO_NTSCSI();
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
	HANDLE				dev_fd;
	int				alloc_sectors;
	unsigned char*			alloc_buffer;
	juint64				next_sector;
	char				default_cdrom[16];
	char*				GetFirstDrive();
};

JarchBlockIO_NTSCSI::JarchBlockIO_NTSCSI()
{
	alloc_buffer = NULL;
	alloc_sectors = 0;
	next_sector = 0;
	dev_fd = INVALID_HANDLE_VALUE;
}

JarchBlockIO_NTSCSI::~JarchBlockIO_NTSCSI()
{
	close();
}

int JarchBlockIO_NTSCSI::atapi_read(juint64 sector,unsigned char *buf,int N)
{
	unsigned char cmd[12];
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

int JarchBlockIO_NTSCSI::open(char *given_name)
{
	OSVERSIONINFO osver;
	DWORD flags;
	char *name;

	if (dev_fd >= 0)
		close();

	if (!given_name)
		return -1;

	if (!strlen(given_name))
		name = GetFirstDrive();
	else
		name = given_name;

	if (!name)
		return -1;

	bitch(BITCHINFO,"JarchBlockIO_NTSCSI: Windows NT SCSI ioctl driver");
	bitch(BITCHINFO,"JarchBlockIO_NTSCSI: Using device %s",name);

	if (!IsNTDriveLetterName(name)) {
		bitch(BITCHINFO,"%s is not a valid device name",name);
		return 0;
	}

	memset(&osver,0,sizeof(osver));
	osver.dwOSVersionInfoSize = sizeof(osver);
	GetVersionEx(&osver);

	flags = GENERIC_READ;
	if ((osver.dwPlatformId == VER_PLATFORM_WIN32_NT) && (osver.dwMajorVersion > 4)) {
		flags |= GENERIC_WRITE;
		bitch(BITCHINFO,"...Using GENERIC_READ|GENERIC_WRITE for Windows 2000/XP");
	}

	dev_fd = CreateFileA(name,flags,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
	if (dev_fd == INVALID_HANDLE_VALUE) {
		flags ^= GENERIC_WRITE;
		bitch(BITCHINFO,"...Now using GENERIC_READ because Windows doesn't like it");
		dev_fd = CreateFileA(name,flags,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
	}

	if (dev_fd == INVALID_HANDLE_VALUE) {
		bitch(BITCHERROR,"Unable to open block device %s (errno = %s)",name,strerror(errno));
		return 0;
	}

	/* we can do large transfers. the problem is that it really slows down the system! */
	/* HACK: Windows NT has problems with transfers of more than 50 sectors! */
	alloc_sectors = 50;
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

int JarchBlockIO_NTSCSI::close()
{
	if (alloc_buffer) free(alloc_buffer);
	alloc_buffer = NULL;

	if (dev_fd != INVALID_HANDLE_VALUE) CloseHandle(dev_fd);
	dev_fd = INVALID_HANDLE_VALUE;

	return 0;
}

unsigned char *JarchBlockIO_NTSCSI::buffer()
{
	if (dev_fd < 0) return NULL;
	return alloc_buffer;
}

int JarchBlockIO_NTSCSI::buffersize()
{
	if (dev_fd < 0) return 0;
	return alloc_sectors;
}

int JarchBlockIO_NTSCSI::read(int sectors)
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

int JarchBlockIO_NTSCSI::seek(juint64 s)
{
	next_sector = s;
	return 1;
}

int JarchBlockIO_NTSCSI::scsi(unsigned char *cmd,int cmdlen,unsigned char *data,int datalen,int dir)
{
	SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
	unsigned long returned;
	BOOL status;
	int length;

	if (dev_fd == INVALID_HANDLE_VALUE) return -1;

	// a way for the caller to determine if we support this method
	if (cmd == NULL || cmdlen <= 0)
		return 0;	// -1 would mean unsupported

	memset(&swb,0,sizeof(swb));
	swb.spt.Length				= sizeof(SCSI_PASS_THROUGH);
	swb.spt.CdbLength			= cmdlen;
	if (dir == 1) swb.spt.DataIn		= SCSI_IOCTL_DATA_IN;
	else if (dir == 2) swb.spt.DataIn	= SCSI_IOCTL_DATA_OUT;
	else swb.spt.DataIn			= SCSI_IOCTL_DATA_UNSPECIFIED;
	swb.spt.DataTransferLength		= datalen;
	swb.spt.TimeOutValue			= 5000;
	swb.spt.DataBuffer			= data;
	swb.spt.SenseInfoOffset			= ((int)(&swb.ucSenseBuf)) - ((int)(&swb));
	memcpy(swb.spt.Cdb,cmd,cmdlen);
	length = sizeof(swb);

	status = DeviceIoControl(dev_fd,IOCTL_SCSI_PASS_THROUGH_DIRECT,&swb,length,&swb,
		length,&returned,NULL);

	if (!status)
		return -1;

	if (swb.ucSenseBuf[2] & 0xF)
		return -1;

	return 0;
}

int JarchBlockIO_NTSCSI::fd()
{
	return -1;
}

char *JarchBlockIO_NTSCSI::GetFirstDrive()
{
	DWORD ba;
	int N;

	ba = GetLogicalDrives();
	for (N=0;N < 26;N++) {
		if (ba & (1<<N)) {
			sprintf(default_cdrom,"%c:\\",N+'A');
			if (GetDriveType(default_cdrom) == DRIVE_CDROM) {
				sprintf(default_cdrom,"\\\\.\\%c:",N+'A');
				return default_cdrom;
			}
		}
	}

	return NULL;
}

// interface to the rest of the program
JarchBlockIO* blockiodefault()
{
	return new JarchBlockIO_NTSCSI;
}

#endif //READ_METHOD
