
#include "config.h"
#include <stdio.h>
#include "keystore.h"

int main(int argc,char **argv)
{
	KeyStorage k;

	bitch_init(stderr);

	if (argc < 2) {
		printf("%s <JarchDVD key storage file>",argv[0]);
		return 1;
	}

	if (k.open(argv[1]) < 0) {
		printf("Cannot open %s\n",argv[1]);
		return 1;
	}

	k.dumptree();
	k.close();
}

