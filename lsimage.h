
#ifndef __LSIMAGE_H
#define __LSIMAGE_H

#include "config.h"

class LargeSplitImage {
public:
	class frag {
	public:
		frag();
		~frag();
	public:
		int		fd;
	};
public:
	LargeSplitImage();
	~LargeSplitImage();
public:
	int		open(char *basename);
	int		close();
	juint64		seek(juint64 ofs);
	int		read(unsigned char *buf,int l);
	int		write(unsigned char *buf,int l);
private:
	int		update_max();
	juint64		current_pos;
	juint64		max_pos;
	unsigned long	fragsize;
	int		no_fragments;
	int		iso_fd;
	char*		bname;
	int		sitrks;
	frag**		sitr;
	int		fd;
};

#endif //__LSIMAGE_H

