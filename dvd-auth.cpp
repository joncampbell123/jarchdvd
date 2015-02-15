/* code for authenticating a DVD on systems that do not provide
   structures and IOCTLs for doing so (e.g. we must do it ourself
   because Windows NT does not provide this for us) */

#include "dvd-auth.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Linux ioctl() authentication not supported */
#if DVD_AUTHENTICATE == DVD_AUTHENTICATE_LINUX
#error DVD_AUTHENTICATE_LINUX not supported!
#endif

#if DVD_AUTHENTICATE == DVD_AUTHENTICATE_INDEPENDENT

DVDAuth::DVDAuth(JarchBlockIO *dev)
{
	have_disc_key = 0;
	have_bus_key = 0;
	auth_stfu = 0;
	bdev = dev;
}

DVDAuth::~DVDAuth()
{
}

int DVDAuth::IsAuthenticated()
{
	unsigned char buf[20],cmd[12],asf;

	memset(buf,0,sizeof(buf));
	memset(cmd,0,sizeof(cmd));
	cmd[ 0] = 0xA4;		// REPORT_KEY
	cmd[ 9] = 8;
	cmd[10] = 0x05;		// agid=0 type=5
	if (bdev->scsi(cmd,12,buf,8,1) < 0) {
		bitch(BITCHWARNING,"ioctl(DVD_AUTH) failed");
		return 0;
	}

	asf = buf[7] & 1;
//	bitch(BITCHINFO,"DVD is %sauthenticated (0x%X)",(asf) ? "" : "not ",asf);
	return asf;
}

int DVDAuth::authdrive()
{
	int i;

	for (i=0;i < 5;i++)
		Key1.b[i] = LUkey1[i];

	for (i=0;i < 32;i++) {
		CryptKey1(i,Challenge,&KeyCheck);
		if (!memcmp(KeyCheck.b,Key1.b,5)) {
			varient = i;
//			bitch(BITCHINFO,"Drive authenticated using varient %d",i);
			return 1;
		}
	}

	bitch(BITCHWARNING,"Unable to determine varient");
	return 0;
}

int DVDAuth::GetDiscKey(int agid,unsigned char *key)
{
	unsigned char cmd[12],buf[2052],*keyval;
	int index;

	memset(cmd,0,sizeof(cmd));
	memset(buf,0,2052);
	cmd[ 0] = 0xAD;		// READ DVD STRUCTURE
	cmd[ 7] = 0x02;		// STRUCT_DISCKEY
	cmd[ 8] = 2052 >> 8;
	cmd[ 9] = 2052 & 0xFF;
	cmd[10] = agid << 6;
	keyval = buf + 4;
	if (bdev->scsi(cmd,12,buf,2052,1) < 0) {
//		bitch(BITCHWARNING,"Unable to retrieve disc key errno %s",strerror(errno));
		return 0;
	}

	for (index=0;index < 2048;index++)
		keyval[index] ^= key[4 - (index%5)];

	bitch(BITCHINFO,"Attempting to find disc key...");
	if (DecryptDiscKey(keyval,DiscKey) < 0) {
		bitch(BITCHWARNING,"Unable to find disc key!");
		return 0;
	}

	bitch(BITCHINFO,"Found disc key: %02X %02X %02X %02X %02X",
			DiscKey[0],
			DiscKey[1],
			DiscKey[2],
			DiscKey[3],
			DiscKey[4]);

	have_disc_key = 1;
	return 1;
}

