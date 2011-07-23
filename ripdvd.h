
#ifndef __RIPDVD_H
#define __RIPDVD_H

#include "config.h"
#include "rippedmap.h"
#include "lsimage.h"
#include "keystore.h"
#include "jarchdvd.h"

void RipDVD(JarchSession *session);
void RipCMI(JarchSession *session);
void GenPOI(JarchSession *session);
void AuthPOI(JarchSession *session);
void DecryptDVD(JarchSession *session);

class ImageDVDCombo {
public:
	RippedMap*		map;
	LargeSplitImage*	dvd;
	KeyStorage*		poi;
public:
	int dvdread(unsigned long sector,unsigned char *buf,int N);
};

int GenPOI_DVDVideo_helper(JarchSession *session,ImageDVDCombo *idc);

#endif

