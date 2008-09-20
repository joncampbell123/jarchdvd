/* low-level blockio code for jarchdvd.
 * opens the first CD/DVD-ROM drive in the system and uses the Adaptec ASPI layer
 * WNASPI32.DLL to issue SCSI commands */

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

#if READ_METHOD == READ_METHOD_WINASPI

//*****************************************************************************
//      %%% SCSI MISCELLANEOUS EQUATES %%%
//*****************************************************************************

#define SENSE_LEN                   14          // Default sense buffer length
#define SRB_DIR_SCSI                0x00        // Direction determined by SCSI
#define SRB_POSTING                 0x01        // Enable ASPI posting
#define SRB_ENABLE_RESIDUAL_COUNT   0x04        // Enable residual byte count reporting
#define SRB_DIR_IN                  0x08        // Transfer from SCSI target to host
#define SRB_DIR_OUT                 0x10        // Transfer from host to SCSI target
#define SRB_EVENT_NOTIFY            0x40        // Enable ASPI event notification

#define RESIDUAL_COUNT_SUPPORTED    0x02        // Extended buffer flag
#define MAX_SRB_TIMEOUT             108000lu    // 30 hour maximum timeout in s
#define DEFAULT_SRB_TIMEOUT         108000lu    // Max timeout by default


//*****************************************************************************
//      %%% ASPI Command Definitions %%%
//*****************************************************************************

#define SC_HA_INQUIRY               0x00        // Host adapter inquiry
#define SC_GET_DEV_TYPE             0x01        // Get device type
#define SC_EXEC_SCSI_CMD            0x02        // Execute SCSI command
#define SC_ABORT_SRB                0x03        // Abort an SRB
#define SC_RESET_DEV                0x04        // SCSI bus device reset
#define SC_SET_HA_PARMS             0x05        // Set HA parameters
#define SC_GET_DISK_INFO            0x06        // Get Disk information
#define SC_RESCAN_SCSI_BUS          0x07        // ReBuild SCSI device map
#define SC_GETSET_TIMEOUTS          0x08        // Get/Set target timeouts

//*****************************************************************************
//      %%% SRB Status %%%
//*****************************************************************************

#define SS_PENDING                  0x00        // SRB being processed
#define SS_COMP                     0x01        // SRB completed without error
#define SS_ABORTED                  0x02        // SRB aborted
#define SS_ABORT_FAIL               0x03        // Unable to abort SRB
#define SS_ERR                      0x04        // SRB completed with error

#define SS_INVALID_CMD              0x80        // Invalid ASPI command
#define SS_INVALID_HA               0x81        // Invalid host adapter number
#define SS_NO_DEVICE                0x82        // SCSI device not installed

#define SS_INVALID_SRB              0xE0        // Invalid parameter set in SRB
#define SS_OLD_MANAGER              0xE1        // ASPI manager doesn't support Windows
#define SS_BUFFER_ALIGN             0xE1        // Buffer not aligned (replaces OLD_MANAGER in Win32)
#define SS_ILLEGAL_MODE             0xE2        // Unsupported Windows mode
#define SS_NO_ASPI                  0xE3        // No ASPI managers resident
#define SS_FAILED_INIT              0xE4        // ASPI for windows failed init
#define SS_ASPI_IS_BUSY             0xE5        // No resources available to execute cmd
#define SS_BUFFER_TO_BIG            0xE6        // Buffer size to big to handle!
#define SS_MISMATCHED_COMPONENTS    0xE7        // The DLLs/EXEs of ASPI don't version check
#define SS_NO_ADAPTERS              0xE8        // No host adapters to manage
#define SS_INSUFFICIENT_RESOURCES   0xE9        // Couldn't allocate resources needed to init
#define SS_ASPI_IS_SHUTDOWN         0xEA        // Call came to ASPI after PROCESS_DETACH
#define SS_BAD_INSTALL              0xEB        // The DLL or other components are installed wrong

//*****************************************************************************
//      %%% Host Adapter Status %%%
//*****************************************************************************

