//======================================================================
//
// COMPRESS.c - compression routines for ram doubler
//
// Copyright (c) 1995 Mark Russinovich and Bryce Cogswell
//
// This file contains compress/decompress routines used by the RAM
// Doubler. They are the C version of the Dr. Dobb's compression
// contest submission entitled "FIN" converted by Nico E. de Vries.
// The original code, along with other compression sources can be
// found as part of the lds-11.zip compression source libraries on
// garbo.XXX.fin.
//
//======================================================================
#if !TESTING
#include   <vtoolsc.h>
#else  // TESTING
#include   <windows.h>
#endif // TESTING

#include   "compress.h"


//======================================================================
//
//                            LOCKED DATA
//
//======================================================================
#include LOCKED_DATA_SEGMENT

/* table with predictions */
#define HASH(p1,p2)	(p1<<8|p2)
#define HASH_SIZE	16

#if TESTING
BYTE	pcTable[ 1 << HASH_SIZE ];
#endif // TESTING

extern PDWORD SysTime;


//======================================================================
//
//                           LOCKED CODE
//
//======================================================================
#include LOCKED_CODE_SEGMENT 


//---------------------------------------------------------------------
//
// Compress
//
// Takes an input buffer, output buffer and pointer to a WORD to fill
// in with compressed length.
//
//----------------------------------------------------------------------
DWORD Compress( PBYTE inbuf, int inbuf_len, PBYTE outbuf_orig,
	        BOOL init )
{
#if !TESTING
//    DWORD stime = GetTickCount();
    DWORD len;

    len =  ACompress( inbuf, inbuf_len, outbuf_orig, init );

//    Stats.timeCompress += GetTickCount() - stime;
  
    return len;

#else // TESTING

    BYTE *	outbuf = outbuf_orig;
    BYTE	p1, p2;

    DWORD       stime = GetTickCount();
    
    if ( inbuf_len == 0 )
	return 0;

    /* initialize table with zero because its the most frequent byte value */
    if ( init )  {
	memset( pcTable, 0, 1 << HASH_SIZE );
	p1 = 0;
	p2 = 0;
    } else {
	p1 = inbuf[ -2 ];
	p2 = inbuf[ -1 ];
    }

    /* loop through the data */
    while ( (inbuf_len -= 8) >= 0 )  {
	BYTE	mask	 = 0;	/* mask to mark successful predictions */
	BYTE *	mask_pos = outbuf++;
	BYTE	c;
	PBYTE	pcTab;

	c = *inbuf++;
	pcTab = &pcTable[ HASH(p1,p2) ];
	if ( *pcTab == c )	mask |= 1 << 0; else *outbuf++ = *pcTab = c;
	p1 = p2; p2 = c;

	c = *inbuf++;
	pcTab = &pcTable[ HASH(p1,p2) ];
	if ( *pcTab == c )	mask |= 1 << 1; else *outbuf++ = *pcTab = c;
	p1 = p2; p2 = c;

	c = *inbuf++;
	pcTab = &pcTable[ HASH(p1,p2) ];
	if ( *pcTab == c )	mask |= 1 << 2; else *outbuf++ = *pcTab = c;
	p1 = p2; p2 = c;

	c = *inbuf++;
	pcTab = &pcTable[ HASH(p1,p2) ];
	if ( *pcTab == c )	mask |= 1 << 3; else *outbuf++ = *pcTab = c;
	p1 = p2; p2 = c;

	c = *inbuf++;
	pcTab = &pcTable[ HASH(p1,p2) ];
	if ( *pcTab == c )	mask |= 1 << 4; else *outbuf++ = *pcTab = c;
	p1 = p2; p2 = c;

	c = *inbuf++;
	pcTab = &pcTable[ HASH(p1,p2) ];
	if ( *pcTab == c )	mask |= 1 << 5; else *outbuf++ = *pcTab = c;
	p1 = p2; p2 = c;

	c = *inbuf++;
	pcTab = &pcTable[ HASH(p1,p2) ];
	if ( *pcTab == c )	mask |= 1 << 6; else *outbuf++ = *pcTab = c;
	p1 = p2; p2 = c;

	c = *inbuf++;
	pcTab = &pcTable[ HASH(p1,p2) ];
	if ( *pcTab == c )	mask |= 1 << 7; else *outbuf++ = *pcTab = c;
	p1 = p2; p2 = c;

	/* write mask */
	*mask_pos = mask;
    }
    
//    Stats.timeCompress += GetTickCount() - stime;

    return outbuf - outbuf_orig;
#endif // TESTING
}


