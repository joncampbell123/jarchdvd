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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "config.h"
#include "blockio.h"
#include "bitchin.h"
#include "keystore.h"
#include "rippedmap.h"
#include "todolist.h"
#include "mediagather.h"
#include "ripdvd.h"
#include "dvd-auth.h"

#include <string>

using namespace std;

static FILE*			chosen_bitch_out;
static char			chosen_bitch_out_fname[256];
static char			chosen_input_block_dev[256];
static JarchBlockIO*		chosen_bio=NULL;
static int			chosen_test_mode=0;
static int			chosen_force_info=0;
static int			chosen_force_rip=0;
static int			rip_periodic=0;
static int			rip_noskip=0;
static double			rip_assume_rate=0.0;
static int			rip_expandfill=0;
static int			rip_backwards_from_outermost=0;
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
// test modes are:
//   0 = no test
//   1 = KeyStorage test
//   2 = RippedMap test

void DoTestMode()
{
	switch (chosen_test_mode) {
		case 1:	DoTestKeyStore();		break;
		case 2:	DoTestRippedMap();		break;

		default:
			bitch(BITCHERROR,"Program error: Unknown test mode %u",chosen_test_mode);
	}
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
			else {
				bitch(BITCHERROR,"Unknown command line argument %s",argv[i]);
				bitch(BITCHINFO,"Valid switches are:");
#ifdef WIN32
				bitch(BITCHINFO,"-dev <device>               Specify which device to use (e.g. \\\\.\\D:), default: First DVD-ROM drive");
#else
				bitch(BITCHINFO,"-dev <device>               Specify which device to use, default: /dev/dvd");
#endif
				bitch(BITCHINFO,"-driver <name>              Use a specific driver for ripping");
				bitch(BITCHINFO,"    Linux:");
				bitch(BITCHINFO,"         linux_sg           SG ioctl method");
				bitch(BITCHINFO,"         linux_packet       CDROM packet ioctl method");
				bitch(BITCHINFO,"-bout <file>                Log output to <file>, append if exists");
				bitch(BITCHINFO,"-test-keystore              DIAGNOSTIC: Test KeyStorage code");
				bitch(BITCHINFO,"-info                       Gather media info, even if already done");
				bitch(BITCHINFO,"-rip                        Rip DVD contents, even if previously ripped");
				bitch(BITCHINFO,"-noskip                     Don't skip sectors in DVD ripping");
				bitch(BITCHINFO,"-rate <N>                   Assume rip rate at Nx (N times 1x DVD rate)");
				bitch(BITCHINFO,"-cmi                        Also copy Copyright Management Info (slow!)");
				bitch(BITCHINFO,"-poi                        Generate points of interest list even if already done");
				bitch(BITCHINFO,"-authpoi                    Obtain title keys for POI even if done");
				bitch(BITCHINFO,"-norip                      Skip DVD ripping stage");
				bitch(BITCHINFO,"-decss                      Use keys from key store to perform DVD");
				bitch(BITCHINFO,"                            decryption in place (overwriting original)");
				bitch(BITCHINFO,"-todo                       Don't do anything, just show the TODO list");
				bitch(BITCHINFO,"-dumb-poi                   Use Dumb POI generator");
				bitch(BITCHINFO,"-backwards                  Rip backwards");
				bitch(BITCHINFO,"-from-outermost             When used with -backwards, rip from edge of disc");
				bitch(BITCHINFO,"-expandfill                 Use expanding fill rip method");
				bitch(BITCHINFO,"-periodic <n>               Rip only every nth sector");
				bitch(BITCHINFO,"-single                     Rip single sectors only");
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

void bitchTODO(JarchSession *session)
{
	const char *YN[] = {"  .  ","--X--"};
	RippedMap* r = session->todo;

	bitch(BITCHINFO,"JarchDVD's TODO list thus far:");
	bitch_indent();

	bitch(BITCHINFO,"Gather DVD media information           %s",YN[r->get(TODO_MAIN_GATHER_MEDIA_INFO)	]);
	bitch(BITCHINFO,"  DVD disc info                        %s",YN[r->get(TODO_MEDIAGATHER_DISC_INFO)	]);
	bitch(BITCHINFO,"  DVD format info 0x00                 %s",YN[r->get(TODO_MEDIAGATHER_DVD_STRUCT_00)	]);
	bitch(BITCHINFO,"  DVD format info 0x01                 %s",YN[r->get(TODO_MEDIAGATHER_DVD_STRUCT_01)	]);
	bitch(BITCHINFO,"  DVD format info 0x03                 %s",YN[r->get(TODO_MEDIAGATHER_DVD_STRUCT_03)	]);
	bitch(BITCHINFO,"  DVD format info 0x04                 %s",YN[r->get(TODO_MEDIAGATHER_DVD_STRUCT_04)	]);
	bitch(BITCHINFO,"  DVD format info 0x08                 %s",YN[r->get(TODO_MEDIAGATHER_DVD_STRUCT_08)	]);
	bitch(BITCHINFO,"  DVD format info 0x09                 %s",YN[r->get(TODO_MEDIAGATHER_DVD_STRUCT_09)	]);
	bitch(BITCHINFO,"  DVD format info 0x0A                 %s",YN[r->get(TODO_MEDIAGATHER_DVD_STRUCT_0A)	]);
	bitch(BITCHINFO,"  DVD layer count and data             %s",YN[r->get(TODO_MEDIAGATHER_DVD_LAYERS)	]);
	bitch(BITCHINFO,"DVD ripping                            %s",YN[r->get(TODO_RIPDVD)			]);
	bitch(BITCHINFO,"  Capacity determination               %s",YN[r->get(TODO_RIPDVD_READCAPACITY)		]);
	bitch(BITCHINFO,"Ripping of Copyright Mgmnt Info        %s",YN[r->get(TODO_RIPCMI)			]);
	bitch(BITCHINFO,"Points of Interest Generation          %s",YN[r->get(TODO_GENPOI)			]);
	bitch(BITCHINFO,"  Dumb Points of Interest              %s",YN[r->get(TODO_GENPOI_DUMB)			]);
	bitch(BITCHINFO,"  DVD Video VIDEO_TS.IFO POI           %s",YN[r->get(TODO_GENPOI_DVDVIDEO)		]);
	bitch(BITCHINFO,"Gathering of keys from POI             %s",YN[r->get(TODO_AUTHPOI)			]);

	bitch_unindent();
}

void ReadDVDCapacity(JarchSession *session);
void RipDVDStatus(JarchSession *session)
{
	int leftover,ripped,i;
	RippedMap dvdmap;

	if (dvdmap.open("dvdrom-image.ripmap") < 0) {
		bitch(BITCHINFO,"Ripping has not commenced yet");
		return;
	}

	ReadDVDCapacity(session);
	if (session->DVD_capacity == 0) {
		bitch(BITCHINFO,"Ripping has not determined capacity yet");
		return;
	}

	ripped = 0;
	leftover = 0;
	for (i=0;i < session->DVD_capacity;i++)
		if (!dvdmap.get(i))
			leftover++;
		else
			ripped++;

	bitch(BITCHINFO,"%u sectors out of %u have NOT been ripped",leftover,session->DVD_capacity);
	bitch(BITCHINFO,"%u sectors out of %u have been ripped",ripped,session->DVD_capacity);
}

int main(int argc,char **argv)
{
	JarchSession session;
	RippedMap* todo;
	DVDAuth* dvd=NULL;
	int i;

#ifdef WIN32
	{
		// large SCSI xfers really slow NT down!
		// set our own task priority to low!
		SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);
	}
#endif

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
	bitch(BITCHINFO,"| JarchDVD DVD-ROM archiving utility v2.1 New and Improved |");
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
		if (todo->open("JarchDVD2-TODO.list") < 0) {
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

			// allocate DVD authentication module
			dvd = new DVDAuth(chosen_bio);
			if (!dvd) {
				bitch(BITCHERROR,"Unable to allocate DVD authentication module");
				delete chosen_bio;
				return 1;
			}

			// authenticate the DVD-ROM drive to get a bus key.
			// NOTE: Does not unlock protected sectors at this point!
			bitch(BITCHINFO,"Obtaining bus key for exchange of disc/title keys");
			bitch_indent();
			if (!dvd->Authenticate(-1,0)) bitch(BITCHWARNING,"Unable to obtain bus key");
			bitch_unindent();
		}

		// setup session object
		session.dvdauth = dvd;
		session.todo = todo;
		session.bdev = chosen_bio;
		session.chosen_force_info = chosen_force_info;
		session.chosen_force_rip = chosen_force_rip;
		session.rip_noskip = rip_noskip;
		session.rip_assume_rate = rip_assume_rate;
		session.rip_backwards_from_outermost = rip_backwards_from_outermost;
		session.rip_expandfill = rip_expandfill;
		session.rip_periodic = rip_periodic;
		session.rip_cmi = rip_cmi;
		session.force_genpoi = force_genpoi;
		session.force_authpoi = force_authpoi;
		session.skip_rip = skip_rip;
		session.css_decrypt_inplace = css_decrypt_inplace;
		session.poi_dumb = poi_dumb;
		session.rip_backwards = rip_backwards;
		session.singlesector = singlesector;

		if (!css_decrypt_inplace && !show_todo) {
			// Use various MMC commands to examine the DVD-ROM media!
			GatherMediaInfo(&session);

			if (rip_assume > 0) {
				session.DVD_capacity = rip_assume;

				for (i=0;i < 32;i++)
					session.todo->set(TODO_RIPDVD_CAPACITY_DWORD+i,(session.DVD_capacity >> i) & 1);

				session.todo->set(TODO_RIPDVD_READCAPACITY,1);
			}

			// get a disc key for the DVD-ROM drive. this retrieves the disc key
			// and unlocks "protected" sectors so we can rip them (as-is, encrypted).
			bitch(BITCHINFO,"Obtaining disc key");
			bitch_indent();
			if (!dvd->Authenticate(0,0)) bitch(BITCHWARNING,"Unable to obtain disc key");
			bitch_unindent();

			// Rip DVD!
			RipDVD(&session);

			// Use DVD image to generate a binary tree containing starting sector
			// numbers of "points of interest" where CSS authentication should be
			// attempted in order to build the key store for this DVD. For example,
			// once the DVD has been ripped significantly, scan for and look up
			// the VIDEO_TS directory then list the starting sector for each VOB
			// file in the tree (because ideally you authenticate at the start of
			// each DVD video titleset)
			GenPOI(&session);

			// Scan "points of interest" binary tree and authenticate each sector
			// listed in that tree to obtain the CSS title decryption keys, then
			// store them in the key storage tree.
			AuthPOI(&session);
		}

		// if told to, perform CSS decryption in place with ripped image now
		if (css_decrypt_inplace && !show_todo)
			DecryptDVD(&session);

		if (!css_decrypt_inplace && !show_todo) {
			// Copy down "Copyright Management Information" for each sector.
			RipCMI(&session);
		}

		// show TODO list
		bitchTODO(&session);

		// show status info
		RipDVDStatus(&session);

		// close TODO list
		todo->close();
		delete todo;

		// close DVD authentication module
		if (dvd) {
			delete dvd;
			dvd = NULL;
		}

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

