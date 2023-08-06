//======================================================================
//
// VCACHE.c - main module for VxD VCACHE
//
// Copyright (C) 1995 - Mark Russinovich and Bryce Cogswell
//
// This code implements a replacement for VCache that caches compressed
// data.
//
//======================================================================
#if !TESTING
#define   DEVICE_MAIN
#include  "tvcache.h"
#undef    DEVICE_MAIN
#else  // TESTING
#include  <windows.h>
#endif // TESTING
#include  "compress.h"

#define MEMGRAB		(0x800-240/4)
#define MEMGRAB		0

// if this is 1, turboRAM is on, else its just a plain vcache
#define COMPRESSON	1
#define UPDATEFREQ	2000

#if !TESTING
// VxD Declaration
Declare_Virtual_Device( TVCache )
#endif // TESTING

//======================================================================
//
//                        TYPEDEFS AND DEFINES
// 
//======================================================================

#if TESTING
#define VCache_VERSION  	0x100
#define VCFB_Create     	0x1
#define VCFB_LowPri     	0x2
#define VCFB_MustCreate 	0x4
#define VCFB_Hold       	0x8
#define VCFB_MakeMRU		0x10
#define VCFB_RemoveFromLRU 	0x20
#define VMHANDLE		DWORD
#define HEAPZEROINIT		0
#define PR_SYSTEM		0
#define PR_FIXED		0
#define HeapAllocate(_a,_b)	memset(malloc(_a),0,_a)
#define PageReserve(_a,_b,_c)	memset(malloc(_b*PAGESIZE),0,_b*PAGESIZE)

#else  // TESTING
#define printf		        dprintf
#define Validate(_bool)         (void) 0
#endif // TESTING

// reference mask manipulation
#define CLRBITS( _dword, _lo, _hi )  {                                \
               DWORD   _tmpmask = 1 << (_hi-1); 	              \
	       _dword &= ~((_tmpmask+_tmpmask-1)-((1<<(_lo))-1)); }
#define SETBITS( _dword, _lo, _hi )  {                                \
               DWORD   _tmpmask = 1 << (_hi-1); 	              \
	       _dword |= ((_tmpmask+_tmpmask-1)-((1<<(_lo))-1)); } 

// math
#define CEIL(a,b)         (((a)+(b)-1)/(b))
#define ROUNDUP(a,b)      (CEIL(a,b)*(b))
#define MIN( a, b )       ((a) < (b) ? (a) : (b))
#define MAX( a, b )       ((a) > (b) ? (a) : (b))

// constants
#define NUMBUCKETS         100
#define MINHOLESIZE        0
#define BLCKSIZE           128
#define PAGENULL	   (DWORD) -1

// depth in lru at which it is okay to compress
// this number was found with some experimentation and makes sense
// considering the absolute minimum cache size is 8 blocks
#if !TESTING
#define IGNOREDEPTH        20	/* leave this many pages uncompressed */
#define COMPRESSDEPTH      8	/* attempt to compress a sequence this long */
#else  // TESTING
#define COMPRESSDEPTH      1
#endif // TESTING
#define SHRINKFREQ	   20	/* attempt to compress pages this often */
#define COMPRESSMAX	   20	/* don't compress more than this at a time */

// this matches the real VCache fsd id assignment
#define FSDBASE            0x64

// memory management data structure that matches
// the original vcache structures
typedef struct vc_s {
  struct vc_s     *next;
  struct vc_s     *prev;
  DWORD           key1;
  DWORD           key2;
  PVOID           buffer;
  BYTE            reserved[0x1C];
  WORD            holdcount;
  BYTE            dirty;
  BYTE            fsdid;
  DWORD           age;
  struct vc_s     *lru_next;
  struct vc_s     *lru_prev;

  // extra fields for our use
#if TESTING
  DWORD		  checksum;
  DWORD		  checksumc;
#endif // TESTING
  DWORD           refmask;              // usage bits for page
  BYTE            iscompressed : 1;     // equals 4096 if not compressed
  BYTE            offlru       : 1;     // is this guy on the lru
  BYTE            compresstried: 1;     // did we try compression?

//***FIXFIX
#if TESTING
   BYTE           mirrored:1;
#endif // TESTING
//***FIXFIX
} vcache_s;

// header for compressed data
typedef struct mem_hdr {
    struct mem_hdr     *prev;
    struct mem_hdr     *next;           // next on chain
    WORD               size: 15;        // size of current block
    WORD               first: 1;        // first in chain?
    BYTE               data[ 0 ];
} memhdr_s;

// header for free buffers
typedef struct free_hdr {
    struct free_hdr     *prev; 		// Prev neighbor
    struct free_hdr     *next;		// Next neighbor
    WORD		size;           // size of space (max 4096)
} freehdr_s;


// structure used for any fsd that registers
typedef struct {
    VOID	(*discardcallbackfunc)();
    DWORD	minblocks;
    DWORD	blockcnt;
} fsdlist_s;

// header structure for hash list. To make this work the first field
// in the vcache data structure must be the hash list next pointer.
typedef struct hsh_s {
  vcache_s    *next;
  vcache_s    *prev;
} hash_s;


//======================================================================
//
//                          FORWARD DEFINITIONS
// 
//======================================================================

PVOID Fragment( VOID * buffer, DWORD size, DWORD pageignore, BOOL force );
PVOID DeallocateFrag( memhdr_s *dataptr );
DWORD AllocateFrag( PVOID *dataptr, DWORD size, 
                   DWORD pageignore, BOOL force );
DWORD AgeLRU( BOOL force );
VOID  CTVCACHE_MakeMRU( vcache_s *hCacheBlock);
DWORD CTVCACHE_Hold( vcache_s *hCacheBlock );
VOID  ShrinkPage( vcache_s *hCacheBlock );
VOID  ExpandPage( vcache_s *hCacheBlock );
#if TESTING
DWORD CheckSumComp( const vcache_s * pg );
DWORD CheckSum( const vcache_s * pg );
#endif

//======================================================================
//
//                            LOCKED DATA
//
//======================================================================
#include LOCKED_DATA_SEGMENT

// this prevents us from initializing twice
BOOL		SysCriticalInitComplete	= FALSE;
BOOL		DeviceInitComplete	= FALSE;

#if !TESTING
// idle call-back thunk
Idle_THUNK              IdleThunk;
#endif // TESTING

// max and min sizes
DWORD		MaxSize = 0;
DWORD		MinSize = 0;

// stats for VMM
DWORD           NumMisses   = 0;
DWORD           NumHits     = 0;
DWORD           NumDiscards = 0;

// the current logical cache time 
DWORD           CurTime;

// the minimum discard age
DWORD           CacheBufRRT;

// for keeping track of last 26 discard misses
int		DiscardPtr  =0;
DWORD           DiscardList[26];

// max number of blocks
DWORD		MaxBlocks;
DWORD		NumBlocks;

// the hash buckets
hash_s          hashbuck[ NUMBUCKETS ];

// lru head
vcache_s        LRUHead;

// the last accessed block - we have to be careful not to compress
// this one
vcache_s        *LastTouched = NULL;
vcache_s        *FindTouched = NULL;

// re-entrancy flag
BOOL            Busy = 0;

// start of our initial memory buffer
PVOID           *PageStart;
#if !COMPRESSON && TESTING
//***FIXFIX
PVOID           *PageMirror;
#endif // COMPRESSON

// blocklist
vcache_s        *BlockList;
vcache_s        *BlockFree;

// fragmented memory list (doubly linked for coallescing)
freehdr_s       *FragList;

// whole free pages (singly linked - no coallescing)
freehdr_s       *PageList;

// registration structures
fsdlist_s       fsdlist[26];
DWORD	        fsdcount;

#if MSHASAFUCKINGCLUE
// This triggers a compiler bug where a constant offset into the
// array is added instead of subtracted.
#define FSDLIST(n)	fsdlist[n]
#else
#define FSDLIST(n)	fsdlist[0]
#endif

// compress/decompress staging buffer
BYTE            tmppage[ PAGESIZE ];

DWORD		shrinkCnt = 0;

// statistics
DWORD		TotalBlocks = 0;
DWORD		TotalPages;
DWORD		ActualCount;

// for our own monitoring - FIXFIXFIX
// number of compressed blocks
DWORD NumComp = 0;       
DWORD NumExpand = 0;
DWORD NumShrink = 0;
DWORD NumSave = 0;
DWORD Renter = 0;

DWORD DepthCount[1000];

//======================================================================
//
//                            LOCKED CODE
//
//======================================================================
#include LOCKED_CODE_SEGMENT

__inline _int64 rdtsc( void )
{
    __asm __emit 0x0F;
    __asm __emit 0x31;
}


struct pstat {
    char		name[ 28 ];
    unsigned int	enter;
    unsigned int	exit;
    unsigned _int64	time;
};

#define DECL(p)  struct pstat stat_##p = { #p, 0, 0 };
struct pstat   _dummy;
struct pstat * _cur = &_dummy;
#define ENTER(p) \
    _int64 _now = rdtsc();\
    struct pstat * _prev = _cur;\
    int _dummy = ( _cur = &stat_##p,\
		   _prev->time += _now,\
		   _cur->time  -= _now,\
		   _cur->enter += 1\
		  )
#define EXIT(p) (\
		 _now = rdtsc(),\
		 _cur->time += _now,\
		 _prev->time -= _now,\
		 _cur->exit += 1,\
		 _cur = _prev\
		 )

BOOL DumpCountsNow = FALSE;

    DECL( xxx_first )
    DECL( HoseMe )
    DECL( Fill )
    DECL( IsCorrupt )
    DECL( Validate )
    DECL( RemoveLRU )
    DECL( ShrinkOldPages )
    DECL( InsertMRU )
    DECL( DeallocatePage )
    DECL( AllocatePage )
    DECL( Defragment )
    DECL( Fragment )
    DECL( FragFree )
    DECL( DeallocateFrag )
    DECL( AllocateFrag )
    DECL( FreeBlock )
    DECL( AgeLRU )
    DECL( CheckSumBuf )
    DECL( CheckSum )
    DECL( CheckSumComp )
    DECL( CheckSumMem )
    DECL( Hash )
    DECL( ExpandPage )
    DECL( ShrinkPage )
    DECL( TestTimeout )
    DECL( IdleCallback )
    DECL( MemInit )
    DECL( CTVCACHE_Get_Version  )
    DECL( CTVCACHE_Register )
    DECL( CTVCACHE_GetSize  )
    DECL( CTVCACHE_CheckAvail )
    DECL( CTVCACHE_FindBlock )
    DECL( CTVCACHE_FreeBlock )
    DECL( CTVCACHE_MakeMRU )
    DECL( CTVCACHE_Hold )
    DECL( CTVCACHE_Unhold )
    DECL( EnumCallback )
    DECL( CTVCACHE_Enum )
    DECL( CTVCACHE_TestHold )
    DECL( CTVCACHE_GetStats )
    DECL( CTVCACHE_Deregister )
    DECL( CTVCACHE_AdjustMinimum )
    DECL( CTVCACHE_SwapBuffers )
    DECL( CTVCACHE_RelinquishPage )
    DECL( CTVCACHE_UseThisPage )
    DECL( _CTVCACHE_CreateLookupCache  )
    DECL( _CTVCACHE_CloseLookupCache  )
    DECL( _CTVCACHE_DeleteLookupCache  )
    DECL( _CTVCACHE_Lookup  )
    DECL( _CTVCACHE_UpdateLookup  )
    DECL( ControlDispatcher )
    DECL( OnSysCriticalInit )
    DECL( GetFreePageCount )
    DECL( OnDeviceInit )
    DECL( OnSysCriticalExit ) 
    DECL( xxx_last )


