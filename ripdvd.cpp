
#include "ripdvd.h"
#include "main.h"
#include "rippedmap.h"
#include "todolist.h"
#include "keystore.h"
#include "lsimage.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

void ReadDVDCapacity(JarchSession *session)
{
	unsigned long x;
	int i;

	x = 0;
	for (i=0;i < 32;i++)
		x |= (session->todo->get(TODO_RIPDVD_CAPACITY_DWORD+i) << i);

	session->DVD_capacity = x;
}

void GetDVDCapacity(JarchSession *session)
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

	x = session->DVD_capacity = binBeHe32(buf);
	bitch(BITCHINFO,"Obtained DVD capacity as %u",x);

	for (i=0;i < 32;i++)
		session->todo->set(TODO_RIPDVD_CAPACITY_DWORD+i,(x >> i) & 1);

	session->todo->set(TODO_RIPDVD_READCAPACITY,1);
}

void RipDVDPeriodic(JarchSession *session,RippedMap *map,LargeSplitImage *dvd)
{
	unsigned long cur = 0,max = session->DVD_capacity;
	unsigned char *buffer = session->bdev->buffer();
	int rdmax = (!session->singlesector ? session->bdev->buffersize() : 1);
	int rd = 0,ard,j,refound=1;
	juint64 fofs;

	while (cur < max) {
		/* look for gaps */
		while (cur < max && map->get(cur)) cur++;

		if (cur < max) {
			rd = 1;
			while (rd < rdmax && (cur+rd) < max && !map->get(cur+rd)) rd++;

			printf("\x0D" "                         " "\x0D");
			printf("periodic gap @ %u size %u",cur,rd);
			fflush(stdout);

			fofs = (juint64)cur;
			fofs *= (juint64)2048;

			if (	session->bdev->seek(cur) && (ard = session->bdev->read(rd)) >= 1 &&
				dvd->seek(fofs) == fofs && dvd->write(buffer,ard*2048) == (2048*ard)) {
				for (j=0;j < ard;j++) map->set(cur+ard,1);

				if (refound) {
					printf("\x0D" "                                " "\x0D");
					bitch(BITCHINFO,"Beginning of valid data at %u",cur);
				}

				refound=0;
			}
			else {
				printf("\x0D" "                              " "\x0D");
				bitch(BITCHINFO,"Unable to get %u, oh well",cur);
				refound=1;
			}

			cur += session->rip_periodic;
		}
	}
}

