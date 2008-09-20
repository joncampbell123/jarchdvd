
#include "config.h"
#include "mediagather.h"
#include "bitchin.h"
#include "todolist.h"
#include "main.h"
#include <string.h>
#include <fcntl.h>

// state of last session
char *strSOLS[4] = {	"empty",		"incomplete",
			"reserved/damaged",	"complete"};

// disc status
char *strDS[4] = {	"empty",		"incomplete",
			"finalized",		"random access only"};

// background format status
char *strBGS[4] = {	"none",					"background format started but not complete",
			"background format in progress",	"background formatting complete"};

// Linear density field
char *strLinDen[16] = {	"0.267 um/bit",		"0.293 um/bit",		"0.409-0.435 um/bit",	"unknown (3)",
			"0.280-0.291 um/bit",	"unknown (5)",		"unknown (6)",		"unknown (7)",
			"0.353 um/bit",		"unknown (9)",		"unknown (10)",		"unknown (11)",
			"unknown (12)",		"unknown (13)",		"unknown (14)",		"unknown (15)"};

// Track density field
char *strTrkDen[16] = {	"0.74 um/track",	"0.80 um/track",	"0.615 um/track",	"unknown",
			"unknown",		"unknown",		"unknown",		"unknown",
			"unknown",		"unknown",		"unknown",		"unknown",
			"unknown",		"unknown",		"unknown",		"unknown"};

char *strDiscType(unsigned char x)
{
	if (x == 0x00)		return "CD-DA or CD-ROM disc";
	else if (x == 0x10)	return "CD-I disc";
	else if (x == 0x20)	return "CD-ROM XA disc";
	else if (x == 0xFF)	return "undefined";
	return "?";
}

// book type
char *strBT(unsigned char x)
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
char *strDscSz(unsigned char x)
{
	if (x == 0x0)		return "120mm";
	if (x == 0x1)		return "80mm";
	return "unknown";
}

char *strMaxRt(unsigned char x)
{
	if (x == 0x0)		return "2.52 mbps";
	if (x == 0x1)		return "5.04 mbps";
	if (x == 0x3)		return "10.08 mbps";
	if (x == 0xF)		return "unspecified";
	return "unknown";
}

char *strTrkPth(unsigned char x)
{
	if (x)	return "Parallel Track Path";
	return "Opposite Track Path";
}

static char __strLayTypTmp[140];
char *strLayTyp(unsigned char x)
{
	x &= 7;
	if (x == 0) return "Not writable";

	strcpy(__strLayTypTmp,"Layer contains ");
	if (x & 1) strcat(__strLayTypTmp,"embossed data, ");
	if (x & 2) strcat(__strLayTypTmp,"recordable area, ");
	if (x & 4) strcat(__strLayTypTmp,"rewriteable area, ");
	return __strLayTypTmp;
}

char *strCPST(unsigned char x)
{
	if (x == 0x00)	return "none";
	if (x == 0x01)	return "CSS/CPPM";
	if (x == 0x02)	return "CPRM";
	return "unknown";
}

char *DVDRegionName[8] = {
	"United States of America, Canada",
	"Japan, Europe, South Africa, Middle East, Greenland",
	"South Korea, Taiwan, Hong Kong, Parts of South East Asia",
	"Australia, New Zealand, Latin America, Mexico",
	"Eastern Europe, Russia, India, Africa",
	"China",
	"?",
	"?",
};

