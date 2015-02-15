
#include "config.h"
#include "keystore.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

static const char* KeyHeader = "JKeys100";

KeyStorage::KeyStorage()
{
	fd = -1;
	max_block = 0;
}

KeyStorage::~KeyStorage()
{
	close();
}

int KeyStorage::open(const char *name)
{
	unsigned char b[16];
	int sz,r;

	/* close key store if already open */
	close();

	bitch(BITCHINFO,"Opening key storage %s",name);
	bitch_indent();

	fd = ::open(name,O_RDWR | O_BINARY,0644);
	if (fd < 0) {
		if (errno == ENOENT) {
			bitch(BITCHINFO,"File does not exist, creating key storage");
			fd = ::open(name,O_RDWR | O_CREAT | O_BINARY,0644);
			if (fd < 0) {
				bitch(BITCHWARNING,"Cannot create key storage!");
				goto common_exit;
			}

			if (lseek(fd,0,SEEK_SET) != 0) {
				bitch(BITCHWARNING,"Cannot lseek(0) (is this even a file?)");
				goto common_exit_fd;
			}

			// header
			write(fd,KeyHeader,8);

			// root node 0
			write(fd,LeHe32bin(0),4);

			// block size 24
			write(fd,LeHe32bin(24),4);
		}
		else {
			bitch(BITCHWARNING,"Cannot open key storage (%s)",strerror(errno));
			goto common_exit;
		}
	}

	if (lseek(fd,0,SEEK_SET) != 0) {
		bitch(BITCHWARNING,"Cannot lseek(0) (is this even a file?)");
		goto common_exit_fd;
	}

	sz = lseek(fd,0,SEEK_END);
	if (sz < 16) {
		bitch(BITCHWARNING,"File too small or error seeking to end");
		goto common_exit_fd;
	}

	if (lseek(fd,0,SEEK_SET) != 0) {
		bitch(BITCHWARNING,"Cannot lseek(0) second time");
		goto common_exit_fd;
	}

	if ((r=read(fd,b,16)) < 16) {
		bitch(BITCHWARNING,"Cannot read header (only got %u bytes)",r);
		goto common_exit_fd;
	}

	if (memcmp(b,KeyHeader,8)) {
		bitch(BITCHWARNING,"Invalid key storage header");
		goto common_exit_fd;
	}

	root =		binLeHe32(b +  8);
	blocksize =	binLeHe32(b + 12);

	if (blocksize < 24 || blocksize > 512) {
		bitch(BITCHWARNING,"Illegal block size %u",blocksize);
		goto common_exit_fd;
	}

	max_block = (sz - 16) / blocksize;
	bitch(BITCHINFO,"Key storage has %u blocks, root at block %u, %u bytes/block",max_block,root,blocksize);

	if (	(root >= max_block && max_block != 0) ||
		(root != 0         && max_block == 0)) {
		bitch(BITCHWARNING,"Illegal root node value %u",root);
		goto common_exit_fd;
	}

	bitch_unindent();
	return 0;

common_exit_fd:	::close(fd);
common_exit:	bitch_unindent();
	return -1;
}

int KeyStorage::close()
{
	if (fd >= 0) {
		::close(fd);
		fd = -1;
	}

	return 0;
}

int KeyStorage::getblocksize()
{
	if (fd < 0)
		return -1;

	return blocksize;
}

int KeyStorage::readblock(unsigned int N,unsigned char *buf)
{
	unsigned long x;

	if (fd < 0) return -1;
	if (N >= max_block) return 0;

	x = 16 + (N * blocksize);
	if (lseek(fd,x,SEEK_SET) != x) return 0;
	if (read(fd,buf,24) < 24) return 0;
	return blocksize;
}

int KeyStorage::writeblock(unsigned int N,unsigned char *buf)
{
	unsigned long x;

	if (fd < 0) return -1;

	x = 16 + (N * blocksize);
	if (lseek(fd,x,SEEK_SET) != x) {
// What we do depends on the OS here...
//
// Linux:              lseek() will not point past the end of the file, and we must
//                     use ftruncate() to extend the file so we can write the block.
//                     
// DOS/Windows:        lseek() DOES allow the pointer to go past the EOF, but does
//                     not actually extend the file until we write(). between the
//                     old EOF and the new block we write to there is the possibility
//                     that old leftover trash will show up (Windows 95 & 98 and FAT)
//                     which fortunately does not matter unless something in the tree
//                     already points to the trash area. So.... if Windows failed
//                     this call there must be something very wrong!
#ifdef LINUX
		if (ftruncate(fd,x) < 0)	return 0;
		if (lseek(fd,x,SEEK_SET) != x)	return 0;
		max_block = N+1;
#else
		return 0;
#endif
	}

// DOS/Windows: lseek() succeeded but it's possible the file is not really that large!
//              Fortunately when we write() Windows will lengthen the file for us to
//              make sure that write() succeeds. But that also means we must update
//              the max_block variable if N >= max_block!
	if (N >= max_block) max_block = N+1;
	if (write(fd,buf,24) < 24) return 0;
	return blocksize;
}

