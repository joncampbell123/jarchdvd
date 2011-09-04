#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
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

int main(int argc,char **argv) {
	unsigned char sector[2048];
	int rd;

	if (isatty(0) || isatty(1)) {
		fprintf(stderr,"Hey! You're supposed to pipe the decrypted ISO into my STDIN, and pipe my STDOUT to a file or other program prepared to handle VOB data\n");
		return 1;
	}

	while (force_read(0/*stdin*/,sector,2048) == 2048) {
		if (!memcmp(sector,"\x00\x00\x01\xBA",4))
			force_write(1/*stdout*/,sector,2048);
	}

	return 0;
}

