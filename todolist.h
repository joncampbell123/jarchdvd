
#ifndef __TODO_LIST_H
#define __TODO_LIST_H

/* these represent bits in a TODO list maintained by the main loop.
 * the TODO list prevents JarchDVD2 from needlessly performing the
 * same operations over and over again everytime it is instructed
 * to rip a DVD. */

/* main loop TODO items */
#define TODO_MAIN_GATHER_MEDIA_INFO		0

/* mediagather.cpp */
#define TODO_MEDIAGATHER_DISC_INFO		100
#define TODO_MEDIAGATHER_DVD_STRUCT_00		101
#define TODO_MEDIAGATHER_DVD_STRUCT_01		102
#define TODO_MEDIAGATHER_DVD_STRUCT_03		103
#define TODO_MEDIAGATHER_DVD_STRUCT_04		104
#define TODO_MEDIAGATHER_DVD_STRUCT_08		105
#define TODO_MEDIAGATHER_DVD_STRUCT_09		106
#define TODO_MEDIAGATHER_DVD_STRUCT_0A		107

#define TODO_MEDIAGATHER_DVD_LAYERS		200	// 8 bits starting here, number of layers

/* ripdvd.cpp */
#define TODO_RIPDVD				300	// DVD ripping done
#define TODO_RIPDVD_READCAPACITY		301	// has obtained DVD-ROM capacity
#define TODO_RIPDVD_CAPACITY_DWORD		350	// 32 bits starting here, number of sectors on DVD

#define TODO_RIPCMI				380

#define TODO_GENPOI				400
#define TODO_GENPOI_DUMB			401
#define TODO_GENPOI_DVDVIDEO			402
#define TODO_AUTHPOI				410

#endif