int KeyStorage::setroot(unsigned int N)
{
	if (fd < 0) return -1;
	if (N >= max_block) return -1;
	if (lseek(fd,8,SEEK_SET) != 8) return -1;
	if (write(fd,LeHe32bin(N),4) < 4) return -1;
	root = N;
	return 0;
}

unsigned int KeyStorage::getroot()
{
	return root;
}

int KeyStorage::readkey(unsigned int N,KeyStorage::Node *nd)
{
	unsigned char buffer[24];
	unsigned int x;

	if (readblock(N,buffer) <= 0)
		return -1;

	x = binLeHe32(buffer);
	if (x == 0)
		return -1;
	if (x != 0x12345678) {
		bitch(BITCHWARNING,"KeyStorage::readkey invalid signature 0x%08X for block %u",x,N);
		return -1;
	}

	nd->left =	binLeHe32(buffer +  4);
	nd->right =	binLeHe32(buffer +  8);
	nd->sector =	binLeHe32(buffer + 12);
	memcpy(nd->key,buffer+16,8);
	return 0;
}

int KeyStorage::writekey(unsigned int N,KeyStorage::Node *nd)
{
	unsigned char buffer[24];

	memcpy(buffer +  0,LeHe32bin(0x12345678),	4);
	memcpy(buffer +  4,LeHe32bin(nd->left),		4);
	memcpy(buffer +  8,LeHe32bin(nd->right),	4);
	memcpy(buffer + 12,LeHe32bin(nd->sector),	4);
	memcpy(buffer + 16,nd->key,			8);

	if (writeblock(N,buffer) <= 0)
		return -1;

	return 0;
}

int KeyStorage::addkey(unsigned long sector,unsigned char *key)
{
	KeyStorage::Node n,nu;
	int x,y,z;

// NOTE: -1 is used as nil in this code, even though once within
//       the tree 0 is treated as nil also. The problem is that
//       0 is a valid block number.
	y = -1;
	x = (readkey(root,&n) >= 0) ? root : (-1);
	z = max_block;

	while (x >= 0) {
		y = x;

		if (readkey(x,&n) < 0) {
			bitch(BITCHWARNING,"KeyStorage::addkey......readkey failed for block %u. Sector %u not added to tree",x,sector);
			return -1;
		}

		if (sector < n.sector)	x = n.left;
		else			x = n.right;

		// 0 is treated as nil since no node is allowed to point back to root
		if (x == 0) x = -1;
	}

	nu.left =	0;
	nu.right =	0;
	nu.sector =	sector;
	memcpy(nu.key,key,8);
	if (writekey(z,&nu) < 0) {
		bitch(BITCHWARNING,"KeyStorage::addkey......writeblock failed for block %u. Sector %u was not added to tree",z,sector);
		return -1;
	}

	if (y < 0) {
		if (setroot(z) < 0) {
			bitch(BITCHWARNING,"KeyStorage::addkey......setroot(%u) failed for sector %u",z,sector);
			return -1;
		}
	}
	else {
		if (readkey(y,&n) < 0) {
			bitch(BITCHWARNING,"KeyStorage::addkey......readkey failed for block %u. Sector %u was not added to tree",y,sector);
			return -1;
		}

		if (sector < n.sector)	n.left = z;
		else			n.right = z;

		if (writekey(y,&n) < 0) {
			bitch(BITCHWARNING,"KeyStorage::addkey......writekey failed for block %u. Sector %u was not added",y,sector);
			return -1;
		}
	}

	return 0;
}

int KeyStorage::clearblock(unsigned int N)
{
	unsigned char buffer[24];

	if (fd < 0) return -1;
	memset(buffer,0,24);
	return writeblock(N,buffer);
}

int KeyStorage::readmin(KeyStorage::Node *n)
{
	int x = root,cn;

	do {
		cn = x;
		if (readkey(x,n) < 0) return -1;
		x = n->left;
		if (x == 0) return cn;
	} while (1);

	return -1;
}

