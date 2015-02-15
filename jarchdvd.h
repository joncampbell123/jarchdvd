
#ifndef __MAIN_H
#define __MAIN_H

#include "config.h"
#include "rippedmap.h"
#include "blockio.h"
#include "dvd-auth.h"

class JarchSession {
public:
	RippedMap*	todo;
	JarchBlockIO*	bdev;
	DVDAuth*	dvdauth;
// option switches set by user
	int		chosen_force_info;
	int		chosen_force_rip;
	int		rip_noskip;
	double		rip_assume_rate;
	int		rip_cmi;
	int		force_genpoi;
	int		force_authpoi;
	int		skip_rip;
	int		css_decrypt_inplace;
	int		poi_dumb;
	int		rip_backwards;
	int		rip_backwards_from_outermost;
	int		singlesector;
	int		rip_expandfill;
	int		rip_periodic;
	int		dumbpoi_interval;
// does the blockio object support direct SCSI commands?
	int		bdev_scsi;
// set by media gathering code
	int		DVD_layers;
// set by DVD ripping code
	unsigned long	DVD_capacity;
};

#endif //__MAIN_H