void DumpCounts( void )
{
    _int64	sum = 0;
    struct pstat * p;

    if ( !DumpCountsNow )
	return;
    DumpCountsNow = FALSE;

    for ( p = & stat_xxx_first + 1; p != & stat_xxx_last; ++p )  {
	DWORD q  = p->enter;
	if ( q )  {
	    DWORD lo =  (DWORD)   p->time;
	    DWORD hi = ((DWORD *)&p->time)[1];
	    __asm {
		mov	eax, lo;
		mov	edx, hi;
		mov	ecx, q;
		div	ecx;
		mov	q, eax;
	    }
	}
	dprintf( "%8d,%8d %4d:%10u = %8u  %s\n",
		 p->enter, p->exit,
		 ((DWORD *)&p->time)[1], (DWORD)p->time,
		 q,
		 p->name );
	sum += p->time;
    }
    dprintf( "total time = %d:%8u\n", ((DWORD *)&sum)[1], (DWORD)sum );
}




//----------------------------------------------------------------------
//
// HoseMe
// 
// Choke and die in a way that will wake up Winice.
//
//----------------------------------------------------------------------
void HoseMe() 
{
    ENTER( HoseMe );
    _asm int 3;
    EXIT( HoseMe );
}


#if TESTING

//----------------------------------------------------------------------
//
// Fill
//
// Fills a buffer with the buffer's page number.
//
//----------------------------------------------------------------------
VOID Fill( BYTE * buffer, BYTE page )
{
    ENTER( Fill );
    int j;
    for ( j = 0; j < PAGESIZE; ++j )
	buffer[j] = page;
    EXIT( Fill );
}


//----------------------------------------------------------------------
//
// IsCorrupt
//
// Checks a buffer randomly to see if there are signs of corruption.
//
//----------------------------------------------------------------------
BOOL IsCorrupt( BYTE * buffer, BYTE page )
{
    ENTER( IsCorrupt );
#if 1
    EXIT( IsCorrupt );
    return buffer[0] != page;
#else
    int j, i;
    for ( j = 0; j < 128; ++j ) {
    	i = rand() % PAGESIZE;
	if ( buffer[ i ] != page ) {
	    printf( "page corrupt\n" );
	    HoseMe();
	    EXIT( IsCorrupt );
	    return TRUE;
	}
    }
    EXIT( IsCorrupt );
    return FALSE;
#endif
}

#endif // TESTING

#if 0 // TESTING
//----------------------------------------------------------------------
//
// Validate
//
// Checks LRU and hash table, and free lists to make sure everything
// is in order.
//
//----------------------------------------------------------------------
void Validate( BOOL checkpages )
{
    ENTER( Validate );
  vcache_s  *blck, *pblck, *freeblck;
  memhdr_s  *memp = NULL, *pmemp = NULL;
  freehdr_s *freep;
  int	    i;
  PBYTE     buf;
  DWORD	    totalmem = 0;

  // general check
  if( FragList && FragList == PageList ) {
  	printf(" Free lists are screwed\n");
	HoseMe();
  }

  // check frag list
  freep = FragList;
  while( freep ) {
   	totalmem += freep->size;
	if( (DWORD) freep < (DWORD) PageStart ) {
	        printf(" Frag list has bad pointer\n");
		HoseMe();
        }
   	if( freep->size > 4095 ) {
		printf(" Frag list is fragged\n");
		HoseMe();
	}
   	if( freep->size % BLCKSIZE ) {
		printf(" Frag list is fragged\n");
		HoseMe();
	}
	if( freep == freep->next ) {
		printf(" Frag list is looped\n");
		HoseMe();
	}		
	freep = freep->next;
  }

  if( PageList ) {
    if( PageList->next == PageList ) {
      printf(" Page list is looped\n");
      HoseMe();
    }

    if( (DWORD) PageList < (DWORD) PageStart ) {
      printf(" Page list is fragged\n");
      HoseMe();
    }
  }

  // check page list
  freep = PageList;
  while( freep ) {
  	totalmem += 4096;
  	if( freep->size != 4096 ) {
		printf(" Page list is hosed\n");
		HoseMe();
	}
	if( ((DWORD) freep - (DWORD) PageStart) % 4096 ) {
		printf(" Page list not aligned\n");
		HoseMe();
	}
	if( freep == freep->next ) {
		printf(" Page list is looped\n");
		HoseMe();
	}
	freep = freep->next;
  }

  // check lru
  if( TotalBlocks > 10000 ) {
  	printf(" LRU - what?\n");
	HoseMe();
  }
  blck = LRUHead.lru_next;
  if( !blck ) {
  	printf(" LRU empty\n");
	HoseMe();
  }
  ActualCount = 0;
  pblck = &LRUHead;
  while( blck != &LRUHead ) {
    ActualCount++;
    if( blck->lru_prev != pblck ) {
    	printf(" LRU prev pointer bad\n");
	HoseMe();
    }
    if( blck->fsdid != FSDBASE ) {
    	printf(" LRU messed up\n");
	HoseMe();
    }
    if( blck->holdcount > 5 ) {
    	printf(" Holdcount too high\n");
	HoseMe();
    } 
#if TESTING
    if( checkpages && !blck->iscompressed && 
    	blck->holdcount && blck->buffer )
    	IsCorrupt( blck->buffer, (BYTE) blck->key1 );
#endif // TESTING
    pblck = blck;
    blck = blck->lru_next;
  }
  if( ActualCount != TotalBlocks ) {
  	printf(" We've lost somebody\n");
	HoseMe();
  }

  // check block free list
  freeblck = BlockFree;
  while( freeblck ) {
  	if( freeblck->fsdid ) {
		printf(" Non-free guy on free block list\n");
		HoseMe();
	}
	if( freeblck->holdcount ) {
		printf(" Held guy on the free list\n");
		HoseMe();
	}
	freeblck = freeblck->next;
  }

  // check hash table
  for( i= 0; i< NUMBUCKETS; i++ ) {
    blck = hashbuck[i].next;
    pblck = (vcache_s *) &hashbuck[i];
    while( blck != (vcache_s *) &hashbuck[i] ) {
#if COMPRESSON 
    	if( blck->iscompressed && checkpages ) {
		memp = (memhdr_s *) blck->buffer;
		pmemp = (memhdr_s *) blck;
		while( memp ) {
			totalmem += ROUNDUP(memp->size, BLCKSIZE);
			if( !memp->size || memp->size > 4095 ) {
				printf(" Bad compressed data\n");
				HoseMe();
			}
			if( memp->prev != pmemp ) {
				printf(" Bad back pointer in block data\n");
				HoseMe();
			}
			pmemp = memp;
			memp = memp->next;
		}
#if TESTING
		if ( CheckSumComp( blck ) != blck->checksumc )  {
			printf( "bad compressed checksum\n" );
			HoseMe();
		}
#endif // TESTING
	} else if( checkpages && blck->buffer ) {
		totalmem += PAGESIZE;
		buf = (PBYTE) blck->buffer;
		buf[0] = buf[0];
	 	if( ((DWORD) blck->buffer - (DWORD) PageStart)%PAGESIZE ) {
			printf(" Page is not aligned\n");
			HoseMe();
		}
#if TESTING
		if( !blck->dirty && !blck->holdcount ) {
		  if ( CheckSum( blck ) != blck->checksum )  {
		    printf( "bad uncompressed checksum\n" );
		    HoseMe();
		  }
		}
#endif // TESTING
	}
#endif // COMPRESSON
	if( blck->prev != pblck ) {
		printf(" Hash prev pointer bad\n");
		HoseMe();
	}
	pblck = blck;
	blck = blck->next;
    }
  }

#if COMPRESSON
  if( checkpages && totalmem != TotalPages * PAGESIZE ) {
  	printf(" Memory error: found:%d actual:%d\n",
	       totalmem, TotalPages * PAGESIZE );
	HoseMe();
  }
#endif // COMPRESSON
    EXIT( Validate );
}
#endif

//----------------------------------------------------------------------
//
// RemoveLRU
//
// Takes a block header off the LRU
//
//----------------------------------------------------------------------
VOID RemoveLRU( vcache_s *hdr )
{
    ENTER( RemoveLRU );

  // is this guy even on the lru?
  if ( hdr->offlru )  {
      EXIT( RemoveLRU );
      return;
  }

  Validate( FALSE );

  // remove the block from the lru
  hdr->lru_prev->lru_next = hdr->lru_next;
  hdr->lru_next->lru_prev = hdr->lru_prev;

  hdr->offlru = TRUE;

  TotalBlocks--;

  Validate( FALSE );

    EXIT( RemoveLRU );
}

//----------------------------------------------------------------------
//
// ShrinkOldPages
//
// Gets nth unheld lru entry to see if it can be compressed. Note that
// we cannot compress dirty pages either! We only go a few blocks into
// the lru, because otherwise this operation would be very expensive.
// The idle callback will get any blocks that we miss.
//
//---------------------------------------------------------------------- 
void ShrinkOldPages( BOOL wholescan )
{
    ENTER( ShrinkOldPages );
    vcache_s    *lru = LRUHead.lru_prev;
    int          n;
    int		cnt = 0;

    /* Only attempt to shrink pages occasionally */
    if ( ++shrinkCnt < SHRINKFREQ )
	return;
    else
	shrinkCnt = 0;

#if TESTING
    wholescan = TRUE;
#endif // TESTING

    // seek a ways into the lru
    for ( n = 0; n < IGNOREDEPTH; ++n )  {
	if ( lru == &LRUHead )  {
	    EXIT( ShrinkOldPages );
	    return;
	}
	lru = lru->lru_prev;
    }

    /* Compress the next few pages we find */
    for ( n = wholescan ? -1 : COMPRESSDEPTH; n >= 0; --n )  {
	if ( lru == &LRUHead )
	    break;
	if ( ! lru->iscompressed   &&
	     ! lru->compresstried  &&
	     ! lru->holdcount      &&
	     ! lru->dirty	   &&
	     lru != LastTouched    &&
	     lru != FindTouched )
	{
	    ShrinkPage( lru );
	    if ( ++cnt >= COMPRESSMAX )
		break;
	}
	lru = lru->lru_prev;
    }
    EXIT( ShrinkOldPages );
}

