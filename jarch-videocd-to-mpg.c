#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

unsigned char sector[2352];

int main(int argc,char **argv) {
	int video = 0,video2 = 0;
	unsigned long sec;
	char name[32];
	int ofd = -1;

	while (read(0,sector,sizeof(sector)) == sizeof(sector)) {
		if (!memcmp(sector,"\x00" "\xFF\xFF\xFF\xFF\xFF" "\xFF\xFF\xFF\xFF\xFF" "\x00",12)) {
			/* is this mode 2? */
			if ((sector[15] & 3) == 2) {
				if ((sector[16+2] & 0x40) && (sector[16+2] & 0x20)) { /* must be "realtime block" and mode 2 form 2 */
					if (sector[16+2] & 0x06) { /* if a video or audio block, start rippin */
						if (sector[16] != 0) {
							if (video != sector[16] || video2 != sector[17]) {
								if (ofd >= 0) close(ofd);
								sprintf(name,"VideoCD.%u.%u.mpg",sector[16],sector[17]);
								ofd = open(name,O_WRONLY | O_CREAT,0644);
								if (ofd >= 0) lseek(ofd,0,SEEK_END);
								video2 = sector[17];
								video = sector[16];
							}
						}
					}

					if (ofd >= 0)
						write(ofd,sector+24,2324);
				}
			}
		}
	}

	if (ofd >= 0) close(ofd);
	return 0;
}

