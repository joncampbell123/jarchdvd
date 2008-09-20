/* low-level blockio code for jarchdvd.
 * Uses the Mac OS X IOKit */

#include <sys/stat.h>
#include <sys/types.h>
#ifdef LINUX
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

#if READ_METHOD == READ_METHOD_IOKIT

#ifndef MACOSX
#error This code requires Mac OS X
#endif

//#include <statdefs.h>
#include <mach/mach.h>
#include <Carbon/Carbon.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/scsi-commands/SCSITaskLib.h>
#include <mach/mach_error.h>

class JarchBlockIO_SG : public JarchBlockIO
{
public:
					JarchBlockIO_SG();
					~JarchBlockIO_SG();
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
	int				alloc_sectors;
	unsigned char*			alloc_buffer;
	juint64				next_sector;
	IOCFPlugInInterface **plugInInterface;
	MMCDeviceInterface **mmcDeviceInterface;
	SCSITaskDeviceInterface **scsiTaskDeviceInterface;
	mach_port_t masterPort;
};

JarchBlockIO_SG::JarchBlockIO_SG()
{
	alloc_buffer = NULL;
	alloc_sectors = 0;
	next_sector = 0;
}

JarchBlockIO_SG::~JarchBlockIO_SG()
{
	close();
}

int JarchBlockIO_SG::atapi_read(juint64 sector,unsigned char *buf,int N)
{
        unsigned char cmd[12],sense[60];
        int retr=1,err;

        memset(buf,0,N*2048);
        cmd[ 0] = 0xA8;         /* SCSI MMC-2 reference says to use READ(12) for DVD media */
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

int JarchBlockIO_SG::open(char *given_name)
{
	char *name;

	if (!given_name)
		return -1;

	if (!strlen(given_name))
		name = "IODVDServices";		// default device
	else
		name = given_name;

	bitch(BITCHINFO,"JarchBlockIO_SG: Mac OS X IOKit driver");
	bitch(BITCHINFO,"JarchBlockIO_SG: Using device %s",name);

	io_iterator_t scsiObjectIterator = 0;
	IOReturn ioReturnValue = kIOReturnSuccess;
	CFMutableDictionaryRef dict = NULL;
	io_object_t scsiDevice = 0;
	HRESULT plugInResult = 0;
	SInt32 score = 0;
	int err = -1;
	char *realdevice = NULL, *tmp;
	int driveidx = 1, idx = 1;

	realdevice = tmp = strdup(name);
	tmp = strchr(tmp, '/');
	if (tmp != NULL) {
		*tmp++ = '\0';
		driveidx = atoi(tmp);
	}

	ioReturnValue = IOMasterPort(bootstrap_port, &masterPort);

	if (ioReturnValue != kIOReturnSuccess)
		goto out;

	dict = IOServiceMatching(realdevice);
	if (dict == NULL)
		goto out;

	ioReturnValue = IOServiceGetMatchingServices(masterPort, dict,
						    &scsiObjectIterator);
	dict = NULL;

	if (ioReturnValue != kIOReturnSuccess)
		goto out;

	if (driveidx <= 0)
		driveidx = 1;

	idx = 1;
	while ((scsiDevice = IOIteratorNext(scsiObjectIterator)) != 0) {
		if (idx == driveidx)
			break;
		IOObjectRelease(scsiDevice);
		scsiDevice = 0;
		idx++;
	}

	if (scsiDevice == 0)
		goto out;

	ioReturnValue = IOCreatePlugInInterfaceForService(scsiDevice,
			kIOMMCDeviceUserClientTypeID,
			kIOCFPlugInInterfaceID,
			&plugInInterface, &score);
	if (ioReturnValue != kIOReturnSuccess) {
		goto try_generic;
	}

	plugInResult = (*plugInInterface)->QueryInterface(plugInInterface,
				CFUUIDGetUUIDBytes(kIOMMCDeviceInterfaceID),
				(void**)(&mmcDeviceInterface));

	if (plugInResult != KERN_SUCCESS)
		goto out;

	scsiTaskDeviceInterface =
		(*mmcDeviceInterface)->GetSCSITaskDeviceInterface(mmcDeviceInterface);

	if (scsiTaskDeviceInterface == NULL)
		goto out;

	goto init;

try_generic:
	ioReturnValue = IOCreatePlugInInterfaceForService(scsiDevice,
					kIOSCSITaskDeviceUserClientTypeID,
					kIOCFPlugInInterfaceID,
					&plugInInterface, &score);
	if (ioReturnValue != kIOReturnSuccess)
		goto out;

	plugInResult = (*plugInInterface)->QueryInterface(plugInInterface,
			    CFUUIDGetUUIDBytes(kIOSCSITaskDeviceInterfaceID),
					(void**)(&scsiTaskDeviceInterface));

	if (plugInResult != KERN_SUCCESS)
		goto out;

init:
    do {
		bitch(BITCHINFO,"Waiting for opportunity to get exclusive access to drive...");
		usleep(1000000);
	} while (!(*scsiTaskDeviceInterface)->IsExclusiveAccessAvailable(scsiTaskDeviceInterface));

	ioReturnValue =
		(*scsiTaskDeviceInterface)->ObtainExclusiveAccess(scsiTaskDeviceInterface);

	if (ioReturnValue != kIOReturnSuccess) {
		bitch(BITCHWARNING,"Unable to get exclusive access to drive.");
		goto out;
	}

	if (mmcDeviceInterface) {
		(*mmcDeviceInterface)->AddRef(mmcDeviceInterface);
	}
	(*scsiTaskDeviceInterface)->AddRef(scsiTaskDeviceInterface);
//	scglocal(scgp)->mmcDeviceInterface = mmcDeviceInterface;
//	scglocal(scgp)->scsiTaskDeviceInterface = scsiTaskDeviceInterface;
//	scglocal(scgp)->masterPort = masterPort;
	err = 1;

out:
	if (scsiTaskDeviceInterface != NULL) {
		(*scsiTaskDeviceInterface)->Release(scsiTaskDeviceInterface);
	}

	if (plugInInterface != NULL) {
		(*plugInInterface)->Release(plugInInterface);
	}

	if (scsiDevice != 0) {
		IOObjectRelease(scsiDevice);
	}

	if (scsiObjectIterator != 0) {
		IOObjectRelease(scsiObjectIterator);
	}

	if (err < 0) {
		if (masterPort) {
			mach_port_deallocate(mach_task_self(), masterPort);
		}
	}

	if (dict != NULL) {
		CFRelease(dict);
	}

	if (realdevice != NULL) {
		free(realdevice);
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
	return err;
}

int JarchBlockIO_SG::close()
{
	if (alloc_buffer) free(alloc_buffer);
	alloc_buffer = NULL;

	SCSITaskDeviceInterface	**sc;
	MMCDeviceInterface	**mmc;

	sc = scsiTaskDeviceInterface;
	(*sc)->ReleaseExclusiveAccess(sc);
	(*sc)->Release(sc);
	scsiTaskDeviceInterface = NULL;

	mmc = mmcDeviceInterface;
	if (mmc != NULL)
		(*mmc)->Release(mmc);

	mach_port_deallocate(mach_task_self(), masterPort);
	return 0;
}

unsigned char *JarchBlockIO_SG::buffer()
{
	return alloc_buffer;
}

int JarchBlockIO_SG::buffersize()
{
	return alloc_sectors;
}

int JarchBlockIO_SG::read(int sectors)
{
	int rd;

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

int JarchBlockIO_SG::scsi(unsigned char *cmdx,int cmdlen,unsigned char *data,int datalen,int dir)
{
	SCSITaskDeviceInterface	**sc = NULL;
	SCSITaskInterface	**cmd = NULL;
	IOVirtualRange		iov;
	SCSI_Sense_Data		senseData;
	SCSITaskStatus		status;
	UInt64			bytesTransferred;
	IOReturn		ioReturnValue;
	unsigned char cmdtmp[12];
	int			ret = 0;
        UInt8			txdir;

	// a way for the caller to determine if we support this method
	if (cmdx == NULL || cmdlen <= 0)
		return 0;	// -1 would mean unsupported

	// it would be easier to deal with Apple's command length restrictions by
	// simply padding all commands out to 12 bytes
	memcpy(cmdtmp,cmdx,cmdlen);
	if (cmdlen < 12) memset(cmdtmp+cmdlen,0,12-cmdlen);

        sc = scsiTaskDeviceInterface;
        cmd = (*sc)->CreateSCSITask(sc);
	if (cmd == NULL) {
                bitch(BITCHERROR,"Unable to create SCSI task");
		ret = -1;
		goto out;
	}

	iov.address = (IOVirtualAddress)data;
	iov.length = datalen;
	ioReturnValue = (*cmd)->SetCommandDescriptorBlock(cmd,cmdtmp,12);
	if (ioReturnValue != kIOReturnSuccess) {
                bitch(BITCHERROR,"Failed to set command descriptor block %u bytes",cmdlen);
		ret = -1;
		goto out;
	}

        if (dir == 0)
            txdir = kSCSIDataTransfer_NoDataTransfer;
        else if (dir == 1)
            txdir = kSCSIDataTransfer_FromTargetToInitiator;
        else // dir == 2
            txdir = kSCSIDataTransfer_FromInitiatorToTarget;

	ioReturnValue = (*cmd)->SetScatterGatherEntries(cmd,&iov,1,datalen,txdir);
	if (ioReturnValue != kIOReturnSuccess) {
                bitch(BITCHERROR,"Failed to set Scatter/Gather entries");
		ret = -1;
		goto out;
	}

	ioReturnValue = (*cmd)->SetTimeoutDuration(cmd,10000000);
	if (ioReturnValue != kIOReturnSuccess) {
                bitch(BITCHERROR,"Failed to set command timeout");
		ret = -1;
		goto out;
	}

	memset(&senseData,0,sizeof(senseData));
	ioReturnValue = (*cmd)->ExecuteTaskSync(cmd, &senseData, &status, &bytesTransferred);
	if (ioReturnValue != kIOReturnSuccess) {
                bitch(BITCHERROR,"Failed");
		ret = -1;
		goto out;
	}

	if (status == kSCSITaskStatus_No_Status) {
                bitch(BITCHWARNING,"No status?");
		ret = -1;
		goto out;
	}

/* Hey Apple! Your documentation is nice and all but you forgot to document the SCSI sense data
   structure. I despise having to hunt all over my system looking for the stupid header file! */
	if ((senseData.SENSE_KEY & 0xF) != 0)
		ret = -1;

out:
	if (cmd != NULL)
		(*cmd)->Release(cmd);

	return ret;
}

int JarchBlockIO_SG::fd()
{
	return -1;
}

// interface to the rest of the program
JarchBlockIO* blockiodefault()
{
	return new JarchBlockIO_SG;
}

#endif //READ_METHOD
