
#include "config.h"
#include "ripdvd.h"
#include "udf.h"
#include <string.h>

static JarchSession *_session;
static ImageDVDCombo *_idc;
static void mark(unsigned long sector)
{
	unsigned char key[8];

	if (_idc->poi->lookup(sector,key) == sector)
		return;

	memset(key,0,8);
	_idc->poi->addkey(sector,key);
}

void LookAtVTS(JarchSession *session,ImageDVDCombo *idc,unsigned long title_ofs)
{
	unsigned char buffer[2048];
	unsigned long titlelast;
	unsigned long titlevob;
	unsigned long ifolast;
	unsigned long menuvob;

	if (!idc->dvdread(title_ofs,buffer,1))
		return;

	if (memcmp(buffer,"DVDVIDEO-VTS",12)) {
		bitch(BITCHWARNING,"Uh, this does not look like a DVD titleset IFO");
		return;
	}

	titlelast =	binBeHe32(buffer + 0x00C) + 1 + title_ofs;
	ifolast =	binBeHe32(buffer + 0x01C) + 1 + title_ofs;
	menuvob =	binBeHe32(buffer + 0x0C0) + title_ofs;
	titlevob =	binBeHe32(buffer + 0x0C4) + title_ofs;

	bitch(BITCHINFO,"Menu VOB is at %u",menuvob);
	bitch(BITCHINFO,"Title VOB is at %u",titlevob);

	// mark as POI the starting sector of the title, the starting sector of the menu,
	// and the sectors following the last IFO & title sectors
	mark(titlelast);
	mark(titlevob);
	mark(ifolast);
	mark(menuvob);
}

int GenPOI_DVDVideo_helper(JarchSession *session,ImageDVDCombo *idc)
{
	UDFfs udf(idc);
	UDFfs::dir *root,*VIDEO_TS,*IFO;
	unsigned char buffer[2048];
	unsigned char tot[2048];
	unsigned char *tent;
	unsigned long tt_srpt,menu_vob,title_ofs,eovmg;
	unsigned long so,sz,ifolast;
	int titles,t;

	_session = session;
	_idc = idc;

	bitch(BITCHINFO,"Calling UDF reader to mount image");
	bitch_indent();

	if (!udf.mount()) {
		bitch(BITCHINFO,"failed");
		bitch_unindent();
		return 0;
	}

	root = udf.root();
	if (!root) {
		bitch(BITCHINFO,"Cannot obtain root directory");
		bitch_unindent();
		delete root;
		return 0;
	}

	if (root->find("VIDEO_TS",1) < 0) {
		bitch(BITCHINFO,"Unable to find VIDEO_TS directory");
		bitch_unindent();
		delete root;
		return 0;
	}

	VIDEO_TS = root->get(root->find_dirent);
	if (!VIDEO_TS) {
		bitch(BITCHINFO,"Unable to get VIDEO_TS directory");
		bitch_unindent();
		delete root;
		return 0;
	}

	if (VIDEO_TS->find("VIDEO_TS.IFO",0) < 0) {
		bitch(BITCHINFO,"Unable to find VIDEO_TS.IFO");
		bitch_unindent();
		delete VIDEO_TS;
		delete root;
		return 0;
	}

	IFO = VIDEO_TS->get(VIDEO_TS->find_dirent);
	if (!IFO) {
		bitch(BITCHINFO,"Unable to get VIDEO_TS.IFO");
		bitch_unindent();
		delete VIDEO_TS;
		delete root;
		return 0;
	}

	bitch(BITCHINFO,"Found /VIDEO_TS/VIDEO_TS.IFO @ %u (abs %u)",IFO->dirext.location,
		IFO->dirext.location + udf.udf_nsr_partition_start);
	bitch_indent();

	// OK then, read the first sector of /VIDEO_TS/VIDEO_TS.IFO
	so = IFO->dirext.location + udf.udf_nsr_partition_start;
	sz = (IFO->dirext.len + 2047) >> 11;

	// mark the start+end as points of interest
	mark(so);
	mark(so+sz);

	// read 1st sector
	if (!idc->dvdread(so,buffer,1))
		goto finish;

	// make sure it's a VMG IFO not a VTS IFO
	if (memcmp(buffer,"DVDVIDEO-VMG",12)) {
		bitch(BITCHWARNING,"Uh, this doesn't look like a DVD VMG-IFO file");
		goto finish;
	}

	// get last sector of IFO
	ifolast = binBeHe32(buffer + 0x01C) + 1;
	if (ifolast != sz) {
		bitch(BITCHWARNING,"IFO file indiciates size that is contrary to what UDF filesystem specifies");
		bitch(BITCHWARNING,"  IFO:%u != UDFfs:%u",ifolast,sz);
	}
	mark(so+ifolast);

	// get last sector of BUP
	eovmg = binBeHe32(buffer + 0x00C) + 1;
	mark(so+eovmg);

	// I work best with v1.1 or earlier IFO files
	if (buffer[0x21] > 0x11) {
		bitch(BITCHWARNING,"IFO file gives version %u.%u which is beyond what I am built to handle",
			buffer[0x21] >> 4,
			buffer[0x21] & 0xF);
	}

	// get offset to main menu VOB
	menu_vob = binBeHe32(buffer + 0x0C0);
	if (menu_vob != 0) {
		menu_vob += so;
		bitch(BITCHINFO,"Got VMG menu @ %u",menu_vob);

		// add to POI (if not already)
		mark(menu_vob);
	}

	// get offset to "table of titles" so we can locate
	// the titlesets and the corresponding title VOBs
	tt_srpt = binBeHe32(buffer + 0x0C4);
	if (tt_srpt == 0) {
		bitch(BITCHWARNING,"Bizarre, this IFO indicates that there are no titles on this disc");
		goto finish;
	}
	tt_srpt += so;
	bitch(BITCHINFO,"Got Table of Titles @ %u",tt_srpt);
	if (tt_srpt < so || tt_srpt >= (so+ifolast)) {
		bitch(BITCHWARNING,"Table of Titles offset out of range of IFO file");
		goto finish;
	}

	// read Table of Titles
	if (!idc->dvdread(tt_srpt,tot,1))
		goto finish;

	titles = binBeHe16(tot + 0x000);
	bitch(BITCHINFO,"There are %u titles on this disc",titles);
	for (t=0;t < titles;t++) {
		tent = tot + 8 + (t*12);
		bitch(BITCHINFO,"Reading info on titleset %u",t+1);
		bitch_indent();
		bitch(BITCHINFO,"Type:                  0x%02X",			tent[  0]);
		bitch(BITCHINFO,"Number of angles:      %u",				tent[  1]);
		bitch(BITCHINFO,"Number of chapters:    %u",		binBeHe16(	tent + 2));
		bitch(BITCHINFO,"Parental mask:         0x%04X",	binBeHe16(	tent + 4));
		bitch(BITCHINFO,"Video title set #:     %u",				tent[  6]);
		bitch(BITCHINFO,"Title within VTSN:     %u",				tent[  7]);
		title_ofs = binBeHe32(tent + 8);
		if (title_ofs == 0) {
			bitch(BITCHINFO,"No VTS data");
			goto title_next;
		}
		title_ofs += so;
		bitch(BITCHINFO,"Starting sector:       %u",		title_ofs);
		mark(title_ofs);

		bitch(BITCHINFO,"VTS IFO info");
		bitch_indent();
		LookAtVTS(session,idc,title_ofs);
		bitch_unindent();

title_next:
		bitch_unindent();
	}

finish:
	bitch_unindent();
	delete IFO;
	delete VIDEO_TS;
	delete root;
	return 0;
}

