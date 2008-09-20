
#include "config.h"
#include "udf.h"
#include <string.h>

static unsigned int bin2intsle(unsigned char *buf)
{
	unsigned int x;

	x  =  (unsigned int)(buf[0]);
	x |= ((unsigned int)(buf[1]))<<8;
	return x;
}

static unsigned int bin2intsbe(unsigned char *buf)
{
	unsigned int x;

	x  = ((unsigned int)(buf[0]))<<8;
	x |=  (unsigned int)(buf[1]);
	return x;
}

static unsigned long bin2intle(unsigned char *buf)
{
	unsigned long x;

	x  =  (unsigned long)(buf[0]);
	x |= ((unsigned long)(buf[1]))<<8;
	x |= ((unsigned long)(buf[2]))<<16;
	x |= ((unsigned long)(buf[3]))<<24;
	return x;
}

static unsigned long bin2intbe(unsigned char *buf)
{
	unsigned long x;

	x  = ((unsigned long)(buf[0]))<<24;
	x |= ((unsigned long)(buf[1]))<<16;
	x |= ((unsigned long)(buf[2]))<<8;
	x |=  (unsigned long)(buf[3]);
	return x;
}

void bitch_udftag(UDFfs::UDFtag *t)
{
	bitch_indent();
	bitch(BITCHINFO,"Tag:                   %u",t->ident);
	bitch(BITCHINFO,"Version:               %u",t->ver);
	bitch(BITCHINFO,"Checksum:              0x%02X",t->checksum);
	bitch(BITCHINFO,"Reserved:              0x%02X",t->reserved);
	bitch(BITCHINFO,"Serial:                %u",t->serial);
	bitch(BITCHINFO,"Descriptor CRC:        %u",t->desc_crc);
	bitch(BITCHINFO,"Descriptor CRC length: %u",t->desc_crc_len);
	bitch(BITCHINFO,"Location:              %u",t->location);
	bitch_unindent();
}

void bitch_extentad(UDFfs::UDFextentad *e)
{
	bitch_indent();
	bitch(BITCHINFO,"Length:                %u bytes",e->len);
	bitch(BITCHINFO,"Location:              %u",e->location);
	bitch_unindent();
}

void bitch_lb_addr(UDFfs::UDFlbaddr *u)
{
	bitch_indent();
	bitch(BITCHINFO,"Logical block number:  %d",u->log_no);
	bitch(BITCHINFO,"Partition #:           %d",u->partition);
	bitch_unindent();
}

void bitch_udfregid(UDFfs::UDFregid *u)
{
	bitch_indent();
	bitch(BITCHINFO,"Flags:                 0x%02X",u->flags);
	bitch(BITCHINFO,"Identifier:            %s",u->identifier);
	bitch(BITCHINFO,"Identifier suffix:     %02X %02X %02X %02X %02X %02X %02X %02X",
		u->identsuffix[0],      u->identsuffix[1],      u->identsuffix[2],      u->identsuffix[3],
		u->identsuffix[4],      u->identsuffix[5],      u->identsuffix[6],      u->identsuffix[7]);
	bitch_unindent();
}

void bitch_longad(UDFfs::UDFlongad *u)
{
	bitch_indent();
	bitch(BITCHINFO,"Length:                %d",u->len);
	bitch(BITCHINFO,"Location:");
	bitch_lb_addr(&u->location);
	bitch_unindent();
}

void bitch_shortad(UDFfs::UDFshortad *u)
{
	bitch_indent();
	bitch(BITCHINFO,"Length:                %d",u->len);
	bitch(BITCHINFO,"Location:              %u",u->location);
	bitch_unindent();
}

UDFfs::UDFfs(ImageDVDCombo *src)
{
	idc = src;
}

UDFfs::~UDFfs()
{
}

int UDFfs::mount()
{
	UDFfs::UDFtag anchor;

	got_partition_contents = 0;
	if (idc->dvdread(256,sector,1) < 1)
		return 0;

	bitch(BITCHINFO,"UDF anchor tag");
	UDFtagCopy(sector,&anchor);
	bitch_udftag(&anchor);
	if (anchor.ident != 2 || anchor.location != 256) {
		bitch(BITCHWARNING,"Not a valid anchor. Not type 2 or location is wrong)");
		return 0;
	}

	bitch(BITCHINFO,"Main volume descriptor extent");
	UDFextentadCopy(sector+16,&main_volume_extent);
	bitch_extentad(&main_volume_extent);
	if (main_volume_extent.location < 32)
		return 0;

	if (!mount_CheckMainVolDesc())
		return 0;

	return 1;
}

