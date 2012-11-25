/* jarchdvd (DVD Video archiving utility) v2.0 New and Improved.
 * main.c
 *
 * (C) 2005 Jonathan Campbell
 *
 * 05-05-2005: Threw out some old code, modified bitch() routines to
 *             improve tiered bitch messages through bitch_indent()
 *             and bitch_unindent(). Threw out old ripping engine,
 *             RSAPI code, decryption code, implemented new core.
 *             This code now uses C++ features, not C.
 *
 * TODO:
 *   * Write a fix for Mark Eberhardt who complains that this code doesn't
 *     work under 2.4.x kernels.
 */

#ifdef LINUX
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef WIN32
#include <windows.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "config.h"
#include "blockio.h"
#include "bitchin.h"
#include "jarchcd.h"
#include "lsimage.h"
#include "rippedmap.h"
#include "todolist.h"

#include <stdint.h>
#include <string>

using namespace std;

const unsigned char zero4[4] = {0,0,0,0};

static uint32_t get32lsb(const uint8_t* src) {
    return
        (((uint32_t)(src[0])) <<  0) |
        (((uint32_t)(src[1])) <<  8) |
        (((uint32_t)(src[2])) << 16) |
        (((uint32_t)(src[3])) << 24);
}

static void put32lsb(uint8_t* dest, uint32_t value) {
    dest[0] = (uint8_t)(value      );
    dest[1] = (uint8_t)(value >>  8);
    dest[2] = (uint8_t)(value >> 16);
    dest[3] = (uint8_t)(value >> 24);
}

////////////////////////////////////////////////////////////////////////////////
//
// LUTs used for computing ECC/EDC
//
static uint8_t  ecc_f_lut[256];
static uint8_t  ecc_b_lut[256];
static uint32_t edc_lut  [256];