//----------------------------------------------------------------------
//
// Decompress
//
// Does our awesomely fast compression algorithm - if we are in VxD
// mode we call the much more efficient assembly language version.
//
//----------------------------------------------------------------------
DWORD Decompress( PBYTE inbuf_orig, int outbuf_len, PBYTE outbuf_orig,
		  BOOL init )
{
#if !TESTING
//    DWORD stime = GetTickCount();
    DWORD len;

    len =  ADecompress( inbuf_orig, outbuf_len, outbuf_orig, init );

//    Stats.timeCompress += GetTickCount() - stime;
  
    return len;

#else // TESTING

    BYTE       *outbuf	= outbuf_orig;
    const BYTE *inbuf	= inbuf_orig;
    BYTE	p1, p2;
    
    DWORD      stime = GetTickCount();

    if ( outbuf_len == 0 )
	return 0;

    /* initialize table with zero because its the most frequent byte value */
    if ( init )  {
	memset( pcTable, 0, 1 << HASH_SIZE );
	p1 = 0;
	p2 = 0;
    } else {
	p1 = outbuf[ -2 ];
	p2 = outbuf[ -1 ];
    }

    while ( (outbuf_len -= 8) >= 0 )  {
	BYTE	co;
	BYTE *	pcTab;
	BYTE	mask = *inbuf++;

	pcTab = & pcTable[ HASH(p1,p2) ];
	if ( mask & 1<<0 )	co = *pcTab;
	else			co = *pcTab = *inbuf++;
	*outbuf++ = co;
	p1 = p2; p2 = co;

	pcTab = & pcTable[ HASH(p1,p2) ];
	if ( mask & 1<<1 )	co = *pcTab;
	else			co = *pcTab = *inbuf++;
	*outbuf++ = co;
	p1 = p2; p2 = co;

	pcTab = & pcTable[ HASH(p1,p2) ];
	if ( mask & 1<<2 )	co = *pcTab;
	else			co = *pcTab = *inbuf++;
	*outbuf++ = co;
	p1 = p2; p2 = co;

	pcTab = & pcTable[ HASH(p1,p2) ];
	if ( mask & 1<<3 )	co = *pcTab;
	else			co = *pcTab = *inbuf++;
	*outbuf++ = co;
	p1 = p2; p2 = co;

	pcTab = & pcTable[ HASH(p1,p2) ];
	if ( mask & 1<<4 )	co = *pcTab;
	else			co = *pcTab = *inbuf++;
	*outbuf++ = co;
	p1 = p2; p2 = co;

	pcTab = & pcTable[ HASH(p1,p2) ];
	if ( mask & 1<<5 )	co = *pcTab;
	else			co = *pcTab = *inbuf++;
	*outbuf++ = co;
	p1 = p2; p2 = co;

	pcTab = & pcTable[ HASH(p1,p2) ];
	if ( mask & 1<<6 )	co = *pcTab;
	else			co = *pcTab = *inbuf++;
	*outbuf++ = co;
	p1 = p2; p2 = co;

	pcTab = & pcTable[ HASH(p1,p2) ];
	if ( mask & 1<<7 )	co = *pcTab;
	else			co = *pcTab = *inbuf++;
	*outbuf++ = co;
	p1 = p2; p2 = co;
    }

//    Stats.timeCompress += GetTickCount() - stime;

    return inbuf - inbuf_orig;
#endif // TESTING
}



//----------------------------------------------------------------------
//
// SafeCompress
//
// Compresses a page of memory. Safe means that we only compress it
// if we see that it will actually compress (rather than expand). This
// should be modified in the future to only compress if a certain
// compression threshold is achieved.
//
//----------------------------------------------------------------------
DWORD SafeCompress( PBYTE inbuf, BYTE * outbuf )
{
    BOOL comp;
    DWORD len;

    len = Compress( inbuf, PAGESIZE/2, outbuf, TRUE );
    comp = len <= PAGESIZE/2 - PAGESIZE/2/8 - MIN_COMPRESSION_SAVINGS;
    if ( comp )  {
	// It will fit.  Go for it. 
	len += Compress( inbuf+PAGESIZE/2, PAGESIZE/2, outbuf+len, FALSE );
    } else {
	// Second half may not fit.  Just copy it.
	memcpy( outbuf, inbuf, PAGESIZE );
	len = PAGESIZE;
    }
    return len;
}

