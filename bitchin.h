
#ifndef __INCLUDE_BITCHIN_H
#define __INCLUDE_BITCHIN_H

#include <stdio.h>

enum {
	BITCHERROR=1,
	BITCHWARNING,
	BITCHINFO,
};

void bitch(int CLASS,const char *fmt,...);
void bitch_init(FILE *outfp);
void bitch_indent();
void bitch_unindent();
void bitch_cwd();

#endif //__INCLUDE_BITCHIN_H