static void eccedc_init(void) {
    size_t i;
    for(i = 0; i < 256; i++) {
        uint32_t edc = i;
        size_t j = (i << 1) ^ (i & 0x80 ? 0x11D : 0);
        ecc_f_lut[i] = j;
        ecc_b_lut[i ^ j] = i;
        for(j = 0; j < 8; j++) {
            edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001 : 0);
        }
        edc_lut[i] = edc;
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// Check ECC block (either P or Q)
// Returns true if the ECC data is an exact match
//
static int8_t ecc_checkpq(
    const uint8_t* address,
    const uint8_t* data,
    size_t major_count,
    size_t minor_count,
    size_t major_mult,
    size_t minor_inc,
    const uint8_t* ecc
) {
    size_t size = major_count * minor_count;
    size_t major;
    for(major = 0; major < major_count; major++) {
        size_t index = (major >> 1) * major_mult + (major & 1);
        uint8_t ecc_a = 0;
        uint8_t ecc_b = 0;
        size_t minor;
        for(minor = 0; minor < minor_count; minor++) {
            uint8_t temp;
            if(index < 4) {
                temp = address[index];
            } else {
                temp = data[index - 4];
            }
            index += minor_inc;
            if(index >= size) { index -= size; }
            ecc_a ^= temp;
            ecc_b ^= temp;
            ecc_a = ecc_f_lut[ecc_a];
        }
        ecc_a = ecc_b_lut[ecc_f_lut[ecc_a] ^ ecc_b];
        if(
            ecc[major              ] != (ecc_a        ) ||
            ecc[major + major_count] != (ecc_a ^ ecc_b)
        ) {
            return 0;
        }
    }
    return 1;
}

//
// Check ECC P and Q codes for a sector
// Returns true if the ECC data is an exact match
//
static int8_t ecc_checksector(
    const uint8_t *address,
    const uint8_t *data,
    const uint8_t *ecc
) {
    return
        ecc_checkpq(address, data, 86, 24,  2, 86, ecc) &&      // P
        ecc_checkpq(address, data, 52, 43, 86, 88, ecc + 0xAC); // Q
}

////////////////////////////////////////////////////////////////////////////////
//
// Compute EDC for a block
//
static uint32_t edc_compute(
    uint32_t edc,
    const uint8_t* src,
    size_t size
) {
    for(; size; size--) {
        edc = (edc >> 8) ^ edc_lut[(edc ^ (*src++)) & 0xFF];
    }
    return edc;
}

enum {
	RAW96=1,
	DEINT96=4
};

static FILE*			chosen_bitch_out;
static char			chosen_bitch_out_fname[256];
static char			chosen_input_block_dev[256];
static JarchBlockIO*		chosen_bio=NULL;
static int			chosen_test_mode=0;
static int			chosen_force_info=0;
static int			chosen_force_rip=0;
static int			chosen_dont_rip=0;
static int			rip_subchannel=0;
static int			rip_periodic=0;
static int			rip_noskip=0;
static double			rip_assume_rate=0.0;
static int			rip_expandfill=0;
static int			rip_backwards_from_outermost=0;
static int			rip_verify=0;
static int			rip_verify_by_reading=0;
static int			rip_cmi=0;
static int			rip_assume=0;
static int			rip_backwards=0;
static int			force_genpoi=0;
static int			force_authpoi=0;
static int			skip_rip=0;
static int			css_decrypt_inplace=0;
static int			show_todo=0;
static int			poi_dumb=0;
static int			singlesector=0;
static string			driver_name;
static int			p_w_mode = RAW96;
// test modes are:
//   0 = no test
//   1 = KeyStorage test
//   2 = RippedMap test

// state of last session
const char *strSOLS[4] = {
			"empty",		"incomplete",
			"reserved/damaged",	"complete"};

// disc status
const char *strDS[4] = {	
			"empty",		"incomplete",
			"finalized",		"random access only"};

// background format status
const char *strBGS[4] = {
			"none",					"background format started but not complete",
			"background format in progress",	"background formatting complete"};

// Linear density field
const char *strLinDen[16] = {	
			"0.267 um/bit",		"0.293 um/bit",		"0.409-0.435 um/bit",	"unknown (3)",
			"0.280-0.291 um/bit",	"unknown (5)",		"unknown (6)",		"unknown (7)",
			"0.353 um/bit",		"unknown (9)",		"unknown (10)",		"unknown (11)",
			"unknown (12)",		"unknown (13)",		"unknown (14)",		"unknown (15)"};

// Track density field
const char *strTrkDen[16] = {
			"0.74 um/track",	"0.80 um/track",	"0.615 um/track",	"unknown",
			"unknown",		"unknown",		"unknown",		"unknown",
			"unknown",		"unknown",		"unknown",		"unknown",
			"unknown",		"unknown",		"unknown",		"unknown"};

// profile
const char *strProfile(unsigned short x)
{
	if (x == 0x0008)	return "CD-ROM";
	else if (x == 0x0009)	return "CD-R";
	else if (x == 0x000A)	return "CD-RW";
	else if (x == 0x0010)	return "DVD-ROM";
	else if (x == 0x0011)	return "DVD sequential recording";
	else if (x == 0x0012)	return "DVD-RAM";
	else if (x == 0x0013)	return "DVD-RW restricted overwrite";
	else if (x == 0x0014)	return "DVD-RW sequential recording";
	else if (x == 0x0015)	return "DVD-R DL sequential recording";
	else if (x == 0x0016)	return "DVD-R DL jump recording";
	else if (x == 0x0017)	return "DVD-RW dual layer";
	else if (x == 0x0018)	return "DVD-Download disc recording";
	else if (x == 0x001A)	return "DVD+RW";
	else if (x == 0x001B)	return "DVD+R";
	else if (x == 0x002A)	return "DVD+RW dual layer";
	else if (x == 0x002B)	return "DVD+R dual layer";
	return "?";
}

const char *strDiscType(unsigned char x)
{
	if (x == 0x00)		return "CD-DA or CD-ROM disc";
	else if (x == 0x10)	return "CD-I disc";
	else if (x == 0x20)	return "CD-ROM XA disc";
	else if (x == 0xFF)	return "undefined";
	return "?";
}

// book type
const char *strBT(unsigned char x)
{
	if (x == 0x00)		return "DVD-ROM";
	if (x == 0x01)		return "DVD-RAM";
	if (x == 0x02)		return "DVD-R";
	if (x == 0x03)		return "DVD-RW";
	if (x == 0x09)		return "DVD+RW";
	if (x == 0x0A)		return "DVD+R";
	return "unknown";
}

// disc size code
const char *strDscSz(unsigned char x)
{
	if (x == 0x0)		return "120mm";
	if (x == 0x1)		return "80mm";
	return "unknown";
}

const char *strMaxRt(unsigned char x)
{
	if (x == 0x0)		return "2.52 mbps";
	if (x == 0x1)		return "5.04 mbps";
	if (x == 0x3)		return "10.08 mbps";
	if (x == 0xF)		return "unspecified";
	return "unknown";
}

const char *strTrkPth(unsigned char x)
{
	if (x)	return "Parallel Track Path";
	return "Opposite Track Path";
}

static char __strLayTypTmp[140];
const char *strLayTyp(unsigned char x)
{
	x &= 7;
	if (x == 0) return "Not writable";

	strcpy(__strLayTypTmp,"Layer contains ");
	if (x & 1) strcat(__strLayTypTmp,"embossed data, ");
	if (x & 2) strcat(__strLayTypTmp,"recordable area, ");
	if (x & 4) strcat(__strLayTypTmp,"rewriteable area, ");
	return __strLayTypTmp;
}

const char *strCPST(unsigned char x)
{
	if (x == 0x00)	return "none";
	if (x == 0x01)	return "CSS/CPPM";
	if (x == 0x02)	return "CPRM";
	return "unknown";
}

static void GatherDiscInfo(JarchSession *session)
{
	unsigned char cmd[12];
	unsigned char buf[2052];
	int opc,i;

	bitch(BITCHINFO,"Getting Disc Information");
	bitch_indent();

	/* First: get the media profile. The reason is that we could be used to rip a CD-ROM
	 * though that is not ideal because CD-ROM discs could contain non-data and would show
	 * up as a "DVD" with sector errors. If we know it's not DVD-ROM we can print a
	 * warning message for the user that says "Hey! This is a CD. Use JarchCD to rip this!" */
	memset(cmd,0,12);
	cmd[ 0] = 0x46; // GET CONFIGURATION
	cmd[ 7] = (unsigned char)(sizeof(buf) >> 8);
	cmd[ 8] = (unsigned char)sizeof(buf);
	memset(buf,0,2052);
	if (session->bdev->scsi(cmd,10,buf,2052,1) >= 0) {
		unsigned short profile =
			((unsigned short)buf[6] << 8) |
			 (unsigned short)buf[7];

		bitch(BITCHINFO,"Disc profile: 0x%04x (%s)",profile,strProfile(profile));
		if (!(profile >= 0x0008 && profile <= 0x000A)) {
			bitch(BITCHWARNING,"    This program is designed for CDs");
			sleep(5);
		}
	}
	else {
		bitch(BITCHWARNING,"Unable to gather disc configuration (failed to send SCSI command)");
	}

	memset(cmd,0,12);
	cmd[ 0] = 0x51;	// READ DISC INFO
	cmd[ 7] = (unsigned char)(sizeof(buf) >> 8);
	cmd[ 8] = (unsigned char)sizeof(buf);

	memset(buf,0,2052);
	if (session->bdev->scsi(cmd,9,buf,2052,1) < 0) {
		bitch(BITCHWARNING,"Unable to gather disc information (failed to send SCSI command)");
		goto end;
	}

	bitch(BITCHINFO,"Disc Information Length:                          %u",binBeHe16(       buf+  0           ));
	bitch(BITCHINFO,"Erasable:                                         %s",YesNo((          buf[  2] >> 4) & 1));
	bitch(BITCHINFO,"State of last session:                            %s",strSOLS[(        buf[  2] >> 2) & 3]);
	bitch(BITCHINFO,"Disc status:                                      %s",strDS[           buf[  2]       & 3]);
	bitch(BITCHINFO,"Number of first track:                            %u",                 buf[  3]           );
	bitch(BITCHINFO,"Number of sessions:                               %u",(((int)buf[ 9]) << 8) | ((int)buf[4]));
	bitch(BITCHINFO,"First track number in last session:               %u",(((int)buf[10]) << 8) | ((int)buf[5]));
	bitch(BITCHINFO,"Last track number in last session:                %u",(((int)buf[11]) << 8) | ((int)buf[6]));
	bitch(BITCHINFO,"Disc Identification field valid:                  %s",YesNo((          buf[  7] >> 7) & 1));
	bitch(BITCHINFO,"Disc Bar Code field valid:                        %s",YesNo((          buf[  7] >> 6) & 1));
	bitch(BITCHINFO,"Unrestricted Use:                                 %s",YesNo((          buf[  7] >> 5) & 1));
	bitch(BITCHINFO,"Disc Application Code valid:                      %s",YesNo((          buf[  7] >> 4) & 1));
	bitch(BITCHINFO,"Disc Dirty:                                       %s",YesNo((          buf[  7] >> 2) & 1));
	bitch(BITCHINFO,"Background format status:                         %s",strBGS[          buf[  7]       & 3]);
	bitch(BITCHINFO,"Disc type:                                        0x%02X",             buf[  8]           );
		bitch_indent();
		bitch(BITCHINFO,"%s",strDiscType(buf[8]));
		bitch_unindent();
	bitch(BITCHINFO,"Disc Identification Number:                       %u",binBeHe32(       buf+ 12           ));
	bitch(BITCHINFO,"Last session lead-in start address:               %u",binBeHe32(       buf+ 16           ));
	bitch(BITCHINFO,"Last session lead-out start address:              %u",binBeHe32(       buf+ 20           ));

	memcpy(cmd,buf+24,8); cmd[8] = 0;
	bitch(BITCHINFO,"Disc bar code:                                    %s",cmd);

	bitch(BITCHINFO,"Disc application code:                            0x%02X",             buf[ 32]           );
	bitch(BITCHINFO,"Number of OPC tables:                             %u",                 buf[ 33]           );
		bitch_indent();
		for (opc=0;opc < buf[33];opc++) {
			bitch(BITCHINFO,"entry %u");
				bitch_indent();
				bitch(BITCHINFO,"speed (bytes per sec): %u",binBeHe16(buf + 34 + (opc * 8)));
				bitch(BITCHINFO,"values are:");
					bitch_indent();
					for (i=2;i < 8;i++) bitch(BITCHINFO,"0x%02X",buf[34+(opc*8)+i]);
					bitch_unindent();
				bitch_unindent();
		}
		bitch_unindent();

end:
	bitch_unindent();
	session->todo->set(TODO_MEDIAGATHER_DISC_INFO,1);
}

void GatherMediaInfo(JarchSession *session)
{
	RippedMap *m = session->todo;
	int i;

	if (!m->get(TODO_MAIN_GATHER_MEDIA_INFO) || session->chosen_force_info) {
		bitch(BITCHINFO,"Gathering DVD-ROM information");
		bitch_indent();

		if (!m->get(TODO_MEDIAGATHER_DISC_INFO) || session->chosen_force_info)
			GatherDiscInfo(session);

		bitch_unindent();
		/* if all steps are done, we're done */
		if (	m->get(TODO_MEDIAGATHER_DISC_INFO) &&
			m->get(TODO_MEDIAGATHER_DVD_STRUCT_00) &&
			m->get(TODO_MEDIAGATHER_DVD_STRUCT_01) &&
			m->get(TODO_MEDIAGATHER_DVD_STRUCT_03) &&
			m->get(TODO_MEDIAGATHER_DVD_STRUCT_04) &&
			m->get(TODO_MEDIAGATHER_DVD_STRUCT_08) &&
			m->get(TODO_MEDIAGATHER_DVD_STRUCT_09) &&
			m->get(TODO_MEDIAGATHER_DVD_STRUCT_0A))
			m->set(TODO_MAIN_GATHER_MEDIA_INFO,1);
	}
}

void DoTestMode()
{
}

int params(int argc,char **argv)
{
	char *p;
	int i;

	chosen_bitch_out = NULL;
	chosen_bitch_out_fname[0] = 0;
	chosen_input_block_dev[0] = 0;

	for (i=1;i < argc;i++) {
		if (argv[i][0] == '-') {
			p = argv[i]+1;

			/* -dev <dev>
			 * -dev=<dev>
			 *
			 *  either way works with me */
			if (!strncmp(p,"dev",3)) {
				p += 3;
				if (*p == '=') {
					p++;
					strcpy(chosen_input_block_dev,p);
				}
				else {
					i++;
					if (i < argc)	strcpy(chosen_input_block_dev,argv[i]);
					else		bitch(BITCHERROR,"-dev requires another argument");
				}
			}
			/* -driver <name>
			 * -driver=<name> */
			else if (!strncmp(p,"driver",6)) {
				p += 6;
				if (*p == '=') {
					p++;
					driver_name = p;
				}
				else {
					i++;
					if (i < argc)	driver_name = argv[i];
					else		bitch(BITCHERROR,"-driver requires another argument");
				}
			}
			/* -bout <file>
			 * -bout=<file>
			 *
			 *  either way...
			 *  if not specified, all bitchin' and moanin' is routed to standard output. */
			else if (!strncmp(p,"bout",4)) {
				p += 4;
				if (*p == '=') {
					p++;
					strcpy(chosen_bitch_out_fname,p);
				}
				else {
					i++;
					if (i < argc)	strcpy(chosen_bitch_out_fname,argv[i]);
					else		bitch(BITCHERROR,"-bout requires another argument");
				}
			}
			/* -info */
			else if (!strncmp(p,"info",4)) {
				chosen_force_info=1;
			}
			/* -rip */
			else if (!strncmp(p,"rip",3)) {
				chosen_force_rip=1;
			}
			/* -d96 */
			else if (!strncmp(p,"d96",3)) {
				p_w_mode=DEINT96;
			}
			/* -r96 */
			else if (!strncmp(p,"r96",3)) {
				p_w_mode=RAW96;
			}
			/* -no-rip */
			else if (!strncmp(p,"no-rip",6)) {
				chosen_dont_rip=1;
			}
			/* -noskip */
			else if (!strncmp(p,"noskip",6)) {
				rip_noskip=1;
			}
			/* -4gb */
			else if (!strncmp(p,"4gb",3)) {
				rip_assume=(4720000000LL/2048);
			}
			/* -8gb */
			else if (!strncmp(p,"8gb",3)) {
				rip_assume=(8600000000LL/2048);
			}
			/* -rate */
			else if (!strncmp(p,"rate",4)) {
				i++;
				if (i >= argc) return 0;
				rip_assume_rate = atof(argv[i]);
				if (rip_assume_rate < 0.1) {
					bitch(BITCHERROR,"Invalid rate %f",rip_assume_rate);
					return 0;
				}
			}
			/* -cmi */
			else if (!strncmp(p,"cmi",3)) {
				rip_cmi=1;
			}
			/* -dumb-poi */
			else if (!strncmp(p,"dumb-poi",8)) {
				poi_dumb=1;
			}
			/* -poi */
			else if (!strncmp(p,"poi",3)) {
				force_genpoi=1;
			}
			/* -authpoi */
			else if (!strncmp(p,"authpoi",7)) {
				force_authpoi=1;
			}
			/* -norip */
			else if (!strncmp(p,"norip",5)) {
				skip_rip=1;
			}
			/* -decss */
			else if (!strncmp(p,"decss",5)) {
				css_decrypt_inplace=1;
			}
			/* -single */
			else if (!strncmp(p,"single",6)) {
				singlesector=1;
			}
			/* -todo */
			else if (!strncmp(p,"todo",4)) {
				show_todo=1;
			}
			/* -backwards */
			else if (!strncmp(p,"backwards",9)) {
				rip_backwards=1;
			}
			/* -expandfill */
			else if (!strncmp(p,"expandfill",10)) {
				rip_expandfill=1;
			}
			/* -from-outermost */
			else if (!strncmp(p,"from-outermost",14)) {
				rip_backwards_from_outermost=1;
			}
			/* -periodic <n> */
			else if (!strncmp(p,"periodic",8)) {
				i++;
				if (i >= argc) return 0;
				rip_periodic = atoi(argv[i]);
			}
			/* -sub */
			else if (!strncmp(p,"sub",3)) {
				rip_subchannel = 1;
			}
			/* -test-... */
			else if (!strncmp(p,"test-",5)) {
				p += 5;

				if (!strncmp(p,"keystore",8)) {
					chosen_test_mode = 1;
				}
				else if (!strncmp(p,"ripmap",6)) {
					chosen_test_mode = 2;
				}
				else {
					bitch(BITCHERROR,"Unknown test mode");
					return 0;
				}
			}
			else if (!strncmp(p,"verify",6)) {
				rip_verify=1;
			}
			else if (!strncmp(p,"verify-read",6+5)) {
				rip_verify_by_reading=1;
			}
			else {
				bitch(BITCHERROR,"Unknown command line argument %s",argv[i]);
				bitch(BITCHINFO,"Valid switches are:");
#ifdef WIN32
				bitch(BITCHINFO,"-dev <device>               Specify which device to use (e.g. \\\\.\\D:), default: First CD-ROM drive");
#else
				bitch(BITCHINFO,"-dev <device>               Specify which device to use, default: /dev/dvd");
#endif
				bitch(BITCHINFO,"-driver <name>              Use a specific driver for ripping");
				bitch(BITCHINFO,"    Linux:");
				bitch(BITCHINFO,"         linux_sg           SG ioctl method");
				bitch(BITCHINFO,"         linux_packet       CDROM packet ioctl method");
				bitch(BITCHINFO,"-d96                        Read subchannel data as 96-byte deinterlaved");
				bitch(BITCHINFO,"-r96                        Read subchannel data as 96-byte raw non-interleaved");
				bitch(BITCHINFO,"-bout <file>                Log output to <file>, append if exists");
				bitch(BITCHINFO,"-test-keystore              DIAGNOSTIC: Test KeyStorage code");
				bitch(BITCHINFO,"-info                       Gather media info, even if already done");
				bitch(BITCHINFO,"-rip                        Rip CD contents, even if previously ripped");
				bitch(BITCHINFO,"-noskip                     Don't skip sectors in DVD ripping");
				bitch(BITCHINFO,"-rate <N>                   Assume rip rate at Nx (N times 1x DVD rate)");
				bitch(BITCHINFO,"-cmi                        Also copy Copyright Management Info (slow!)");
				bitch(BITCHINFO,"-poi                        Generate points of interest list even if already done");
				bitch(BITCHINFO,"-sub                        Rip subchannel data");
				bitch(BITCHINFO,"-authpoi                    Obtain title keys for POI even if done");
				bitch(BITCHINFO,"-norip                      Skip DVD ripping stage");
				bitch(BITCHINFO,"-decss                      Use keys from key store to perform DVD");
				bitch(BITCHINFO,"                            decryption in place (overwriting original)");
				bitch(BITCHINFO,"-verify                     Verify sectors");
				bitch(BITCHINFO,"-verify-read                Verify sectors by re-reading from the disc");
				bitch(BITCHINFO,"                               * NOTE: If not specified, the code will only re-read the");
				bitch(BITCHINFO,"                                 sector data for Mode 2 form 2 sectors where the checksum");
				bitch(BITCHINFO,"                                 field is zero");
				bitch(BITCHINFO,"-todo                       Don't do anything, just show the TODO list");
				bitch(BITCHINFO,"-dumb-poi                   Use Dumb POI generator");
				bitch(BITCHINFO,"-backwards                  Rip backwards");
				bitch(BITCHINFO,"-from-outermost             When used with -backwards, rip from edge of disc");
				bitch(BITCHINFO,"-expandfill                 Use expanding fill rip method");
				bitch(BITCHINFO,"-periodic <n>               Rip only every nth sector");
				bitch(BITCHINFO,"-single                     Rip single sectors only");
				bitch(BITCHINFO,"Subchannel data is read using raw 96-byte mode. If your drive is modern and");
				bitch(BITCHINFO,"does not return any subchannel data, try -d96");
				return 0;
			}
		}
		else {
			bitch(BITCHERROR,"Unknown command line argument %s",argv[i]);
			return 0;
		}
	}

	return 1;
}

void ReadCDCapacity(JarchSession *session)
{
	unsigned long x;
	int i;

	x = 0;
	for (i=0;i < 32;i++)
		x |= (session->todo->get(TODO_RIPDVD_CAPACITY_DWORD+i) << i);

	session->CD_capacity = x;
}

void GetCDCapacity(JarchSession *session)
{
	unsigned char cmd[10],buf[8];
	unsigned long x;
	int i;

	cmd[0] = 0x25;
	cmd[1] = 0x00;
	cmd[2] =
	cmd[3] =
	cmd[4] =
	cmd[5] = 0;
	cmd[6] = 0;
	cmd[7] = 0;
	cmd[8] = 0;
	cmd[9] = 0;
	if (session->bdev->scsi(cmd,10,buf,sizeof(buf),1) < 0) {
		bitch(BITCHERROR,"Unable to read DVD capacity");
		return;
	}

	x = binBeHe32(buf+4);
	if (x != 2048) {
		bitch(BITCHERROR,"Sector size != 2048. I cannot rip this!");
		return;
	}

	x = session->CD_capacity = binBeHe32(buf);
	bitch(BITCHINFO,"Obtained DVD capacity as %u",x);

	for (i=0;i < 32;i++)
		session->todo->set(TODO_RIPDVD_CAPACITY_DWORD+i,(x >> i) & 1);

	session->todo->set(TODO_RIPDVD_READCAPACITY,1);
}

void RipCDStatus(JarchSession *session)
{
	int leftover,ripped,i;
	RippedMap dvdmap;

	if (dvdmap.open("cdrom-image.ripmap") < 0) {
		bitch(BITCHINFO,"Ripping has not commenced yet");
		return;
	}

	ReadCDCapacity(session);
	if (session->CD_capacity == 0) {
		bitch(BITCHINFO,"Ripping has not determined capacity yet");
		return;
	}

	ripped = 0;
	leftover = 0;
	for (i=0;i < session->CD_capacity;i++)
		if (!dvdmap.get(i))
			leftover++;
		else
			ripped++;

	bitch(BITCHINFO,"%u sectors out of %u have NOT been ripped",leftover,session->CD_capacity);
	bitch(BITCHINFO,"%u sectors out of %u have been ripped",ripped,session->CD_capacity);
}

void CD2MSFnb(unsigned char *d,unsigned long n) {
	d[0] = (n / 75UL / 60UL);
	d[1] = (n / 75UL) % 60UL;
	d[2] = n % 75UL;
}

void DeinterleaveRaw96(unsigned char *r96) {
	unsigned char out[96],mask;
	unsigned int ch,b;

	memset(out,0,96);
	for (ch=0;ch < 8;ch++) {
		mask = 0x80 >> ch;
		for (b=0;b < 96;b++) {
			if (r96[b] & mask)
				out[(ch*12)+(b>>3)] |= 0x80 >> (b&7);
		}
	}

	memcpy(r96,out,96);
}

int nonzero(unsigned char *d,size_t len) {
	size_t i;

	for (i=0;i < len;i++) {
		if (d[i] != 0)
			return 1;
	}

	return 0;
}

unsigned short qsub_crctab[256] = {
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7, 0x8108,
  0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF, 0x1231, 0x0210,
  0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6, 0x9339, 0x8318, 0xB37B,
  0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE, 0x2462, 0x3443, 0x0420, 0x1401,
  0x64E6, 0x74C7, 0x44A4, 0x5485, 0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE,
  0xF5CF, 0xC5AC, 0xD58D, 0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6,
  0x5695, 0x46B4, 0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D,
  0xC7BC, 0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
  0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B, 0x5AF5,
  0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12, 0xDBFD, 0xCBDC,
  0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A, 0x6CA6, 0x7C87, 0x4CE4,
  0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41, 0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD,
  0xAD2A, 0xBD0B, 0x8D68, 0x9D49, 0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13,
  0x2E32, 0x1E51, 0x0E70, 0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A,
  0x9F59, 0x8F78, 0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E,
  0xE16F, 0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
  0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E, 0x02B1,
  0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256, 0xB5EA, 0xA5CB,
  0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D, 0x34E2, 0x24C3, 0x14A0,
  0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xA7DB, 0xB7FA, 0x8799, 0x97B8,
  0xE75F, 0xF77E, 0xC71D, 0xD73C, 0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657,
  0x7676, 0x4615, 0x5634, 0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9,
  0xB98A, 0xA9AB, 0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882,
  0x28A3, 0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
  0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92, 0xFD2E,
  0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9, 0x7C26, 0x6C07,
  0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1, 0xEF1F, 0xFF3E, 0xCF5D,
  0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8, 0x6E17, 0x7E36, 0x4E55, 0x5E74,
  0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

unsigned short qsub_crc(unsigned char *Q) {
  register unsigned short crc = 0;
  register int i;

  for (i = 0; i < 10; i++)
    crc = qsub_crctab[(crc >> 8) ^ Q[i]] ^ (crc << 8);

  return (~crc)&0xFFFF;
}

int PSUB_Check(unsigned char *P) {
#define TOL 4
	unsigned int s1=0,s0=0,i;
	for (i=0;i < 96;i++) {
		if (P[i>>3] & (0x80 >> (i&7))) s1++;
		else s0++;
	}

	assert((s1+s0) == 96);
	return	(s1 <= TOL && s0 >= (96-TOL)) ||
		(s0 <= TOL && s1 >= (96-TOL));
#undef TOL
}

int QSUB_Check(unsigned char *Q) {
	unsigned short crc = qsub_crc(Q);
	return (Q[10] == (crc >> 8)) && (Q[11] == (crc & 0xFF));
}

unsigned char bcd2dec(unsigned char x) {
	return (((x >> 4) & 0xF) * 10) + (x & 0xF);
}

unsigned char dec2bcd(unsigned char x) {
	return ((x / 10) * 16) + (x % 10);
}

void RipCD(JarchSession *session)
{
#define RAWSEC 2352
#define RAWSUB 96
#define C2 294
	LargeSplitImage dvd;
	RippedMap dvdmap;
	RippedMap dvdvmap;
	RippedMap dvdtrmap;
	LargeSplitImage dvdsub;
	RippedMap dvdsubmap;
	LargeSplitImage dvdlead;
	RippedMap dvdleadmap;
	juint64 byteo;
	unsigned long cur,full,rate,expsect,isect,pfull,lastend,adjsect,backwards_next;
	unsigned long last_recover,first_recover,leftover;
	unsigned char *buf,*sense,cmd[12],sector[(RAWSEC+RAWSUB)*2];
	time_t start,expect,curt,prep;
	int rd,rdmax,percent,sz,got,i;
	double d1,d2;
	char adjrate,recover;

	if (session->todo->get(TODO_RIPDVD) && !session->chosen_force_rip)
		return;
	if (session->skip_rip)
		return;

	if (!session->todo->get(TODO_RIPDVD_READCAPACITY) || session->chosen_force_info)
		GetCDCapacity(session);

	ReadCDCapacity(session);
	bitch(BITCHINFO,"DVD capacity is officially %u",session->CD_capacity);
	if (session->CD_capacity == 0) {
		bitch(BITCHINFO,"I cannot rip nothing, skipping rip stage");
		return;
	}

	if (dvd.open("cdrom-image") < 0) {
		bitch(BITCHERROR,"Cannot open Large Split Image dvdrom-image");
		return;
	}

	if (dvdmap.open("cdrom-image.ripmap") < 0) {
		bitch(BITCHERROR,"Cannot open dvdrom-image ripped map");
		return;
	}

	if (dvdvmap.open("cdrom-image.verified.ripmap") < 0) {
		bitch(BITCHERROR,"Cannot open dvdrom-image ripped map");
		return;
	}

	if (dvdtrmap.open("cdrom-image.timeout.ripfail.ripmap") < 0) {
		bitch(BITCHERROR,"Cannot open dvdrom-image ripped map");
		return;
	}

	if (dvdsub.open("cdrom-image.sub") < 0) {
		bitch(BITCHERROR,"Cannot open Large Split Image dvdrom-image");
		return;
	}

	if (dvdsubmap.open("cdrom-image.sub.ripmap") < 0) {
		bitch(BITCHERROR,"Cannot open dvdrom-image ripped map");
		return;
	}

	if (dvdlead.open("cdrom-image.lead") < 0) {
		bitch(BITCHERROR,"Cannot open Large Split Image dvdrom-image");
		return;
	}

	if (dvdleadmap.open("cdrom-image.lead.ripmap") < 0) {
		bitch(BITCHERROR,"Cannot open dvdrom-image ripped map");
		return;
	}

	cur = 0;
	full = session->CD_capacity + 150UL + 150UL; /* +pregap +lead-out */

	if (session->rip_expandfill) {
		return;
	}
	else if (session->rip_periodic > 0) {
		return;
	}
	else if (session->rip_verify) {
		unsigned char sector[RAWSEC];
		unsigned long chk;
		juint64 ofs;

		curt = prep = time(NULL);
		buf = session->bdev->buffer();
		rdmax = session->bdev->buffersize();

		while (cur < full) {
			ofs = (juint64)cur * (juint64)RAWSUB;
			if (dvdsubmap.get(cur)) {
				if (dvdsub.seek(ofs) == ofs && dvdsub.read(sector,RAWSUB) == RAWSUB) {
					if (nonzero(sector,96) && QSUB_Check(sector+(12*1)) && PSUB_Check(sector)) {
						unsigned char *q = sector+(12*1);
						if ((q[0]&0xF) == 1) { /* Mode-1 Q */
							if (q[1] > 0 && q[1] <= 0x99) { /* actual track */
								unsigned char exp_msf[3];
								CD2MSFnb(exp_msf,cur); /* FIXME: Is my DVD-ROM drive being weird or does the time reflect the M:S:F values of the NEXT sector? */
								if (dec2bcd(exp_msf[0]) == q[7] && dec2bcd(exp_msf[1]) == q[8] && dec2bcd(exp_msf[2]) == q[9]) {
								}
								else {
									/* is it the expected value +1?!? */
									dvdsubmap.set(cur,0);
									bitch(BITCHINFO,"Q-subchannel: absolute M:S:F mismatch sector %lu: %02x:%02x:%02x should be %02u:%02u:%02u",
										cur,q[7],q[8],q[9],
										exp_msf[0],exp_msf[1],exp_msf[2]);

									memset(sector,0,RAWSUB);
									if (dvdsub.seek(ofs) == ofs && dvdsub.write(sector,RAWSUB) == RAWSUB) {
									}
								}
							}
						}
					}
					else {
						dvdsubmap.set(cur,0);
						memset(sector,0,RAWSUB);
						if (dvdsub.seek(ofs) == ofs && dvdsub.write(sector,RAWSUB) == RAWSUB) {
						}
					}
				}
			}
			else {
				dvdsubmap.set(cur,0);
				memset(sector,0,RAWSUB);
				if (dvdsub.seek(ofs) == ofs && dvdsub.write(sector,RAWSUB) == RAWSUB) {
				}
			}

			if (dvdmap.get(cur) && !dvdvmap.get(cur)) {
				ofs = (juint64)cur * (juint64)RAWSEC;
				if (dvd.seek(ofs) == ofs && dvd.read(sector,RAWSEC) == RAWSEC) {
					if (!memcmp(sector,"\x00" "\xFF\xFF\xFF\xFF\xFF" "\xFF\xFF\xFF\xFF\xFF" "\x00",12)) {
						/* FIXME: As far as I know the M:S:F numbers tend to differ only on multisession CDs
						 *        or "extended CDs" with audio tracks followed by data. So this check would
						 *        fail on those. But we'll deal with those when we meet them.
						 *
						 *        This check acts as additional protection against bad reads, even though
						 *        experience so far tells me that most CD-ROM and DVD-ROM drives are very
						 *        good about using what they can to get the right sector even when asked
						 *        to read the sector raw. It's entitely possible that even in raw sector
						 *        mode the drives use this exact field to locate the sector precisely. */
						/* bytes 12,13,14 carry BCD M:S:F of the sector which should match up with what we
						 * asked for. */
						int M = (int)(cur / 75L / 60L);
						int S = (int)((cur / 75L) % 60L);
						int F = (int)(cur % 75L);

						if (dec2bcd(M) != sector[12] || dec2bcd(S) != sector[13] || dec2bcd(F) != sector[14]) {
							bitch(BITCHINFO,"Sector %lu, Data M:S:F fields do not match. Expected %02u:%02u:%02u got %02x:%02x:%02x\n",
								(long)cur,M,S,F,sector[12],sector[13],sector[14]);
							dvdvmap.set(cur,0);
							dvdmap.set(cur,0);
						}
						else {
							/* Mode 1 or 2 data sector */
							switch (sector[15] & 3) {
								case 0:	/* Mode 0 user data is zero */
									if (!nonzero(sector+16,RAWSEC-16)) {
										bitch(BITCHINFO,"Mode 0 user sector %lu has nonzero data",cur);
										dvdmap.set(cur,0);
									}
									else {
										dvdmap.set(cur,1);
										dvdvmap.set(cur,1);
									}
									break;
								case 1:	/* Mode 1 data (2048) */
									/* check one: does the data have the correct EDC+ECC? */
									chk = edc_compute(0,sector,2048+16);
									if (chk != get32lsb(sector+2048+16)) {
										fprintf(stderr,"Sector %lu [Mode1]: EDC checksum failed. 0x%08lx != 0x%08lx\n",cur,chk,get32lsb(sector+2048+16));
										dvdmap.set(cur,0);
									}
									else if (!ecc_checksector(sector+0xC,sector+0x10,sector+0x81C)) {
										fprintf(stderr,"Sector %lu [Mode1]: ECC check failed\n",cur);
										dvdmap.set(cur,0);
									}
									else if (!session->rip_verify_by_reading) {
										dvdmap.set(cur,1);
										dvdvmap.set(cur,1);
									}
									else {
										/* Okay good. Now: if we re-read the sector using the proper mode, does the drive return the same data? */
										memset(cmd,0,12);
										cmd[ 0] = 0xB9;
										cmd[ 1] = (2 << 2);	/* expected sector type=2 DAP=0 */
										CD2MSFnb(cmd+3,cur);
										CD2MSFnb(cmd+6,cur+1);
										cmd[ 9] = 0x10;		/* user area only */
										cmd[10] = 0;
										if (session->bdev->scsi(cmd,12,buf,2048,1) < 2048 || (sense=session->bdev->get_last_sense(NULL)) == NULL) {
											bitch(BITCHINFO,"Cannot seek to sector %u!",cur);
										}
										else if ((sense[2]&0xF) != 0) {
											bitch(BITCHINFO,"Sector %lu returned sense code %u",cur,sense[2]&0xF);
										}
										else if (memcmp(buf,sector+16,2048)) {
											bitch(BITCHINFO,"Mode 1 verification: data differs %lu",cur);
											dvdmap.set(cur,0);
										}
										else {
											dvdmap.set(cur,1);
											dvdvmap.set(cur,1);
										}
									}
									break;
								case 2: /* Mode 2 */
									/* look at the subheader. Mode 2 form 1, or Mode 2 form 2? */
									if (memcmp(sector+16,sector+20,4)) {
										bitch(BITCHINFO,"Mode 2 sector, subheaders don't match %lu",cur);
										dvdmap.set(cur,0);
									}
									else if (sector[16+2] & 0x20) { /* Mode 2 form 2 */
										/* does the EDC check out? */
										uint32_t got = get32lsb(sector+16+2332);
										chk = edc_compute(0,sector+16,2332);
										if (got != 0 && chk != got) {
											fprintf(stderr,"Sector %lu [Mode2Form2]: EDC checksum failed. 0x%08lx != 0x%08lx\n",(long)cur,(long)chk,(long)got);
											dvdmap.set(cur,0);
										}
										/* If the CRC is given we can verify data integrity without re-reading. Otherwise,
										 * we can only hope to catch errors by re-reading the sector to see if any bits
										 * flip. */
										else if (!session->rip_verify_by_reading && got != 0) {
											dvdmap.set(cur,1);
											dvdvmap.set(cur,1);
										}
										else {
											/* TODO: Add code that does not re-read unless the user says to do so explicitly, or the CRC checksum field is zero */
#if 0
											if (got == 0) /* Apparently the checksum field can be zero, for no checksum */
												fprintf(stderr,"Sector %lu [Mode2Form2]: EDC checksum warning: Field is zero, checksum not given\n",cur);
#endif
											/* does the drive return the same data? */
											memset(cmd,0,12);
											cmd[ 0] = 0xB9;
											cmd[ 1] = (5 << 2);	/* expected sector type=5 DAP=0 */
											CD2MSFnb(cmd+3,cur);
											CD2MSFnb(cmd+6,cur+1);
											cmd[ 9] = 0x10;		/* user area only */
											cmd[10] = 0;
											/* FIXME: Uhhhhh so if I say the buffer is 2324 bytes long then the drive fails reading with sense key == 7,
											 *        even though the returned data is 2324 bytes long? */
											if (session->bdev->scsi(cmd,12,buf,2352,1) < 2324 || (sense=session->bdev->get_last_sense(NULL)) == NULL) {
												bitch(BITCHINFO,"Cannot seek to sector %u!",cur);
											}
											else if ((sense[2]&0xF) != 0) {
												bitch(BITCHINFO,"Sector %lu returned sense code %u (Mode 2 Form 2)",cur,sense[2]&0xF);
											}
											else if (memcmp(buf,sector+24,2324)) {
												bitch(BITCHINFO,"Mode 2 Form 2 verification: data differs %lu",cur);
												dvdmap.set(cur,0);
											}
											else {
												dvdmap.set(cur,1);
												dvdvmap.set(cur,1);
											}
										}
									}
									else { /* Mode 2 Form 1 */
										/* do the EDC+ECC check out? */
										/* FIXME: Like Mode 2 form 2, are authoring programs allowed to set the CRC field to zero? */
										chk = edc_compute(0,sector+16,2048+8);
										if (chk != get32lsb(sector+16+2048+8)) {
											fprintf(stderr,"Sector %lu [Mode2Form1]: EDC checksum failed. 0x%08lx != 0x%08lx\n",cur,chk,get32lsb(sector+16+2048+8));
											dvdmap.set(cur,0);
										}
										else if (!ecc_checksector(zero4,sector+16,sector+16+0x80C)) {
											fprintf(stderr,"Sector %lu [Mode2Form1]: ECC check failed\n",cur);
											dvdmap.set(cur,0);
										}
										else if (!session->rip_verify_by_reading) {
											dvdmap.set(cur,1);
											dvdvmap.set(cur,1);
										}
										else {
											/* does the drive return the same data? */
											memset(cmd,0,12);
											cmd[ 0] = 0xB9;
											cmd[ 1] = (4 << 2);	/* expected sector type=4 DAP=0 */
											CD2MSFnb(cmd+3,cur);
											CD2MSFnb(cmd+6,cur+1);
											cmd[ 9] = 0x10;		/* user area only */
											cmd[10] = 0;
											if (session->bdev->scsi(cmd,12,buf,2048,1) < 2048 || (sense=session->bdev->get_last_sense(NULL)) == NULL) {
												bitch(BITCHINFO,"Cannot seek to sector %u!",cur);
											}
											else if ((sense[2]&0xF) != 0) {
												bitch(BITCHINFO,"Sector %lu returned sense code %u",cur,sense[2]&0xF);
											}
											else if (memcmp(buf,sector+24,2048)) {
												bitch(BITCHINFO,"Mode 2 Form 1 verification: data differs %lu",cur);
												dvdmap.set(cur,0);
											}
											else {
												dvdmap.set(cur,1);
												dvdvmap.set(cur,1);
											}
										}
									}
									break;
								default: /* 3 */
									bitch(BITCHINFO,"Invalid mode 3 in sector %lu",cur);
									dvdmap.set(cur,0);
									break;
							}
						}
					}
					else {
						int zm=0,om=0,i;

						/* then it's an audio track. maybe.... */
						/* 2011/08/20: I just realized something: Past experience with raw-ripping
						 *             tells me that drives can return a read from the lower level
						 *             circuitry, and that some times bits do flip in the raw data.
						 *             So what happens if the bit flip happens in the sync pattern?
						 *
						 *             This is highly unlikely on today's smart DVD-ROM drives, but
						 *             something worth checking for. Because I'm paranoid.
						 *
						 *             The sync pattern for data is:
						 *
						 *             0x00 0xFF 0xFF 0xFF  0xFF 0xFF 0xFF 0xFF  0xFF 0xFF 0xFF 0x00 */
						for (i=0;i < 8;i++) { /* bytes 0 and 11 should be zero */
							zm += ((sector[0] >> i) & 1) == 0;
							zm += ((sector[11] >> i) & 1) == 0;
						}
						for (i=0;i < (10*8);i++) { /* bytes 0 and 11 should be zero */
							om += ((sector[(i>>3)+1] >> (i&7)) & 1) == 1;
						}

						/* there should be 16 zero bits and 80 one bits */
						/* allow slop for up to 3 wrong zero bits and 6 wrong one bits */
						if (zm == 16 && om == 80) {
							/* perfect match?!? */
							bitch(BITCHERROR,"Perfect match on %lu data pattern but memcmp() failed?!? How can that be?!?",cur);
							abort();
						}
						else if (om >= (80-6) && zm >= (16-3)) {
							bitch(BITCHWARNING,"Sector %lu does not have explicit data pattern but it looks very close to one!",cur);
							bitch(BITCHWARNING,"It's possible this data sector's sync pattern has bit flip errors");
							bitch(BITCHWARNING,"  zero=%u one=%u",zm,om);
							dvdmap.set(cur,0);
						}
					}
				}
			}

			/* compute percent ratio */
			percent = (int)((((juint64)cur) * ((juint64)10000) / ((juint64)full)));

			curt = time(NULL);
			if (prep != curt)
				bitch(BITCHINFO,"Rip: %%%3u.%02u @ %8u %.2fx, expected %8u %.1fx (verify)",
					percent/100,percent%100,
					cur,d1,expsect,d2);

			prep = curt;
			cur++;
		}
		return;
	}

	/* start off by skipping sectors we've already ripped */
	while (cur < full && dvdmap.get(cur)) cur++;

	/* assuming a typical DVD-ROM drive expect initially a 1x
	 * ripping rate = 10600000 bits/sec = 176400 bytes */
	if (session->rip_assume_rate > 0.0) {
		rate = (int)(176400.00 * session->rip_assume_rate);
		adjrate = 0;
	}
	else {
		rate = 176400;		// assume 1x
		adjrate = 1;
	}
	/* initialize time() specific stuff. why? so that we can track whether or not
	 * the ripping is happening at a timely pace. If not, there's probably some
	 * bad sectors and keeping a timely pace will skip over them and get to good
	 * data faster. */
	start = time(NULL);
	expect = start;

	recover = 0;
	adjsect = 0;
	pfull = dvdmap.getsize();
	prep = 0;
	isect = cur;
	buf = session->bdev->buffer();
	rdmax = session->bdev->buffersize();
	rdmax = (rdmax * 2048) / (RAWSEC+RAWSUB);
	if (rdmax > 24) rdmax = 24;
	rd = rdmax;
	lastend = cur;
	
	if (session->rip_backwards) {
		if (session->rip_backwards_from_outermost) {
			backwards_next = full-1;
		}
		else if (pfull != 0) {
			backwards_next = pfull-1;
		}
		else {
			bitch(BITCHWARNING,"Cannot rip anything backwards since nothing has been ripped");
			return;
		}

		bitch(BITCHINFO,"Starting rip backwards at %u",backwards_next);
	}
	else {
		bitch(BITCHINFO,"Starting rip forwards at %u",cur);
	}

	/* copy down Table of Contents, PMA, etc */
	{
		unsigned long alloc;
		unsigned char trk;
		struct stat st;
		FILE *fp;
		int i;

		alloc = 2048 * rdmax;
		if (alloc > 16384) alloc = 16384;
		if (stat("cdrom.toc",&st) != 0) {
			fp = fopen("cdrom.toc","w");
			if (fp) {
				/* copy down table of contents */
				for (trk=0;trk < 0xFF;trk++) {
					memset(cmd,0,12);
					cmd[ 0] = 0x43;
					cmd[ 1] = 0x02;	/* MSF=1 */
					cmd[ 2] = 0; /* format=0 */
					cmd[ 6] = trk;
					cmd[ 7] = alloc >> 8;
					cmd[ 8] = alloc;
					memset(buf,0,alloc);
					if (session->bdev->scsi(cmd,10,buf,alloc,1) >= 4 && (sense=session->bdev->get_last_sense(NULL)) != NULL && (sense[2]&0xF) == 0) {
						unsigned long dlen = ((unsigned long)buf[0] << 8) | ((unsigned long)buf[1]);
						if (dlen >= 2) dlen -= 2;
						else dlen = 0;
						if (dlen > alloc) {
							bitch(BITCHINFO,"WARNING: TOC %02u data len > alloc len",trk);
							dlen = alloc;
						}
						if ((dlen % 8) != 0) {
							bitch(BITCHINFO,"WARNING: TOC %02u data is not multiple of 8",trk);
							dlen -= dlen % 8;
						}
						fprintf(fp,"# TOC track 0x%02x len %u first=%02x last=%02x\n",trk,dlen,buf[2],buf[3]);
						fprintf(fp,"TOC 0x%02x:%u\n",trk,dlen);
						for (i=0;i < dlen;i += 8) {
							fprintf(fp,"\tADR=0x%02x CTRL=0x%02x TRACK=0x%02x START=%02u:%02u:%02u:%02u Res1=0x%02x Res2=0x%02x\n",
								buf[4+i+1] >> 4,
								buf[4+i+1] & 0xF,
								buf[4+i+2],
								buf[4+i+4],
								buf[4+i+5],
								buf[4+i+6],
								buf[4+i+7],
								buf[4+i+0],
								buf[4+i+3]);
						}
					}
				}

				/* copy down last session */
				{
					memset(cmd,0,12);
					cmd[ 0] = 0x43;
					cmd[ 1] = 0x02;	/* MSF=1 */
					cmd[ 2] = 1; /* format=1 */
					cmd[ 6] = 0x00; /* ignored by drive */
					cmd[ 7] = alloc >> 8;
					cmd[ 8] = alloc;
					memset(buf,0,alloc);
					if (session->bdev->scsi(cmd,10,buf,alloc,1) >= 4 && (sense=session->bdev->get_last_sense(NULL)) != NULL && (sense[2]&0xF) == 0) {
						unsigned long dlen = ((unsigned long)buf[0] << 8) | ((unsigned long)buf[1]);
						if (dlen >= 2) dlen -= 2;
						else dlen = 0;
						if (dlen > alloc) {
							bitch(BITCHINFO,"WARNING: TOC last session %02u data len > alloc len",trk);
							dlen = alloc;
						}
						if ((dlen % 8) != 0) {
							bitch(BITCHINFO,"WARNING: TOC last session %02u data is not multiple of 8",trk);
							dlen -= dlen % 8;
						}
						fprintf(fp,"# Last known session len %u first=%02x last=%02x\n",dlen,buf[2],buf[3]);
						fprintf(fp,"LAST-SESSION %u\n",dlen);
						for (i=0;i < dlen;i += 8) {
							fprintf(fp,"\tADR=0x%02x CTRL=0x%02x FIRST-TRACK-IN-LAST-SESSION=0x%02x START=%02u:%02u:%02u:%02u Res1=0x%02x Res2=0x%02x\n",
								buf[4+i+1] >> 4,
								buf[4+i+1] & 0xF,
								buf[4+i+2],
								buf[4+i+4],
								buf[4+i+5],
								buf[4+i+6],
								buf[4+i+7],
								buf[4+i+0],
								buf[4+i+3]);
						}
					}
				}

				/* copy down table of contents (raw) */
				for (trk=0;trk < 0xFF;trk++) {
					memset(cmd,0,12);
					cmd[ 0] = 0x43;
					cmd[ 1] = 0x02;	/* MSF=1 */
					cmd[ 2] = 2; /* format=2 */
					cmd[ 6] = trk;
					cmd[ 7] = alloc >> 8;
					cmd[ 8] = alloc;
					memset(buf,0,alloc);
					if (session->bdev->scsi(cmd,10,buf,alloc,1) >= 4 && (sense=session->bdev->get_last_sense(NULL)) != NULL && (sense[2]&0xF) == 0) {
						unsigned long dlen = ((unsigned long)buf[0] << 8) | ((unsigned long)buf[1]);
						if (dlen >= 2) dlen -= 2;
						else dlen = 0;
						if (dlen > alloc) {
							bitch(BITCHINFO,"WARNING: TOC %02u data len > alloc len",trk);
							dlen = alloc;
						}
						if ((dlen % 11) != 0) {
							bitch(BITCHINFO,"WARNING: TOC %02u data is not multiple of 8",trk);
							dlen -= dlen % 11;
						}
						fprintf(fp,"# TOC raw track 0x%02x len %u first-session=%02x last-session=%02x\n",trk,dlen,buf[2],buf[3]);
						fprintf(fp,"TOC-RAW 0x%02x:%u\n",trk,dlen);
						for (i=0;i < dlen;i += 11) {
							fprintf(fp,"\tSESSION=0x%02x ADR=0x%02x CTRL=0x%02x TNO=%u POINT=0x%02x MSF=%02u:%02u:%02u ZERO=0x%02x pMSF=%02u:%02u:%02u\n",
								buf[4+i+0],
								buf[4+i+1] >> 4,
								buf[4+i+1] & 0xF,
								buf[4+i+2],
								buf[4+i+3],
								buf[4+i+4],
								buf[4+i+5],
								buf[4+i+6],
								buf[4+i+7],
								buf[4+i+8],
								buf[4+i+9],
								buf[4+i+10]);
						}
					}
				}

				/* copy down PMA (raw) */
				{
					memset(cmd,0,12);
					cmd[ 0] = 0x43;
					cmd[ 1] = 0x02;	/* MSF=1 */
					cmd[ 2] = 3; /* format=3 */
					cmd[ 6] = 0;
					cmd[ 7] = alloc >> 8;
					cmd[ 8] = alloc;
					memset(buf,0,alloc);
					if (session->bdev->scsi(cmd,10,buf,alloc,1) >= 4 && (sense=session->bdev->get_last_sense(NULL)) != NULL && (sense[2]&0xF) == 0) {
						unsigned long dlen = ((unsigned long)buf[0] << 8) | ((unsigned long)buf[1]);
						if (dlen >= 2) dlen -= 2;
						else dlen = 0;
						if (dlen > alloc) {
							bitch(BITCHINFO,"WARNING: TOC %02u data len > alloc len",trk);
							dlen = alloc;
						}
						if ((dlen % 11) != 0) {
							bitch(BITCHINFO,"WARNING: TOC %02u data is not multiple of 8",trk);
							dlen -= dlen % 11;
						}
						if (dlen != 0) {
							fprintf(fp,"# PMA raw track 0x%02x len %u first-session=%02x last-session=%02x\n",trk,dlen,buf[2],buf[3]);
							fprintf(fp,"PMA-RAW 0x%02x:%u\n",trk,dlen);
							for (i=0;i < dlen;i += 11) {
								fprintf(fp,"\tSESSION=0x%02x ADR=0x%02x CTRL=0x%02x TNO=%u POINT=0x%02x MSF=%02u:%02u:%02u ZERO=0x%02x pMSF=%02u:%02u:%02u\n",
									buf[4+i+0],
									buf[4+i+1] >> 4,
									buf[4+i+1] & 0xF,
									buf[4+i+2],
									buf[4+i+3],
									buf[4+i+4],
									buf[4+i+5],
									buf[4+i+6],
									buf[4+i+7],
									buf[4+i+8],
									buf[4+i+9],
									buf[4+i+10]);
							}
						}
					}
				}

				/* copy down ATIP */
				{
					memset(cmd,0,12);
					cmd[ 0] = 0x43;
					cmd[ 1] = 0x02;	/* MSF=1 */
					cmd[ 2] = 4; /* format=4 */
					cmd[ 6] = 0;
					cmd[ 7] = alloc >> 8;
					cmd[ 8] = alloc;
					memset(buf,0,alloc);
					if (session->bdev->scsi(cmd,10,buf,alloc,1) >= 4 && (sense=session->bdev->get_last_sense(NULL)) != NULL && (sense[2]&0xF) == 0) {
						unsigned long dlen = ((unsigned long)buf[0] << 8) | ((unsigned long)buf[1]);
						if (dlen >= 2) dlen -= 2;
						else dlen = 0;
						if (dlen != 0) {
							fprintf(fp,"# ATIP len=%u\n",dlen);
							fprintf(fp,"ATIP %u\n",dlen);
							fprintf(fp,"\t");
							for (i=0;i < dlen;i++) fprintf(fp,"%02x",buf[4+i]);
							fprintf(fp,"\n");
						}
					}
				}

				/* copy down CD-TEXT */
				{
					memset(cmd,0,12);
					cmd[ 0] = 0x43;
					cmd[ 1] = 0x02;	/* MSF=1 */
					cmd[ 2] = 5; /* format=5 */
					cmd[ 6] = 0;
					cmd[ 7] = alloc >> 8;
					cmd[ 8] = alloc;
					memset(buf,0,alloc);
					if (session->bdev->scsi(cmd,10,buf,alloc,1) >= 4 && (sense=session->bdev->get_last_sense(NULL)) != NULL && (sense[2]&0xF) == 0) {
						unsigned long dlen = ((unsigned long)buf[0] << 8) | ((unsigned long)buf[1]);
						if (dlen >= 2) dlen -= 2;
						else dlen = 0;
						if (dlen != 0) {
							fprintf(fp,"# CD-TEXT len=%u\n",dlen);
							fprintf(fp,"CD-TEXT %u\n",dlen);
							fprintf(fp,"\t");
							for (i=0;i < dlen;i++) fprintf(fp,"%02x",buf[4+i]);
							fprintf(fp,"\n");
						}
					}
				}

				fclose(fp);
			}
		}
	}

	if (session->rip_verify_by_reading) {
		for (unsigned long i=0;i < full;i++) {
			dvdtrmap.set(i,0);
		}
	}

	while (!session->chosen_dont_rip && (cur < full || session->rip_backwards)) {
		if (session->singlesector)
			rd=1;

		if (session->rip_backwards) {
			unsigned long curend;

			/* scan backwards to look for an unripped slot */
			cur = backwards_next;
			while (cur != ((unsigned)-1) && (dvdmap.get(cur) || dvdtrmap.get(cur))) cur--;
			curend = ++cur;

			if (cur == 0) {
				session->todo->set(TODO_RIPDVD,1);
				break;
			}

			/* scan backwards to see how big this gap is */
			sz = 1;
			while (sz < rd && (cur-sz) != 0 && (!dvdmap.get(cur-sz) && !dvdtrmap.get(cur-sz))) sz++;
			cur -= sz;
		}
		else {
			/* look for an unripped slot */
			while (cur < full && (dvdmap.get(cur) || dvdtrmap.get(cur))) cur++;
			if (cur >= full) {
				session->todo->set(TODO_RIPDVD,1);
				break;
			}

			/* assume at minimum one sector. how many more can we rip? */
			sz = 1;
			while (sz < rd && (cur+sz) < full && (!dvdmap.get(cur+sz) && !dvdtrmap.get(cur+sz))) sz++;

			/* now, based on where we last left off, adjust isect. if we don't
			 * do this the rate calculator will come up with insanely high values
			 * if this program is re-run to fill in sparse gaps that are far apart. */
			adjsect += cur - lastend;

			/* how far should we be? */
			curt = time(NULL);
			expect = curt - start;
			byteo = ((juint64)expect) * ((juint64)rate);
			expsect = ((unsigned long)(byteo / ((juint64)2048))) + isect;

			/* how fast is the rip going? */
			if (cur > expect)	d1 = ((double)(cur - (isect + adjsect))) / ((double)(expsect - isect));
			else			d1 = 0.0;
			d2 = ((double)rate) / 176400.00;
			d1 *= d2;
		}

		/* compute percent ratio */
		percent = (int)((((juint64)cur) * ((juint64)10000) / ((juint64)full)));

		/* print status once a second */
		if (session->rip_backwards)
			bitch(BITCHINFO,"Rip: @ %8u (%u sectors)",
				cur,sz);
		else if (prep != curt)
			bitch(BITCHINFO,"Rip: %%%3u.%02u @ %8u %.2fx, expected %8u %.1fx rd=%u",
				percent/100,percent%100,
				cur,d1,expsect,d2,rd);

		/* rip! */
		bitch_indent();

		int rdr;
		time_t ot,nt;
		ot = time(NULL);

		memset(cmd,0,12);
		cmd[ 0] = 0xB9;
		cmd[ 1] = (0 << 2);	/* expected sector type=0 DAP=0 */
		CD2MSFnb(cmd+3,cur);
		CD2MSFnb(cmd+6,cur+sz);
		cmd[ 9] = 0xF8;		/* raw sector */
		cmd[10] = p_w_mode;
		errno = 0;
		if ((rdr=session->bdev->scsi(cmd,12,buf,(RAWSEC+RAWSUB)*sz,1)) < ((RAWSEC+RAWSUB)*sz) || (sense=session->bdev->get_last_sense(NULL)) == NULL) {
			nt = time(NULL);
			bitch(BITCHINFO,"Cannot seek to sector %u! rdr=%d errno=%s sense=%p",cur,rdr,strerror(errno),sense);
			if (sense != NULL) {
				bitch(BITCHINFO,"  Sense bytes %02X %02X, %02X %02X, %02X %02X, %02X %02X, %02X %02X, %02X %02X",
					sense[ 0],sense[ 1],sense[ 2],sense[ 3],sense[ 4],sense[ 5],
					sense[ 6],sense[ 7],sense[ 8],sense[ 9],sense[10],sense[11]);
				bitch(BITCHINFO,"           >> %02X %02X, %02X %02X, %02X %02X",
					sense[12],sense[13],sense[14],sense[15],sense[16],sense[17]);
			}
			got = 0;
			cur++;
			rd=1;

			if (nt >= (ot+3)) {
				bitch(BITCHINFO,"   will not attempt again, took too long");
				dvdtrmap.set(cur,1);
			}
		}
		else if ((sense[2]&0xF) != 0) {
			nt = time(NULL);

			if ((sense[2]&0xF) != 5)
				bitch(BITCHINFO,"Sector %lu returned sense code %u",cur,sense[2]&0xF);
			if ((sense[2]&0xF) == 3 && nt >= (ot+3)) {
				bitch(BITCHINFO,"   will not attempt again, took too long");
				dvdtrmap.set(cur,1);
			}

			if (session->rip_backwards) {
				cur--;
				got=0;
			}
			else {
				cur++;
				rd=1;
			}
		}
		else {
			juint64 ofs;
			got = sz;

			if (cur < pfull && !session->rip_backwards) {
				if (!recover) {
					first_recover = cur;
					last_recover = cur + got;
					recover = 1;
				}
				else {
					if (cur <= last_recover) {
						last_recover = cur + got;
					}
					else {
						bitch(BITCHINFO,"Recovered %u sectors @ %u",
								(cur + got) - first_recover,first_recover);

						first_recover = cur;
						last_recover = cur + got;
					}
				}

				if (!recover) {
					if (got > 1)	bitch(BITCHINFO,"Recovered %u sectors @ %u",got,cur);
					else		bitch(BITCHINFO,"Recovered sector %u",cur);
				}
			}

			{
				unsigned char *p = buf;
				int c = got,zero;

				while (c > 0) {
					for (zero=0;zero < (RAWSEC+RAWSUB) && p[zero] == 0;) zero++;

					if (zero == (RAWSEC+RAWSUB)) {
						bitch(BITCHWARNING,"Sector %llu is completely zero",cur);
					}
					else {
						ofs = (juint64)cur * (juint64)RAWSEC;
						if (dvd.seek(ofs) != ofs) {
							bitch(BITCHWARNING,"Problem seeking to offset " PRINT64F,ofs);
							return;
						}
						else if (dvd.write(p,RAWSEC) < RAWSEC) {
							bitch(BITCHWARNING,"Problem writing data at offset " PRINT64F,ofs);
							return;
						}
						else {
							dvdmap.set(cur,1);
						}

						if (p_w_mode == RAW96) DeinterleaveRaw96(p+RAWSEC);
						ofs = (juint64)cur * (juint64)RAWSUB;
						if (dvdsub.seek(ofs) != ofs) {
							bitch(BITCHWARNING,"Problem seeking to offset " PRINT64F,ofs);
							return;
						}
						else if (dvdsub.write(p+RAWSEC,RAWSUB) < RAWSUB) {
							bitch(BITCHWARNING,"Problem writing data at offset " PRINT64F,ofs);
							return;
						}
						else if (nonzero(p+RAWSEC,96) && QSUB_Check(p+RAWSEC+(12*1)) && PSUB_Check(p+RAWSEC)) {
							/* WAITAMINUTE: DVD-ROM drives have trouble returning the *right* subchannel data! */
							unsigned char *q = p+RAWSEC+(12*1);
							if ((q[0]&0xF) == 1) { /* Mode-1 Q */
								if (q[1] > 0 && q[1] <= 0x99) { /* actual track */
									unsigned char exp_msf[3];

									CD2MSFnb(exp_msf,cur); /* FIXME: Is my DVD-ROM drive being weird or does the time reflect the M:S:F values of the NEXT sector? */
									if (dec2bcd(exp_msf[0]) == q[7] && dec2bcd(exp_msf[1]) == q[8] && dec2bcd(exp_msf[2]) == q[9]) {
										dvdsubmap.set(cur,1);
									}
								}
							}
						}
					}

					p += RAWSEC+RAWSUB;
					cur++;
					c--;
				}
			}

			if (rd < rdmax)
				rd++;
		}

		/* keep track of where we last ended */
		lastend = cur;
		usleep(10000);

		if (!session->rip_backwards) {
			/* if we are far behind timewise, jump ahead */
			if (expsect >= 10000 && cur < (expsect-10000)) {
				if (d1 < (d2+0.25) && d2 > 1.0 && adjrate) {
					d2 -= 0.4;
					if (d2 < 1.0) d2 = 1.0;
					rate = (int)(d2 * 176400.00);
				}
				else {
					if (!session->rip_noskip) {
						bitch(BITCHINFO,"Skipping ahead to keep pace");
						cur = expsect;
					}
					if (adjrate) {
						d2 -= 0.4;
						if (d2 < 1.0) d2 = 1.0;
						rate = (int)(d2 * 176400.00);
					}
				}
			}
			/* if we are far ahead, assume that the drive is in fact faster */
			else if (cur > (expsect+10000) && d1 > (d2+0.2)) {
				if (adjrate) {
					d2 += 0.2;
					rate = (int)(d2 * 176400.00);
				}
			}
		}
		else {	/* session->rip_backwards != 0 */
			backwards_next = cur;
			if (backwards_next >= got)
				backwards_next -= got;
			else
				backwards_next = 0;
		}

		bitch_unindent();
		prep = curt;
	}

	if (session->rip_subchannel) {
		cur = 0;
		bitch(BITCHINFO,"Now trying to recover subchannel full=%lu",(unsigned long)full);
		while (cur < full) {
			curt = time(NULL);

			/* compute percent ratio */
			percent = (int)((((juint64)cur) * ((juint64)10000) / ((juint64)full)));

			if (prep != curt)
				bitch(BITCHINFO,"Rip: %%%3u.%02u @ %8u %.2fx, expected %8u %.1fx (subchannel)",
						percent/100,percent%100,
						cur,d1,expsect,d2);

			if (!dvdsubmap.get(cur) && (session->chosen_dont_rip || dvdmap.get(cur))) {
				usleep(10000);
				memset(cmd,0,12);
				cmd[ 0] = 0xB9;
				cmd[ 1] = (0 << 2);	/* expected sector type=0 DAP=0 */
				CD2MSFnb(cmd+3,cur);
				CD2MSFnb(cmd+6,cur+1);
				cmd[ 9] = 0x00;		/* no data */
				cmd[10] = p_w_mode;	/* raw unformatted P-W bits */
				fprintf(stderr,"%lu   \x0D",cur); fflush(stderr);
				if (session->bdev->scsi(cmd,12,buf,(RAWSUB),1) < (RAWSUB) || (sense=session->bdev->get_last_sense(NULL)) == NULL) {
					bitch(BITCHINFO,"Cannot seek to sector %u!",cur);
					got = 0;
					cur++;
					rd=1;
				}
				else if ((sense[2]&0xF) != 0) {
					if ((sense[2]&0xF) != 5)
						bitch(BITCHINFO,"Sector %lu returned sense code %u",cur,sense[2]&0xF);
				}
				else {
					juint64 ofs;

					if (p_w_mode == RAW96) DeinterleaveRaw96(buf);
					ofs = (juint64)cur * (juint64)RAWSUB;
					if (dvdsub.seek(ofs) != ofs) {
						bitch(BITCHWARNING,"Problem seeking to offset " PRINT64F,ofs);
						return;
					}
					else if (dvdsub.write(buf,RAWSUB) < RAWSUB) {
						bitch(BITCHWARNING,"Problem writing data at offset " PRINT64F,ofs);
						return;
					}
					else if (nonzero(buf,96) && QSUB_Check(buf+(12*1)) && PSUB_Check(buf)) {
						int ok = 1,rt,i;

						/* WAITAMINUTE: DVD-ROM drives have trouble returning the *right* subchannel data! */
						{
							unsigned char *q = buf+(12*1);
							if ((q[0]&0xF) == 1) { /* Mode-1 Q */
								if (q[1] > 0 && q[1] <= 0x99) { /* actual track */
									unsigned char exp_msf[3];
									unsigned char exp_msf1[3];

									CD2MSFnb(exp_msf,cur); /* FIXME: Is my DVD-ROM drive being weird or does the time reflect the M:S:F values of the NEXT sector? */
									CD2MSFnb(exp_msf1,cur+1); /* FIXME: Is my DVD-ROM drive being weird or does the time reflect the M:S:F values of the NEXT sector? */

									if (dec2bcd(exp_msf[0]) == q[7] && dec2bcd(exp_msf[1]) == q[8] && dec2bcd(exp_msf[2]) == q[9]) {
										ok = 1;
									}
									else {
										/* well then where DOES it belong? */
										unsigned long aloc =	(bcd2dec(q[7]) * 75UL * 60UL) +
											(bcd2dec(q[8]) * 75UL) +
											bcd2dec(q[9]);
										unsigned long long nofs = (unsigned long long)aloc * (unsigned long long)RAWSUB;

										if (labs(aloc - cur) < 8) {
											//										bitch(BITCHWARNING,"Drive gave me subchannel data for sector %lu not %lu, using anyway",aloc,cur);
											if (!dvdsubmap.get(aloc)) {
												if (dvdsub.seek(nofs) != nofs) {
													bitch(BITCHWARNING,"Problem seeking to offset " PRINT64F,ofs);
													return;
												}
												else if (dvdsub.write(buf,RAWSUB) < RAWSUB) {
													bitch(BITCHWARNING,"Problem writing data at offset " PRINT64F,ofs);
													return;
												}

												dvdsubmap.set(aloc,1);
											}
										}

										ok = 0;
									}
								}
							}
						}

						if (ok) dvdsubmap.set(cur,1);
					}
				}
			}

			prep = curt;
			cur++;
		}
	}

	cur = 0;
	bitch(BITCHINFO,"Now trying to recover lead-in");
	while (cur < 0x10000000UL) {
		unsigned long sn = 0xFFFFFFFF - cur;
		int rdr;

		curt = time(NULL);

		/* compute percent ratio */
		percent = (int)((((juint64)cur) * ((juint64)10000) / ((juint64)0x10000000UL)));

		if (prep != curt)
			bitch(BITCHINFO,"Rip: %%%3u.%02u @ %08lX %.2fx, expected %8u %.1fx",
				percent/100,percent%100,
				sn,d1,expsect,d2);

		if (!dvdleadmap.get(cur)) {
			usleep(10000);
			memset(cmd,0,12);
			cmd[ 0] = 0xBE;
			cmd[ 1] = (0 << 2);	/* expected sector type=0 DAP=0 */
			cmd[ 2] = (unsigned char)(sn >> 24);
			cmd[ 3] = (unsigned char)(sn >> 16);
			cmd[ 4] = (unsigned char)(sn >> 8);
			cmd[ 5] = (unsigned char)(sn);
			cmd[ 6] = (unsigned char)(1 >> 16);
			cmd[ 7] = (unsigned char)(1 >> 8);
			cmd[ 8] = (unsigned char)(1);
			cmd[ 9] = 0xF8;		/* raw sector */
			cmd[10] = p_w_mode;	/* raw unformatted P-W bits */
			if ((rdr=session->bdev->scsi(cmd,12,buf,(RAWSEC+RAWSUB),1)) < (RAWSEC+RAWSUB) || (sense=session->bdev->get_last_sense(NULL)) == NULL) {
				bitch(BITCHINFO,"Cannot seek to sector 0x%08lX! rdr=%d errno=%s sense=%p",sn,rdr,strerror(errno),sense);
				if (sense != NULL) {
					bitch(BITCHINFO,"  Sense bytes %02X %02X, %02X %02X, %02X %02X, %02X %02X, %02X %02X, %02X %02X",
						sense[ 0],sense[ 1],sense[ 2],sense[ 3],sense[ 4],sense[ 5],
						sense[ 6],sense[ 7],sense[ 8],sense[ 9],sense[10],sense[11]);
					bitch(BITCHINFO,"           >> %02X %02X, %02X %02X, %02X %02X",
						sense[12],sense[13],sense[14],sense[15],sense[16],sense[17]);
				}

				got = 0;
				rd=1;
			}
			else if ((sense[2]&0xF) != 0) {
				if ((sense[2]&0xF) != 5)
					bitch(BITCHINFO,"Sector %lu returned sense code %u",cur,sense[2]&0xF);
			}
			else {
				juint64 ofs;

				if (p_w_mode == RAW96) DeinterleaveRaw96(buf+RAWSEC);
				ofs = (juint64)cur * (juint64)(RAWSEC+RAWSUB);
				if (dvdlead.seek(ofs) != ofs) {
					bitch(BITCHWARNING,"Problem seeking to offset " PRINT64F,ofs);
					return;
				}
				else if (dvdlead.write(buf+RAWSEC,RAWSEC+RAWSUB) < (RAWSEC+RAWSUB)) {
					bitch(BITCHWARNING,"Problem writing data at offset " PRINT64F,ofs);
					return;
				}
				else {
					dvdleadmap.set(cur,1);
				}
			}
		}

		prep = curt;
		cur++;
		if (cur == 200) cur = 0xFFFFFFFF - 0xF0001000;
	}

	if (recover)
		bitch(BITCHINFO,"Recovered %u sectors @ %u",
			last_recover,first_recover);

	leftover = 0;
	for (cur=0;cur < full;cur++)
		if (!dvdmap.get(cur)) 
			leftover++;

	if (leftover > 0)
		bitch(BITCHINFO,
			"%u sectors have not been ripped yet. Re-run this program to fill them in!",
			leftover);
	else
		session->todo->set(TODO_RIPDVD,1);

	leftover = 0;
	for (cur=0;cur < full;cur++)
		if (!dvdsubmap.get(cur)) 
			leftover++;

	if (leftover > 0)
		bitch(BITCHINFO,
			"%u subchannel sectors have not been ripped yet. Re-run this program to fill them in!",
			leftover);
}

int main(int argc,char **argv)
{
	JarchSession session;
	RippedMap* todo;
	int i;

#ifdef WIN32
	{
		// large SCSI xfers really slow NT down!
		// set our own task priority to low!
		SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);
	}
#endif

	/* EDC+ECC code borrowed from cmd tools */
	eccedc_init();

	/* initial bitchin' output vector */
	bitch_init(stderr);

	/* parameters? */
	if (!params(argc,argv)) {
		bitch(BITCHERROR,"There is an error in the command line argument list");
		return 1;
	}

	/* redirect bitchin' */
	if (strlen(chosen_bitch_out_fname)) {
		chosen_bitch_out = fopen(chosen_bitch_out_fname,"rb+");
		if (!chosen_bitch_out) chosen_bitch_out = fopen(chosen_bitch_out_fname,"w");

		if (!chosen_bitch_out) {
			bitch(BITCHERROR,"Cannot open output file %s for writing",chosen_bitch_out_fname);
			return 1;
		}
		else {
			fseek(chosen_bitch_out,0,SEEK_END);
		}
	}
	else {
		/* all bitchin goes to standard output */
		chosen_bitch_out = stdout;
	}

	bitch_init(chosen_bitch_out);
	
	bitch(BITCHINFO,"+----------------- <<< NEW SESSION >>> --------------------+");
	bitch(BITCHINFO,"| JarchCD CD-ROM archiving utility v2.1 New and Improved   |");
	bitch(BITCHINFO,"| Version v2.1 release 07-17-2011                          |");
	bitch(BITCHINFO,"+----------------------------------------------------------+");
#ifdef WIN32
	bitch(BITCHINFO,"Operating system................... Microsoft Windows");
#endif
#ifdef LINUX
	bitch(BITCHINFO,"Operating system................... Linux");
#endif
	bitch(BITCHINFO,"Chosen status output file.......... %s",chosen_bitch_out_fname);
	bitch(BITCHINFO,"Chosen block device to rip from.... %s",chosen_input_block_dev);

	// for archival noteworthiness print out the command line parameters
	bitch(BITCHINFO,"Command line options:");
	bitch_indent();
	for (i=1;i < argc;i++) bitch(BITCHINFO,"argv[%2u] = %s",i,argv[i]);
	bitch_unindent();

	// ...and the name of the current path
	bitch_cwd();

	// are we doing a test mode?
	if (chosen_test_mode > 0) {
		DoTestMode();
	}
	else {
		// now get the default blockio object IF not -decss
		if (!css_decrypt_inplace) {
			chosen_bio = blockiodefault(driver_name.length() != 0 ? driver_name.c_str() : NULL);
			if (!chosen_bio) {
				bitch(BITCHERROR,"Unable to obtain default JarchBlockIO object");
				return 1;
			}
		}

		// open the "TODO" list
		todo = new RippedMap();
		if (!todo) {
			bitch(BITCHERROR,"Unable to allocate TODO list");
			return 1;
		}
		if (todo->open("JarchCD2-TODO.list") < 0) {
			bitch(BITCHERROR,"Unable to open TODO list");
			return 1;
		}

		if (!css_decrypt_inplace && !show_todo) {
			// open blockio
			if (chosen_bio->open(chosen_input_block_dev) < 0) {
				bitch(BITCHERROR,"Unable to open device %s",chosen_input_block_dev);
				delete chosen_bio;
				return 1;
			}

			// does the blockio object support direct SCSI commands?
			session.bdev_scsi = 0;
			if (chosen_bio->scsi(NULL,0,NULL,0,0) == 0) {
				bitch(BITCHINFO,"BlockIO code supports direct SCSI commands");
				session.bdev_scsi = 1;
			}
		}

		// setup session object
		session.todo = todo;
		session.bdev = chosen_bio;
		session.chosen_force_info = chosen_force_info;
		session.chosen_force_rip = chosen_force_rip;
		session.chosen_dont_rip = chosen_dont_rip;
		session.rip_noskip = rip_noskip;
		session.rip_assume_rate = rip_assume_rate;
		session.rip_verify = rip_verify;
		session.rip_verify_by_reading = rip_verify_by_reading;
		session.rip_backwards_from_outermost = rip_backwards_from_outermost;
		session.rip_expandfill = rip_expandfill;
		session.rip_subchannel = rip_subchannel;
		session.rip_periodic = rip_periodic;
		session.skip_rip = skip_rip;
		session.poi_dumb = poi_dumb;
		session.rip_backwards = rip_backwards;
		session.singlesector = singlesector;

		if (!css_decrypt_inplace && !show_todo) {
			// Use various MMC commands to examine the DVD-ROM media!
			GatherMediaInfo(&session);

			if (rip_assume > 0) {
				session.CD_capacity = rip_assume;

				for (i=0;i < 32;i++)
					session.todo->set(TODO_RIPDVD_CAPACITY_DWORD+i,(session.CD_capacity >> i) & 1);

				session.todo->set(TODO_RIPDVD_READCAPACITY,1);
			}

			// Rip DVD!
			RipCD(&session);
		}

		// show status info
		RipCDStatus(&session);

		// close TODO list
		todo->close();
		delete todo;

		// close blockio
		if (chosen_bio) {
			chosen_bio->close();
			delete chosen_bio;
			chosen_bio = NULL;
		}
	}

	/* close down bitching */
	if (chosen_bitch_out != stdout) {
		bitch_init(NULL);
		fclose(chosen_bitch_out);
	}

	return 0;
}

