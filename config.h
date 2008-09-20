/* TODO LIST FOR JARCHDVD2
 *
 * * Have main loop print out number of sectors remaining to be ripped,
 *   if rip not complete. This way the user knows whether he needs to
 *   re-run this program and/or take the DVD out and clean it before
 *   trying again.
 *
 * * READ_PACKET_SG code not working properly. Try *ACTUALLY* checking
 *   the SCSI sense information rather than taking Linux's word for it.
 *
 * * Have READ_PACKET_IOKIT (Mac OS X) check sense data, since it's
 *   possible the IOKit returns success even if the SCSI command fails.
 *   [DONE?]
 *
 * * Fix code so DVD does not have to be in the drive when using -decss
 *   switch [DONE]
 *
 * * Have main loop print out TODO list at end to give the user an idea
 *   what we're finished with [DONE]
 */

#ifndef CONFIG_H
#define CONFIG_H

#ifdef LINUX
typedef unsigned long long			juint64;
#endif
#ifdef MACOSX
typedef unsigned long long			juint64;
#endif
#ifdef WIN32
typedef unsigned __int64			juint64;
#endif

/* endianness specification
 *
 * ENDIAN_LE		host is little endian (Linux on i386, Windows)
 * ENDIAN_BE		host is big endian (Linux on PPC, Mac OS X) */
#define ENDIAN_LE		1
#define ENDIAN_BE		2

/* Mac OS X is guaranteed to be Big Endian */
#ifdef MACOSX
#define ENDIAN ENDIAN_BE
#endif

/* auto-detect endian if possible */
#ifdef LINUX
# include <endian.h>
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define ENDIAN ENDIAN_LE
# else
#  define ENDIAN ENDIAN_BE
# endif
#endif

#ifdef WIN32
// Microsoft Windows is always little endian
# define ENDIAN ENDIAN_LE
#endif

/* choose endian here */
#ifndef ENDIAN
# define ENDIAN ENDIAN_LE
#endif


