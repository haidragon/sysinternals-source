//======================================================================
// 
// Ioctlcmd.h
//
// Copyright (C) 1996 Mark Russinovich
//
// Common header file for device driver and GUI. Contains common
// data structure definitions and IOCTL command codes.
//
//======================================================================

// Define the various device type values.  Note that values used by Microsoft
// Corporation are in the range 0-32767, and 32768-65535 are reserved for use
// by customers.
#define FILE_DEVICE_SECDEMO     0x00008335

#define SECDEMO_IOCTL (ULONG) CTL_CODE( FILE_DEVICE_SECDEMO, 0x00, METHOD_BUFFERED, FILE_ANY_ACCESS )