#define HASTAT_OK                   0x00        // Host adapter did not detect an                                                                                                                       // error
#define HASTAT_SEL_TO               0x11        // Selection Timeout
#define HASTAT_DO_DU                0x12        // Data overrun data underrun
#define HASTAT_BUS_FREE             0x13        // Unexpected bus free
#define HASTAT_PHASE_ERR            0x14        // Target bus phase sequence                                                                                                                            // failure
#define HASTAT_TIMEOUT              0x09        // Timed out while SRB was                                                                                                                                      waiting to beprocessed.
#define HASTAT_COMMAND_TIMEOUT      0x0B        // Adapter timed out processing SRB.
#define HASTAT_MESSAGE_REJECT       0x0D        // While processing SRB, the                                                                                                                            // adapter received a MESSAGE
#define HASTAT_BUS_RESET            0x0E        // A bus reset was detected.
#define HASTAT_PARITY_ERROR         0x0F        // A parity error was detected.
#define HASTAT_REQUEST_SENSE_FAILED 0x10        // The adapter failed in issuing

//*****************************************************************************
//          %%% SRB - HOST ADAPTER INQUIRY - SC_HA_INQUIRY (0) %%%
//*****************************************************************************

/* "volatile" has been added to the status member to prevent
   compiler optimizations from causing infinite loops when
   checking for completion */
typedef struct                                  // Offset
{                                               // HX/DEC
    BYTE        SRB_Cmd;                        // 00/000 ASPI command code = SC_HA_INQUIRY
    volatile BYTE SRB_Status;                     // 01/001 ASPI command status byte
    BYTE        SRB_HaId;                       // 02/002 ASPI host adapter number
    BYTE        SRB_Flags;                      // 03/003 ASPI request flags
    DWORD       SRB_Hdr_Rsvd;                   // 04/004 Reserved, MUST = 0
    BYTE        HA_Count;                       // 08/008 Number of host adapters present
    BYTE        HA_SCSI_ID;                     // 09/009 SCSI ID of host adapter
    BYTE        HA_ManagerId[16];               // 0A/010 String describing the manager
    BYTE        HA_Identifier[16];              // 1A/026 String describing the host adapter
    BYTE        HA_Unique[16];                  // 2A/042 Host Adapter Unique parameters
    WORD        HA_Rsvd1;                       // 3A/058 Reserved, MUST = 0
}
SRB_HAInquiry, *PSRB_HAInquiry, FAR *LPSRB_HAInquiry;

//*****************************************************************************
//          %%% SRB - GET DEVICE TYPE - SC_GET_DEV_TYPE (1) %%%
//*****************************************************************************

typedef struct                                  // Offset
{                                               // HX/DEC
    BYTE        SRB_Cmd;                        // 00/000 ASPI command code = SC_GET_DEV_TYPE
    volatile BYTE SRB_Status;                     // 01/001 ASPI command status byte
    BYTE        SRB_HaId;                       // 02/002 ASPI host adapter number
    BYTE        SRB_Flags;                      // 03/003 Reserved, MUST = 0
    DWORD       SRB_Hdr_Rsvd;                   // 04/004 Reserved, MUST = 0
    BYTE        SRB_Target;                     // 08/008 Target's SCSI ID
    BYTE        SRB_Lun;                        // 09/009 Target's LUN number
    BYTE        SRB_DeviceType;                 // 0A/010 Target's peripheral device type
    BYTE        SRB_Rsvd1;                      // 0B/011 Reserved, MUST = 0
}
SRB_GDEVBlock, *PSRB_GDEVBlock, FAR *LPSRB_GDEVBlock;

//*****************************************************************************
//          %%% SRB - EXECUTE SCSI COMMAND - SC_EXEC_SCSI_CMD (2) %%%
//*****************************************************************************

