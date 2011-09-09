#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>

int force_read(int fd,unsigned char *buf,int sz) {
	int got=0,rd;

	while (sz > 0) {
		rd = read(fd,buf,sz);
		if (rd <= 0) break;
		got += rd;
		buf += rd;
		sz -= rd;
	}

	return got;
}

int force_write(int fd,unsigned char *buf,int sz) {
	int got=0,rd;

	while (sz > 0) {
		rd = write(fd,buf,sz);
		if (rd <= 0) break;
		got += rd;
		buf += rd;
		sz -= rd;
	}

	return got;
}

#define SECTORS 64
static unsigned char whole_sector[2048*SECTORS];
static unsigned char output[2048*SECTORS];

int main(int argc,char **argv) {
	unsigned long long offset=0;
	unsigned char *sector,*out;
	int rd,i;

	if (isatty(0) || isatty(1)) {
		fprintf(stderr,"Hey! You're supposed to pipe the decrypted ISO into my STDIN, and pipe my STDOUT to a file or other program prepared to handle VOB data\n");
		return 1;
	}

	while ((rd=force_read(0/*stdin*/,whole_sector,2048*SECTORS)) >= 2048) {
		sector = whole_sector;
		out = output;
		for (i=0;i < rd;i += 2048,sector += 2048) {
			if (((offset >> 11ULL) & 0xFFF) == 0) {
				fprintf(stderr,"\x0D" "%llu     ",offset);
				fflush(stderr);
			}

			if (!memcmp(sector,"\x00\x00\x01\xBA",4)) {
				memcpy(out,sector,2048);
				out += 2048;
			}

			offset += 2048ULL;
		}

		if (out != output)
			force_write(1/*stdout*/,output,(size_t)(out - output));

		if ((rd % 2048) != 0)
			break;
	}

	fprintf(stderr,"\x0D" "                      \n");
	return 0;
}