//----------------------------------------------------------------------
//
// InsertMRU
//
// Takes a block header and puts it onto the end of the lru (makes
// it the newest block)
//
//----------------------------------------------------------------------
VOID InsertMRU( vcache_s *lru )
{
    ENTER( InsertMRU );

    Validate( TRUE );
#if COMRESSON || TESTING
    // determine if pages should compressed or expanded
    ShrinkOldPages( FALSE );

    if( lru->iscompressed )
        ExpandPage( lru );
#endif 
    // now fix the lru
    LRUHead.lru_prev->lru_next	= lru;
    lru->lru_prev		= LRUHead.lru_prev;
    lru->lru_next		= &LRUHead;
    LRUHead.lru_prev		= lru;
    lru->offlru			= FALSE;
    
    // update the blocks timestamp
    lru->age                    = CurTime++;

    TotalBlocks++;

    Validate( TRUE );

    EXIT( InsertMRU );
}


//----------------------------------------------------------------------
//
// DeallocatePage
//
// Easy - just put the entire page onto the memory free list.
//
//----------------------------------------------------------------------
VOID DeallocatePage( PVOID dataptr )
{
    ENTER( DeallocatePage );
  freehdr_s   *freepage = (freehdr_s *) dataptr;

  Validate( FALSE );
#if TESTING
  if( freepage == FragList ) {
  	printf(" Page is already on FragList?\n");
	HoseMe();
  }
#endif // TESTING

  // just put it on the free list
  freepage->size = PAGESIZE;
  freepage->next = PageList;
  freepage->prev = NULL;
  PageList       = freepage;

  // fix reference mask
  BlockList[ ((DWORD) dataptr - (DWORD) PageStart)/PAGESIZE ].refmask = 0;

    EXIT( DeallocatePage );
}


//----------------------------------------------------------------------
//
// AllocatePage
//
// We need to free up a page aligned full page of data. This means
// checking first to see if a full page is free. If not, we need
// to start sending LRU pages out.
//
//----------------------------------------------------------------------
PVOID AllocatePage( BOOL force )
{
    ENTER( AllocatePage );
  DWORD        freed, totfreed, blocknum, mask;
  memhdr_s     *old, *new;
  PVOID        freedata;
  freehdr_s    *freeptr;
  vcache_s     *pageref;
  DWORD        blk, i, oldcount = 0;
  memhdr_s     *oldlist[32];

  // see if there is a whole page free first
  if( PageList ) {
    freeptr = PageList;
    PageList = PageList->next;  
    EXIT( AllocatePage );
    return freeptr;
  }

  Validate( FALSE );

  // nope, time to clear some space
  totfreed = 0;
  do {

    Validate( FALSE );

    freed = AgeLRU( force );
    if( freed == PAGESIZE )
    	totfreed += PAGESIZE;
    else if( freed > sizeof(memhdr_s) )
    	totfreed += freed - sizeof(memhdr_s);

    Validate( FALSE );

  } while( freed && (totfreed < PAGESIZE ));

  // is there enough space?
  if( totfreed < PAGESIZE && !PageList )   {
    EXIT( AllocatePage );
    return NULL;
  }

  // see if a whole page freed up
  if( PageList) {
    freedata = (PVOID) PageList;
    PageList = PageList->next;

    // set reference mask
    BlockList[ ((PBYTE) freedata - (PBYTE) PageStart)/PAGESIZE ].refmask = 
    	(DWORD) -1;

    EXIT( AllocatePage );
    return freedata;
  }

  // nope, have to move stuff around
  blocknum = ((PBYTE) FragList - (PBYTE) PageStart)/PAGESIZE;
  pageref  = &BlockList[ blocknum ];
  mask     = pageref->refmask;

  Validate( FALSE );

  // clear out the page that the first block of data sits on
  while ( mask )  {

    // find first non-free block in page
    _asm bsf eax, mask;
    _asm mov blk, eax;
    old = (memhdr_s *) ((DWORD) PageStart + blocknum*PAGESIZE + blk*BLCKSIZE);
    new = (memhdr_s *) Fragment( old->data, old->size - sizeof(memhdr_s), 
				blocknum, TRUE );

#if TESTING
    if( CheckSumBuf( old->data, old->size - sizeof(memhdr_s) ) !=
	CheckSumMem( new )) {
	printf(" Aha! Got you now!\n");
	HoseMe();
    }			
#endif 
    // clear the bits in our temporary mask
    CLRBITS( mask, blk, blk + ROUNDUP(old->size, BLCKSIZE)/BLCKSIZE );
    
    // fix prev pointers on the memory list
    if( old->first )
      ((vcache_s *)(old->prev))->buffer = new;
    else {
      new->first = FALSE;
      old->prev->next = new;
    }
    new->prev = old->prev;

    // fix next pointer
    if( old->next ) {
      while( new->next ) 
        new = new->next;
      new->next = old->next; 
      old->next->prev = new; 
    }      

    // put it on list of blocks to free	
    oldlist[ oldcount++ ] = old;
  }

  // deallocate the old blocks
  for( i= 0; i < oldcount ; i++ )
    DeallocateFrag( oldlist[i] );


  Validate( FALSE );
  if( !PageList )
  	HoseMe();

  freedata = (PVOID) PageList;
  PageList = PageList->next;

  Validate( FALSE );

  // set reference mask
  BlockList[ ((PBYTE) freedata - (PBYTE) PageStart)/PAGESIZE ].refmask = 
  	(DWORD) -1;

    EXIT( AllocatePage );
  return freedata;
}


//----------------------------------------------------------------------
//
// Defragment
//
// Takes a chained list of data and puts it into a contiguous buffer.
//
//----------------------------------------------------------------------
DWORD Defragment( memhdr_s * list, PVOID buffer )
{
    ENTER( Defragment );
  DWORD      size = 0;
  memhdr_s   *next;
    
  Validate( FALSE );

  while ( list )  {
    memcpy( (BYTE *)buffer+size, list->data, list->size - sizeof(memhdr_s) );
    size += list->size - sizeof(memhdr_s);
    next  = list->next;
    DeallocateFrag( list );
    list  = next;
  }

  Validate( FALSE );

    EXIT( Defragment );
  return size;
}


//----------------------------------------------------------------------
//
// Fragment
//
// Takes a contigous temporary buffer and stuffs it into memory spaces
// that are found on the free list. A non-zero pageignore option tells 
// fragment to ignore any free blocks that it finds on that page since 
// pageallocate is trying to clear that page.
//
//----------------------------------------------------------------------
PVOID Fragment( VOID * buffer, DWORD size, DWORD pageignore, BOOL force )
{
    ENTER( Fragment );
  memhdr_s * buf;
  DWORD fragsize;

  Validate( FALSE );

  if ( size == PAGESIZE && PageList ) {
    buf = (memhdr_s *) PageList;
    PageList = PageList->next;
    if( PageList ) 
    	PageList->prev = NULL;
    memcpy( buf, buffer, PAGESIZE );
    EXIT( Fragment );
    return buf;
  } 

  Validate( FALSE );

  fragsize = AllocateFrag( &buf, size, pageignore, force );
  if ( fragsize == 0 )  {
    EXIT( Fragment );
    return NULL;
  }

  if( fragsize > size + sizeof(memhdr_s) ) 
    fragsize = size + sizeof(memhdr_s);

  Validate( FALSE );

  fragsize -= sizeof(memhdr_s);
  memcpy( buf->data, buffer, fragsize );

  Validate( FALSE );

  if ( size > fragsize )
     size -= fragsize;
  else
     size = 0;
  buffer = (PBYTE) buffer + fragsize;

  buf->size  = (WORD) (fragsize + sizeof(memhdr_s));
  buf->first = TRUE;
  buf->prev  = NULL;

  Validate( FALSE );
  
  // are we done allocating?
  if ( size == 0 )  {
    buf->next = NULL;
  } else {
    buf->next = Fragment( buffer, size, pageignore, force );
    if ( !buf->next )  {
      DeallocateFrag( buf );
      buf = NULL;
    } else {
      buf->next->prev  = buf;
      buf->next->first = FALSE;
    }
  }

  Validate( FALSE );

    EXIT( Fragment );
  return buf;
}


//----------------------------------------------------------------------
// 
// FragFree
//
// Indicates if there is free stuff on the frag list that isn't on
// the page specified.
//
//----------------------------------------------------------------------
BOOL FragFree( DWORD pageignore )
{
    ENTER( FragFree );
    freehdr_s    *fraglist = FragList;

    if ( pageignore != PAGENULL )  {

	// search the list
	for ( ; fraglist; fraglist = fraglist->next )
	    if ( (((DWORD)fraglist - (DWORD)PageStart)/PAGESIZE != pageignore))
		break;
    }

    EXIT( FragFree );
    return fraglist != NULL;
}