void RipDVDExpandFill(JarchSession *session,RippedMap *map,LargeSplitImage *dvd)
{
	unsigned long cur = 0,max = session->DVD_capacity;
	unsigned char *buffer = session->bdev->buffer();
	int rdmax = (!session->singlesector ? session->bdev->buffersize() : 1);
	int rd = 0;

	bitch(BITCHINFO,"Using expansion fill rip method.");
	bitch(BITCHINFO,"Locates an \"island\" of ripped sectors surrounded by unripped sectors");
	bitch(BITCHINFO,"and expands the island by reading the unripped sectors at it's edges.");

	while (cur < max) {
		/* find the end of the current island */
		while (cur < max && map->get(cur)) cur++;

		/* find the start of an island */
		while (cur < max && !map->get(cur)) cur++;
		unsigned long start = cur;

		/* find the end of the island */
		while (cur < max && map->get(cur)) cur++;
		unsigned long send = cur;		/* "end" might be a reserved word, not var name? */

		/* exit out gracefully */
		if (start >= max && send >= max)
			break;
		
		/* insanity check */
		if (start > send)
			break;

		start--;
		/* so we have an island defined as start < x < send. sector "start" should be ripped. */
		/* sector "end" should NOT have been ripped (or send == max) */
		bitch(BITCHINFO,"Found sector island %u <= x <= %u",start+1,send-1);
		bitch_indent();

		juint64 fofs;
		int dostart,dosend,stopstart=0,stopsend=0,ard;
		while (	(!stopstart && (start+1) != 0 && ((dostart = (!map->get(start))) != 0)) ||
			(!stopsend && send < max && ((dosend = (!map->get(send))) != 0))) {

			printf("\x0D" "                                      " "\x0D");
			printf("%s %-8u",(!stopstart && dostart && (start+1) != 0) ? "<<" : "x ",start);
			printf(" %8u %s",send,(!stopsend && dosend && send < max) ? ">>" : "x ");
			fflush(stdout);

			/* if the start and send sector numbers are too far apart we will
			 * get slow rip speeds because the DVD-ROM optical pickup then has
			 * to seek back and forth at greater and greater distances. */
			int avoid_seekprobs = 0;
			int seektaken = 0;
			if (dostart && dosend && (start+1) != 0 && send < max) {
				unsigned long distance;

				if (start < send)	distance = send - start;
				else			distance = start - send;
				avoid_seekprobs = (distance > 96) ? 1 : 0;
			}
			
			if (dostart && (start+1) != 0 && !stopstart) {
				/* how far can we go? */
				for (rd=1;(start+1) >= rd && ((start+1)-rd) < max &&
					rd < rdmax && !map->get((start+1)-rd);) rd++;

				fofs = (juint64)((start+1)-rd);
				fofs *= (juint64)2048;
				if (	session->bdev->seek((start+1)-rd) && (ard = session->bdev->read(rd)) >= 1 &&
					dvd->seek(fofs) == fofs && dvd->write(buffer,2048*ard) == (2048*ard)) {
					while (ard-- > 0) map->set(start--,1);
					seektaken++;
				}
				else {
					/* stop expand filling, give up on this end */
					printf("\x0D" "                                     " "\x0D");
					fflush(stdout);
					bitch(BITCHINFO,"Can't rip this sector, stopping at low end %u",start+1);
					stopstart=1;
				}
			}

			if (dosend && send < max && !stopsend && (!avoid_seekprobs || seektaken == 0)) {
				/* how far can we go? */
				for (rd=1;(send+rd) < max && rd < rdmax && !map->get(send+rd);) rd++;

				fofs = (juint64)send;
				fofs *= (juint64)2048;
				if (	session->bdev->seek(send) && (ard = session->bdev->read(rd)) >= 1 &&
					dvd->seek(fofs) == fofs && dvd->write(buffer,2048*ard) == (2048*ard)) {
					while (ard-- > 0) map->set(send++,1);
				}
				else {
					/* stop expand filling, give up on this end */
					printf("\x0D" "                                     " "\x0D");
					fflush(stdout);
					bitch(BITCHINFO,"Can't rip this sector, stopping at high end %u attempting to read %u sectors",send-1,rd);
					stopsend=1;
				}
			}
		}

		printf("\x0D" "                                " "\x0D");
		bitch(BITCHINFO,"Completed expansion fill, new island %u <= x <= %u",start+1,send-1);
		bitch_unindent();
	}
}

