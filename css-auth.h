typedef unsigned char byte;
struct block {
	byte	b[5];
};

extern void CryptKey1(int varient, byte const *challenge, struct block *key);
extern void CryptKey2(int varient, byte const *challenge, struct block *key);
extern void CryptBusKey(int varient, byte const *challenge, struct block *key);