typedef struct                           // Offset
{                                        // HX/DEC
    BYTE        SRB_Cmd;                 // 00/000 ASPI command code = SC_EXEC_SCSI_CMD
    volatile BYTE SRB_Status;              // 01/001 ASPI command status byte
    BYTE        SRB_HaId;                // 02/002 ASPI host adapter number
    BYTE        SRB_Flags;               // 03/003 ASPI request flags
    DWORD       SRB_Hdr_Rsvd;            // 04/004 Reserved
    BYTE        SRB_Target;              // 08/008 Target's SCSI ID
    BYTE        SRB_Lun;                 // 09/009 Target's LUN number
    WORD        SRB_Rsvd1;               // 0A/010 Reserved for Alignment
    DWORD       SRB_BufLen;              // 0C/012 Data Allocation Length
    BYTE        FAR *SRB_BufPointer;     // 10/016 Data Buffer Pointer
    BYTE        SRB_SenseLen;            // 14/020 Sense Allocation Length
    BYTE        SRB_CDBLen;              // 15/021 CDB Length
    BYTE        SRB_HaStat;              // 16/022 Host Adapter Status
    BYTE        SRB_TargStat;            // 17/023 Target Status
    VOID        FAR *SRB_PostProc;       // 18/024 Post routine
    BYTE        SRB_Rsvd2[20];           // 1C/028 Reserved, MUST = 0
    BYTE        CDBByte[16];             // 30/048 SCSI CDB
    BYTE        SenseArea[SENSE_LEN+2];  // 50/064 Request Sense buffer
}
SRB_ExecSCSICmd, *PSRB_ExecSCSICmd, FAR *LPSRB_ExecSCSICmd;

//*****************************************************************************
//          %%% SRB - ABORT AN SRB - SC_ABORT_SRB (3) %%%
//*****************************************************************************

typedef struct                                  // Offset
{                                               // HX/DEC
    BYTE        SRB_Cmd;                        // 00/000 ASPI command code = SC_ABORT_SRB
    volatile BYTE SRB_Status;                     // 01/001 ASPI command status byte
    BYTE        SRB_HaId;                       // 02/002 ASPI host adapter number
    BYTE        SRB_Flags;                      // 03/003 Reserved
    DWORD       SRB_Hdr_Rsvd;                   // 04/004 Reserved
    VOID        FAR *SRB_ToAbort;               // 08/008 Pointer to SRB to abort
}
SRB_Abort, *PSRB_Abort, FAR *LPSRB_Abort;

//*****************************************************************************
//          %%% SRB - BUS DEVICE RESET - SC_RESET_DEV (4) %%%
//*****************************************************************************

typedef struct                                  // Offset
{                                               // HX/DEC
    BYTE        SRB_Cmd;                        // 00/000 ASPI command code = SC_RESET_DEV
    volatile BYTE SRB_Status;                     // 01/001 ASPI command status byte
    BYTE        SRB_HaId;                       // 02/002 ASPI host adapter number
    BYTE        SRB_Flags;                      // 03/003 ASPI request flags
    DWORD       SRB_Hdr_Rsvd;                   // 04/004 Reserved
    BYTE        SRB_Target;                     // 08/008 Target's SCSI ID
    BYTE        SRB_Lun;                        // 09/009 Target's LUN number
    BYTE        SRB_Rsvd1[12];                  // 0A/010 Reserved for Alignment
    BYTE        SRB_HaStat;                     // 16/022 Host Adapter Status
    BYTE        SRB_TargStat;                   // 17/023 Target Status
    VOID        FAR *SRB_PostProc;              // 18/024 Post routine
    BYTE        SRB_Rsvd2[36];                  // 1C/028 Reserved, MUST = 0
}
SRB_BusDeviceReset, *PSRB_BusDeviceReset, FAR *LPSRB_BusDeviceReset;

//*****************************************************************************
//          %%% SRB - GET DISK INFORMATION - SC_GET_DISK_INFO %%%
//*****************************************************************************

typedef struct                                  // Offset
{                                               // HX/DEC
    BYTE        SRB_Cmd;                        // 00/000 ASPI command code = SC_GET_DISK_INFO
    volatile BYTE SRB_Status;                     // 01/001 ASPI command status byte
    BYTE        SRB_HaId;                       // 02/002 ASPI host adapter number
    BYTE        SRB_Flags;                      // 03/003 Reserved, MUST = 0
    DWORD       SRB_Hdr_Rsvd;                   // 04/004 Reserved, MUST = 0
    BYTE        SRB_Target;                     // 08/008 Target's SCSI ID
    BYTE        SRB_Lun;                        // 09/009 Target's LUN number
    BYTE        SRB_DriveFlags;                 // 0A/010 Driver flags
    BYTE        SRB_Int13HDriveInfo;            // 0B/011 Host Adapter Status
    BYTE        SRB_Heads;                      // 0C/012 Preferred number of heads translation
    BYTE        SRB_Sectors;                    // 0D/013 Preferred number of sectors translation
    BYTE        SRB_Rsvd1[10];                  // 0E/014 Reserved, MUST = 0
}
SRB_GetDiskInfo, *PSRB_GetDiskInfo, FAR *LPSRB_GetDiskInfo;