//----------------------------------------------------------------------
//
// DeallocateFrag
//
// Frees the specified data chunk, coalescing if possible.
//
//----------------------------------------------------------------------
PVOID DeallocateFrag( memhdr_s *dataptr )
{
    ENTER( DeallocateFrag );
  PVOID        next;
  DWORD	       offset, pageoffset;
  DWORD        blkoffset, checkoffset;
  freehdr_s    *freeblck, *nextblck;
  vcache_s     *pageref;
  WORD         size;

  Validate( FALSE );

  // save next pointer
  next = dataptr->next;
  size = ROUNDUP( dataptr->size, BLCKSIZE );

  // get the usage mask
  offset     = (PBYTE) dataptr - (PBYTE) PageStart;
  pageoffset = offset/PAGESIZE;
  pageref    = &BlockList[ pageoffset ];
  blkoffset  = (DWORD) ( offset - pageoffset*PAGESIZE ) / BLCKSIZE;

#if TESTING
  // make sure bits are marked as in use
  {
	DWORD   tmpmask = 1 << ((blkoffset+size/BLCKSIZE)-1); 
	if( !(pageref->refmask & ((tmpmask+tmpmask-1)-((1<<(blkoffset))-1)))) {
		printf(" Usage is messed up\n");
		HoseMe();
	}
  } 
#endif
  // mark the blocks as free
  CLRBITS( pageref->refmask, blkoffset, blkoffset + size/BLCKSIZE );

  // initialize header
  freeblck = (freehdr_s *) dataptr;
  freeblck->size = size;

  Validate( FALSE );

  // is the bit to the left free?
  if( blkoffset && !(pageref->refmask & 1 << (blkoffset-1)) ) {
    // yes, see where that free block starts so that we can coallesce
    checkoffset = blkoffset-2;
    while( checkoffset != -1 && 
           !(pageref->refmask & (1 << checkoffset )))
    {
	checkoffset--;
    }
    checkoffset++;

    Validate( FALSE );

    // checkoffset is the start of the free block to the left
    freeblck = (freehdr_s *) ((DWORD) dataptr - 
        (blkoffset - checkoffset)*BLCKSIZE);

    if( freeblck->size % BLCKSIZE || !freeblck->size ) {
    	printf(" something wrong with reference masks\n");
	HoseMe();
    }

    // add us onto that free block
    freeblck->size += size;
    if( freeblck == FragList )
      FragList = FragList->next;
    if( freeblck->next )
      freeblck->next->prev = freeblck->prev;
    if( freeblck->prev )
      freeblck->prev->next = freeblck->next;
  } 

  Validate( FALSE );

  // is the bit to the right free?
  if( (blkoffset + size/BLCKSIZE) < 32 && 
     !(pageref->refmask & (1 << (blkoffset+size/BLCKSIZE))) ) {

    // get the data structure
    nextblck = (freehdr_s *) ((DWORD) freeblck + freeblck->size);

    if( nextblck->size % BLCKSIZE || !nextblck->size) {
    	printf(" something wrong with reference masks\n");
	HoseMe();
    }

    // yes, coallesce - no need to search on this side
    freeblck->size += nextblck->size;
    if( nextblck == FragList )
      FragList = FragList->next;
    if( nextblck->prev )
      nextblck->prev->next = nextblck->next;
    if( nextblck->next )
      nextblck->next->prev = nextblck->prev;
  }

  Validate( FALSE );

  // see which list to put this on
  if( freeblck->size == PAGESIZE ) {
    // put on page list
    freeblck->next = PageList;
    PageList       = freeblck;
  } else {
    // put the coallesced block onto the front of the free list
    freeblck->next = FragList;
    if( freeblck->next )
    	freeblck->next->prev = freeblck;
    freeblck->prev = NULL;
    FragList       = freeblck;
  }

  Validate( FALSE );

    EXIT( DeallocateFrag );
  return next;
}


//----------------------------------------------------------------------
//
// AllocateFrag
//
// Returns the next block of the free list. If there is none,
// we resort to aging out pages and returning their space. 
//
//----------------------------------------------------------------------
DWORD AllocateFrag( PVOID *dataptr, DWORD size, DWORD pageignore, BOOL force )
{
    ENTER( AllocateFrag );
  DWORD        freesize, reqsize;
  DWORD        offset, pageoffset, blckoffset;
  freehdr_s    *freeptr, *fraglist;
  vcache_s     *refblck;

  // round up to next 128 byte boundary
  size += sizeof(memhdr_s);
  reqsize = ROUNDUP( size, BLCKSIZE );

  // if no room, make some
  while( !FragFree( pageignore ) && !PageList ) {
    if( !AgeLRU( force ) ) {
      // still no room!
      EXIT( AllocateFrag );
      return 0;
    }
  }

  Validate( FALSE );

  // see which list has the newly freed stuff
  if( FragList ) {

    // if ignorepage is set, move to first block not on the page
    fraglist = FragList;
    while( ((DWORD) fraglist - (DWORD) PageStart)/PAGESIZE == pageignore )
    	fraglist = fraglist->next;

    // return next block on the free list
    *dataptr = (PVOID) fraglist;
    freesize = fraglist->size;
    if( fraglist->prev )
      fraglist->prev->next = fraglist->next;
    else
      FragList = FragList->next;
    if( fraglist->next )
      fraglist->next->prev = fraglist->prev;

  } else {
    // return next block on the free list
    *dataptr = (PVOID) PageList;
    freesize = PageList->size;
    PageList = PageList->next;
  }

  Validate( FALSE );

  // if we got more than we wanted, put rest back on frag free list
  if( freesize > reqsize ) {
    freeptr = (freehdr_s *) ((DWORD) *dataptr + reqsize);
    freeptr->next = FragList;
    freeptr->prev = NULL;
    if( FragList )
    	FragList->prev = freeptr;
    FragList = freeptr;
    freeptr->size = (WORD) (freesize - reqsize);
    freesize = reqsize;
  }

  Validate( FALSE );

  // fix the reference count
  offset     = (PBYTE) *dataptr - (PBYTE) PageStart;
  pageoffset = offset/PAGESIZE;
  refblck    = &BlockList[ pageoffset ];
  blckoffset = (DWORD) ( offset - pageoffset*PAGESIZE ) / BLCKSIZE;
  SETBITS( refblck->refmask, blckoffset, blckoffset + freesize/BLCKSIZE );

    EXIT( AllocateFrag );
    return freesize;
}


//----------------------------------------------------------------------
//
// FreeBlock
// 
// This routine is called by AgeLRU and CTVCACHE_FreeBlock. It takes a
// cache entry and frees everything associated with it, removing it
// from all lists.
//
//----------------------------------------------------------------------
DWORD FreeBlock( vcache_s *blck )
{
    ENTER( FreeBlock );
    DWORD      size = 0;
    PVOID      dataptr;

    Validate( FALSE );

#if !COMPRESSON && TESTING
    // fix the pointer if compression off
    if( blck->iscompressed || blck->mirrored) 
      ExpandPage( blck );
    if( blck->mirrored )
      ExpandPage( blck );
#endif

#if TESTING
    if( blck->holdcount ) {
    	printf(" Freeing held block\n");
	HoseMe();
    }
    if( blck->buffer && blck->buffer == FragList ) {
    	printf(" Here it is\n");
	HoseMe();
    }
#endif // TESTING

    // remove from lru
    RemoveLRU( blck );

    // remove it from the hash list
    blck->prev->next = blck->next;
    blck->next->prev = blck->prev;

    if ( blck->iscompressed )  { 
      NumComp--;
      dataptr = blck->buffer;
      while( dataptr ) {
	  size    += ((memhdr_s *) dataptr)->size;
	  dataptr = DeallocateFrag( (memhdr_s *) dataptr );
      }
    } else if( blck->buffer ) {
      DeallocatePage( blck->buffer );
      size = PAGESIZE;
    }

    blck->fsdid = 0;
    blck->next	= BlockFree;
    BlockFree	= blck;

    Validate( FALSE );
    
    EXIT( FreeBlock );
    return size;
}

//----------------------------------------------------------------------
//
// AgeLRU
//
// Takes the entry on the front of the LRU and frees it.
//
//----------------------------------------------------------------------
DWORD AgeLRU( BOOL force )
{
    ENTER( AgeLRU );
  vcache_s   *lru;

  Validate( FALSE );
  
  // first, search the lru for somebody not held
  lru = LRUHead.lru_next;
  while( lru != &LRUHead && (lru->holdcount || lru->dirty ||
	lru == LastTouched || lru == FindTouched ) ) 
  {
      lru = lru->lru_next;
  }

  // find anybody to free?
  if ( lru == &LRUHead || 
     (!force && CacheBufRRT >= CurTime - lru->age ))
  {
      EXIT( AgeLRU );
      return 0;
  }

  // deal with discard and miss stats
  DiscardList[ DiscardPtr++ ] = (lru->key1 ^ lru->key2 );
  if( DiscardPtr == 26 ) DiscardPtr = 0;
  NumDiscards++;
  
  // call the callback with pointer to data structure in esi
  _asm pusha;
  _asm mov esi, lru;
  (FSDLIST( lru->fsdid - FSDBASE ).discardcallbackfunc)();
  _asm popa;


    {
	DWORD n = FreeBlock( lru );
	EXIT( AgeLRU );
	return n;
    }
}

#if TESTING
DWORD CheckSumBuf( const BYTE * buffer, DWORD len )
{
    ENTER( CheckSumBuf );
    int		j;
    DWORD	sum = 0;
    for ( j = 0; j < len; ++j )
    	sum += buffer[ j ];
    EXIT( CheckSumBuf );
    return sum;
}

DWORD CheckSum( const vcache_s * pg )
{
    ENTER( CheckSum );
    DWORD n = CheckSumBuf( pg->buffer, PAGESIZE );
    EXIT( CheckSum );
    return n;

}

DWORD CheckSumComp( const vcache_s * pg )
{
    ENTER( CheckSumComp );
    memhdr_s *	list;
    DWORD	sum = 0;
    for ( list = (memhdr_s *) pg->buffer; list; list = list->next )
    	sum += CheckSumBuf( list->data, list->size - sizeof(memhdr_s) );

    EXIT( CheckSumComp );
    return sum;
}

DWORD CheckSumMem( memhdr_s *list )
{
    ENTER( CheckSumMem );
    DWORD	sum = 0;

    for ( ; list; list = list->next )
    	sum += CheckSumBuf( list->data, list->size - sizeof(memhdr_s) );

    EXIT( CheckSumMem );
    return sum;
}
#endif

