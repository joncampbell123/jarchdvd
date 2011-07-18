
#ifndef __RIPPEDMAP_H
#define __RIPPEDMAP_H

#include "config.h"

class RippedMap {
public:
	RippedMap();
	~RippedMap();
public:
	int		open(const char *name);
	int		close();
	int		getsize();
	int		set(int N,int val);
	int		get(int N);
private:
	int		fd;
	int		file_size;
	int		bitmap_size;
};

void DoTestRippedMap();

#endif