int DVDAuth::GetTitleKey(int agid,juint64 lba,unsigned char *key)
{
	unsigned char cmd[12],buf[12];
	unsigned char *title_key;
	unsigned char cpm,cp_sec,cgms;
	int i;

	memset(cmd,0,sizeof(cmd));
	cmd[ 0] = 0xA4;		// REPORT KEY
	cmd[ 2] = lba >> 24;
	cmd[ 3] = lba >> 16;
	cmd[ 4] = lba >> 8;
	cmd[ 5] = lba;
	cmd[ 9] = 12;
	cmd[10] = 0x04 | (agid << 6);

	memset(buf,0,12);
	if (bdev->scsi(cmd,12,buf,12,1) < 0) {
//		bitch(BITCHWARNING,"Unable to retrieve title key errno %s",strerror(errno));
		return 0;
	}

	cpm = (buf[4] >> 7) & 1;
	cp_sec = (buf[4] >> 6) & 1;
	cgms = (buf[4] >> 4) & 3;
	title_key = buf + 5;

	for (i=0;i < 5;i++)
		title_key[i] ^= key[4 - (i%5)];

	if (	title_key[0] != 0 || title_key[1] != 0 ||
		title_key[2] != 0 || title_key[3] != 0 ||
		title_key[4] != 0) {
		DecryptKey(0xFF,DiscKey,title_key,title_key);
	}

//	bitch(BITCHINFO,"Title key: %02X %02X %02X %02X %02X",
//			title_key[0],	title_key[1],
//			title_key[2],	title_key[3],
//			title_key[4]);
//	bitch(BITCHINFO,"CPM=0x%X, CP_SEC=0x%X, CGMS=0x%X",
//			cpm,
//			cp_sec,
//			cgms);

	memcpy(RawTitleKey,buf+4,8);
	memcpy(TitleKey,title_key,5);
	return 1;
}

/* NOTE: To decrypt the title keys you must first call this function with title = 0 to
 *       obtain/decrypt the disc key. */