//----------------------------------------------------------------------
//
// Hash
//
// Looks up in the has table the keys passed in. If the key isn't found
// and the create parameter is set, we allocate a block and fill it
// in with the appropriate values.
//
//----------------------------------------------------------------------
vcache_s* Hash( BYTE fsdid, DWORD key1, DWORD key2, BOOL * create, 
                BOOL *found)
{
    ENTER( Hash );
  vcache_s    *bucket;
  vcache_s    *h, *lru;
  int         i;

  Validate( TRUE );

  bucket  = (vcache_s *) &hashbuck[ (key1 ^ key2) % NUMBUCKETS ];
  h  	  = bucket->next;
  while ( h != bucket )	
    if ( h->fsdid == fsdid && h->key1 == key1  &&  h->key2 == key2 ) {
      // aha! We got it!
      *create = FALSE;
      *found  = TRUE;

#if 0
//****FIXFIX
      // see if it would have been a miss
      {
      DWORD depth=0;
      lru = LRUHead.lru_next;
      for( i=0; i < 26; i++ ) {
	if( lru == &LRUHead ) break;
	if( lru == h ) {
	  if( TotalBlocks > TotalPages && depth < TotalBlocks - TotalPages )
	     NumSave++;
	  break;
	}
	depth++;
	lru = lru->lru_next;
      }  
      if( TotalBlocks - depth < 1000 )
        DepthCount[ TotalBlocks - depth ]++;
      }
//****FIXFIX      
#endif

      // see if was hit on last 26 blocks
      lru = LRUHead.lru_next;
      for( i=0; i < 26; i++ ) {
	if( lru == &LRUHead ) break;
	if( lru == h ) {
	  NumHits++;
	  break;
	}
	lru = lru->lru_next;
      }

      EXIT( Hash );
      return h;
    } else 
      h = h->next;

  // didn't find it, see if we have to create it
  *found = FALSE;
  h = NULL;

  if ( *create  &&  BlockFree )  {
    h = BlockFree;
    BlockFree = h->next;

    // initialize fields
    h->buffer	    = NULL;
    h->key1	    = key1;
    h->key2	    = key2;
    h->holdcount    = 0;
    h->dirty        = 0;
    h->fsdid	    = fsdid;
    h->lru_next	    = NULL;
    h->lru_prev	    = NULL;
    h->compresstried= FALSE;
    h->iscompressed = FALSE;
//***FIXFIX
#if TESTING
    h->mirrored = FALSE;
#endif // TESTING
//***FIXFIX

    // clear fsd fields
    for(i=0;i<0x1C;i++) 
      h->reserved[i] = 0;
    
    // link it onto the list
    h->next	 = bucket->next;
    h->prev      = bucket;
    bucket->next->prev = h;
    bucket->next = h;

    *create = TRUE;

    Validate( TRUE );
  }

  // see if was miss on one of last 26 discards
  for( i=0; i < 26; i++ )
    if( DiscardList[ i ] == (key1 ^ key2) ) {
      NumMisses++;
      break;
    }
  
    EXIT( Hash );
  return h;
}


//----------------------------------------------------------------------
//
// ExpandPage
//
// Uncompresses (defragments) a page.
//
//----------------------------------------------------------------------
VOID ExpandPage( vcache_s *hCacheBlock )
{
    ENTER( ExpandPage );
    Validate( TRUE );

    // mark page as decompressed
    NumComp--;
    NumExpand++;
    hCacheBlock->iscompressed  = FALSE;

#if !COMPRESSON && TESTING
    // fake decompression
    {
    	PDWORD buf;
	DWORD len, diff;

//    	(PBYTE) hCacheBlock->buffer -= 0x10000000;
    	buf = (PDWORD) hCacheBlock->buffer;
	// copy to appropriate place
	if( hCacheBlock->mirrored ) {
	  diff = (DWORD) hCacheBlock->buffer - (DWORD) PageMirror;
	  memcpy( (PVOID)((DWORD) PageStart + diff),
		 hCacheBlock->buffer, PAGESIZE );
	  memset( hCacheBlock->buffer, 0, PAGESIZE );
	  hCacheBlock->buffer = (PVOID)((DWORD) PageStart + diff);
	  hCacheBlock->mirrored = FALSE;
	} else {
	  diff = (DWORD) hCacheBlock->buffer - (DWORD) PageStart;
	  memcpy( (PVOID) ((DWORD) PageMirror + diff), 
		 hCacheBlock->buffer, PAGESIZE );
	  memset( hCacheBlock->buffer, 0, PAGESIZE );
	  hCacheBlock->buffer = (PVOID)((DWORD) PageMirror + diff);
	  hCacheBlock->mirrored = TRUE;
	}
//    	for( len = 0; len < PAGESIZE/4; len++ )
//		buf[len] ^= (DWORD) -1;      
    }
    EXIT( ExpandPage );
    return;
#endif // COMPRESSON

#if TESTING
    if ( CheckSumComp( hCacheBlock ) != hCacheBlock->checksumc )  {
    	printf( "compressed checksum failure\n" );
	HoseMe();
    }
#endif
    // Defragment into temporary buffer
    Defragment( (memhdr_s *)hCacheBlock->buffer, tmppage );

#if TESTING
    hCacheBlock->buffer = NULL;
#endif // TESTING

    // this should never fail
    hCacheBlock->buffer = AllocatePage( TRUE );
    Decompress( tmppage, PAGESIZE, hCacheBlock->buffer, TRUE );
#if TESTING
    if ( CheckSum( hCacheBlock ) != hCacheBlock->checksum )  {
    	printf( "checksum failure\n" );
	HoseMe();
    }
#endif
    Validate( TRUE );
    EXIT( ExpandPage );
}


//----------------------------------------------------------------------
//
// ShrinkPage
//
// Compresses a page.
//
//----------------------------------------------------------------------
VOID ShrinkPage( vcache_s *hCacheBlock )
{
    ENTER( ShrinkPage );
    DWORD      len;

    Validate( TRUE );

#if TESTING
    hCacheBlock->checksum = CheckSum( hCacheBlock );
#endif
    if( hCacheBlock->iscompressed ) {
      printf("  SHRINKING BLOCK ALREADY SHRUNK\n");
      HoseMe();
    }

#if !COMPRESSON && TESTING
    // fake compression
    {
    PDWORD buf = (PDWORD) hCacheBlock->buffer;
//    for( len = 0; len < PAGESIZE/4; len++ )
//	buf[len] ^= (DWORD) -1;
    _asm mov esi, hCacheBlock;
    (FSDLIST( hCacheBlock->fsdid - FSDBASE ).discardcallbackfunc)();
//    (PBYTE) hCacheBlock->buffer += 0x10000000;
    NumComp++;
    }
    EXIT( ShrinkPage );
    return;
#endif // COMPRESSON

#if TESTING
    if ( ((DWORD) hCacheBlock->buffer - (DWORD) PageStart) % PAGESIZE )
    	HoseMe();
#endif // TESTING

    // when compressing, act like page is being discarded
    // need to do it this way to get around compiler bug
    _asm pusha;
    _asm mov esi, hCacheBlock;
    (FSDLIST( hCacheBlock->fsdid - FSDBASE ).discardcallbackfunc)();
    _asm popa;

    // Compress page if its worth it
    len = SafeCompress( (PBYTE) hCacheBlock->buffer, tmppage );
#if TESTING
    hCacheBlock->checksumc = CheckSumBuf( tmppage, len );
#endif
    if ( len != PAGESIZE )  {
      NumShrink++;
      NumComp++;
      DeallocatePage( hCacheBlock->buffer );
#if TESTING
      hCacheBlock->buffer = NULL;
#endif // TESTING

      // this will always succeed since we just freed space
      hCacheBlock->buffer = Fragment( tmppage, len, PAGENULL, FALSE );
      ((memhdr_s *) hCacheBlock->buffer)->prev = (memhdr_s *) hCacheBlock;
#if TESTING
      if ( hCacheBlock->checksumc != CheckSumComp( hCacheBlock ) )  {
      	printf( "bad compressed checksum\n" );
	Defragment( hCacheBlock->buffer, tmppage );
	if ( hCacheBlock->checksumc == CheckSumBuf( tmppage, len ) )  {
		printf( "poor fragment\n" );
	}
	HoseMe();
      }
#endif
#if 1 // TESTING ***FIXFIX
// for validation
      hCacheBlock->iscompressed = TRUE;
#endif // TESTING
    } else {
      // won't compress - mark at as a bad guy
      hCacheBlock->iscompressed  = FALSE;
      hCacheBlock->compresstried = TRUE;
    }

    Validate( TRUE );

    EXIT( ShrinkPage );
}

//***FIXFIX
TIMEOUTHANDLE  TestTimeoutHandle;
TIMEOUT_THUNK  TestTimeThunk;
BYTE OrigCount[0x1040];
BYTE SinceFirst[0x1040];
BOOL First=0;
BYTE PageCount[0x1040];
BYTE PageLast[0x1040];
DWORD testhandle;
PDWORD testbuffer;


DWORD NumFirst = 0;
DWORD NumZero = 0;
DWORD NumLastZero = 0;
VOID __stdcall TestTimeout(VMHANDLE hVM, PCLIENT_STRUCT pcrs,
			  PVOID RefData, DWORD Extra)
{
    ENTER( TestTimeout );
  DWORD       page, i;
  PDWORD      buffer;
  DWORD       first;

  NumZero = 0;
  NumFirst = 0;
  NumLastZero = 0;
  for( page = 0; page < 0x1040; page++ ) {
    buffer = (PDWORD) _MapPhysToLinear( (PVOID) (page << 0xC),
				       0x1000, 0 );
    if( !buffer) continue;
    if( page > 0xA0 && page < 0xF0 ) continue;
    first = buffer[0];
    for( i= 0 ; i < 0x400; i++ ) 
      if( buffer[i] != first) 
	break;
    if( i==0x400 ) {
      if( PageCount[ page ] ) {
	NumZero++;
	if( PageLast[ page ] )
	  NumLastZero++;
      }
      PageLast[ page ] = PageCount[ page ];
      if( !First )
	OrigCount[ page ] = 1;
      else
	if( OrigCount[ page ] )
	  NumFirst++;
      PageCount[ page ] = 1;
    } else {
      PageLast[ page ] = PageCount[ page ];
      OrigCount[ page ]  = 0;
      PageCount[ page ] = 0;
    }
  }
  First = TRUE;
  TestTimeoutHandle = Set_Global_Time_Out( UPDATEFREQ - Extra, 0, 
					   TestTimeout, &TestTimeThunk );
    EXIT( TestTimeout );
}
//***FIXFIX


#if !TESTING
//----------------------------------------------------------------------
//
// IdleCallback
//
// We compress pages while the system is idle. The kinds of pages that
// will be compressed by this routine that are missed by the standard
// checks are blocks that were dirty when they moved out of our
// default search depth in the lru and that later became clean, becoming
// compression candidates.
//
//----------------------------------------------------------------------
BOOL __stdcall IdleCallback( VMHANDLE SysVM, PCLIENT_STRUCT pcrs ) 
{
    ENTER( IdleCallback );

    _disable();
    if( !Busy++ ) {
      _enable();
      ShrinkOldPages( TRUE );
    } else
      _enable();
    Busy--;

    EXIT( IdleCallback );
    return FALSE;
}
#endif // TESTING

