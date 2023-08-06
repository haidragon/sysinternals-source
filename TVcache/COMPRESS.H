//======================================================================
//
// COMPRESS.h - include file for VxD RAMDBLR
//
// Copyright (c) 1995 Mark Russinovich and Bryce Cogswell
//
// Contains externs for compress.c.
//
//======================================================================

//======================================================================
//
//                                DEFINES
//
//======================================================================

// this is the amount we have to compress by to consider it worth while
#define MIN_COMPRESSION_SAVINGS   100 // bytes

#if TESTING
#define LOCKED_CODE_SEGMENT  <stdio.h>
#define LOCKED_DATA_SEGMENT	<stdio.h>
#define PAGESIZE  4096
#endif // TESTING

//======================================================================
//
//                                EXTERNS
//
//======================================================================

extern DWORD Compress( PBYTE inbuf, int olen, PBYTE outbuf, 
		      BOOL init );
extern DWORD Decompress( PBYTE inbuf_orig, int olen, PBYTE outbuf,
			BOOL init );
extern DWORD SafeCompress( PBYTE inbuf, PBYTE outbuf );
