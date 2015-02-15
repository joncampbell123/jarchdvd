
#include "config.h"
#include "css-auth.h"
#include "css-cipher.h"
#include "css-tables.h"
#include <string.h>

/*****************************************************************************
 * DecryptKey: decrypt p_crypted with p_key.
 *****************************************************************************
 * Used to decrypt the disc key, with a player key, after requesting it
 * in _dvdcss_disckey and to decrypt title keys, with a disc key, requested
 * in _dvdcss_titlekey.
 * The player keys and the resulting disc key are only used as KEKs
 * (key encryption keys).
 * Decryption is slightly dependant on the type of key:
 *  -for disc key, invert is 0x00,
 *  -for title key, invert if 0xff.
 *****************************************************************************/
void DecryptKey(unsigned char invert,unsigned char const *p_key,unsigned char const *p_crypted,unsigned char *p_result)
{
    unsigned int    i_lfsr1_lo;
    unsigned int    i_lfsr1_hi;
    unsigned int    i_lfsr0;
    unsigned int    i_combined;
    unsigned char         o_lfsr0;
    unsigned char         o_lfsr1;
    unsigned char         k[5];
    int             i;

    i_lfsr1_lo = p_key[0] | 0x100;
    i_lfsr1_hi = p_key[1];

    i_lfsr0    = ( ( p_key[4] << 17 )
                 | ( p_key[3] << 9 )
                 | ( p_key[2] << 1 ) )
                 + 8 - ( p_key[2] & 7 );
    i_lfsr0    = ( p_css_tab4[i_lfsr0 & 0xff] << 24 ) |
                 ( p_css_tab4[( i_lfsr0 >> 8 ) & 0xff] << 16 ) |
                 ( p_css_tab4[( i_lfsr0 >> 16 ) & 0xff] << 8 ) |
                   p_css_tab4[( i_lfsr0 >> 24 ) & 0xff];

    i_combined = 0;
    for (i=0;i < 5;i++)
    {
        o_lfsr1     = p_css_tab2[i_lfsr1_hi] ^ p_css_tab3[i_lfsr1_lo];
        i_lfsr1_hi  = i_lfsr1_lo >> 1;
        i_lfsr1_lo  = ( ( i_lfsr1_lo & 1 ) << 8 ) ^ o_lfsr1;
        o_lfsr1     = p_css_tab4[o_lfsr1];

        o_lfsr0 = ((((((( i_lfsr0 >> 8 ) ^ i_lfsr0 ) >> 1 )
                        ^ i_lfsr0 ) >> 3 ) ^ i_lfsr0 ) >> 7 );
        i_lfsr0 = ( i_lfsr0 >> 8 ) | ( o_lfsr0 << 24 );

        i_combined += ( o_lfsr0 ^ invert ) + o_lfsr1;
        k[i] = i_combined & 0xff;
        i_combined >>= 8;
    }

    p_result[4] = k[4] ^ p_css_tab1[p_crypted[4]] ^ p_crypted[3];
    p_result[3] = k[3] ^ p_css_tab1[p_crypted[3]] ^ p_crypted[2];
    p_result[2] = k[2] ^ p_css_tab1[p_crypted[2]] ^ p_crypted[1];
    p_result[1] = k[1] ^ p_css_tab1[p_crypted[1]] ^ p_crypted[0];
    p_result[0] = k[0] ^ p_css_tab1[p_crypted[0]] ^ p_result[4];

    p_result[4] = k[4] ^ p_css_tab1[p_result[4]] ^ p_result[3];
    p_result[3] = k[3] ^ p_css_tab1[p_result[3]] ^ p_result[2];
    p_result[2] = k[2] ^ p_css_tab1[p_result[2]] ^ p_result[1];
    p_result[1] = k[1] ^ p_css_tab1[p_result[1]] ^ p_result[0];
    p_result[0] = k[0] ^ p_css_tab1[p_result[0]];

    return;
}

/*****************************************************************************
 * DecryptDiscKey
 *****************************************************************************
 * Decryption of the disc key with player keys if they are available.
 * Try to decrypt the disc key from every position with every player key.
 * p_struct_disckey: the 2048 byte DVD_STRUCT_DISCKEY data
 * p_disc_key: result, the 5 byte disc key
 *****************************************************************************/