//----------------------------------------------------------------------
//
// MemInit
//
// Goes through the pages we initially allocated and strings them
// onto the free memory list.
//
//----------------------------------------------------------------------
BOOL MemInit( DWORD maxlen, DWORD len )
{
    ENTER( MemInit );
  DWORD         i;
  freehdr_s     *freehdr;
#if TESTING
  int freecount = 0;
#endif

  // set up free memory list
  FragList = NULL;
  freehdr  = (freehdr_s *) PageStart;
  PageList = freehdr;
  for( i = 0; i < (len/PAGESIZE)-1; i++ ) {
    freehdr->next = (freehdr_s *) ((DWORD) freehdr + PAGESIZE );
    freehdr->size  = PAGESIZE;
    freehdr = freehdr->next;
  }

  // do the last one
  freehdr->next  = NULL;
  freehdr->size  = PAGESIZE;

#if TESTING
  freehdr = PageList;
  while( freehdr ) {
    freecount++;
    freehdr = freehdr->next;
  }
  printf(" free pages: %d\n", freecount );
#endif

  // initialize hash buckets
  for ( i = 0; i < NUMBUCKETS; ++i )  {
    hashbuck[i].next = (vcache_s *) &hashbuck[i];
    hashbuck[i].prev = (vcache_s *) &hashbuck[i];
  }

  // initialize LRU
  LRUHead.lru_next = &LRUHead;
  LRUHead.lru_prev = &LRUHead;
    EXIT( MemInit );
  return TRUE;
}

//======================================================================
//
//                          VCACHE APIs
//
//======================================================================


//----------------------------------------------------------------------
//
// CTVCACHE_Get_Version
// 
// Returns our version.
// 
//----------------------------------------------------------------------
DWORD __cdecl CTVCACHE_Get_Version (void )
{
    ENTER( CTVCACHE_Get_Version  );
#if 1
    EXIT( CTVCACHE_Get_Version  );
    return 0x100;
#else
    EXIT( CTVCACHE_Get_Version  );
    return TVCache_Major << 16 | TVCache_Minor;
#endif
}


//----------------------------------------------------------------------
//
// CTVCACHE_Register
// 
// This is called whenever a new FSD coms in and registers with VCache
// services. Note that in practice a typical system will only have
// one VCache client: VFAT.
// 
//----------------------------------------------------------------------
BYTE __cdecl CTVCACHE_Register( PVOID pBufferDiscardProc, DWORD nBlocks ) 
{
    ENTER( CTVCACHE_Register );
  DWORD   fsd;

  fsd = fsdcount++;

  FSDLIST( fsd ).discardcallbackfunc = pBufferDiscardProc;
  FSDLIST( fsd ).minblocks	     = nBlocks;
  FSDLIST( fsd ).blockcnt	     = 0;

    EXIT( CTVCACHE_Register );
  return (BYTE) (fsd + FSDBASE);
}


//----------------------------------------------------------------------
//
// CTVCACHE_GetSize
//
// Docs are wrong. Returns current size, and sets max and min sizes.
// 
//----------------------------------------------------------------------
DWORD __cdecl CTVCACHE_GetSize (BYTE FsdID, 
				PDWORD pMaxBlocks,
				PDWORD pMinBlocks )
{
    ENTER( CTVCACHE_GetSize  );

    *pMaxBlocks = MaxSize;
    *pMinBlocks = MinSize;

    EXIT( CTVCACHE_GetSize  );
    return TotalPages;
}


//----------------------------------------------------------------------
//
// CTVCACHE_CheckAvail
//
// Report the number of blocks that are free. Note that we do not
// take into account any compression. This means that we report exactly
// how much is free.
// 
//----------------------------------------------------------------------
DWORD __cdecl CTVCACHE_CheckAvail( BYTE FsdID, DWORD NumberOfBuffersNeeded )
{
    ENTER( CTVCACHE_CheckAvail );
    freehdr_s     *list;
    DWORD	  cnt = 0;
    vcache_s      *lru;

    Busy++;

    for ( list = PageList; list; list = list->next )
	cnt += PAGESIZE;

    // Early exit if we can easily satisy the request 
    if ( cnt/PAGESIZE >= NumberOfBuffersNeeded ) {
        Busy--;
	EXIT( CTVCACHE_CheckAvail );
	return NumberOfBuffersNeeded;
    }

    for ( list = FragList; list; list = list->next )
	cnt += list->size;

    // Early exit if we can easily satisy the request 
    if ( cnt/PAGESIZE >= NumberOfBuffersNeeded ) {
        Busy--;
	EXIT( CTVCACHE_CheckAvail );
	return NumberOfBuffersNeeded;
    }

    // if no free memory, check the LRU
    if( cnt/PAGESIZE < NumberOfBuffersNeeded ) {
      lru = LRUHead.lru_next;
      while( lru != &LRUHead ) {
	if( !lru->dirty && !lru->holdcount ) {

	  // is it too young?
	  if( CacheBufRRT >= CurTime - lru->age )
	    break;

	  cnt += PAGESIZE; 

	  // have we satisified the request?
	  if( cnt/PAGESIZE == NumberOfBuffersNeeded )
	    break;
	}
	lru = lru->lru_next;
      }
    }

    Busy--;

    EXIT( CTVCACHE_CheckAvail );
    return cnt / PAGESIZE;
}


//----------------------------------------------------------------------
//
// CTVCACHE_FindBlock
//
// This is the work-horse routine of VCache. 
// 
//----------------------------------------------------------------------
BOOL __cdecl CTVCACHE_FindBlock( BYTE FsdID, BYTE Opt, DWORD key1, DWORD key2,
			       PHANDLE phCacheBlock, PBOOL pbBufferIsLocked,
			       PVOID *pBufferAddress )
{
    int foo = (DumpCounts(),0);
    ENTER( CTVCACHE_FindBlock );
    BOOL	create;	
    BOOL        found;
    vcache_s *	block;

    Validate( TRUE );

    Busy++;

    // default for failure
    *phCacheBlock     = NULL;
    *pbBufferIsLocked = FALSE;
    *pBufferAddress   = 0;
    create = Opt & VCFB_Create;
    block = Hash( FsdID, key1, key2, &create, &found );

#if 0
    printf("%d:Findblock: %x ", ++Renter, block );
    if( Opt & VCFB_Create ) printf("create ");
    if( Opt & VCFB_Hold ) printf("hold ");
    if( Opt & VCFB_MustCreate ) printf("mustcreate ");
    if( Opt & VCFB_RemoveFromLRU ) printf("removelru ");
    if( Opt & VCFB_MakeMRU ) printf("makemru ");
    if( Opt & VCFB_LowPri ) printf("lowpri ");
    printf("\n"); 
#endif

    if ( !block )  {
        // didn't find it
        Busy--;
        EXIT( CTVCACHE_FindBlock );
	return found;
    } else if ( create )  {
	if ( Opt & VCFB_LowPri )  {
	    // not implemented 
	} else if ( Opt & VCFB_MustCreate )  {
	    // do same as create (we're not sure what this really
            // means!)
            LastTouched = block;
            FindTouched = block;
	    block->buffer = AllocatePage( TRUE );
	    if ( block->buffer == NULL ) {
	        FreeBlock( block );
		Busy--;
		EXIT( CTVCACHE_FindBlock );
		return found;
	    }
	    // by default, put on LRU
	    if( !(Opt & VCFB_RemoveFromLRU ))
 	       InsertMRU( block );
            else
	       block->age = CurTime++;
	} else {
	    // allocate memory for block 
            LastTouched = block;
	    FindTouched = block;
	    block->buffer = AllocatePage( FALSE );
	    if ( block->buffer == NULL ) {
	        FreeBlock( block );
		Busy--;
		EXIT( CTVCACHE_FindBlock );
		return found;
	    }
	    // by default, put on LRU
	    if( !(Opt & VCFB_RemoveFromLRU ))
	      InsertMRU( block );
	    else
	      block->age = CurTime++;
#if TESTING
	    memset( block->buffer, 0xA5, PAGESIZE );
	    block->checksum = CheckSum( block );
#endif // TESTING
	}
    }
    
    Validate( TRUE );

    // if we got a block, excersize options, otherwise fail
    if ( block )  {
	if ( Opt & VCFB_Hold )
	    CTVCACHE_Hold( (HANDLE) block );
	if ( Opt & VCFB_MakeMRU )
	    CTVCACHE_MakeMRU( (HANDLE) block );
	if ( Opt & VCFB_RemoveFromLRU ) 
            RemoveLRU( block );
	*phCacheBlock	  = block;
	*pbBufferIsLocked = block->holdcount != 0;
	*pBufferAddress	  = block->buffer;

	Validate( TRUE );
        
        LastTouched = block;
        FindTouched = block;

        // if block is compressed, need to expand it
        if( block->iscompressed )  
          ExpandPage( block );
        Busy--;

        FindTouched = NULL;
        EXIT( CTVCACHE_FindBlock );
	return found;
    } else {

        // miss with no create or failed create
        Busy--;

        FindTouched = NULL;
        EXIT( CTVCACHE_FindBlock );
	return found;
    }
}


//----------------------------------------------------------------------
//
// CTVCACHE_FreeBlock
//
// Just let the block and its data go. We don't call the block's
// callback routine since the FSD is supposed to already have cleared
// up the data.
// 
//----------------------------------------------------------------------
VOID __cdecl CTVCACHE_FreeBlock( vcache_s *hCacheBlock )
{
    ENTER( CTVCACHE_FreeBlock );
  Busy++;
  LastTouched = hCacheBlock;
  FreeBlock( hCacheBlock );
  Busy--;

    EXIT( CTVCACHE_FreeBlock );
}


//----------------------------------------------------------------------
//
// CTVCACHE_MakeMRU
//
// Move the block to the front of the LRU list (the LRU list actually
// has the MRU block on the front of it).
// 
//----------------------------------------------------------------------
VOID __cdecl CTVCACHE_MakeMRU( vcache_s *hCacheBlock) 
{
    ENTER( CTVCACHE_MakeMRU );
  Busy++;
  LastTouched = hCacheBlock;
  Validate( TRUE );
  RemoveLRU( hCacheBlock );
  InsertMRU( hCacheBlock );
  Validate( TRUE );
  Busy--;

    EXIT( CTVCACHE_MakeMRU );
}


//----------------------------------------------------------------------
//
// CTVCACHE_Hold
//
// Increase the lock count on the block. If it was previously unheld,
// it might be in compressed state. In that case we have to decmopress
// it before the system accesses it.
// 
//----------------------------------------------------------------------
DWORD __cdecl CTVCACHE_Hold( vcache_s *hCacheBlock )
{
    ENTER( CTVCACHE_Hold );
    Validate( TRUE );

    Busy++;
    
    // put the hold on the page first, so we don''t age it!
    hCacheBlock->holdcount++;

    LastTouched = hCacheBlock;
    if ( hCacheBlock->holdcount == 1 && hCacheBlock->iscompressed )
	ExpandPage( hCacheBlock );

    Validate( TRUE );

    Busy--;

    EXIT( CTVCACHE_Hold );
    return hCacheBlock->holdcount-1;
}