int KeyStorage::readmax(KeyStorage::Node *n)
{
	int x = root,cn;

	do {
		cn = x;
		if (readkey(x,n) < 0) return -1;
		x = n->right;
		if (x == 0) return cn;
	} while (1);

	return -1;
}

int KeyStorage::lookup_helper(unsigned int x,unsigned int N,unsigned char *key)
{
	KeyStorage::Node node;
	int r;

	if (readkey(x,&node) < 0)
		return -1;
	if (node.sector == N)
		goto success;

	if (N < node.sector) {
		if (node.left != 0) {
			r = lookup_helper(node.left,N,key);
			return r;
		}
	}
	else {
		if (node.right != 0) {
			r = lookup_helper(node.right,N,key);
			if (r < 0) goto success;
			return r;
		}
		else {
			goto success;
		}
	}

	return -1;
success:
	memcpy(key,node.key,8);
	return node.sector;
}

int KeyStorage::lookup(unsigned int N,unsigned char *key)
{
	return lookup_helper(root,N,key);
}

int KeyStorage::dumptree_helper(unsigned int N)
{
	KeyStorage::Node n;

	if (readkey(N,&n) < 0)
		return -1;

	if (n.left != 0)
		dumptree_helper(n.left);

	bitch(BITCHINFO,"Sector %u: Key %02X %02X %02X %02X %02X %02X %02X %02X",n.sector,
		n.key[0],n.key[1],n.key[2],n.key[3],n.key[4],n.key[5],n.key[6],n.key[7]);

	if (n.right != 0)
		dumptree_helper(n.right);

	return 0;
}

int KeyStorage::dumptree()
{
	if (fd < 0) return -1;
	return dumptree_helper(root);
}

int KeyStorage::enumtree_helper(unsigned int N,int (*enumf)(unsigned long s,unsigned char *k,unsigned long N))
{
	KeyStorage::Node n;

	if (readkey(N,&n) < 0)
		return -1;

	if (n.left != 0)
		enumtree_helper(n.left,enumf);

	enumf(n.sector,n.key,N);

	if (n.right != 0)
		enumtree_helper(n.right,enumf);

	return 0;
}

int KeyStorage::enumtree(int (*enumf)(unsigned long s,unsigned char *k,unsigned long N))
{
	if (fd < 0) return -1;
	return enumtree_helper(root,enumf);
}

typedef struct {
	int		sector;
	unsigned char	key[8];
} STests;

STests	tests[] = {
	{4000,	{0x12,0x34,0x56,0x78,0x9A,0,0,0}	},
	{8001,	{0xAA,0xBB,0xCC,0xDD,0xEE,0,0,0}	},
	{2000,	{0x22,0x33,0x44,0x55,0x66,0,0,0}	},
	{3400,	{0xAB,0xCD,0xEF,0xF0,0xF1,0,0,0}	},
	{-1,	{0x00,0x00,0x00,0x00,0x00,0,0,0}	},
};

