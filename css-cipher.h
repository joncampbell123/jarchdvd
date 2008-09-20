
#ifndef __CSSCIPHER_H
#define __CSSCIPHER_H

#include "config.h"

void DecryptKey(unsigned char invert,unsigned char const *p_key,unsigned char const *p_crypted,unsigned char *p_result);
int DecryptDiscKey( unsigned char const *p_struct_disckey, unsigned char *p_disc_key);
void CSS_unscramble(unsigned char *sect,unsigned char *key);

#endif //__CSSCIPHER_H