//----------------------------------------------------------------------
//
// CTVCACHE_Unhold
//
// Here is where we can perform our compression. When the hold count
// goes to zero and the buffer is clean, we know that the FSD will not
//  access the data in the buffer. So, we compress the data if we can.
// 
//----------------------------------------------------------------------
DWORD __cdecl CTVCACHE_Unhold( vcache_s *hCacheBlock) 
{
    ENTER( CTVCACHE_Unhold );
    vcache_s    *cmprss;

    Busy++;

#if TESTING
    // recalc checksum
    hCacheBlock->checksum = CheckSum( hCacheBlock );
#endif 

    // don't compress pages that are too near the front of the LRU
    LastTouched = hCacheBlock;

#if COMPRESSON || TESTING
    // are we the only ones in here right now?
    if( Busy == 1 ) {

      Validate( TRUE );

      // check to see if there is somebody to compress 
      ShrinkOldPages( FALSE );

      Validate( TRUE );
    }
#endif

    Busy--;

    EXIT( CTVCACHE_Unhold );
    return hCacheBlock->holdcount--;
}


#if TESTING
VOID EnumCallback( PVOID handler, vcache_s *block, PDWORD ebx, PDWORD ecx, 
                   PDWORD ebp, BOOL held )
{
    ENTER( EnumCallback );
    EXIT( EnumCallback );
}
#endif // TESTING

//----------------------------------------------------------------------
//
// CTVCACHE_Enum
//
// An FSD calls this routine when it wants to flush the cache, for
// example, where a common routine is called for each block that
// is stored in the cache.
// 
//----------------------------------------------------------------------
VOID __cdecl CTVCACHE_Enum( BYTE FsdID, PVOID pCallback,
			  DWORD refebx, DWORD refecx, DWORD refebp )
{
    ENTER( CTVCACHE_Enum );
  DWORD j;

  Busy++;
  for ( j = 0; j < MaxBlocks; ++j )
    if ( BlockList[j].fsdid == FsdID ) {
      EnumCallback( pCallback,  &BlockList[ j ], 
		   &refebx, &refecx, &refebp,
		   BlockList[j].holdcount == 0 );
    }
  Busy--;
    EXIT( CTVCACHE_Enum );
}


//----------------------------------------------------------------------
//
// CTVCACHE_TestHold
//
// Return the usage count for the block.
// 
//----------------------------------------------------------------------
DWORD __cdecl CTVCACHE_TestHold( vcache_s *hCacheBlock) 
{
    ENTER( CTVCACHE_TestHold );
  LastTouched = hCacheBlock;
    EXIT( CTVCACHE_TestHold );
  return hCacheBlock->holdcount;
}


//----------------------------------------------------------------------
//
// CTVCACHE_GetStats
//
// VMM calls this routine periodically to get the cache's hit/miss
// ratios. This figures into the VMM's decisions to grow and shrink
// the cache.
// 
//----------------------------------------------------------------------
VOID __cdecl CTVCACHE_GetStats( PDWORD pNumberOfMisses, PDWORD pNumberOfHits,
			     PVOID  *pBaseAddr,
			     PDWORD pNumberOfDiscardedBlocks )
{
    ENTER( CTVCACHE_GetStats );
  // tell VMM what we've been up to 
  *pNumberOfMisses		= NumMisses;
  *pNumberOfHits		= NumHits;
  *pBaseAddr			= (PVOID) PageStart;
  *pNumberOfDiscardedBlocks	= NumDiscards;

  // reset
  NumMisses   = 0;
  NumHits     = 0;
  NumDiscards = 0;
    EXIT( CTVCACHE_GetStats );
}


//----------------------------------------------------------------------
//
// CTVCACHE_Deregister
//
// This routine is typicall only called by an FSD when the system
// is shutting down.
// 
//----------------------------------------------------------------------
VOID __cdecl CTVCACHE_Deregister( BYTE FsdID )
{
    ENTER( CTVCACHE_Deregister );
  // age everybody with that fsdid
    EXIT( CTVCACHE_Deregister );
}


//----------------------------------------------------------------------
//
// CTVCACHE_AdjustMinimum
//
// We guarentee that we always have enough space to hold the 
// uncompressed number of blocks that satisfy the minimums for the
// fsd.
// 
//----------------------------------------------------------------------
BOOL __cdecl CTVCACHE_AdjustMinimum( BYTE FsdID, DWORD NewBufferQuota ) 
{
    ENTER( CTVCACHE_AdjustMinimum );
  FSDLIST( FsdID - FSDBASE ).minblocks = NewBufferQuota;
    EXIT( CTVCACHE_AdjustMinimum );
  return FALSE; // FIX THIS
}


//----------------------------------------------------------------------
//
// CTVCACHE_SwapBuffers
// 
// Just swap the buffers that are owned by to blocks. This routine 
// assumes that the buffers are held so they are uncompressed.
// 
//----------------------------------------------------------------------
BOOL __cdecl CTVCACHE_SwapBuffers( vcache_s *hCacheBlock1, 
				vcache_s *hCacheBlock2 )
{
    ENTER( CTVCACHE_SwapBuffers );
  PVOID     tmp;

  if ( hCacheBlock1->holdcount == 0  ||  hCacheBlock2->holdcount == 0 )  {
    EXIT( CTVCACHE_SwapBuffers );
    return FALSE;
  }

  tmp = hCacheBlock1->buffer;
  hCacheBlock1->buffer = hCacheBlock2->buffer;
  hCacheBlock2->buffer = tmp;

    EXIT( CTVCACHE_SwapBuffers );
  return TRUE;
}


//----------------------------------------------------------------------
//
// CTVCACHE_RelinquishPage
//
// We have to give a page back to VMM if we at all possibly can do it.
// 
//----------------------------------------------------------------------
PVOID __cdecl CTVCACHE_RelinquishPage( void ) 
{
    PVOID linear;

    ENTER( CTVCACHE_RelinquishPage );
    Busy++;

    if ( TotalPages > MinSize )
	linear = AllocatePage( FALSE );
    else
	linear = NULL;

    if ( linear )  {
	CacheBufRRT = TotalPages*3/2;
	TotalPages--;
    }

    Busy--;
    EXIT( CTVCACHE_RelinquishPage );
    return linear;
}


//----------------------------------------------------------------------
//
// CTVCACHE_UseThisPage
//
// The system is giving us a full page. Simply mark the reference mask
// as free, and put it on the front of the memory free list.
// 
//----------------------------------------------------------------------
VOID __cdecl CTVCACHE_UseThisPage( PVOID linear ) 
{
    ENTER( CTVCACHE_UseThisPage );

    if( TotalPages < MaxSize ) {
	Busy++;
	TotalPages++;
	DeallocatePage( linear );
	CacheBufRRT = TotalPages*3/2;
	Busy--;
    }

    EXIT( CTVCACHE_UseThisPage );
}


//----------------------------------------------------------------------
//
// CTVCACHE_CreateLookupCache
// 
//----------------------------------------------------------------------
#if TESTING
// FIX
#define HLOOKUP DWORD 
#endif // TESTING
DWORD __cdecl _CTVCACHE_CreateLookupCache (PCHAR szName, DWORD nMaxElems,
					 DWORD Flags, HLOOKUP *phlookup) 
{
    ENTER( _CTVCACHE_CreateLookupCache  );
    EXIT( _CTVCACHE_CreateLookupCache  );
	return 0;
}


//----------------------------------------------------------------------
//
// CTVCACHE_CloseLookupCache
// 
//----------------------------------------------------------------------
DWORD __cdecl _CTVCACHE_CloseLookupCache ( HLOOKUP _hnd_ ) 
{
    ENTER( _CTVCACHE_CloseLookupCache  );
    EXIT( _CTVCACHE_CloseLookupCache  );
	return 0;
}


//----------------------------------------------------------------------
//
// CTVCACHE_DeleteLookupCache
// 
//----------------------------------------------------------------------
DWORD __cdecl _CTVCACHE_DeleteLookupCache (PCHAR szName) 
{
    ENTER( _CTVCACHE_DeleteLookupCache  );
    EXIT( _CTVCACHE_DeleteLookupCache  );
	return 0;	
}


//----------------------------------------------------------------------
//
// CTVCACHE_Lookup
// 
//----------------------------------------------------------------------
DWORD __cdecl _CTVCACHE_Lookup ( HLOOKUP hLookup, ULONG keylen, PVOID pKey,
			       PULONG pdatalen, PVOID pData) 
{
    ENTER( _CTVCACHE_Lookup  );
    EXIT( _CTVCACHE_Lookup  );
	return 0;
}


//----------------------------------------------------------------------
//
// CTVCACHE_UpdateLookup
// 
//----------------------------------------------------------------------
DWORD __cdecl _CTVCACHE_UpdateLookup (HLOOKUP hLookup, ULONG keylen, 
				    PVOID pKey, ULONG datalen, PVOID pData) 
{
    ENTER( _CTVCACHE_UpdateLookup  );
    EXIT( _CTVCACHE_UpdateLookup  );
 	return 0;
}


//======================================================================
//
//                     CONTROL HANDLERS
//
//======================================================================
#if !TESTING
DefineControlHandler(SYS_CRITICAL_INIT, OnSysCriticalInit);
DefineControlHandler(DEVICE_INIT, OnDeviceInit);
DefineControlHandler(SYS_CRITICAL_EXIT, OnSysCriticalExit);

//----------------------------------------------------------------------
//
// ControlDispatcher
//
// Calls routines to handle VMM messages.
//
//----------------------------------------------------------------------
BOOL ControlDispatcher(
  DWORD dwControlMessage,
  DWORD EBX,
  DWORD EDX,
  DWORD ESI,
  DWORD EDI,
  DWORD ECX)
{
#if 0
    ENTER( ControlDispatcher );
#endif
  START_CONTROL_DISPATCH

    ON_SYS_CRITICAL_INIT(OnSysCriticalInit);
    ON_DEVICE_INIT(OnDeviceInit);
    ON_SYS_CRITICAL_EXIT(OnSysCriticalExit);

  END_CONTROL_DISPATCH
#if 0
    EXIT( ControlDispatcher );
#endif
  return TRUE;
}
#endif

