
#ifndef __KEYSTORE_H
#define __KEYSTORE_H

#include "bitchin.h"

// C++ class to manage DVD CSS keys in a binary tree in a file
class KeyStorage
{
public:
	// format of key data:
	//
	// byte 0: CPM/CP_SEC/CGMS/CP_MOD bits
	// bytes 1-5: title key (decrypted with bus key then stored in database)
	// bytes 6-7: reserved
	//
	// See MMC-5 standard for details
	typedef struct {
		unsigned int	left,right;
		unsigned int	sector;
		unsigned char	key[8];
	} Node;
public:
	KeyStorage();
	~KeyStorage();
public:
	int		open(const char *name);
	int		close();
	int		addkey(unsigned long sector,unsigned char *key);	// where key is 8 bytes long
	int		setroot(unsigned int N);
	unsigned int	getroot();
	int		clearblock(unsigned int N);
	int		readkey(unsigned int N,Node *nd);
	int		writekey(unsigned int N,Node *nd);
	int		readmin(Node *n);
	int		readmax(Node *n);
	int		lookup(unsigned int N,unsigned char *key);
	int		dumptree();
	int		enumtree_helper(unsigned int N,int (*enumf)(unsigned long s,unsigned char *k,unsigned long N));
	int		enumtree(int (*enumf)(unsigned long sector,unsigned char *key,unsigned long N));
public: // should be private but are left public for utility use/testing
	int		readblock(unsigned int N,unsigned char *block);
	int		writeblock(unsigned int N,unsigned char *block);
	int		getblocksize();
public:
	int		lookup_helper(unsigned int root,unsigned int N,unsigned char *key);
	int		dumptree_helper(unsigned int N);
	int		fd;
	unsigned int	max_block;
	unsigned int	blocksize;
	unsigned int	root;
};

void DoTestKeyStore();

#endif