/* method of ripping DVD data:
 *
 * READ_METHOD_STANDARD
 *   Linux:
 *     use lseek64(), read(), etc. to rip the DVD. This should be most
 *     compatible with Linux kernels 2.6.x, 2.4.x, and earlier. The
 *     posix functions themselves offer no means to send SCSI commands
 *     and can not be used themselves to authenciate the drive. For
 *     protected DVDs that require authentication you must set
 *     DVD_AUTHENTICATE == DVD_AUTHENTICATE_LINUX which uses a Linux
 *     kernel-level DVD authentication ioctl to do the job.
 *
 *   Windows:
 *     use CreateFile(), ReadFile(), SetFilePointer(), etc. to rip the
 *     DVD. Should provide greatest compatibility across Windows NT.
 *     Note that there is no means in this code for authenticating the
 *     DVD-ROM drive and therefore no way to rip protected DVDs.
 *
 *   NOTE: For both Windows and Linux this puts the timeout and error
 *         handling completely in the hands of the kernel (both Windows
 *         and Linux). For damaged DVDs this can cause the kernel-mode
 *         read loop to waste a LOT of time attempting to fullfill the
 *         read request when obviously it's doing so from an area
 *         riddled with bad sectors. For Linux this may overflow your
 *         syslog with IDE-ATAPI error messages.
 *
 *   NOTE: This method does not permit anything other than ripping.
 *         DVD authentication, preservation of Copyright Mangement,
 *         gathering of title keys, etc. will not be available. In
 *         fact, any attempt to compile this method will result
 *         in an error message (on purpose) because there really is
 *         no point in bothering...
 *
 *
 * READ_METHOD_PACKET
 *   Linux:
 *     use CDROM_SEND_PACKET ioctl to send read commands, which is
 *     supported under both the 2.4.x and 2.6.x Linux kernels.
 *
 *   NOTE: This is Linux only. The ioctl allows sending SCSI commands
 *         directly which allows jarchdvd to maintain finer control
 *         over the read process as well as authenticate the DVD-ROM
 *         drive.
 *
 *   NOTE: For 2.4.x kernels this will not work if you have SCSI
 *         emulation compiled in to your kernel and you instructed it
 *         to map your ATAPI DVD-ROM drive as a SCSI device. For that
 *         case you must use READ_METHOD_SG which will use the native
 *         SG SCSI ioctls instead.
 *
 *
 * READ_METHOD_SG
 *   Linux:
 *     use the SCSI SG style (/dev/sg) ioctls to send SCSI commands
 *     directly to the drive, which is supported under both the 2.4.x
 *     and 2.6.x kernels.
 *
 *   NOTE: For 2.4.x kernels you must have the ide-scsi hack (SCSI
 *         emulation) compiled in to your kernel and enabled for this
 *         to work with ATAPI DVD-ROM drives. If this is not the case
 *         use READ_METHOD_PACKET instead.
 *
 *   NOTE: For 2.6.x kernels this is the optimal choice for ripping.
 *
 *
 * READ_METHOD_NTSCSI
 *   Windows:
 *     use the Windows NT/2000/XP native SCSI transport to send SCSI
 *     commands directly to the drive. Not supported under Windows 95,
 *     98, or ME.
 *
 *   NOTE: Microsoft Windows does not use the /dev/xxx naming scheme.
 *         The -dev command line parameter is expected to be an NT
 *         style drive specification such as "\\.\D:". If no -dev
 *         switch is given the first DVD-ROM drive on your system is
 *         used.
 *
 *   NOTE: The large block transfers typically used by jarchdvd seem
 *         to cause reliability issues e.g. slows down the entire
 *         system and even the mouse cursor movement is jumpy. Please
 *         keep this in mind when using jarchdvd under Windows and
 *         do not attempt anything hard-disk intensive while ripping.
 *
 *
 * READ_METHOD_WINASPI
 *   Windows:
 *     use the ASPI layer (WNASPI32.DLL) to send SCSI commands
 *     directly to the drive. This is fully supported on Windows 95,
 *     98, ME, NT, 2000, XP, and Server 2003.
 *
 *   NOTE: The ASPI layer uses neither the /dev/xxx naming scheme nor
 *         the DOS drive letter scheme. Instead the ASPI layer thinks
 *         in terms of HOST:TARGET:LUN. When using the -dev switch
 *         specify the drive to use in the form "host:target:lun" e.g.
 *         1:0:0 specifies host 1 target 0 LUN 0. Failure to provide
 *         the -dev switch causes this code to assume the first host
 *         it finds, target 0 LUN 0.
 *
 *   NOTE: For Windows NT/2000/XP this code will work if WNASPI32.DLL
 *         is installed but it is not necessary. Use of
 *         READ_METHOD_NTSCSI is highly recommended.
 *
 *   NOTE: For Windows 95, 98, and ME you will likely not be able to
 *         rip from your DVD-ROM drive because the default version
 *         of WNASPI32.DLL (installed by Windows) only enumerates
 *         SCSI host adapters in your system (they do not list the
 *         ATAPI CD-ROM drives in your system). To obtain a version
 *         that does, install a CD/DVD burner software package such
 *         as Easy CD Creator or Nero Burning ROM which comes with
 *         it's own version of WNASPI32.DLL. The custom version of
 *         WNASPI32.DLL will more often than not work in conjunction
 *         with kernel-level drivers to emulate SCSI over ATAPI.
 *
 *         Easy CD Creator:      Inconsiderate program, will overwite
 *                               WNASPI32.DLL with it's own. If you
 *                               need one that retains the ability to
 *                               talk to SCSI host adapters this is
 *                               not it.
 *
 *         Nero Burning ROM:     Installs WNASPI32.DLL to Windows
 *                               system directory unless it's already
 *                               there. In either case it also installs
 *                               a copy in it's installation path and
 *                               stores the path in the registry.
 *                               This program's WNASPI32 code knows
 *                               how to look up this path and use it.
 *                               HIGHLY RECOMMENDED.
 *
 *         Creative PC-DVD player: Installs it's own version,
 *                               regardless of which one is already
 *                               there. Can totally screw up Easy
 *                               CD Creator's ability to burn CDs.
 *                               Their stupid $!@$-ing replacement
 *                               for Windows Autorun (Creative Disc
 *                               Detector) can only cause more hell,
 *                               which is why I went to such trouble
 *                               writing a program that kills the task,
 *                               removes it's entries from the Registry,
 *                               and deletes both the executable and
 *                               Control Panel module, restoring sanity.
 *
 * READ_METHOD_IOKIT
 *   Mac OS X/Darwin:
 *     Uses the Mac OS X IOKit interfaces to send SCSI commands
 *     directly to the drive.
 *
 *   NOTE: It's probably very likely a good idea to disable automatic
 *         running of software if you're going to use Mac OS X to rip
 *         DVDs. If you have a Mac Mini (like I do) avoid relying on
 *         it for all-purpose DVD ripping because... well... I never
 *         learned to trust those types of CD/DVD-ROM drives that
 *         must be hand-fed a disc and then are depended on to eject
 *         it reliably (though I have yet to have any problems with
 *         it so far). Give me the trusty plastic tray and eject
 *         button anyday! */
