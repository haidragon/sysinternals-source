//======================================================================
//
// STATS.h - include file for VxD VCMON
//
// Copyright (c) 1996 Mark Russinovich and Bryce Cogswell
//
// Contains the definition for the VxD statistics data structure. This
// file is shared by the GUI and the VxD.
//
//======================================================================

// number of milliseconds between statistical updates
#define STATSFREQ        500

//----------------------------------------------------------------------
//
// W32 IOCTL command definitions
//
//----------------------------------------------------------------------
#define VCMON_STATUS      1
#define VCMON_SETRATE     2

//
// Data structure that contains all the statistics that we
// pass to the gui for it to print out
typedef struct {
    DWORD    free;
    DWORD    size;
    DWORD    hits;
    DWORD    misses;
    DWORD    holds;
    DWORD    new;
    DWORD    repmisses;
    DWORD    rephits;
    DWORD    pagereads;
    DWORD    pagewrites;
} VcmonStatus_s;

//----------------------------------------------------------------------
//
// Externs
//
//----------------------------------------------------------------------
extern VcmonStatus_s  Stats;


