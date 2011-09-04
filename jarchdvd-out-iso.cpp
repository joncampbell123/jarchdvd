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

static int			iso_fd = -1;
static FILE*			iso_keyfile = NULL;
static int			iso_dont_decrypt = 0;
static FILE*			chosen_bitch_out;
static char			chosen_bitch_out_fname[256];
static char			chosen_input_block_dev[256];
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
				}
				else if (!strncmp(p,"ripmap",6)) {
				}
				else {
					bitch(BITCHERROR,"Unknown test mode");
					return 0;
				}
			}
			else if (!strncmp(p,"keep-css",8)) {
				iso_dont_decrypt = 1;
				fprintf(stderr,"Keeping CSS encryption\n");
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

				bitch(BITCHINFO,"-keep-css                   Keep CSS encryption intact, write CSS keys out to keyfile");
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

void RipDVDStatus(JarchSession *session)
{
	int leftover,ripped,i;
	RippedMap dvdmap;

	if (dvdmap.open("dvdrom-image.ripmap") < 0) {
		bitch(BITCHINFO,"Ripping has not commenced yet");
		return;
	}

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

/*-----------------warning modified copypasta from ripdvd.cpp--------------------*/

typedef struct {
	unsigned long	sector;
	unsigned char	key[8];
} ISODecryptKeyEnt;

static ISODecryptKeyEnt *ddv_keys;
static int ddv_keys_len;
static int ddv_keys_next;
int ISODecryptDVDenum(unsigned long s,unsigned char *k,unsigned long N)
{
	if (ddv_keys_next >= ddv_keys_len)
		bitch(BITCHERROR,"Program error: more title keys than anticipated!");

	ddv_keys[ddv_keys_next].sector = s;
	memcpy(ddv_keys[ddv_keys_next].key,k,8);
	ddv_keys_next++;
	return 0;
}

int ISODVDVideoDecrypt(unsigned long sector,unsigned char *data,int N,unsigned char *keydata)
{
	unsigned char *key = keydata + 1;
	int x,d=0;

	if (key[0] == 0x00 && key[1] == 0x00 && key[2] == 0x00 && key[3] == 0x00 && key[4] == 0x00)
		return 0;

	for (x=0;x < N;x++,data += 2048) {
		if (data[0] == 0 && data[1] == 0 && data[2] == 1 && data[3] == 0xBA) {
			if (data[0x14] & 0x30) {	// encryption bits set in PES packet
				CSS_unscramble(data,key);
				data[0x14] &= ~0x30;	// clear encryption bits
				d++;
			}
		}
	}

	return d;
}

void ISODecryptDVD(JarchSession *session)
{
	unsigned char sector[2048];
	unsigned char *buf,*key;
	KeyStorage title;
	juint64 ofs;
	RippedMap dvdmap;
	LargeSplitImage dvd;
	unsigned long long total=0;
	unsigned long cur,full;
	int sz,rdmax,got;
	int key_cur,decrypted=0,dss=0;
	time_t curt,prep;

	if (title.open("JarchDVD-Title.keystore") < 0) {
		bitch(BITCHINFO,"Unable to open title keystore");
		return;
	}

	if (dvdmap.open("dvdrom-image.ripmap") < 0) {
		bitch(BITCHINFO,"Unable to open DVD ripmap");
		return;
	}

	if (dvd.open("dvdrom-image") < 0) {
		bitch(BITCHINFO,"Unable to open DVD rip image");
		return;
	}

	full = session->DVD_capacity;
	if (full == 0) return;

	/* enumerate all keys in storage */
	ddv_keys_next= 0;
	ddv_keys_len = title.max_block;
	ddv_keys = new ISODecryptKeyEnt[ddv_keys_len];
	if (!ddv_keys) return;
	title.enumtree(ISODecryptDVDenum);
	bitch(BITCHINFO,"%u keys (%u expected) gathered",ddv_keys_next,ddv_keys_len);

	if (ddv_keys_next < 1) {
		bitch(BITCHINFO,"No keys gathered, not decrypting");
		return;
	}

	rdmax = 32;	/* decrypt 32 sectors at a time max */
	buf = new unsigned char[rdmax * 2048];
	if (!buf) {
		delete ddv_keys;
		return;
	}

	bitch(BITCHINFO,"Decrypting CSS protected sectors in image using harvested title keys");
	bitch_indent();

	cur = 0;
	prep = 0;
	key_cur = 0;
	key = ddv_keys[key_cur].key;
	if (iso_keyfile) {
		fprintf(stderr,"Key Sector=%lu Key=%02x-%02x-%02x-%02x-%02x-%02x\n",
			ddv_keys[key_cur].sector,
			ddv_keys[key_cur].key[1],
			ddv_keys[key_cur].key[2],
			ddv_keys[key_cur].key[3],
			ddv_keys[key_cur].key[4],
			ddv_keys[key_cur].key[5]);
	}
	while (cur < full) {
		curt = time(NULL);

		if (!dvdmap.get(cur)) {
			bitch(BITCHWARNING,"  sector %u is missing\n",cur);
			memset(sector,0,2048);
			write(1/*stdout*/,sector,2048);
			cur++;
			continue;
		}

		/* match sector to key */
		while (key_cur < (ddv_keys_next-1) && (cur >= ddv_keys[key_cur+1].sector)) {
			key_cur++;

			if (iso_keyfile) {
				fprintf(stderr,"Key Sector=%lu Key=%02x-%02x-%02x-%02x-%02x-%02x\n",
					ddv_keys[key_cur].sector,
					ddv_keys[key_cur].key[1],
					ddv_keys[key_cur].key[2],
					ddv_keys[key_cur].key[3],
					ddv_keys[key_cur].key[4],
					ddv_keys[key_cur].key[5]);
			}

			bitch(BITCHINFO,"key %u sector %u: using key %02X %02X %02X %02X %02X found at %u",
				key_cur,
				cur,
				ddv_keys[key_cur].key[1],
				ddv_keys[key_cur].key[2],
				ddv_keys[key_cur].key[3],
				ddv_keys[key_cur].key[4],
				ddv_keys[key_cur].key[5],
				ddv_keys[key_cur].sector);

			key = ddv_keys[key_cur].key;
		}

		/* how many more can we do in one go? */
		for (sz=1;sz < rdmax && (cur+sz) < full && dvdmap.get(cur+sz);) sz++;

		/* how many sectors before coming across a different key? */
		if (key_cur < (ddv_keys_next-1)) {
			int xxx = ddv_keys[key_cur+1].sector - cur;
			if (xxx < 0) xxx = 0;
			if (sz > xxx) sz = xxx;
		}

		if (sz < 1) {
			fprintf(stderr,"WARNING: Unusual situation with 0 sectors to next key sector %u\n",cur);
			abort();
		}

		/* seek */
		ofs = ((juint64)cur) * ((juint64)2048);
		if (dvd.seek(ofs) != ofs) break;

		/* read */
		got = dvd.read(buf,sz * 2048) >> 11;
		if (got == 0) {
			fprintf(stderr,"WARNING: Cannot read sector %u\n",cur);
			abort();
		}

		/* ensure that it's MPEG-2 PES and decrypt */
		if (!iso_dont_decrypt) dss = ISODVDVideoDecrypt(cur,buf,got,key);
		decrypted += dss;

		/* write to stdout */
		if (write(iso_fd,buf,got*2048) < (got*2048)) {
			fprintf(stderr,"Problem writing to ISO\n");
			abort();
		}

		/* updates */
		if (prep != curt) {
			bitch(BITCHINFO,"@ %u, having decrypted %u sectors so far",cur,decrypted);
			prep = curt;
		}

		/* next? */
		cur += got;

		/* make SURE the file offsets are in sync */
		if (lseek(iso_fd,0,SEEK_CUR) != ((unsigned long long)cur * 2048LL)) {
			fprintf(stderr,"ISO writing fell out of sync\n");
			abort();
		}
	}

	delete buf;
	delete ddv_keys;
	bitch_unindent();
}

/*-------------------------------------------------------------------------------*/

int main(int argc,char **argv)
{
	JarchSession session;
	RippedMap* todo;
	int i;

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

	/* if stdout is a terminal, redirect to jarchdvd-output.iso */
	iso_fd = open("jarchdvd-output.iso",O_CREAT|O_TRUNC|O_WRONLY,0644);
	if (iso_fd < 0) {
		fprintf(stderr,"Cannot open jarchdvd-output.iso\n");
		return 1;
	}

	if (iso_dont_decrypt) {
		if ((iso_keyfile = fopen("jarchdvd-output.iso.css-keys","w")) == NULL) {
			fprintf(stderr,"Cannot open keyfile\n");
			return 1;
		}
		setbuf(iso_keyfile,NULL);
	}

	// ...and the name of the current path
	bitch_cwd();

	// are we doing a test mode?
	{
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

		// setup session object
		session.dvdauth = NULL;
		session.todo = todo;
		session.bdev = NULL;
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

		/* do css decryption and write ISO to stdout */
		ISODecryptDVD(&session);

		// show TODO list
		bitchTODO(&session);

		// close TODO list
		todo->close();
		delete todo;
	}

	/* close down bitching */
	if (chosen_bitch_out != stdout) {
		bitch_init(NULL);
		fclose(chosen_bitch_out);
	}

	if (iso_keyfile) fclose(iso_keyfile);
	close(iso_fd);
	return 0;
}