//----------------------------------------------------------------------
//
// OnSysCriticalInit
//
// Simply query the system.ini file for mincache and maxcache values.
//
//----------------------------------------------------------------------
BOOL OnSysCriticalInit(VMHANDLE hVM, PCHAR CommandTail, DWORD refData)
{
    ENTER( OnSysCriticalInit );
#if !TESTING
  DWORD   status;  

  // Don't initialize more than once
  if ( SysCriticalInitComplete )  {
    EXIT( OnSysCriticalInit );
    return TRUE;
  }
  SysCriticalInitComplete = TRUE;

  // Replace VCACHE with ourself 
  {
    PDDB old = Get_DDB(  VCache_Device_ID, NULL );
    PDDB new = Get_DDB( TVCache_DEVICE_ID, NULL );

    if ( !old  ||  !new )  {
      EXIT( OnSysCriticalInit );
      return FALSE;
    }

    // Replace critical services of original vcache with our own
    old->DDB_Control_Proc		= new->DDB_Control_Proc;
    old->DDB_V86_API_Proc		= new->DDB_V86_API_Proc;
    old->DDB_PM_API_Proc		= new->DDB_PM_API_Proc;
    old->DDB_V86_API_CSIP		= new->DDB_V86_API_CSIP;
    old->DDB_PM_API_CSIP		= new->DDB_PM_API_CSIP;
    old->DDB_Reference_Data		= new->DDB_Reference_Data;
    old->DDB_VxD_Service_Table_Ptr	= new->DDB_VxD_Service_Table_Ptr;
    old->DDB_VxD_Service_Table_Size	= new->DDB_VxD_Service_Table_Size;
    old->DDB_Win32_Service_Table	= new->DDB_Win32_Service_Table;
  }

  // query system.ini for min and max sizes
  if( (MinSize = Get_Profile_Decimal_Int( 0, "vcache",
				    "minfilecache", &status )) != 0 ) 
  
  if( (MaxSize = Get_Profile_Decimal_Int( 0, "vcache",
					 "maxfilecache", &status )) != 0 )
#endif // TESTING


//***FIXFIX
  TestTimeoutHandle = Set_Global_Time_Out( UPDATEFREQ, 0, 
					   TestTimeout, &TestTimeThunk );


#if MEMGRAB
  if ( ! _PageAllocate( MEMGRAB, PG_SYS, 0, 0, 0, 0, 0,
		  PAGELOCKED|PAGEZEROINIT, &testhandle, 
                  (PVOID *) &testbuffer ) )
       _PageAllocate( MEMGRAB, PG_SYS, 0, 0, 0, 0, 0,
		  PAGELOCKED|PAGEZEROINIT, &testhandle, 
                  (PVOID *)&testbuffer );
  if ( testhandle ) {
          int i, j, k;
          k=0;
          for( i= 0; i < MEMGRAB; i++ ) 
            for( j = 0; j < PAGESIZE/4; j++ )
              testbuffer[k++] = k;
  }
#endif /* MEMGRAB */
//***FIXFIX

    EXIT( OnSysCriticalInit );
  return TRUE;
}

#if TESTING
DWORD GetFreePageCount( DWORD flags, PDWORD lockable )
{
    ENTER( GetFreePageCount );
	*lockable = 420;
    EXIT( GetFreePageCount );
 	return 420;
}
#endif // TESTING

//----------------------------------------------------------------------
//
// OnDeviceInit
//
// Allocate all the memory we could ever hope for. 
//
//----------------------------------------------------------------------
BOOL OnDeviceInit( VMHANDLE hVM, PCHAR CommandTail)
{
    ENTER( OnDeviceInit );
  DWORD     pagelockable;
  DWORD	    j;

  // Don't initialize more than once
  if ( DeviceInitComplete )  {
    EXIT( OnDeviceInit );
    return TRUE;
  }
  DeviceInitComplete = TRUE;

#if !TESTING && COMPRESSON
  // set up an idle time compression call-back
  Call_When_Idle( IdleCallback, &IdleThunk );
#endif // TESTING

  // see how much memory is free
  GetFreePageCount( 0, &pagelockable );

  MaxSize = CEIL( MaxSize, 4 );
  MinSize = CEIL( MinSize, 4 );

  // determine minsize
  if ( MinSize == 0 )  {
    if ( pagelockable < 0x500 )
        MinSize = pagelockable / 10;
    else
        MinSize = pagelockable / 6;
    MinSize /= 4;
  }

  if ( pagelockable > 0x188 )
    MaxSize = pagelockable - 0x180;
  else
    MaxSize = 8;

  if ( MinSize > MaxSize )
      MinSize = MaxSize;

  if ( MinSize < 8 )
      MinSize = 8;

  if ( MaxSize < MinSize )
      MaxSize = MinSize;

  if ( MaxSize >= 0x32000 )  {
      MaxSize = 0x32000;
      if ( MinSize > 0x32000 )
          MinSize = 0x32000;
  }

  // limit to a max compression ratio of 3:1
  MaxBlocks = MaxSize * 3;
  NumBlocks = 0;

  // allocate data structures
  BlockList = (vcache_s *) HeapAllocate( sizeof(vcache_s) * MaxBlocks, 
					 HEAPZEROINIT );

  // build a free list of block structures
  BlockFree = & BlockList[0];
  for ( j = 0; j < MaxBlocks-1; ++j )
      BlockList[j].next = & BlockList[j+1];

  // allocate memory
  PageStart = (PVOID) PageReserve( PR_SYSTEM, MaxSize, PR_FIXED );
  if ( PageStart == (PVOID) 1 )  {
    EXIT( OnDeviceInit );
    return FALSE;
  }

#if !TESTING
  // do page commit here
  NumBlocks  = MIN( 0x40, pagelockable );
  NumBlocks  = MAX( (long)NumBlocks, (long)MaxSize - 0x400 );
  NumBlocks  = MIN( NumBlocks, MaxSize );
  NumBlocks  = MIN( NumBlocks, 0x900 );
  TotalPages = NumBlocks;
  if ( ! PageCommit( (DWORD)PageStart/PAGESIZE, NumBlocks,
		    PD_FIXEDZERO, 0, PC_FIXED ) )
  {
    EXIT( OnDeviceInit );
    return FALSE;
  }

  if ( CacheBufRRT == 0 )
      CacheBufRRT = TotalPages*3/2;
  CurTime     = CacheBufRRT+1;
#else  // TESTING
  TotalPages = MaxSize;
  NumBlocks  = MaxSize;
#endif // TESTING

#if !COMPRESSON && TESTING
  // ****FIXFIX
  // allocate a mirrored buffer for switching!
  PageMirror = (PVOID) PageReserve( PR_SYSTEM, MaxSize, PR_FIXED );
  if( PageMirror == (PVOID) 1 )  {
    EXIT( OnDeviceInit );
    return FALSE;
  }
  if ( ! PageCommit( (DWORD)PageMirror/PAGESIZE, NumBlocks,
		    PD_FIXEDZERO, 0, PC_FIXED ) )
  {
    EXIT( OnDeviceInit );
    return FALSE;
  }
#endif // COMPRESSON

  // set up our memory
  if ( ! MemInit( MaxSize * PAGESIZE, NumBlocks * PAGESIZE ) )  {
    EXIT( OnDeviceInit );
    return FALSE;
  }

  EXIT( OnDeviceInit );
  return TRUE;
}



VOID OnSysCriticalExit(void)
{
    ENTER( OnSysCriticalExit ); 
    EXIT( OnSysCriticalExit ); 
}


#if TESTING

VOID discard(vcache_s *disblock)
{
}


main()
{
	DWORD  	count=0;
	BOOL    pages[1000];
	vcache_s *handle[1000];
	BYTE	fsdid;
	PVOID	buffer;
	BOOL	isheld;
	DWORD	page;
	vcache_s *rethandle;
	DWORD   fraction;
	DWORD	j;
	int	heldguy = -1; 

	memset( pages, TRUE, 1000 * sizeof(BOOL) );

	OnDeviceInit( 0, NULL);

	fsdid = CTVCACHE_Register( discard, 4);
	while( count < 100000 ) {
		switch(rand()%6) {
		case 0: 
			// cache a new block
			page = rand() % 1000;
			if( !handle[page] ) {
				CTVCACHE_FindBlock( fsdid, VCFB_Create, page, page+1,
			       		(PVOID) &handle[page], &isheld,
			       		&buffer );
				if( handle[page] )  {
					fraction = rand() % 4095;
					memset( handle[page]->buffer, page, PAGESIZE-fraction );
					for ( j = fraction; j < PAGESIZE; ++j )
						((BYTE *)handle[page]->buffer)[j] = rand();
					handle[page]->checksum = CheckSum( handle[page] );
				}
			} else {
				if( handle[page]->fsdid && !handle[page]->holdcount ) {
					CTVCACHE_FreeBlock( handle[page] );
					handle[page] = NULL;
				}
			}
			printf("%d: Create(%d): %x\n", count++, page, handle[page]);
			break;
		case 1:
			// lookup an existing block
			page = rand() % 1000;
			if( handle[page] ) {
				// cache a new block
				CTVCACHE_FindBlock( fsdid, 0, page, page+1,
			       		(PVOID) &rethandle, &isheld,
			       		&buffer );
				printf("%d: Find(%d): %x\n", count++, page, rethandle );
			}
			break;
		case 2:
		case 3:
			// hold a block
			page = rand() % 1000;
			if( handle[page] && heldguy == -1 &&
				 handle[page]->fsdid && 
				 handle[page]->holdcount == 0 ) {
				CTVCACHE_Hold( handle[page] );
				printf("%d: Hold(%d): %d\n", count++, 
					page, handle[page]->holdcount);
				heldguy = page;
				Validate( TRUE );
			}
		//	break;
		//case 3:
			// unhold a block
			if( heldguy != -1 ) {
				CTVCACHE_Unhold( handle[heldguy] );
				printf("%d: Unhold(%d): %d\n", count++,
					heldguy, handle[heldguy]->holdcount );
				heldguy = -1;
				Validate( TRUE );
			}
			break;
		case 4:
			// use a page
			page = rand() % 1000;
			if( !pages[page] ) {
				CTVCACHE_UseThisPage( (PBYTE) PageStart + page*PAGESIZE );
				printf("%d: Use: %d\n", count++, page );
				pages[page] = TRUE;
			}
			break;
		case 5:
			// get back a page
			if( !(rand() % 10) ) {
				page = (DWORD) CTVCACHE_RelinquishPage();
				if( page ) {
					printf("%d: Relinquish: %d\n", count++,
						((PBYTE)page-(PBYTE)PageStart)/PAGESIZE );
					pages[ ((PBYTE)page-(PBYTE)PageStart)/PAGESIZE] = FALSE;
				}
			}
			break;
		}
	}
	return 0;
}
#endif // TESTING