// testing mode for KeyStorage class
void DoTestKeyStore()
{
// begin macro that helps reduce redundancy
//
// BEGIN_CONDITION(m)
//   m = a string to pass to bitch();
// END_CONDITION(c)
//   c = condition of test. if false (0) execution jumps to label "finish"
#define BEGIN_CONDITION(m) \
	bitch(BITCHINFO,m); \
	bitch_indent();
#define END_CONDITION(c) \
	if (c) bitch(BITCHINFO,"ok"); \
	else { bitch(BITCHERROR,"failed"); bitch_unindent(); goto finish; } \
	bitch_unindent();
// end macros
	unsigned char buffer[24];
	KeyStorage::Node node;
	KeyStorage *x = NULL;
	int r,i;

	// remove previous instance of test keystore file, if any
	remove("test.keys");

	bitch(BITCHINFO,"*** KeyStorage test ***");
	bitch_indent();

	BEGIN_CONDITION("Allocating object");
	x = new KeyStorage;
	END_CONDITION(x);

	BEGIN_CONDITION("Opening test file");
	r = x->open("test.keys");
	END_CONDITION(r >= 0);

	BEGIN_CONDITION("Creating a dummy root node to test file expansion code in KeyStorage::write");
	memset(buffer,0x4C,24);
	r = x->writeblock(0,buffer);
	END_CONDITION(r > 0);

	BEGIN_CONDITION("Creating another dummy node");
	memset(buffer,0x31,24);
	r = x->writeblock(1,buffer);
	END_CONDITION(r > 0);

	BEGIN_CONDITION("Setting block 0 as root node");
	r = x->setroot(0);
	if (r >= 0) r = (x->getroot() == 0);
	END_CONDITION(r >= 0);

	BEGIN_CONDITION("Setting block 1 as root node");
	r = x->setroot(1);
	if (r >= 0) r = (x->getroot() == 1);
	END_CONDITION(r >= 0);

	// now we must clear the test blocks so that the binary tree code can work properly
	// without panicing about invalid nodes.
	BEGIN_CONDITION("Clearing block 0");
	r = x->clearblock(0);
	END_CONDITION(r >= 0);

	BEGIN_CONDITION("Clearing block 1");
	r = x->clearblock(0);
	END_CONDITION(r >= 0);

	BEGIN_CONDITION("Setting block 0 as root node");
	r = x->setroot(0);
	if (r >= 0) r = (x->getroot() == 0);
	END_CONDITION(r >= 0);

	// now add keys
	for (i=0;tests[i].sector >= 0;i++) {
		BEGIN_CONDITION("Adding test key");
		bitch(BITCHINFO,"Sector %u, key %02X %02X %02X %02X %02X",tests[i].sector,
			tests[i].key[0],	tests[i].key[1],	tests[i].key[2],
			tests[i].key[3],	tests[i].key[4]);
		r = x->addkey(tests[i].sector,tests[i].key);
		END_CONDITION(r >= 0);
	}

	// test the binary tree
	BEGIN_CONDITION("Doing binary search for minimum");
	r = x->readmin(&node);
	bitch(BITCHINFO,"Got node %d, sector %u",r,node.sector);
	END_CONDITION(r >= 0 && node.sector == 2000);

	BEGIN_CONDITION("Doing binary search for maximum");
	r = x->readmax(&node);
	bitch(BITCHINFO,"Got node %d, sector %u",r,node.sector);
	END_CONDITION(r >= 0 && node.sector == 8001);

	BEGIN_CONDITION("Searching for sector 4000");
	r = x->lookup(4000,buffer);
	bitch(BITCHINFO,"Got sector %d, key %02X %02X %02X %02X %02X",r,buffer[0],buffer[1],
		buffer[2],buffer[3],buffer[4]);
	END_CONDITION(r == 4000 && !memcmp(buffer,tests[0].key,5));

	// THIS ONE is SUPPOSED to fail!
	BEGIN_CONDITION("Searching for sector 100");
	r = x->lookup(100,buffer);
	bitch(BITCHINFO,"Got sector %d",r);
	END_CONDITION(r <= 0);

	BEGIN_CONDITION("Searching for sector 2894");
	r = x->lookup(2894,buffer);
	bitch(BITCHINFO,"Got sector %d, key %02X %02X %02X %02X %02X",r,buffer[0],buffer[1],
		buffer[2],buffer[3],buffer[4]);
	END_CONDITION(r == 2000 && !memcmp(buffer,tests[2].key,5));

	BEGIN_CONDITION("Searching for sector 8000");
	r = x->lookup(8000,buffer);
	bitch(BITCHINFO,"Got sector %d, key %02X %02X %02X %02X %02X",r,buffer[0],buffer[1],
		buffer[2],buffer[3],buffer[4]);
	END_CONDITION(r == 4000 && !memcmp(buffer,tests[0].key,5));

	BEGIN_CONDITION("Searching for sector 16385");
	r = x->lookup(16385,buffer);
	bitch(BITCHINFO,"Got sector %d, key %02X %02X %02X %02X %02X",r,buffer[0],buffer[1],
		buffer[2],buffer[3],buffer[4]);
	END_CONDITION(r == 8001 && !memcmp(buffer,tests[1].key,5));

	BEGIN_CONDITION("Searching for sector 3999");
	r = x->lookup(3999,buffer);
	bitch(BITCHINFO,"Got sector %d, key %02X %02X %02X %02X %02X",r,buffer[0],buffer[1],
		buffer[2],buffer[3],buffer[4]);
	END_CONDITION(r == 3400 && !memcmp(buffer,tests[3].key,5));

	BEGIN_CONDITION("Calling object to dump it's tree out");
	r = x->dumptree();
	END_CONDITION(r >= 0);

	bitch(BITCHINFO,"All tests passed, KeyStorage class is safe to use!");

	x->close();
	remove("test.keys");

finish:
	// free object if allocated
	if (x) {
		bitch(BITCHINFO,"Calling KeyStorage::close()");
		x->close();
		bitch(BITCHINFO,"Deleting (freeing) object");
		delete x;
	}
	bitch_unindent();
}