//*****************************************************************************
//          %%%  SRB - RESCAN SCSI BUS(ES) ON SCSIPORT %%%
//*****************************************************************************

typedef struct                                  // Offset
{                                               // HX/DEC
    BYTE        SRB_Cmd;                        // 00/000 ASPI command code = SC_RESCAN_SCSI_BUS
    volatile BYTE SRB_Status;                     // 01/001 ASPI command status byte
    BYTE        SRB_HaId;                       // 02/002 ASPI host adapter number
    BYTE        SRB_Flags;                      // 03/003 Reserved, MUST = 0
    DWORD       SRB_Hdr_Rsvd;                   // 04/004 Reserved, MUST = 0
}
SRB_RescanPort, *PSRB_RescanPort, FAR *LPSRB_RescanPort;

//*****************************************************************************
//          %%% SRB - GET/SET TARGET TIMEOUTS %%%
//*****************************************************************************

typedef struct                                  // Offset
{                                               // HX/DEC
    BYTE        SRB_Cmd;                        // 00/000 ASPI command code = SC_GETSET_TIMEOUTS
    volatile BYTE SRB_Status;                     // 01/001 ASPI command status byte
    BYTE        SRB_HaId;                       // 02/002 ASPI host adapter number
    BYTE        SRB_Flags;                      // 03/003 ASPI request flags
    DWORD       SRB_Hdr_Rsvd;                   // 04/004 Reserved, MUST = 0
    BYTE        SRB_Target;                     // 08/008 Target's SCSI ID
    BYTE        SRB_Lun;                        // 09/009 Target's LUN number
    DWORD       SRB_Timeout;                    // 0A/010 Timeout in half seconds
}
SRB_GetSetTimeouts, *PSRB_GetSetTimeouts, FAR *LPSRB_GetSetTimeouts;

//*****************************************************************************
//          %%% ASPIBUFF - Structure For Controllng I/O Buffers %%%
//*****************************************************************************

typedef struct tag_ASPI32BUFF                   // Offset
{                                               // HX/DEC
    PBYTE                   AB_BufPointer;      // 00/000 Pointer to the ASPI allocated buffer
    DWORD                   AB_BufLen;          // 04/004 Length in bytes of the buffer
    DWORD                   AB_ZeroFill;        // 08/008 Flag set to 1 if buffer should be zeroed
    DWORD                   AB_Reserved;        // 0C/012 Reserved
}
ASPI32BUFF, *PASPI32BUFF, FAR *LPASPI32BUFF;

#ifndef WIN32
#error READ_METHOD set to WINASPI which requires Microsoft Windows
#endif

#define THIS_FD_CLOSED		0xFFFFFFFF

class JarchBlockIO_WINASPI : public JarchBlockIO
{
public:
					JarchBlockIO_WINASPI();
					~JarchBlockIO_WINASPI();
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
	DWORD				dev_fd;
	int				alloc_sectors;
	unsigned char*			alloc_buffer;
	juint64				next_sector;
	char				default_cdrom[16];
	DWORD (__stdcall *		__GetASPI32SupportInfo)();
	DWORD (__cdecl *		__SendASPI32Command)(void *p);
	HMODULE				WNASPI32;

	HMODULE				Load_WNASPI32_Default();
	HMODULE				Load_WNASPI32_Sys1();
	HMODULE				Load_WNASPI32_Sys2();
	HMODULE				Load_WNASPI32_Nero();
	void				ScsiInquiry(int host,int target,int lun,char *name);
};

HMODULE JarchBlockIO_WINASPI::Load_WNASPI32_Default()
{
	bitch(BITCHINFO,"...WNASPI32.DLL (by default path)");
	return LoadLibrary("WNASPI32.DLL");
}

HMODULE JarchBlockIO_WINASPI::Load_WNASPI32_Sys1()
{
	char path[270],tmp[270];

	GetSystemDirectory(tmp,sizeof(tmp));
	sprintf(path,"%s\\WNASPI32.DLL",tmp);
	bitch(BITCHINFO,"...Trying %s",path);
	return LoadLibrary(path);
}

