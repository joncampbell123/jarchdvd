
#include "config.h"

unsigned char ulhbuf[4];

#if ENDIAN == ENDIAN_BE

unsigned short int utilLeHe16(unsigned short int x)
{
	return	  (x >> 8) |
		  (x << 8);
}

unsigned int utilLeHe32(unsigned int x)
{
	return	  (x >> 24) |
		(((x >> 16) & 0xFF) <<  8) |
		(((x >>  8) & 0xFF) << 16) |
		  (x << 24);
}

#endif

unsigned char *utilLeHe32bin(unsigned int x)
{
	ulhbuf[0] = x;
	ulhbuf[1] = x >> 8;
	ulhbuf[2] = x >> 16;
	ulhbuf[3] = x >> 24;
	return ulhbuf;
}

unsigned int utilbinLeHe32(unsigned char *buf)
{
	unsigned int x;

	x  = ((unsigned int)buf[0]);
	x |= ((unsigned int)buf[1]) << 8;
	x |= ((unsigned int)buf[2]) << 16;
	x |= ((unsigned int)buf[3]) << 24;
	return x;
}

unsigned int utilbinBeHe32(unsigned char *buf)
{
	unsigned int x;

	x  = ((unsigned int)buf[3]);
	x |= ((unsigned int)buf[2]) << 8;
	x |= ((unsigned int)buf[1]) << 16;
	x |= ((unsigned int)buf[0]) << 24;
	return x;
}

unsigned short int utilbinLeHe16(unsigned char *buf)
{
	unsigned short int x;

	x  = ((unsigned short int)buf[0]);
	x |= ((unsigned short int)buf[1]) << 8;
	return x;
}

unsigned short int utilbinBeHe16(unsigned char *buf)
{
	unsigned short int x;

	x  = ((unsigned short int)buf[1]);
	x |= ((unsigned short int)buf[0]) << 8;
	return x;
}

char *YesNo(int x)
{
	if (x)	return "yes";
	else	return "no";
}

