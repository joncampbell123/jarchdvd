
#ifndef __DVDAUTH_H
#define __DVDAUTH_H

#include "config.h"
#include "blockio.h"
#include "css-cipher.h"
#include "css-auth.h"
#include "dvd-auth.h"
#include "bitchin.h"

class DVDAuth {
public:
	DVDAuth(JarchBlockIO *dev);
	~DVDAuth();
public:
	int			Authenticate(int title,juint64 LBA);	// authenticate
	int			DeAuthenticate();			// undo authentication
	int			IsAuthenticated();			// are we authenticated?
private:
	int			authdrive();
	int			GetDiscKey(int agid,unsigned char *key);
	int			GetTitleKey(int agid,juint64 lba,unsigned char *key);
public:
	JarchBlockIO*		bdev;
	unsigned char		RawTitleKey[8];
	unsigned char		TitleKey[5];
	unsigned char		DiscKey[5];
	unsigned char		BusKey[5];
	unsigned char		LUkey1[5];
	unsigned char		Challenge[10];
	int			varient;
	int			dvd_auth_good;
	int			have_bus_key;
	int			have_disc_key;
	int			agid;
	block			Key1;
	block			Key2;
	block			KeyCheck;
	char			auth_stfu;
};

#endif //__DVDAUTH_H