void RipDVD(JarchSession *session)
{
	LargeSplitImage dvd;
	RippedMap dvdmap;
	juint64 byteo;
	unsigned long cur,full,rate,expsect,isect,pfull,lastend,adjsect,backwards_next;
	unsigned long last_recover,first_recover,leftover;
	unsigned char *buf;
	time_t start,expect,curt,prep;
	int rd,rdmax,percent,sz,got,i;
	double d1,d2;
	char adjrate,recover;

	if (session->todo->get(TODO_RIPDVD) && !session->chosen_force_rip)
		return;
	if (session->skip_rip)
		return;

	if (!session->todo->get(TODO_RIPDVD_READCAPACITY) || session->chosen_force_info)
		GetDVDCapacity(session);

	ReadDVDCapacity(session);
	bitch(BITCHINFO,"DVD capacity is officially %u",session->DVD_capacity);
	if (session->DVD_capacity == 0) {
		bitch(BITCHINFO,"I cannot rip nothing, skipping rip stage");
		return;
	}

	if (dvd.open("dvdrom-image") < 0) {
		bitch(BITCHERROR,"Cannot open Large Split Image dvdrom-image");
		return;
	}

	if (dvdmap.open("dvdrom-image.ripmap") < 0) {
		bitch(BITCHERROR,"Cannot open dvdrom-image ripped map");
		return;
	}

	cur = 0;
	full = session->DVD_capacity;

	if (session->rip_expandfill) {
		RipDVDExpandFill(session,&dvdmap,&dvd);
		return;
	}
	else if (session->rip_periodic > 0) {
		RipDVDPeriodic(session,&dvdmap,&dvd);
		return;
	}

	/* start off by skipping sectors we've already ripped */
	while (cur < full && dvdmap.get(cur)) cur++;

	/* if all have been ripped, signal that we are done */
	if (cur >= full) {
		session->todo->set(TODO_RIPDVD,1);
		return;
	}

	/* assuming a typical DVD-ROM drive expect initially a 1x
	 * ripping rate = 10600000 bits/sec = 1325000 bytes */
	if (session->rip_assume_rate > 0.0) {
		rate = (int)(1325000.00 * session->rip_assume_rate);
		adjrate = 0;
	}
	else {
		rate = 1325000;		// assume 1x
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
	rd = rdmax = session->bdev->buffersize();
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

	while (cur < full || session->rip_backwards) {
		if (session->singlesector)
			rd=1;

		if (session->rip_backwards) {
			unsigned long curend;

			/* scan backwards to look for an unripped slot */
			cur = backwards_next;
			while (cur != ((unsigned)-1) && dvdmap.get(cur)) cur--;
			curend = ++cur;

			if (cur == 0) {
				session->todo->set(TODO_RIPDVD,1);
				return;
			}

			/* scan backwards to see how big this gap is */
			sz = 1;
			while (sz < rd && (cur-sz) != 0 && !dvdmap.get(cur-sz)) sz++;
			cur -= sz;
		}
		else {
			/* look for an unripped slot */
			while (cur < full && dvdmap.get(cur)) cur++;
			if (cur >= full) {
				session->todo->set(TODO_RIPDVD,1);
				return;
			}

			/* assume at minimum one sector. how many more can we rip? */
			sz = 1;
			while (sz < rd && (cur+sz) < full && !dvdmap.get(cur+sz)) sz++;

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
			d2 = ((double)rate) / 1325000.00;
			d1 *= d2;
		}

		/* compute percent ratio */
		percent = (int)((((juint64)cur) * ((juint64)10000) / ((juint64)full)));

		/* print status once a second */
		if (session->rip_backwards)
			bitch(BITCHINFO,"Rip: @ %8u (%u sectors)",
				cur,sz);
		else if (prep != curt)
			bitch(BITCHINFO,"Rip: %%%3u.%02u @ %8u %.2fx, expected %8u %.1fx",
				percent/100,percent%100,
				cur,d1,expsect,d2);

		/* rip! */
		bitch_indent();

		if (!session->bdev->seek(cur)) {
			bitch(BITCHINFO,"Cannot seek to sector %u!",cur);
			got = 0;
			cur++;
		}
		else {
			got = session->bdev->read(sz);

			if (got <= 0) {
				bitch(BITCHINFO,"Failed to read sector %u!",cur);
				if (session->rip_backwards) {
					cur--;
					got=0;
				}
				else {
					cur++;
					rd=1;
				}
			}
			else if (got < sz) {
				/* bad sectors ahead, probably */
				rd=1;
			}

			if (got > 0) {
				juint64 ofs;
				int wsz = got * 2048;

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

				ofs = (juint64)cur;
				ofs *= (juint64)2048;
				if (dvd.seek(ofs) != ofs) {
					bitch(BITCHWARNING,"Problem seeking to offset " PRINT64F,ofs);
				}
				else if (dvd.write(buf,wsz) < wsz) {
					bitch(BITCHWARNING,"Problem writing data at offset " PRINT64F,ofs);
					cur++;
				}
				else {
					for (i=0;i < got;i++)
						dvdmap.set(cur+i,1);

					cur += got;
				}
			}
		}

		/* keep track of where we last ended */
		lastend = cur;

		if (!session->rip_backwards) {
			/* if we are far behind timewise, jump ahead */
			if (expsect >= 10000 && cur < (expsect-10000)) {
				if (d1 < (d2+0.25) && d2 > 1.0 && adjrate) {
					d2 -= 0.4;
					if (d2 < 1.0) d2 = 1.0;
					rate = (int)(d2 * 1325000.00);
				}
				else {
					if (!session->rip_noskip) {
						bitch(BITCHINFO,"Skipping ahead to keep pace");
						cur = expsect;
					}
					if (adjrate) {
						d2 -= 0.4;
						if (d2 < 1.0) d2 = 1.0;
						rate = (int)(d2 * 1325000.00);
					}
				}
			}
			/* if we are far ahead, assume that the drive is in fact faster */
			else if (cur > (expsect+10000) && d1 > (d2+0.2)) {
				if (adjrate) {
					d2 += 0.2;
					rate = (int)(d2 * 1325000.00);
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
}

void RipCMI(JarchSession *session)
{
	unsigned char cmd[12],buf[8];
	int perp;
	juint64 ju;
	time_t curt,prep,leftover;
	unsigned long cur,full,pfull;
	LargeSplitImage dvd;
	RippedMap dvdmap;

	if (session->todo->get(TODO_RIPCMI))
		return;

	if (!session->rip_cmi)
		return;

	ReadDVDCapacity(session);
	bitch(BITCHINFO,"DVD capacity is officially %u",session->DVD_capacity);

	full = session->DVD_capacity;
	if (full == 0) return;

	if (dvd.open("dvdrom-CopyrightManagementInfo") < 0) {
		bitch(BITCHERROR,"Cannot create file to hold Copyright Management Info");
		return;
	}

	if (dvdmap.open("dvdrom-CopyrightManagementInfo.ripmap") < 0) {
		bitch(BITCHERROR,"Cannot create rip map file");
		return;
	}

	pfull = dvdmap.getsize();
	cur = 0;
	while (cur < full && dvdmap.get(cur)) cur++;
	if (cur >= full) {
		session->todo->set(TODO_RIPCMI,1);
		return;
	}

	prep = 0;
	while (cur < full) {
		while (cur < full && dvdmap.get(cur)) cur++;
		if (cur >= full) {
			session->todo->set(TODO_RIPCMI,1);
			return;
		}

		curt = time(NULL);
		if (prep != curt) {
			perp = (int)(((juint64)cur) * ((juint64)10000) / ((juint64)full));
			bitch(BITCHINFO,"Rip CMI: %%%3u.%02u @ %u/%u",perp/100,perp%100,cur,full);
		}

		/* form the command */
		cmd[ 0] = 0xAD;
		cmd[ 1] = 0x00;
		cmd[ 2] = cur >> 24;
		cmd[ 3] = cur >> 16;
		cmd[ 4] = cur >> 8;
		cmd[ 5] = cur;
		cmd[ 6] = 0x00;
		cmd[ 7] = 0x05;
		cmd[ 8] = sizeof(buf) >> 8;
		cmd[ 9] = sizeof(buf);
		cmd[10] = 0x00;
		cmd[11] = 0x00;
		if (session->bdev->scsi(cmd,12,buf,sizeof(buf),1) < 0) {
			bitch(BITCHWARNING,"Unable to get Copyright Management Info for sector %u",cur);
			cur++;
		}
		else {
			ju = ((juint64)cur) * ((juint64)4);
			if (dvd.seek(ju) != ju) {
				bitch(BITCHERROR,"Unable to seek to " PRINT64F,ju);
				return;
			}
			if (dvd.write(buf+4,4) < 4) {
				bitch(BITCHERROR,"Unable to write data to " PRINT64F,ju);
				return;
			}

			dvdmap.set(cur,1);
			cur++;
		}

		prep = curt;
	}

	leftover = 0;
	for (cur=0;cur < full;cur++)
		if (!dvdmap.get(cur))
			leftover++;

	if (leftover > 0)
		bitch(BITCHINFO,"%u sectors have not had their CMI info ripped!",leftover);
	else
		session->todo->set(TODO_RIPCMI,1);
}

/* helper for GenPOI() -- The "dumb" generator.
 * the simplest to implement and use because it does not need to know
 * or scan the filesystem on the DVD-ROM. all it does is brute-force
 * all possible keys by listing every Nth sector. This may cause the
 * key store file to get unusually big, and it's possible that some
 * sectors will have different keys and these different sectors will
 * "fall through the cracks".
 *
 * unfortunately we cannot just go for (sector=0;sector < max;sector += step)
 * because the KeyStorage class doesn't know how to optimize it's
 * data storage and will result in a very inefficient binary search
 * tree. So, we work recursively. */

void printspc(int x)
{
	while (x-- > 0)
		printf(" ");
}

static int GenPOIi=0;
static void GenPOI_dumb_recurse(JarchSession *s,KeyStorage *k,unsigned long astep,
	unsigned long step,unsigned long min,unsigned long max,unsigned long full)
{
	unsigned char buf[8];
	unsigned long st,ns,nmin,nmax;

//	printspc(GenPOIi++);
//	printf("genPOI astep=%u step=%u min=%u max=%u\n",astep,step,min,max);

	if (step < astep)
		return;

	ns  = step >> 1;
	ns -= ns % astep;

	for (st=min;st <= max;st += step) {
//		printspc(GenPOIi);
//		printf("add %u\n",st);
		if (k->lookup(st,buf) == st) {
//			printspc(GenPOIi);
//			printf("BAH! %u\n",st);
		}
		else {
			memset(buf,0,8);
			if (k->addkey(st,buf) < 0) {
				bitch(BITCHERROR,"GenPOI_dumb fails to add a key");
//				GenPOIi--;
				return;
			}
		}

		if (ns != 0) {
			nmin = st + astep;
			nmax = st + step + -astep;
			if (nmax > full) nmax = full;
			GenPOI_dumb_recurse(s,k,astep,ns,nmin,nmax,full);
		}
	}

//	GenPOIi--;
}

void GenPOI_Dumb(JarchSession *session,ImageDVDCombo *idc)
{
	unsigned long cur,full,step,stud;

	if (session->todo->get(TODO_GENPOI_DUMB) && !session->force_genpoi)
		return;
	if (!session->poi_dumb)
		return;

	/* defaults */
	cur     = 0;
	full    = session->DVD_capacity;
	step    = 65536;
	bitch(BITCHINFO,"*** Dumb Points of Interest engine");
	bitch(BITCHINFO,"Starting at %u, every %u sector",cur,step);

	GenPOIi = 0;
	stud    = full / 2;
	stud   -= stud % step;
	full   -= full % step;
	GenPOI_dumb_recurse(session,idc->poi,step,stud,0,full,full);
	session->todo->set(TODO_GENPOI_DUMB,1);
}

#define SECTOFS(x)	( ((juint64)(x)) * ((juint64)2048) )

int ImageDVDCombo::dvdread(unsigned long sector,unsigned char *buf,int N)
{
	int x;

	if (!map->get(sector))
		return 0;
	if (dvd->seek(SECTOFS(sector)) != SECTOFS(sector))
		return 0;

	for (x=1;x < N && map->get(sector+x);) x++;

	N = dvd->read(buf,x * 2048) >> 11;
	if (N < 0) N = 0;
	return N;
}

/* assuming a DVD video disc, detect the UDF filesystem, look
 * for /VIDEO_TS/VIDEO_TS.IFO and use the info there to find
 * the starting point for each DVD titleset. */
void GenPOI_DVDVideo(JarchSession *session,ImageDVDCombo *idc)
{
	KeyStorage key;
	LargeSplitImage dvd;
	RippedMap dvdmap;

	if (session->todo->get(TODO_GENPOI_DVDVIDEO) && !session->force_genpoi)
		return;

	bitch(BITCHINFO,"*** DVD video VIDEO_TS POI engine");
	bitch_indent();

	if (GenPOI_DVDVideo_helper(session,idc))
		session->todo->set(TODO_GENPOI_DVDVIDEO,1);

	bitch_unindent();
}

/* generate a binary tree with "points of interest" that
 * the ripper will then check for and acquire title keys from */
void GenPOI(JarchSession *session)
{
	ImageDVDCombo idc;
	KeyStorage poi;
	LargeSplitImage dvd;
	RippedMap map;

	if (session->todo->get(TODO_GENPOI) && !session->force_genpoi)
		return;

	ReadDVDCapacity(session);
	if (session->DVD_capacity == 0)
		return;

	if (poi.open("JarchDVD-POI.keystore") < 0)
		return;
	if (dvd.open("dvdrom-image") < 0)
		return;
	if (map.open("dvdrom-image.ripmap") < 0)
		return;

	idc.map = &map;
	idc.dvd = &dvd;
	idc.poi = &poi;

	GenPOI_Dumb(session,&idc);
	GenPOI_DVDVideo(session,&idc);

	if (	session->todo->get(TODO_GENPOI_DUMB) &&
		session->todo->get(TODO_GENPOI_DVDVIDEO)) {
		session->todo->set(TODO_GENPOI,1);
	}
}

static time_t ap_prep;
static unsigned long ap_full;
static KeyStorage *ap_poi,*ap_title;
static JarchSession *ap_ses;
static int AuthPOIBS_title=0,AuthPOIBS_poi=0,ap_blocks;
int AuthPOIBS(unsigned long s,unsigned char *k,unsigned long N)
{
	time_t curt;
	KeyStorage::Node node;
	unsigned char buf[8];
	int x;

	// all bytes set to 0x4C means this entry has been check already, do not check again
	if (	k[0] == 0x4C && k[1] == 0x4C && k[2] == 0x4C && k[3] == 0x4C &&
		k[4] == 0x4C && k[5] == 0x4C && k[6] == 0x4C && k[7] == 0x4C)
		return 0;

	// mark that we've already been here
	if (ap_poi->readkey(N,&node) < 0) return 0;
	memset(node.key,0x4C,8);
	if (ap_poi->writekey(N,&node) < 0) return 0;

	// give the user some indiciation that we're working
	curt = time(NULL);
	if (ap_prep != curt) {
		int percent = (((juint64)s) * ((juint64)100) / ((juint64)ap_full));
		if (percent > 100) percent = 100;
		bitch(BITCHINFO,"%%%3u @ %u",percent,s);
		ap_prep = curt;
	}

	// look up this sector in the title tree
	x = ap_title->lookup(s,buf);
	if (x == s)				/* entry for this sector already exists */
		return 0;

	AuthPOIBS_poi++;
	if (ap_ses->dvdauth->Authenticate(1,s) > 0) {
		/* if the key is already there for some sector prior to us, don't add it */
		/* NOTE: Do not include the CPM/CP_SEC/CGMS/CP_MOD byte in the comparison!
		 *       If the archivist intends to preserve that byte he will run JarchDVD
		 *       and instruct it to collect the Copyright Management Information
		 *       (same byte, stored in different array) */
		if (x >= 0 && !memcmp(buf+1,ap_ses->dvdauth->RawTitleKey+1,7))
			return 0;

		/* otherwise add it */
		bitch(BITCHINFO,"Found new title key %02X %02X %02X %02X %02X @ %u",
			ap_ses->dvdauth->TitleKey[0],
			ap_ses->dvdauth->TitleKey[1],
			ap_ses->dvdauth->TitleKey[2],
			ap_ses->dvdauth->TitleKey[3],
			ap_ses->dvdauth->TitleKey[4],
			s);

		if (ap_title->addkey(s,ap_ses->dvdauth->RawTitleKey) < 0)
			bitch(BITCHERROR,"Error adding sector %u",s);
		else
			AuthPOIBS_title++;
	}

	return 0;
}

void AuthPOI(JarchSession *session)
{
	KeyStorage poi,title;

	if (session->todo->get(TODO_AUTHPOI) && !session->force_authpoi)
		return;

	if (poi.open("JarchDVD-POI.keystore") < 0) {
		bitch(BITCHINFO,"Unable to open POI keystore");
		return;
	}

	if (title.open("JarchDVD-Title.keystore") < 0) {
		bitch(BITCHINFO,"Unable to open title keystore");
		return;
	}

	ReadDVDCapacity(session);

	bitch(BITCHINFO,"Gathering DVD title keys");
	bitch_indent();

	AuthPOIBS_title = 0;
	AuthPOIBS_poi = 0;

	ap_poi = &poi;
	ap_ses = session;
	ap_prep = 0;
	ap_full = session->DVD_capacity;
	ap_blocks = poi.max_block;
	ap_title = &title;
	poi.enumtree(AuthPOIBS);

	if (AuthPOIBS_title == AuthPOIBS_poi)
		session->todo->set(TODO_AUTHPOI,1);

	bitch_unindent();
}

typedef struct {
	unsigned long	sector;
	unsigned char	key[8];
} DecryptKeyEnt;

static DecryptKeyEnt *ddv_keys;
static int ddv_keys_len;
static int ddv_keys_next;
int DecryptDVDenum(unsigned long s,unsigned char *k,unsigned long N)
{
	if (ddv_keys_next >= ddv_keys_len)
		bitch(BITCHERROR,"Program error: more title keys than anticipated!");

	ddv_keys[ddv_keys_next].sector = s;
	memcpy(ddv_keys[ddv_keys_next].key,k,8);
	ddv_keys_next++;
	return 0;
}

int DVDVideoDecrypt(unsigned long sector,unsigned char *data,int N,unsigned char *keydata)
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

void DecryptDVD(JarchSession *session)
{
	unsigned char *buf,*key;
	KeyStorage title;
	juint64 ofs;
	RippedMap dvdmap;
	LargeSplitImage dvd;
	unsigned long cur,full;
	int sz,rdmax,got;
	int key_cur,decrypted=0,dss;
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

	ReadDVDCapacity(session);
	full = session->DVD_capacity;
	if (full == 0) return;

	/* enumerate all keys in storage */
	ddv_keys_next= 0;
	ddv_keys_len = title.max_block;
	ddv_keys = new DecryptKeyEnt[ddv_keys_len];
	if (!ddv_keys) return;
	title.enumtree(DecryptDVDenum);
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
	while (cur < full) {
		curt = time(NULL);

		/* look for a ripped sector */
		while (cur < full && !dvdmap.get(cur)) cur++;
		if (cur >= full) break;

		/* match sector to key */
		while (key_cur < (ddv_keys_next-1) && (cur >= ddv_keys[key_cur+1].sector)) {
			key_cur++;
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
			cur++;
			continue;
		}

		/* seek */
		ofs = ((juint64)cur) * ((juint64)2048);
		if (dvd.seek(ofs) != ofs) break;

		/* read */
		got = dvd.read(buf,sz * 2048) >> 11;
		if (got == 0) {
			cur++;
			continue;
		}

		/* ensure that it's MPEG-2 PES and decrypt */
		if ((dss=DVDVideoDecrypt(cur,buf,got,key)) > 0) {
			if (dvd.seek(ofs) != ofs) break;
			if (dvd.write(buf,got * 2048) < (got * 2048)) break;
		}
		decrypted += dss;

		/* updates */
		if (prep != curt) {
			bitch(BITCHINFO,"@ %u, having decrypted %u sectors so far",cur,decrypted);
			prep = curt;
		}

		/* next? */
		cur += got;
	}

	delete buf;
	delete ddv_keys;
	bitch_unindent();
}