int UDFfs::mount_CheckMainVolDesc()
{
	UDFtag utag,fsd;
	unsigned long o = main_volume_extent.location;
	int sz = (main_volume_extent.len + 2047) >> 11;
	int i,L_EA,LA;

	for (i=0;i < sz;i++) {
		if (idc->dvdread(o+i,sector,1) < 1)
			return 0;

		UDFtagCopy(sector,&utag);

		if (utag.ident == 5) {
			bitch(BITCHINFO,"Partition descriptor:");
			UDFregidCopy(sector+24,&partition_contents);
			bitch_udfregid(&partition_contents);
			/* NOTE: ECMA-167 says this should be "+NSR03" but they're wrong LOL */
			if (!strcmp((char*)partition_contents.identifier,"+NSR02") && !got_partition_contents) {
				got_partition_contents =	1;
				udf_nsr_partition_start =	bin2intle (sector + 188);
				udf_nsr_partition_length =	bin2intle (sector + 192);
				udf_nsr_partition_number =	bin2intsle(sector +  22);

				bitch(BITCHINFO,"Partition of interest: ECMA-167 UDF");
				bitch_indent();
				bitch(BITCHINFO,"Partition starts at:       %u",udf_nsr_partition_start);
				bitch(BITCHINFO,"Partition length is:       %u",udf_nsr_partition_length);
				bitch(BITCHINFO,"Partition number is:       %u",udf_nsr_partition_number);
				bitch_unindent();
			}
		}
	}

	/* if we didn't find the +NSR02 partition then leave */
	if (!got_partition_contents)
		return 0;

	if (!pread(0,sector,2)) return 0;
	UDFtagCopy(sector     ,&fsd);
	UDFtagCopy(sector+2048,&utag);
	bitch(BITCHINFO,"First tag");	bitch_udftag(&fsd);
	bitch(BITCHINFO,"Second tag");	bitch_udftag(&utag);
	if (fsd.ident != 0x100 || utag.ident != 8)
		return 0;

	bitch(BITCHINFO,"UDF root directory ICB");
	UDFlongadCopy(sector+ 400,&udf_root_ICB);
	bitch_longad(&udf_root_ICB);
	if (udf_root_ICB.location.partition != udf_nsr_partition_number)
		return 0;

	/* read UDF root directory ICB */
	if (!pread(udf_root_ICB.location.log_no,sector,1)) return 0;
	UDFtagCopy(sector,&utag);
	bitch(BITCHINFO,"UDF root directory ICB");
	bitch_udftag(&utag);
	if (utag.ident != 261)
		return 0;

	/* read the root directory allocation descriptor */
	L_EA =	bin2intle(sector+168);
	LA =	bin2intle(sector+172);
	if (LA >= 2048 || L_EA >= 2048 || L_EA > (2048-(172+LA)))
		return 0;
	if (LA < 8)
		return 0;
	if (LA >= 16)
		bitch(BITCHWARNING,"UDF reader: Root directories with multiple allocation extents not fully supported");

	UDFshortadCopy(sector+176+L_EA,&udf_root_dir_extent);
	bitch(BITCHINFO,"Root directory extent");
	bitch_shortad(&udf_root_dir_extent);
	if (udf_root_dir_extent.len & 0xC0000000)
		return 0;

	return 1;
}

void UDFfs::UDFtagCopy(unsigned char *b,UDFfs::UDFtag *u)
{
	u->ident =              bin2intsle(b     );
	u->ver =                bin2intsle(b +  2);
	u->checksum =                      b[   4];
	u->reserved =                      b[   5];
	u->serial =             bin2intsle(b +  6);
	u->desc_crc =           bin2intsle(b +  8);
	u->desc_crc_len =       bin2intsle(b + 10);
	u->location =           bin2intle (b + 12);
}

void UDFfs::UDFextentadCopy(unsigned char *b,UDFfs::UDFextentad *u)
{
	u->len =		bin2intle (b     );
	u->location =		bin2intle (b +  4);
}

void UDFfs::UDFregidCopy(unsigned char *b,UDFfs::UDFregid *r)
{
	r->flags =		           b[   0];
	memcpy(r->identifier,              b +  1,23);	r->identifier[23] = 0;
	memcpy(r->identsuffix,             b + 24,8);	r->identsuffix[8] = 0;
}

void UDFfs::UDFlbaddrCopy(unsigned char *b,UDFfs::UDFlbaddr *u)
{
	u->log_no =		bin2intle (b     );
	u->partition =		bin2intsle(b +  4);
}

void UDFfs::UDFlongadCopy(unsigned char *b,UDFfs::UDFlongad *u)
{
	u->len =		bin2intle (b     );
	UDFlbaddrCopy			  (b +  4,&u->location);
	memcpy(u->implementation,          b + 10,6);
}

