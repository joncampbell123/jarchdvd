
#ifndef __UDF_H
#define __UDF_H

#include "config.h"
#include "ripdvd.h"

// UDF 1.02 
class UDFfs {
public:
	UDFfs(ImageDVDCombo *src);
	~UDFfs();
public:
	// EMCA-167 UDF v1.02 structures
	typedef struct {
		unsigned short int      ident;          // tag identifier
		unsigned short int      ver;            // descriptor version
		unsigned char           checksum;       // tag checksum
		unsigned char           reserved;
		unsigned short int      serial;         // tag serial #
		unsigned short int      desc_crc;       // descriptor CRC
		unsigned short int      desc_crc_len;   // descriptor CRC length
		unsigned long           location;       // tag location
	} UDFtag;
	// copying function
	void UDFtagCopy(unsigned char *src,UDFtag *dst);

	typedef struct {
		unsigned long           len;
		unsigned long           location;
	} UDFextentad;
	// copying function
	void UDFextentadCopy(unsigned char *src,UDFextentad *ext);

	typedef struct {
		unsigned long           log_no;         // logical block number
		unsigned short int      partition;      // partition ref. number
	} UDFlbaddr;
	// copying function
	void UDFlbaddrCopy(unsigned char *b,UDFlbaddr *u);

	typedef struct {
		unsigned long           len;
		UDFlbaddr               location;
		unsigned char           implementation[6];      // implementation use
	} UDFlongad;
	// copying function
	void UDFlongadCopy(unsigned char *src,UDFlongad *la);

	typedef struct {
		unsigned char           flags;
		unsigned char           identifier[24];
		unsigned char           identsuffix[9];
	} UDFregid;
	// copying function
	void UDFregidCopy(unsigned char *src,UDFregid *ext);

	typedef struct {
		unsigned long           len;            // NOTE: upper 2 bits used for other things
		unsigned long           location;       // NOTE: 0=means unallocated
	} UDFshortad;
	// copying function
	void UDFshortadCopy(unsigned char *src,UDFshortad *sad);

	void UDFdstrcpy(char *dst,unsigned char *src,int srclen);
public:
	class dir {
	public:
		dir(UDFfs *parent);
		~dir();
	public:
		unsigned char*	enumfirst();
		unsigned char*	enumnext();
		int		find(char *name,char dir);
		dir*		get(unsigned char *ent);
	public:
		UDFshortad	dirext;
		int		dirsz;
		int		idx;
		unsigned char	dirent[2048];
		unsigned char*	find_dirent;
		unsigned char*	find_dirnext;
	public:
		UDFfs*		mom;
	};
public:
	int		mount();
	dir*		root();
public:
	UDFextentad	main_volume_extent;
	UDFregid	partition_contents;
	UDFlongad	udf_root_ICB;
	UDFshortad	udf_root_dir_extent;
public:
	int		got_partition_contents;
	int		udf_nsr_partition_start;
	int		udf_nsr_partition_length;
	int		udf_nsr_partition_number;
	ImageDVDCombo*	idc;
	unsigned char	sector[2048*2];
public:
	int		mount_CheckMainVolDesc();
	int		pread(unsigned long sector,unsigned char *buf,int N);
};

#endif

