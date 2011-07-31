
#ifndef __MAIN_H
#define __MAIN_H

#include "config.h"
#include "rippedmap.h"
#include "blockio.h"

class JarchSession {
public:
	RippedMap*	todo;
	JarchBlockIO*	bdev;
// option switches set by user
	int		chosen_force_info;
	int		chosen_force_rip;
	int		rip_subchannel;
	int		rip_noskip;
	double		rip_assume_rate;
	int		skip_rip;
	int		poi_dumb;
	int		rip_verify;
	int		rip_verify_by_reading;
	int		rip_backwards;
	int		rip_backwards_from_outermost;
	int		singlesector;
	int		rip_expandfill;
	int		rip_periodic;
// does the blockio object support direct SCSI commands?
	int		bdev_scsi;
// set by DVD ripping code
	unsigned long	CD_capacity;
};

#endif //__MAIN_H