void UDFfs::UDFshortadCopy(unsigned char *b,UDFshortad *u)
{
	u->len =		bin2intle (b     );
	u->location =		bin2intle (b +  4);
}

int UDFfs::pread(unsigned long s,unsigned char *buf,int N)
{
	if (!got_partition_contents)
		return 0;

	s += udf_nsr_partition_start;
	return idc->dvdread(s,buf,N);
}

UDFfs::dir *UDFfs::root()
{
	dir* d;

	d = new dir(this);
	if (!d) return NULL;
	memcpy(&d->dirext,&udf_root_dir_extent,sizeof(UDFshortad));
	d->dirsz = (d->dirext.len + 2047) >> 1;
	return d;
}

UDFfs::dir::dir(UDFfs* that)
{
	find_dirnext = NULL;
	find_dirent = NULL;
	mom = that;
	idx = 0;
}

UDFfs::dir::~dir()
{
}

unsigned char* UDFfs::dir::enumfirst()
{
	idx = 0;
	find_dirent = NULL;
	find_dirnext = NULL;
	if (idx >= dirsz) return NULL;
	if (mom->pread(dirext.location+idx,dirent,1) < 1) return NULL;
	idx++;
	return dirent;
}

unsigned char* UDFfs::dir::enumnext()
{
	find_dirent = NULL;
	find_dirnext = NULL;
	if (idx >= dirsz) return NULL;
	if (mom->pread(dirext.location+idx,dirent,1) < 1) return NULL;
	idx++;
	return dirent;
}

int UDFfs::dir::find(char *name,char dir)
{
	unsigned char *sector,*sm;
	unsigned char flags;
	char tempname[256];
	UDFlongad icb;
	int padding;
	int LIU,LFI;
	UDFtag tag;

	if (!find_dirnext)
		sector = enumfirst();
	else
		sector = find_dirnext;

	while (sector) {
		sm = dirent + 2048;
		do {
			mom->UDFtagCopy(sector,&tag);
			if (tag.ident != 257) break;

			LFI = sector[19];
			flags = sector[18];
			LIU = bin2intsle(sector+36);
			mom->UDFlongadCopy(sector+20,&icb);
			padding = 4 * ((38 + LFI + LIU + 3)/4);

			if (!(flags & 8)) {
				if (	( dir &&  (flags & 2)) ||
					(!dir && !(flags & 2))) {
					mom->UDFdstrcpy(tempname,sector+LIU+38,LFI);
					if (!strcmp(tempname,name)) {
						find_dirent = sector;
						find_dirnext = sector + padding;
						return 1;
					}
				}
			}

			sector += padding;
		} while (tag.ident == 257 && sector < sm);
		sector = enumnext();
	}

	find_dirnext = NULL;
	find_dirent = NULL;
	return -1;
}

UDFfs::dir *UDFfs::dir::get(unsigned char *sector)
{
	unsigned char tmp[2048];
	UDFtag tag;
	dir *d;
	int LEA,LAD;
	int LIU,LFI;
	UDFshortad sad;
	UDFlongad icb;

	mom->UDFtagCopy(sector,&tag);
	if (tag.ident != 257) return NULL;

	LFI = sector[19];
	LIU = bin2intsle(sector+36);
	mom->UDFlongadCopy(sector+20,&icb);
	if (mom->pread(icb.location.log_no,tmp,1) < 1) return NULL;

	LEA = bin2intle(tmp+168);
	LAD = bin2intle(tmp+172);
	if (LEA > (2048-(176+8+LAD))) return NULL;
	if (LAD < 8) return NULL;

	mom->UDFshortadCopy(tmp+176+LEA,&sad);
	d = new dir(mom);
	if (!d) return NULL;

        memcpy(&d->dirext,&sad,sizeof(UDFshortad));
	d->dirsz = (d->dirext.len + 2047) >> 1;
	return d;
}

void UDFfs::UDFdstrcpy(char *d,unsigned char *s,int max)
{
        unsigned char c;

	if (max <= 0) return;
	c = *s++; max--;
	if (max <= 0) {
		*d++ = 0;
		return;
	}

	if (c == 8) {
		memcpy(d,s,max); d[max]=0;
		return;
	}
	else if (c == 16) {
		while (max > 1) {
			if (s[1] != 0)
				*d++ = '?';
			else
				*d++ = s[0];

			s += 2;
			max -= 2;
		}

		*d++ = 0;
	}
	else if (c == 0) {
		*d++ = 0;
	}
	else {
		*d++ = 0;
	}
}