#define READ_METHOD_STANDARD		1
#define READ_METHOD_PACKET		2
#define READ_METHOD_SG			3
#define READ_METHOD_NTSCSI		4
#define READ_METHOD_WINASPI		5
#define READ_METHOD_IOKIT		6

/* choose your method here */
#ifndef READ_METHOD
#define READ_METHOD			READ_METHOD_SG
#endif //READ_METHOD

/* this represents one of two sets of code for DVD authentication */
#define DVD_AUTHENTICATE_LINUX		1	/* use the DVD authentication IOCTLs and structures in <linux/cdrom.h> */
#define DVD_AUTHENTICATE_INDEPENDENT	2	/* use SCSI commands for DVD authentication */

/* choose your method here */
#define DVD_AUTHENTICATE		DVD_AUTHENTICATE_INDEPENDENT

// Linux needs this...
#ifdef LINUX
#include <unistd.h>
#endif
// so does Mac OS X
#ifdef MACOSX
#include <unistd.h>
#endif
#ifdef WIN32
#include <fcntl.h>
#endif

#ifdef WIN32
#include <io.h>
#endif

// O_BINARY hacks (needed for Linux, because only stupid C/C++ runtime
// libraries like Microsoft's would assume text CR/LF translation!)
#ifndef O_BINARY
#define O_BINARY 0
#endif

// hacks for differences in printing 64-bit integers
#ifdef LINUX
# define PRINT64F "%Lu"
#endif
#ifdef MACOSX
# define PRINT64F "%Lu"
#endif
#ifdef WIN32
# define PRINT64F "%I64u"
#endif

/* macros for conversion from LE to host order */
#if ENDIAN == ENDIAN_LE
#  define LeHe16(x)		(x)
#  define LeHe32(x)		(x)
#else
#  define LeHe16(x)		utilLeHe16(x)
#  define LeHe32(x)		utilLeHe32(x)
#endif

unsigned char *utilLeHe32bin(unsigned int x);
unsigned int utilbinBeHe32(unsigned char *x);
unsigned int utilbinLeHe32(unsigned char *x);
unsigned short int utilbinBeHe16(unsigned char *x);
unsigned short int utilbinLeHe16(unsigned char *x);

#define LeHe32bin(x)		utilLeHe32bin(x)
#define binLeHe32(x)		utilbinLeHe32(x)
#define binBeHe32(x)		utilbinBeHe32(x)
#define binLeHe16(x)		utilbinLeHe16(x)
#define binBeHe16(x)		utilbinBeHe16(x)

char *YesNo(int x);

#ifdef WIN32
typedef unsigned char byte;
#endif
typedef unsigned long u32;

#endif //CONFIG_H