int DecryptDiscKey( unsigned char const *p_struct_disckey, unsigned char *p_disc_key)
{
    unsigned char p_verify[5];
    unsigned int i, n = 0;

    typedef unsigned char dvd_key_t[5];
    static const dvd_key_t player_keys[] =
    {
        { 0x01, 0xaf, 0xe3, 0x12, 0x80 },
        { 0x12, 0x11, 0xca, 0x04, 0x3b },
        { 0x14, 0x0c, 0x9e, 0xd0, 0x09 },
        { 0x14, 0x71, 0x35, 0xba, 0xe2 },
        { 0x1a, 0xa4, 0x33, 0x21, 0xa6 },
        { 0x26, 0xec, 0xc4, 0xa7, 0x4e },
        { 0x2c, 0xb2, 0xc1, 0x09, 0xee },
        { 0x2f, 0x25, 0x9e, 0x96, 0xdd },
        { 0x33, 0x2f, 0x49, 0x6c, 0xe0 },
        { 0x35, 0x5b, 0xc1, 0x31, 0x0f },
        { 0x36, 0x67, 0xb2, 0xe3, 0x85 },
        { 0x39, 0x3d, 0xf1, 0xf1, 0xbd },
        { 0x3b, 0x31, 0x34, 0x0d, 0x91 },
        { 0x45, 0xed, 0x28, 0xeb, 0xd3 },
        { 0x48, 0xb7, 0x6c, 0xce, 0x69 },
        { 0x4b, 0x65, 0x0d, 0xc1, 0xee },
        { 0x4c, 0xbb, 0xf5, 0x5b, 0x23 },
        { 0x51, 0x67, 0x67, 0xc5, 0xe0 },
        { 0x53, 0x94, 0xe1, 0x75, 0xbf },
        { 0x57, 0x2c, 0x8b, 0x31, 0xae },
        { 0x63, 0xdb, 0x4c, 0x5b, 0x4a },
        { 0x7b, 0x1e, 0x5e, 0x2b, 0x57 },
        { 0x85, 0xf3, 0x85, 0xa0, 0xe0 },
        { 0xab, 0x1e, 0xe7, 0x7b, 0x72 },
        { 0xab, 0x36, 0xe3, 0xeb, 0x76 },
        { 0xb1, 0xb8, 0xf9, 0x38, 0x03 },
        { 0xb8, 0x5d, 0xd8, 0x53, 0xbd },
        { 0xbf, 0x92, 0xc3, 0xb0, 0xe2 },
        { 0xcf, 0x1a, 0xb2, 0xf8, 0x0a },
        { 0xec, 0xa0, 0xcf, 0xb3, 0xff },
        { 0xfc, 0x95, 0xa9, 0x87, 0x35 }
    };

    /* Decrypt disc key with the above player keys */
    while( n < sizeof(player_keys) / sizeof(dvd_key_t) )
    {
        for( i = 1; i < 409; i++ )
        {
            /* Check if player key n is the right key for position i. */
            DecryptKey( 0, player_keys[n], p_struct_disckey + 5 * i,
                        p_disc_key );

            /* The first part in the struct_disckey block is the
             * 'disc key' encrypted with itself.  Using this we
             * can check if we decrypted the correct key. */
            DecryptKey( 0, p_disc_key, p_struct_disckey, p_verify );

            /* If the position / player key pair worked then return. */
            if( memcmp( p_disc_key, p_verify, 5 ) == 0 )
            {
                return 0;
            }
        }
        n++;
    }

    /* Have tried all combinations of positions and keys,
     * and we still didn't succeed. */
    memset( p_disc_key, 0, 5 );
    return -1;
}

void CSS_unscramble(unsigned char *sect,unsigned char *key)
{
	unsigned int i_t1,i_t2,i_t3,i_t4,i_t5,i_t6;
	unsigned char *end = sect + 2048;

	i_t1 =		(key[0] ^ sect[0x54]) | 0x100;
	i_t2 =		 key[1] ^ sect[0x55];
	i_t3 =		(key[2] | (key[3] << 8) | (key[4] << 16)) ^
			(sect[0x56] | (sect[0x57] << 8) | (sect[0x58] << 16));
	i_t4 =		i_t3 & 7;
	i_t3 =		i_t3 * 2 + 8 - i_t4;
	sect +=		0x80;
	i_t5 =		0;

	while (sect < end) {
		i_t4   =	p_css_tab2[i_t2] ^ p_css_tab3[i_t1];
		i_t2   =	i_t1>>1;
		i_t1   =	((i_t1&1) << 8) ^ i_t4;
		i_t4   =	p_css_tab5[i_t4];
		i_t6   =	(((((((i_t3 >> 3) ^ i_t3) >> 1) ^ i_t3) >> 8) ^ i_t3) >> 5) & 0xFF;
		i_t3   =	(i_t3 << 8) | i_t6;
		i_t6   =	p_css_tab4[i_t6];
		i_t5  +=	i_t6 + i_t4;
		*sect  =	p_css_tab1[*sect] ^ (i_t5 & 0xFF); sect++;
		i_t5 >>=	8;
	}
}