static void GatherDiscInfo(JarchSession *session)
{
	unsigned char cmd[12];
	unsigned char buf[2052];
	int opc,i;

	bitch(BITCHINFO,"Getting Disc Information");
	bitch_indent();

	memset(cmd,0,12);
	cmd[ 0] = 0x51;	// READ DISC INFO
	cmd[ 7] = sizeof(buf) >> 8;
	cmd[ 8] = sizeof(buf);

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

void GatherDVDInfo00(JarchSession *session)
{
	char tmp[64];
	unsigned char cmd[12];
	unsigned char buf[2052],*desc;
	int layer,layers,fd,i;

	cmd[ 0] = 0xAD;		// READ DVD STRUCTURE
	cmd[ 1] = 0x00;		// READ DVD (not Blue-Ray)
	cmd[ 2] =
	cmd[ 3] =
	cmd[ 4] =
	cmd[ 5] = 0x00;		// address
	cmd[ 6] = 0;		// layer 0
	cmd[ 7] = 0x00;		// format 0: physical format info
	cmd[ 8] = sizeof(buf) >> 8;
	cmd[ 9] = sizeof(buf);
	cmd[10] = 0;
	cmd[11] = 0;

	memset(buf,0,2052);
	if (session->bdev->scsi(cmd,12,buf,2052,1) < 0) {
		bitch(BITCHWARNING,"Unable to read physical layer information");
		return;
	}

	desc = buf + 4;
	layer = 0;
	layers = (desc[2] >> 5) & 3;
	bitch(BITCHINFO,"This DVD has %u layers",layers+1);

	// store number of layers in the TODO list
	for (i=0;i < 8;i++)
		session->todo->set(TODO_MEDIAGATHER_DVD_LAYERS+i,((layers+1) >> i) & 1);

	while (1) {
		bitch(BITCHINFO,"Layer %u:",layer);
		bitch_indent();
		bitch(BITCHINFO,"Book type:                           %s",strBT((     desc[0] >> 4) & 0xF));
		bitch(BITCHINFO,"Part version:                        %u",            desc[0]       & 0xF );
		bitch(BITCHINFO,"Disc size:                           %s",strDscSz((  desc[1] >> 4) & 0xF));
		bitch(BITCHINFO,"Maximum rate:                        %s",strMaxRt(   desc[1]       & 0xF));
		bitch(BITCHINFO,"Number of layers:                    %u",((          desc[2] >> 5) & 0x3)+1);
		bitch(BITCHINFO,"Track path:                          %s",strTrkPth(( desc[2] >> 4) & 0x1));
		bitch(BITCHINFO,"Layer type:                          %s",strLayTyp(  desc[2]       & 0xF));
		bitch(BITCHINFO,"Linear density:                      %s",strLinDen[( desc[3] >> 4) & 0xF]);
		bitch(BITCHINFO,"Track density:                       %s",strTrkDen[  desc[3]       & 0xF]);
		bitch(BITCHINFO,"Starting physical sector, data area: 0x%08X",binBeHe32(desc+4 ));
		bitch(BITCHINFO,"Ending physical sector, data area:   0x%08X",binBeHe32(desc+8 ));
		bitch(BITCHINFO,"Ending physical sector, this layer:  0x%08X",binBeHe32(desc+12));
		bitch(BITCHINFO,"Burst Cutting Area present:          %s",YesNo(      desc[16] >> 7      ));
		bitch_unindent();

		sprintf(tmp,"raw-layer-%03u-data.bin",layer);
		fd = open(tmp,O_CREAT | O_TRUNC | O_RDWR | O_BINARY,0644);
		if (fd < 0) {
			bitch(BITCHWARNING,"Unable to save raw data returned for layer %u",layer);
			return;
		}

		write(fd,buf,2052);
		close(fd);

		if ((++layer) > layers)
			break;

		cmd[6] = layer;
		bitch(BITCHINFO,"Requesting layer %u",layer);
		if (session->bdev->scsi(cmd,12,buf,2052,1) < 0) {
			bitch(BITCHWARNING,"Unable to request physical layer %u info",layer);
			return;
		}
	};

	session->todo->set(TODO_MEDIAGATHER_DVD_STRUCT_00,1);
}

void GatherDVDInfo01(JarchSession *session)
{
	unsigned char cmd[12],buf[2052];
	int layer,i;

	/* GatherDVDInfo00 must have been called first to gather the number of layers on the DVD */
	if (!session->todo->get(TODO_MEDIAGATHER_DVD_STRUCT_00))
		return;

	for (layer=0;layer < session->DVD_layers;layer++) {
		cmd[ 0] = 0xAD;
		cmd[ 1] = 0x00;
		cmd[ 2] =
		cmd[ 3] =
		cmd[ 4] =
		cmd[ 5] = 0;
		cmd[ 6] = layer;
		cmd[ 7] = 0x01;	// read copyright info
		cmd[ 8] = 0;
		cmd[ 9] = 8;
		cmd[10] = 0x00;
		cmd[11] = 0x00;

		memset(buf,0,2052);
		bitch(BITCHINFO,"Requesting copyright info for layer %u",layer);
		if (session->bdev->scsi(cmd,12,buf,8,1) < 0) {
			bitch(BITCHWARNING,"failed");
			return;
		}
		bitch_indent();
		bitch(BITCHINFO,"Copyright protection system type:         %s",strCPST(buf[4]));
		bitch(BITCHINFO,"Cannot be played in regions:");
		bitch_indent();
		if (buf[5] == 0x00) {
			bitch(BITCHINFO,"None forbidden, region free");
		} else {
			for (i=0;i < 8;i++)
				if (buf[5] & (1<<i))
					bitch(BITCHINFO,"Region %u: %s",i+1,DVDRegionName[i]);
		}
		bitch_unindent();
		bitch_unindent();
	}

	session->todo->set(TODO_MEDIAGATHER_DVD_STRUCT_01,1);
}

void GatherDVDInfo03(JarchSession *session)
{
	unsigned char cmd[12],buf[2052];

	bitch(BITCHINFO,"Gathering BCA data");

	memset(buf,0,sizeof(buf));
	cmd[ 0] = 0xAD;
	cmd[ 1] = 0x00;
	cmd[ 2] =
	cmd[ 3] =
	cmd[ 4] =
	cmd[ 5] = 0;
	cmd[ 6] = 0;
	cmd[ 7] = 0x03;	// read BCA
	cmd[ 8] = sizeof(buf) >> 8;
	cmd[ 9] = sizeof(buf);
	cmd[10] = 0x00;
	cmd[11] = 0x00;
	if (session->bdev->scsi(cmd,12,buf,sizeof(buf),1) < 0) {
		bitch(BITCHWARNING,"failed");
	}
	else {
		int fd = open("BCA.bin",O_CREAT | O_TRUNC | O_RDWR | O_BINARY,0644);

		if (fd < 0) {
			bitch(BITCHERROR,"Unable to create file to store BCA data");
			return;
		}

		bitch(BITCHINFO,"    writing BCA data");
		write(fd,buf,sizeof(buf));
		close(fd);
	}

	session->todo->set(TODO_MEDIAGATHER_DVD_STRUCT_03,1);
}

void GatherDVDInfo04(JarchSession *session)
{
	char tmp[128];
	unsigned char cmd[12],buf[2052];
	int layer;

	/* GatherDVDInfo00 must have been called first to gather the number of layers on the DVD */
	if (!session->todo->get(TODO_MEDIAGATHER_DVD_STRUCT_00))
		return;

	for (layer=0;layer < session->DVD_layers;layer++) {
		bitch(BITCHINFO,"Gathering disc manufacturing data (layer %u)",layer);

		memset(buf,0,sizeof(buf));
		cmd[ 0] = 0xAD;
		cmd[ 1] = 0x00;
		cmd[ 2] =
		cmd[ 3] =
		cmd[ 4] =
		cmd[ 5] = 0;
		cmd[ 6] = layer;
		cmd[ 7] = 0x04; // read disc manufacturing data
		cmd[ 8] = sizeof(buf) >> 8;
		cmd[ 9] = sizeof(buf);
		cmd[10] = 0x00;
		cmd[11] = 0x00;
		if (session->bdev->scsi(cmd,12,buf,sizeof(buf),1) < 0) {
			bitch(BITCHWARNING,"failed");
		}
		else {
			int fd;

			sprintf(tmp,"disc-manufacturing-data-layer-%u.bin",layer);
			fd = open(tmp,O_CREAT | O_TRUNC | O_RDWR | O_BINARY,0644);
			if (fd < 0) {
				bitch(BITCHERROR,"Unable to create file to store manufacturing data");
				return;
			}

			write(fd,buf,sizeof(buf));
			close(fd);
		}
	}

	session->todo->set(TODO_MEDIAGATHER_DVD_STRUCT_04,1);
}

void GatherDVDInfo08(JarchSession *session)
{
	unsigned char cmd[12],buf[2052];

	memset(buf,0,sizeof(buf));
	bitch(BITCHINFO,"Gathering DVD-RAM Disc Definition Structure data");
	cmd[ 0] = 0xAD;
	cmd[ 1] = 0x00;
	cmd[ 2] =
	cmd[ 3] =
	cmd[ 4] =
	cmd[ 5] = 0;
	cmd[ 6] = 0;
	cmd[ 7] = 0x08;	// read DVD-RAM disc definition structure
	cmd[ 8] = sizeof(buf) >> 8;
	cmd[ 9] = sizeof(buf);
	cmd[10] = 0x00;
	cmd[11] = 0x00;
	if (session->bdev->scsi(cmd,12,buf,sizeof(buf),1) < 0) {
		bitch(BITCHWARNING,"failed");
	}
	else {
		int fd = open("DVD-RAM-disc-definition-structure.bin",O_RDWR | O_CREAT | O_TRUNC | O_BINARY,0644);

		if (fd < 0) {
			bitch(BITCHERROR,"Unable to create file");
			return;
		}

		write(fd,buf,sizeof(buf));
		close(fd);
	}

	session->todo->set(TODO_MEDIAGATHER_DVD_STRUCT_08,1);
}

void GatherDVDInfo09(JarchSession *session)
{
	unsigned char cmd[12],buf[2052];

	memset(buf,0,sizeof(buf));
	bitch(BITCHINFO,"Gathering DVD-RAM medium status");
	cmd[ 0] = 0xAD;
	cmd[ 1] = 0x00;
	cmd[ 2] =
	cmd[ 3] =
	cmd[ 4] =
	cmd[ 5] = 0;
	cmd[ 6] = 0;
	cmd[ 7] = 0x09; // read DVD-RAM medium status
	cmd[ 8] = sizeof(buf) >> 8;
	cmd[ 9] = sizeof(buf);
	cmd[10] = 0x00;
	cmd[11] = 0x00;
	if (session->bdev->scsi(cmd,12,buf,sizeof(buf),1) < 0) {
		bitch(BITCHWARNING,"failed");
	}
	else {
		int fd = open("DVD-RAM-medium-status.bin",O_RDWR | O_CREAT | O_TRUNC | O_BINARY,0644);

		if (fd < 0) {
			bitch(BITCHERROR,"Unable to create file");
			return;
		}

		write(fd,buf,sizeof(buf));
		close(fd);
	}

	session->todo->set(TODO_MEDIAGATHER_DVD_STRUCT_09,1);
}

void GatherDVDInfo0A(JarchSession *session)
{
	unsigned char cmd[12],buf[2052];

	memset(buf,0,sizeof(buf));
	bitch(BITCHINFO,"Gathering DVD-RAM spare area info");
	cmd[ 0] = 0xAD;
	cmd[ 1] = 0x00;
	cmd[ 2] =
	cmd[ 3] =
	cmd[ 4] =
	cmd[ 5] = 0;
	cmd[ 6] = 0;
	cmd[ 7] = 0x0A; // read spare area info
	cmd[ 8] = sizeof(buf) >> 8;
	cmd[ 9] = sizeof(buf);
	cmd[10] = 0x00;
	cmd[11] = 0x00;
	if (session->bdev->scsi(cmd,12,buf,sizeof(buf),1) < 0) {
		bitch(BITCHWARNING,"failed");
	}
	else {
		int fd = open("DVD-RAM-spare-area-info.bin",O_RDWR | O_CREAT | O_TRUNC | O_BINARY,0644);

		if (fd < 0) {
			bitch(BITCHERROR,"Unable to create file");
			return;
		}

		write(fd,buf,sizeof(buf));
		close(fd);
	}

	session->todo->set(TODO_MEDIAGATHER_DVD_STRUCT_0A,1);
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

		if (!m->get(TODO_MEDIAGATHER_DVD_STRUCT_00) || session->chosen_force_info)
			GatherDVDInfo00(session);

		session->DVD_layers = 0;
		for (i=0;i < 8;i++)
			session->DVD_layers |= (m->get(TODO_MEDIAGATHER_DVD_LAYERS+i) << i);

		if (!m->get(TODO_MEDIAGATHER_DVD_STRUCT_01) || session->chosen_force_info)
			GatherDVDInfo01(session);

		if (!m->get(TODO_MEDIAGATHER_DVD_STRUCT_03) || session->chosen_force_info)
			GatherDVDInfo03(session);

		if (!m->get(TODO_MEDIAGATHER_DVD_STRUCT_04) || session->chosen_force_info)
			GatherDVDInfo04(session);

		if (!m->get(TODO_MEDIAGATHER_DVD_STRUCT_08) || session->chosen_force_info)
			GatherDVDInfo08(session);

		if (!m->get(TODO_MEDIAGATHER_DVD_STRUCT_09) || session->chosen_force_info)
			GatherDVDInfo09(session);

		if (!m->get(TODO_MEDIAGATHER_DVD_STRUCT_0A) || session->chosen_force_info)
			GatherDVDInfo0A(session);

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

	/* read number of layers from TODO list */
	session->DVD_layers = 0;
	for (i=0;i < 8;i++)
		session->DVD_layers |= (m->get(TODO_MEDIAGATHER_DVD_LAYERS+i) << i);

	bitch(BITCHINFO,"DVD has %u layers, officially",session->DVD_layers);
}

