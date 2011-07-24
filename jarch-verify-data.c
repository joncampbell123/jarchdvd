#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

const unsigned char zero4[4] = {0,0,0,0};

unsigned char sector[2352];

static uint32_t get32lsb(const uint8_t* src) {
    return
        (((uint32_t)(src[0])) <<  0) |
        (((uint32_t)(src[1])) <<  8) |
        (((uint32_t)(src[2])) << 16) |
        (((uint32_t)(src[3])) << 24);
}

static void put32lsb(uint8_t* dest, uint32_t value) {
    dest[0] = (uint8_t)(value      );
    dest[1] = (uint8_t)(value >>  8);
    dest[2] = (uint8_t)(value >> 16);
    dest[3] = (uint8_t)(value >> 24);
}

////////////////////////////////////////////////////////////////////////////////
//
// LUTs used for computing ECC/EDC
//
static uint8_t  ecc_f_lut[256];
static uint8_t  ecc_b_lut[256];
static uint32_t edc_lut  [256];

static void eccedc_init(void) {
    size_t i;
    for(i = 0; i < 256; i++) {
        uint32_t edc = i;
        size_t j = (i << 1) ^ (i & 0x80 ? 0x11D : 0);
        ecc_f_lut[i] = j;
        ecc_b_lut[i ^ j] = i;
        for(j = 0; j < 8; j++) {
            edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001 : 0);
        }
        edc_lut[i] = edc;
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// Check ECC block (either P or Q)
// Returns true if the ECC data is an exact match
//
static int8_t ecc_checkpq(
    const uint8_t* address,
    const uint8_t* data,
    size_t major_count,
    size_t minor_count,
    size_t major_mult,
    size_t minor_inc,
    const uint8_t* ecc
) {
    size_t size = major_count * minor_count;
    size_t major;
    for(major = 0; major < major_count; major++) {
        size_t index = (major >> 1) * major_mult + (major & 1);
        uint8_t ecc_a = 0;
        uint8_t ecc_b = 0;
        size_t minor;
        for(minor = 0; minor < minor_count; minor++) {
            uint8_t temp;
            if(index < 4) {
                temp = address[index];
            } else {
                temp = data[index - 4];
            }
            index += minor_inc;
            if(index >= size) { index -= size; }
            ecc_a ^= temp;
            ecc_b ^= temp;
            ecc_a = ecc_f_lut[ecc_a];
        }
        ecc_a = ecc_b_lut[ecc_f_lut[ecc_a] ^ ecc_b];
        if(
            ecc[major              ] != (ecc_a        ) ||
            ecc[major + major_count] != (ecc_a ^ ecc_b)
        ) {
            return 0;
        }
    }
    return 1;
}

//
// Check ECC P and Q codes for a sector
// Returns true if the ECC data is an exact match
//
static int8_t ecc_checksector(
    const uint8_t *address,
    const uint8_t *data,
    const uint8_t *ecc
) {
    return
        ecc_checkpq(address, data, 86, 24,  2, 86, ecc) &&      // P
        ecc_checkpq(address, data, 52, 43, 86, 88, ecc + 0xAC); // Q
}

////////////////////////////////////////////////////////////////////////////////
//
// Compute EDC for a block
//
static uint32_t edc_compute(
    uint32_t edc,
    const uint8_t* src,
    size_t size
) {
    for(; size; size--) {
        edc = (edc >> 8) ^ edc_lut[(edc ^ (*src++)) & 0xFF];
    }
    return edc;
}

int main(int argc,char **argv) {
	int video = 0,video2 = 0;
	unsigned long sec,chk;
	char name[32];
	int ofd = -1;

	sec = 0UL - 1UL;
	eccedc_init();
	while (read(0,sector,sizeof(sector)) == sizeof(sector)) {
		sec++;
		if (!memcmp(sector,"\x00" "\xFF\xFF\xFF\xFF\xFF" "\xFF\xFF\xFF\xFF\xFF" "\x00",12)) {
			/* is this mode 1? */
			if ((sector[15] & 3) == 1) {
				/* does it check out like one? */
				chk = edc_compute(0,sector,2048+16);
				if (chk != get32lsb(sector+2048+16)) {
					fprintf(stderr,"Sector %lu [Mode1]: EDC checksum failed. 0x%08lx != 0x%08lx\n",sec,chk,get32lsb(sector+2048+16));
					continue;
				}
				if (!ecc_checksector(sector+0xC,sector+0x10,sector+0x81C)) {
					fprintf(stderr,"Sector %lu [Mode1]: ECC check failed\n",sec);
					continue;
				}
			}
			/* is this mode 2? */
			else if ((sector[15] & 3) == 2) {
				if (sector[16+2] & 0x20) { /* FORM 2 */
					/* does it check out like one? */
					chk = edc_compute(0,sector+16,2332);
					if (chk != get32lsb(sector+16+2332)) {
						fprintf(stderr,"Sector %lu [Mode2Form2]: EDC checksum failed. 0x%08lx != 0x%08lx\n",sec,chk,get32lsb(sector+16+2332));
						continue;
					}
				}
				else { /* FORM 1 */
					/* does it check out like one? */
					chk = edc_compute(0,sector+16,2048+8);
					if (chk != get32lsb(sector+16+2048+8)) {
						fprintf(stderr,"Sector %lu [Mode2Form1]: EDC checksum failed. 0x%08lx != 0x%08lx\n",sec,chk,get32lsb(sector+16+2048+8));
						continue;
					}
					if (!ecc_checksector(zero4,sector+16,sector+16+0x80C)) {
						fprintf(stderr,"Sector %lu [Mode2Form1]: ECC check failed\n",sec);
						continue;
					}
				}
			}
		}
	}

	if (ofd >= 0) close(ofd);
	return 0;
}

