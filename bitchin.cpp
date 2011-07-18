
#include <sys/stat.h>
#include <sys/types.h>
#ifdef LINUX
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "blockio.h"
#include "bitchin.h"
#include "main.h"

static const char *BITCHCLASSES[] = {
	"",			// DUMMY
	":<ERROR>:  ",
	":<WARNING>:",
	":<INFO>:   ",
	NULL
};

static FILE*	bitchfp = NULL;
static int	bitchlevel = 0;
static char	bitchlevel_sp[128];

void bitch(int CLASS,const char *fmt,...)
{
	va_list va;

	if (CLASS < 0 || CLASS > BITCHINFO)
		CLASS = BITCHERROR;

	va_start(va,fmt);
	
	if (bitchfp) {
		fprintf(bitchfp,"%s%s",BITCHCLASSES[CLASS],bitchlevel_sp);
		vfprintf(bitchfp,fmt,va);
		fprintf(bitchfp,"\n");
		fflush(bitchfp);
	}
	
	/* and to STDOUT */
	printf("%s%s",BITCHCLASSES[CLASS],bitchlevel_sp);
	vprintf(fmt,va);
	printf("\n");
	fflush(stdout);
	
	va_end(va);
}

void bitch_init(FILE *output)
{
	if (output == stdout || output == stderr)
		bitchfp = NULL;
	else
		bitchfp = output;

	bitchlevel = 0;
	memset(bitchlevel_sp,0,sizeof(bitchlevel));
}

void bitch_indent()
{
	int i;

	if (bitchlevel >= 30) {
		bitch(BITCHWARNING,"bitchin.c program error: Someone indented too far without un-indenting!");
		return;
	}

	i = bitchlevel++; i *= 4;
	bitchlevel_sp[i++] = ' ';
	bitchlevel_sp[i++] = ' ';
	bitchlevel_sp[i++] = ' ';
	bitchlevel_sp[i++] = ' ';
	bitchlevel_sp[i  ] = 0;
}

void bitch_unindent()
{
	int i;

	if (bitchlevel <= 0) {
		bitch(BITCHWARNING,"bitchin.c program error: Someone tried to undo indenting at level 0!");
		return;
	}

	i = --bitchlevel; i *= 4;
	bitchlevel_sp[i++] = 0;
	bitchlevel_sp[i++] = 0;
	bitchlevel_sp[i++] = 0;
	bitchlevel_sp[i++] = 0;
}

void bitch_cwd()
{
#ifdef WIN32	// Windows-specific
	// TODO!
#else
# ifdef MACOSX
	// TODO!
# else		// Linux
	char path[256];

	bitch(BITCHINFO,"Ripping to (current working directory) which is:");
	bitch_indent();
	if (getcwd(path,255) != path)	bitch(BITCHWARNING,"Unable to obtain current working directory!");
	else				bitch(BITCHINFO,"%s",path);
	bitch_unindent();
# endif
#endif
}