HMODULE JarchBlockIO_WINASPI::Load_WNASPI32_Sys2()
{
	char path[270],tmp[270];

	GetWindowsDirectory(tmp,sizeof(tmp));
	sprintf(path,"%s\\WNASPI32.DLL",tmp);
	bitch(BITCHINFO,"...Trying %s",path);
	return LoadLibrary(path);
}

HMODULE JarchBlockIO_WINASPI::Load_WNASPI32_Nero()
{
	char path[270],tmp[270];
	HMODULE DLL=NULL;
	HKEY ahead;

	bitch(BITCHINFO,"...Checking for local installation of Ahead Software's Nero Burning ROM");
	if (RegOpenKey(HKEY_LOCAL_MACHINE,"Software\\Ahead\\Shared",&ahead) == ERROR_SUCCESS) {
		DWORD sz,typ;

		tmp[0] = 0;
		RegQueryValueEx(ahead,"NeroAPI",0,&typ,0,&sz);
		RegQueryValueEx(ahead,"NeroAPI",0,&typ,(BYTE*)tmp,&sz);

		if (strlen(tmp) > 0) {
			bitch(BITCHINFO,"......Registry key HKEY_LOCAL_MACHINE\\Software\\Ahead\\Shared\\NeroAPI = %s",tmp);
			sprintf(path,"%s\\WNASPI32.DLL",tmp);
			bitch(BITCHINFO,"......Trying %s",path);
			DLL = LoadLibrary(path);
		}
	}

	return DLL;
}

void JarchBlockIO_WINASPI::ScsiInquiry(int host,int target,int lun,char *name)
{
	SRB_ExecSCSICmd srbExec;
	unsigned char buf[40];

	memset ( &srbExec, 0, sizeof ( SRB_ExecSCSICmd ) );
	srbExec.SRB_Cmd = SC_EXEC_SCSI_CMD;
	srbExec.SRB_HaId = host;
	srbExec.SRB_Flags = SRB_DIR_IN;
	srbExec.SRB_Target = target;
	srbExec.SRB_Lun = lun;
	srbExec.SRB_BufLen = 36;
	srbExec.SRB_BufPointer = buf;
	srbExec.SRB_SenseLen = SENSE_LEN;
	srbExec.SRB_CDBLen = 6;
	srbExec.CDBByte [ 0 ] = 0x12;	// INQUIRY
	srbExec.CDBByte [ 4 ] = 36;
	__SendASPI32Command(&srbExec);
	while (srbExec.SRB_Status == SS_PENDING);

	if (srbExec.SRB_Status != SS_COMP) {
		strcpy(name,"{unknown, error}");
	}
	else {
		memcpy(name,buf+8,27);
		name[27]=0;
	}
}

JarchBlockIO_WINASPI::JarchBlockIO_WINASPI()
{
	alloc_buffer = NULL;
	alloc_sectors = 0;
	next_sector = 0;
	dev_fd = THIS_FD_CLOSED;
	__GetASPI32SupportInfo = NULL;
	__SendASPI32Command = NULL;
	WNASPI32 = NULL;
}

JarchBlockIO_WINASPI::~JarchBlockIO_WINASPI()
{
	close();
}

int JarchBlockIO_WINASPI::atapi_read(juint64 sector,unsigned char *buf,int N)
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

