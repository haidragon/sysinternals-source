//======================================================================
// 
// Ioctlcmd.h
//
// Copyright (C) 1996, 1998 Mark Russinovich 
//
// Common header file for device driver and GUI. Contains common
// data structure definitions and IOCTL command codes.
//
//======================================================================

//
// Define the various device type values.  Note that values used by Microsoft
// Corporation are in the range 0-32767, and 32768-65535 are reserved for use
// by customers.
//
#define FILE_DEVICE_TOKENMON      0x00008400

//
// Token Monitor version
//
#define TOKENMONVERSION           100

//
// Commands that the GUI can send the device driver
//
#define IOCTL_TOKENMON_VERSION      (ULONG) CTL_CODE( FILE_DEVICE_TOKENMON, 0x00, METHOD_BUFFERED, FILE_ANY_ACCESS )
#define IOCTL_TOKENMON_ZEROSTATS    (ULONG) CTL_CODE( FILE_DEVICE_TOKENMON, 0x03, METHOD_BUFFERED, FILE_ANY_ACCESS )
#define IOCTL_TOKENMON_GETSTATS     (ULONG) CTL_CODE( FILE_DEVICE_TOKENMON, 0x04, METHOD_NEITHER,  FILE_ANY_ACCESS )
#define IOCTL_TOKENMON_SETFILTER    (ULONG) CTL_CODE( FILE_DEVICE_TOKENMON, 0x05, METHOD_BUFFERED, FILE_ANY_ACCESS )
#define IOCTL_TOKENMON_STOPFILTER   (ULONG) CTL_CODE( FILE_DEVICE_TOKENMON, 0x07, METHOD_BUFFERED, FILE_ANY_ACCESS )
#define IOCTL_TOKENMON_STARTFILTER  (ULONG) CTL_CODE( FILE_DEVICE_TOKENMON, 0x08, METHOD_BUFFERED, FILE_ANY_ACCESS )

#pragma pack(4)

//
// Format of an output buffer entry
//
typedef struct {
	ULONG           seq;
	LARGE_INTEGER   time;
    LARGE_INTEGER   perfTime;
	WCHAR	        text[0];
} ENTRY, *PENTRY;

//
// Length of a filter definition string
//
#define MAXFILTERLEN 256

//
// Filter definition
//
typedef struct {
    WCHAR     includefilter[MAXFILTERLEN];
	WCHAR     excludefilter[MAXFILTERLEN];
} FILTER, *PFILTER;

//
// Definition of structure used to pass system call
// indexes from user space to the driver.
//
typedef struct {
    ULONG     NtCreateTokenIndex;
    ULONG     NtAdjustPrivilegesTokenIndex;
    ULONG     NtSetInformationThreadIndex;
    ULONG     NtImpersonateClientOfPortIndex;
    ULONG     NtFsControlFileIndex;
	ULONG	  NtQueryInformationTokenIndex;
} SYSCALL_INDEX, *PSYSCALL_INDEX;

#pragma pack()

//
// Define page size for use by GUI
//
#ifndef PAGE_SIZE
#if defined(_ALPHA_)
#define PAGE_SIZE 0x2000  // 8K
#else
#define PAGE_SIZE 0x1000  // 4K
#endif
#endif

//
// An allocation unit size 
//
#define MAX_STORE	(PAGE_SIZE*16 - 8)

//
// Max length of domain plus user name
//
#define MAX_NAME_LENGTH    1024