int DVDAuth::Authenticate(int title,juint64 LBA)
{
	unsigned char cmd[12],buf[100];
	int tries,rv,i;
	int retry=1;

again:	if (!IsAuthenticated()) have_bus_key = 0;

	if (!have_bus_key) {
		if (!auth_stfu) {
			bitch(BITCHINFO,"DVD authentication: Obtaining bus key for communication with DVD-ROM drive");
			bitch(BITCHINFO,"Requesting AGID");
		}

		for (tries=1,rv=-1;rv == -1 && tries < 4;tries++) {
			memset(cmd,0,sizeof(cmd));
			memset(buf,0,8);
			cmd[ 0] = 0xA4;		// report key a la DVD_LU_SEND_AGID
			cmd[ 9] = 8;
			rv=bdev->scsi(cmd,12,buf,8,1);
			if (rv == -1) {
				bitch(BITCHWARNING,"...DVD auth failed, invalidating");
				DeAuthenticate();
			}
		}

		if (tries >= 4) {
			bitch(BITCHWARNING,"Can't seem to obtain AGID, DVD authentication failed");
			return 0;
		}

		agid = buf[7] >> 6;
		if (!auth_stfu) bitch(BITCHINFO,"Got AGID %08X",agid);

		for (i=0;i < 10;i++)
			Challenge[i] = i;

		if (!auth_stfu) {
			bitch(BITCHINFO,"Attempting challenge with %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
					Challenge[0],Challenge[1],Challenge[2],Challenge[3],Challenge[4],
					Challenge[5],Challenge[6],Challenge[7],Challenge[8],Challenge[9]);
		}

		/* challenge and response (duplicating css-auth talking to itself) */
		memset(cmd,0,sizeof(cmd));
		cmd[ 0] = 0xA3;		// SEND KEY
		cmd[ 9] = 16;
		cmd[10] = 0x01 | (agid << 6);
		memset(buf,0,20);
		buf[ 1] = 0x0E;

		for (i=0;i < 10;i++)
			buf[(9-i)+4] = Challenge[i];

		if (bdev->scsi(cmd,12,buf,16,2) < 0) {
			bitch(BITCHWARNING,"Sending challenge to host failed errno = %s",strerror(errno));
			return 0;
		}

		memset(cmd,0,sizeof(cmd));
		cmd[ 0] = 0xA4;		// REPORT KEY a la DVD_LU_SEND_KEY1
		cmd[ 9] = 12;
		cmd[10] = 0x02 | (agid << 6);
		memset(buf,0,20);

		if (bdev->scsi(cmd,12,buf,12,1) < 0) {
			bitch(BITCHWARNING,"Getting key #1 from host failed errno %s",strerror(errno));
			return 0;
		}

		for (i=0;i < 5;i++)
			LUkey1[i] = buf[(4-i)+4];

		if (!auth_stfu) {
			bitch(BITCHINFO,"LU key #1 %02X %02X %02X %02X %02X",
				LUkey1[0],LUkey1[1],LUkey1[2],LUkey1[3],LUkey1[4]);
		}

		if (!authdrive())
			return 0;

		memset(cmd,0,sizeof(cmd));
		cmd[ 0] = 0xA4;		// REPORT KEY
		cmd[ 9] = 16;
		cmd[10] = 0x01 | (agid << 6);
		memset(buf,0,20);

		if (bdev->scsi(cmd,12,buf,16,1) < 0) {
			bitch(BITCHWARNING,"Unable to send challenge errno %s",strerror(errno));
			return 0;
		}

		for (i=0;i < 10;i++)
			Challenge[i] = buf[(9-i)+4];

		if (!auth_stfu) {
			bitch(BITCHINFO,"LU sent challenge %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
					Challenge[0],Challenge[1],Challenge[2],Challenge[3],Challenge[4],
					Challenge[5],Challenge[6],Challenge[7],Challenge[8],Challenge[9]);
		}

		CryptKey2(varient,Challenge,&Key2);

		memset(cmd,0,sizeof(cmd));
		memset(buf,0,20);
		cmd[ 0] = 0xA3;		// SEND KEY
		cmd[ 9] = 12;
		cmd[10] = 0x03 | (agid << 6);
		buf[ 1] = 0xA;
		for (i=0;i < 5;i++)
			buf[(4-i)+4] = Key2.b[i];

		if (!auth_stfu) {
			bitch(BITCHINFO,"Host sending key 2 %02X %02X %02X %02X %02X",
					Key2.b[0],Key2.b[1],Key2.b[2],Key2.b[3],Key2.b[4]);
		}

		if (bdev->scsi(cmd,12,buf,12,2) < 0) {
			bitch(BITCHWARNING,"LU didn't like key #2 errno %s",strerror(errno));
			return 0;
		}

		if (!auth_stfu) bitch(BITCHINFO,"Hooray, DVD-ROM authenticated!");
		dvd_auth_good = 1;

		memcpy(Challenge,Key1.b,5);
		memcpy(Challenge+5,Key2.b,5);
		CryptBusKey(varient,Challenge,&KeyCheck);

		if (!auth_stfu) {
			bitch(BITCHINFO,"Bus/Session key: %02X %02X %02X %02X %02X",
					KeyCheck.b[0],
					KeyCheck.b[1],
					KeyCheck.b[2],
					KeyCheck.b[3],
					KeyCheck.b[4]);
		}

		memcpy(BusKey,KeyCheck.b,5);
		have_bus_key = 1;
		auth_stfu = 1;
	}

/* note: the retry counter and again label are there to handle
         some DVD-ROM drives that insist on re-negotiating a bus
	 key every time we make a request for a title key or
	 disc key. The problem is that re-negotiation is SLOW.
	 This is why ideally we should only have to do that once
	 and then re-use the same key for every request which
	 is much faster (some drives don't care) */
	if (title > 0) {
		if (!GetTitleKey(agid,LBA,KeyCheck.b)) {
			have_bus_key = 0;
			if (retry-- > 0) goto again;
			return 0;
		}
	}
	else if (title == 0) {
		if (!GetDiscKey(agid,KeyCheck.b)) {
			have_bus_key = 0;
			if (retry-- > 0) goto again;
			return 0;
		}
	}
	else {
		// the caller merely wanted us to re-negotiate a new bus key.
		// once a bus key has been negotiated we can ask for disc
		// and title keys without re-negotiating (contrary to what
		// the DeCSS source code seems to suggest). note that
		// negotiating a bus key does not "authenticate" the drive
		// and allow access to the protected sectors. it merely
		// negotiates a bus key so that we can then de-ofuscate
		// the keys.
		return 1;
	}

	return 1;
}

int DVDAuth::DeAuthenticate()
{
	unsigned char cmd[12],buf[8];

	bitch(BITCHINFO,"DVD authentication: Invalidating current bus key and AGID");
	have_bus_key = 0;
	memset(cmd,0,sizeof(cmd));
	memset(buf,0,8);
	cmd[ 0] = 0xA4;	// DVD_INVALIDATE_AGID
	cmd[ 9] = 8;
	cmd[10] = 0x3F | (agid << 6);
	auth_stfu = 0;
	agid = 0;

	if (bdev->scsi(cmd,12,buf,8,1) < 0) {
		bitch(BITCHINFO,"Failed to deauthenticate AGID WTF?!?");
		return 0;
	}

	return 1;
}

#endif //DVD_AUTHENTICATE == DVD_AUTHENTICATE_INDEPENDENT