int JarchBlockIO_WINASPI::open(char *given_name)
{
	char path[270],triedmap[4],defdrv[32];
	int host,target,lun;
	char *name;
	DWORD x;
	char *p;
	int a;

	if (dev_fd != THIS_FD_CLOSED)
		close();

	if (!given_name)
		return -1;

try_again:
	lun = -1;
	host = -1;
	target = -1;
	defdrv[0] = 0;
	WNASPI32 = NULL;
	memset(triedmap,0,4);
	if (!WNASPI32 && !triedmap[0]) { triedmap[0]=1; WNASPI32=Load_WNASPI32_Default(); }
	if (!WNASPI32 && !triedmap[1]) { triedmap[1]=1; WNASPI32=Load_WNASPI32_Sys1(); }
	if (!WNASPI32 && !triedmap[2]) { triedmap[2]=1; WNASPI32=Load_WNASPI32_Sys2(); }
	if (!WNASPI32 && !triedmap[3]) { triedmap[3]=1; WNASPI32=Load_WNASPI32_Nero(); }

	if (WNASPI32) {
		GetModuleFileName(WNASPI32,path,sizeof(path));
		bitch(BITCHINFO,"...OK, %s loaded @ 0x%08X",path,(DWORD)WNASPI32);
	}
	else {
		bitch(BITCHWARNING,"...Unable to load/use WNASPI32.DLL");
		return 0;
	}

	__GetASPI32SupportInfo =	(unsigned long (__stdcall *)(void))GetProcAddress(WNASPI32,"GetASPI32SupportInfo");
	__SendASPI32Command =		(unsigned long (__cdecl *)(void*))GetProcAddress(WNASPI32,"SendASPI32Command");

	bitch(BITCHINFO,"...WNASPI32.DLL    GetASPI32SupportInfo......0x%08X",__GetASPI32SupportInfo);
	bitch(BITCHINFO,"...WNASPI32.DLL    SendASPI32Command.........0x%08X",__SendASPI32Command);

	if (!__GetASPI32SupportInfo || !__SendASPI32Command) {
		bitch(BITCHWARNING,"...Missing entry points in WNASPI32.DLL!");
		close();
		return 0;
	}

	bitch(BITCHINFO,"...Checking ASPI support");
	x = __GetASPI32SupportInfo();
	bitch(BITCHINFO,"......GetASPI32SupportInfo() returned 0x%08X",x);
	a = (x>>8)&0xFF;
	if (a != SS_COMP) {
		bitch(BITCHWARNING,".........ASPI support is not OK");
		if (a == SS_NO_ADAPTERS) {
			bitch(BITCHWARNING,".........This version of WNASPI32.DLL does not detect any SCSI type devices");
			bitch(BITCHWARNING,".........Perhaps this is the wrong type/driver set?");
			bitch(BITCHWARNING,".........Closing WNASPI32.DLL, looking for another working version");
			close();
			goto try_again;
		}

		close();
		goto try_again;
	}
	a = x&0xFF;
	if (a == 0) {
		bitch(BITCHWARNING,".........ASPI support says there are no SCSI type adaptors in the system!");
		close();
		goto try_again;
	}

	/* at this point we can print diagnostic info if no name given */
	if (host < 0 || target < 0 || lun < 0) {
		SRB_GDEVBlock sob;
		SRB_HAInquiry sai;

		memset(&sai,0,sizeof(sai));
		sai.SRB_Cmd = SC_HA_INQUIRY;
		sai.SRB_HaId = 0;
		__SendASPI32Command(&sai);
		if (sai.SRB_Status == SS_COMP) {
			int hosts = sai.HA_Count;

			sai.HA_ManagerId[16] = 0;
			bitch(BITCHINFO,"...ASPI driver found %u hosts",hosts);
			bitch(BITCHINFO,"...ASPI manager %s",sai.HA_ManagerId);

			for (host=0;host < hosts;host++) {
				bitch(BITCHINFO,"...host #%u",host);
				memset(&sai,0,sizeof(sai));
				sai.SRB_Cmd = SC_HA_INQUIRY;
				sai.SRB_HaId = host;
				__SendASPI32Command(&sai);

				if (sai.SRB_Status == SS_COMP) {
					sai.HA_Identifier[16]=0;
					bitch(BITCHINFO,"...Host identifier %s",sai.HA_Identifier);
					for (target=0;target < 8;target++) {
//						bitch(BITCHINFO,"......target #%u",target);
						for (lun=0;lun < 8;lun++) {
//							bitch(BITCHINFO,".........LUN #%u",lun);

							memset(&sob,0,sizeof(sob));
							sob.SRB_Cmd = SC_GET_DEV_TYPE;
							sob.SRB_HaId = host;
							sob.SRB_Target = target;
							sob.SRB_Lun = lun;
							__SendASPI32Command(&sob);
							if (sob.SRB_Status == SS_COMP) {
								if (sob.SRB_DeviceType == 0x05) {	// CD-ROM drive?
									char name[37];
									ScsiInquiry(host,target,lun,name);
									bitch(BITCHINFO,"............%u:%u:%u is CD-ROM device %s",host,target,lun,name);

									if (!defdrv[0])
										sprintf(defdrv,"%u:%u:%u",host,target,lun);
								}
							}
						}
					}
				}
				else {
					bitch(BITCHINFO,"......failed");
				}
			}
		}
		else {
			bitch(BITCHINFO,"...ASPI failed scan for host adapters!");
		}

//		bitch(BITCHINFO,"...Look at this scan dump and enter the host:target:lun number of the drive to rip from using the -dev switch");
//		close();
//		return 0;
	}

	if (!strlen(given_name))
		name = defdrv;
	else
		name = given_name;

	if (!name)
		return -1;
	if (!strlen(name))
		return -1;

	if (isdigit(*name))	host = atoi(name);
	else			host = -1;

	if (host >= 0) {
		p = strchr(name,':');
		if (p)		target = atoi(p+1);
		else		target = -1;
	}
	else {
		target = -1;
	}

	if (target >= 0) {
		p = strchr(p,':');
		if (p)		lun = atoi(p+1);
		else		lun = -1;
	}
	else {
		lun = -1;
	}

	bitch(BITCHINFO,"JarchBlockIO_WINASPI: Windows WNASPI32.DLL ioctl driver");
	bitch(BITCHINFO,"Using host %d target %d LUN %d",host,target,lun);

	/* the "file handle" is formed from the host:target:lun number */
	dev_fd = (DWORD)((host << 16) | (target << 8) | lun);
	bitch(BITCHINFO,"...Using ASPI SCSI device %d:%d:%d",host,target,lun);

	/* we can do large transfers. the problem is that it really slows down the system! */
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

int JarchBlockIO_WINASPI::close()
{
	if (alloc_buffer) free(alloc_buffer);
	alloc_buffer = NULL;

	__GetASPI32SupportInfo =	NULL;
	__SendASPI32Command =		NULL;

	if (WNASPI32) FreeLibrary(WNASPI32);
	WNASPI32=NULL;

	return 0;
}

unsigned char *JarchBlockIO_WINASPI::buffer()
{
	if (dev_fd == THIS_FD_CLOSED) return NULL;
	return alloc_buffer;
}

int JarchBlockIO_WINASPI::buffersize()
{
	if (dev_fd == THIS_FD_CLOSED) return 0;
	return alloc_sectors;
}

int JarchBlockIO_WINASPI::read(int sectors)
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

int JarchBlockIO_WINASPI::seek(juint64 s)
{
	next_sector = s;
	return 1;
}

int JarchBlockIO_WINASPI::scsi(unsigned char *cmd,int cmdlen,unsigned char *data,int datalen,int dir)
{
	SRB_ExecSCSICmd srbExec;

	if (dev_fd == THIS_FD_CLOSED) return -1;

	// a way for the caller to determine if we support this method
	if (cmd == NULL || cmdlen <= 0)
		return 0;	// -1 would mean unsupported

	memset ( &srbExec, 0, sizeof ( SRB_ExecSCSICmd ) );
	srbExec.SRB_Cmd = SC_EXEC_SCSI_CMD;
	srbExec.SRB_HaId = (dev_fd >> 16) & 0xFF;
	if (dir == 1)		srbExec.SRB_Flags = SRB_DIR_IN;
	else if (dir == 2)	srbExec.SRB_Flags = SRB_DIR_OUT;
	else			srbExec.SRB_Flags = 0;
	srbExec.SRB_Target = (dev_fd >> 8) & 0xFF;
	srbExec.SRB_Lun = dev_fd & 0xFF;
	srbExec.SRB_BufLen = datalen;
	srbExec.SRB_BufPointer = data;
	srbExec.SRB_SenseLen = SENSE_LEN;
	srbExec.SRB_CDBLen = cmdlen;
	srbExec.SRB_Status = SS_COMP;
	memcpy(&srbExec.CDBByte,cmd,cmdlen);
	__SendASPI32Command(&srbExec);
	while (srbExec.SRB_Status == SS_PENDING);

	if (srbExec.SRB_Status != SS_COMP)
		return -1;
	if (srbExec.SenseArea[2] & 0xF)
		return -1;

	return 0;
}

int JarchBlockIO_WINASPI::fd()
{
	return -1;
}

// interface to the rest of the program
JarchBlockIO* blockiodefault()
{
	return new JarchBlockIO_WINASPI;
}

#endif //READ_METHOD
