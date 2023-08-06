//======================================================================
//
// VCMON.c - main module for VxD VCMON
//
// Copyright (c) 1996 Mark Russinovich and Bryce Cogswell
//
//======================================================================
#define   DEVICE_MAIN
#include  "vcmon.h"
#undef    DEVICE_MAIN
#include  "stats.h"

// this should be set to 1 if you want debug output to Winice or WDEB386
#define DEBUGOUT  0

Declare_Virtual_Device(VCMON)

DefineControlHandler(SYS_DYNAMIC_DEVICE_INIT, OnSysDynamicInit);
DefineControlHandler(SYS_DYNAMIC_DEVICE_EXIT, OnSysDynamicExit);
DefineControlHandler(SYS_CRITICAL_INIT, OnSysCriticalInit);
DefineControlHandler(SYS_CRITICAL_EXIT, OnSysCriticalExit);
DefineControlHandler(W32_DEVICEIOCONTROL, OnW32Deviceiocontrol);


//----------------------------------------------------------------------
//
//                            LOCKED DATA
//
//----------------------------------------------------------------------
#include LOCKED_DATA_SEGMENT

// statistics
VcmonStatus_s     Stats;
DWORD             GuiEvent;
TIMEOUTHANDLE     GuiTimeoutHandle;
TIMEOUT_THUNK     GuiTimeThunk;

// sample rate
DWORD             SampleRate = STATSFREQ;
BOOL              Connected  = FALSE;
BOOL              FailConnect = FALSE;

// thunks and original service addresses
PVOID                 VCacheFindBlockService;
DeviceService_THUNK   thunkVCacheFindBlockHook;

PVOID                 VCacheHoldService;
DeviceService_THUNK   thunkVCacheHoldHook;

PVOID                 VCacheGetStatsService;
DeviceService_THUNK   thunkVCacheGetStatsHook;

PVOID                 PageFileReadWriteService;
DeviceService_THUNK   thunkPageFileReadWriteHook;


//----------------------------------------------------------------------
//
//                            LOCKED CODE
//
//----------------------------------------------------------------------
#include LOCKED_CODE_SEGMENT

#if !DEBUGOUT
//----------------------------------------------------------------------
//
// dprintf
//
// If no debug output is desired, this routine stifles the real dprint
// with a no-op.
//
//----------------------------------------------------------------------
void dprintf(const char *format, ...)
{

}
#endif DEBUGOUT

//----------------------------------------------------------------------
// 
// VCMON_Get_Version
//
// Returns our version number.
//
//----------------------------------------------------------------------
DWORD _cdecl VCMON_Get_Version( void )
{
    return VCMON_Major << 8 | VCMON_Minor;
}


//----------------------------------------------------------------------
//
// GuiTimeout
//
// At the specified sample Each second we signal the Gui, telling it that there are some new
// statistics ready for it to pick up.
//
//----------------------------------------------------------------------
VOID __stdcall GuiTimeout(VMHANDLE hVM, PCLIENT_STRUCT pcrs,
			  PVOID RefData, DWORD Extra)
{
  DWORD     lockable;

  // get size of cache
  Stats.size = VCache_GetSize(0, &Stats.size ) * 4;

  // get amount of free memory
  Stats.free = GetFreePageCount( 0, &lockable ) * 4;

  // signal the gui
  VWIN32_DIOCCompletionRoutine( (DWORD) GuiEvent );

  // set-up for next timeout
  if( Extra >= SampleRate ) Extra = SampleRate;
  GuiTimeoutHandle = Set_Global_Time_Out( SampleRate - Extra, 0, 
					 GuiTimeout, &GuiTimeThunk );
}


//----------------------------------------------------------------------
//
// PageFileReadHook
//
// This hook monitors page faults, distinguishing reads from writes.
//
//----------------------------------------------------------------------
VOID __stdcall PageFileReadWriteHook( PDSFRAME pDS )
{
  pagefilecmd_s   *pfcmd = (pagefilecmd_s *) pDS->REBX;

  if( pfcmd->cmd == READ )
    Stats.pagereads++;
  else
    Stats.pagewrites++;
  Call_Previous_Hook_Proc( pDS, &thunkPageFileReadWriteHook );  
}


//----------------------------------------------------------------------
//
// VCacheGetStatsHook
//
// VMM calls this routine periodically to determine how many hits
// and misses occur at, or just past, the end of the LRU. See the 
// VCache documentation for details.
//
//----------------------------------------------------------------------
VOID __stdcall VCacheGetStatsHook( PDSFRAME pDS )
{
  Call_Previous_Hook_Proc( pDS, &thunkVCacheGetStatsHook );  
  Stats.repmisses += pDS->REBX;
  Stats.rephits   += pDS->RECX;
  dprintf("GetStats: miss:%d hits:%d disc:%d\n", pDS->REBX, pDS->RECX,
	  pDS->REDI );
}


//----------------------------------------------------------------------
//
// VCacheHoldHook
//
// Hold is called whenever VFAT wants to access the data related
// to a block
//
//----------------------------------------------------------------------
VOID __stdcall VCacheHoldHook( PDSFRAME pDS )
{
  dprintf("Hold: %x\n", pDS->RESI );
  Call_Previous_Hook_Proc( pDS, &thunkVCacheHoldHook );
  Stats.holds++;
}


//----------------------------------------------------------------------
//
// VCacheFindBlockHook
//
// FindBlock is the main routine called to determine whether a block
// is in the cache or not. We break it into two calls: a lookup followed
// by a create if that is requested so that we can distinguis misses
// from creates.
//
//----------------------------------------------------------------------
VOID __stdcall VCacheFindBlockHook( PDSFRAME pDS )
{
  BOOL  create = FALSE;
  DSFRAME  saveregs;

  dprintf("FindBlock - ");
  if( pDS->REAX & VCFB_Create) 
    dprintf("create ");
  if( pDS->REAX & VCFB_MustCreate) 
    dprintf("Must-Create");
  if( pDS->REAX & VCFB_Hold)
    dprintf("hold ");
  if( pDS->REAX & VCFB_LowPri)
    dprintf("low-pri ");
  if( pDS->REAX & VCFB_MakeMRU)
    dprintf("MRU ");

  // if its a create, break it into two calls
  if( pDS->REAX & VCFB_Create || pDS->REAX & VCFB_MustCreate ) {
    
    // see if its a hit or a miss before its created 
    saveregs = *pDS;
    pDS->REAX &= ~0xFF;
    Call_Previous_Hook_Proc( pDS, &thunkVCacheFindBlockHook );
    if( pDS->RESI ) 
      Stats.hits++;
    else {
      // if it was a miss then a block will be created
      create = TRUE;
      Stats.misses++;
    }

    // restore regs and do the call again
    *pDS = saveregs;
    Call_Previous_Hook_Proc( pDS, &thunkVCacheFindBlockHook );

  } else {

    // its just a lookup, let it go through
    Call_Previous_Hook_Proc( pDS, &thunkVCacheFindBlockHook );
    if( pDS->RESI ) 
      Stats.hits++;
    else
      Stats.misses++;

  }

  // it it was a create, inc the count if successful
  if( pDS->RESI && create )
    Stats.new++;
  dprintf(": %x\n", pDS->RESI );
}


//----------------------------------------------------------------------
//
// DoHooks
//
// Hook all the routines we need to.
//
//----------------------------------------------------------------------
VOID DoHooks( void )
{
  // hook the hold service
  VCacheHoldService = Hook_Device_Service(
		      GetVxDServiceOrdinal(VCache_Hold),
		      VCacheHoldHook,
		      &thunkVCacheHoldHook);

  // hook the findblock service
  VCacheFindBlockService = Hook_Device_Service(
		      GetVxDServiceOrdinal(VCache_FindBlock),
		      VCacheFindBlockHook,
		      &thunkVCacheFindBlockHook);

  // hook the getstats
  VCacheGetStatsService = Hook_Device_Service(
		      GetVxDServiceOrdinal(VCache_GetStats),
		      VCacheGetStatsHook,
		      &thunkVCacheGetStatsHook);

  // hook the pagefile read/write service
  PageFileReadWriteService = Hook_Device_Service(
		      GetVxDServiceOrdinal(VCPageFile_Read_Or_Write),
		      PageFileReadWriteHook,
		      &thunkPageFileReadWriteHook);
}


//----------------------------------------------------------------------
//
// DoUnhooks
//
// Unhook the services we hooked.
//
//----------------------------------------------------------------------
VOID DoUnhooks( void )
{
  // unhook the hold service
  Unhook_Device_Service(GetVxDServiceOrdinal(VCache_Hold),
			VCacheHoldHook,
			&thunkVCacheHoldHook);

  // unhook the findblock service
  Unhook_Device_Service(GetVxDServiceOrdinal(VCache_FindBlock),
			VCacheFindBlockHook,
			&thunkVCacheFindBlockHook);

  // unhook the getstats
  Unhook_Device_Service(GetVxDServiceOrdinal(VCache_GetStats),
			VCacheGetStatsHook,
			&thunkVCacheGetStatsHook);

  // unhook the pagefile read/write service
  Unhook_Device_Service(GetVxDServiceOrdinal(VCPageFile_Read_Or_Write),
			PageFileReadWriteHook,
			&thunkPageFileReadWriteHook);

}


//----------------------------------------------------------------------
//
// OnSysCriticalInit
//
// Initialization: hook all the routines we need to.
//
//----------------------------------------------------------------------
BOOL OnSysCriticalInit( VMHANDLE hVM, PCHAR CommandTail,
			DWORD RealModeReferenceData )
{
  DoHooks();
  return TRUE;
}


//----------------------------------------------------------------------
//
// OnSysDynamicInit
//
// Initialization: hook all the routines we need to.
//
//----------------------------------------------------------------------
BOOL OnSysDynamicInit( void )
{
  DoHooks();
  return TRUE;
}


//----------------------------------------------------------------------
//
// OnW32Deviceiocontrol
//
// Entry point for Win32 requests. Two functions are support:
//
//   STATUS: called by the gui to register. We fail the call if we
//           are already connected. Otherwise we pass back to the
//           gui the address of the statistics data structure.
//   SETRATE: the user can change the stats sampling frequency. This
//           is where the timeout interval is changed to reflect the
//           new value.
//
//----------------------------------------------------------------------
DWORD OnW32Deviceiocontrol( PIOCTLPARAMS p )
{
    DWORD      lockable;

    switch ( p->dioc_IOCtlCode )  {

	case -1:
            if( !FailConnect ) {
	      // gui is closing
	      Cancel_Time_Out( GuiTimeoutHandle );
	      Connected   = FALSE;
	      FailConnect = FALSE;
	    }
	    return 0;

	case 0:
	    return 0;

        case VCMON_STATUS:
	    // if already connected, return error
	    if( Connected ) {
	      FailConnect = TRUE;
	      return 1;
	    }

	    // get size of cache
            Stats.size = VCache_GetSize(0, &Stats.size ) * 4;

	    // get amount of free memory
	    Stats.free = GetFreePageCount( 0, &lockable ) * 4;

	    // Gui is initiating contact 
	    GuiEvent = p->dioc_ovrlp->O_Internal;
	    *(VOID **) p->dioc_OutBuf = &Stats;
	    GuiTimeoutHandle = Set_Global_Time_Out( SampleRate, 0, 
				       GuiTimeout, &GuiTimeThunk );
	    *p->dioc_bytesret = 4;
	    Connected = TRUE;
	    return 0;

        case VCMON_SETRATE:
	    // need to reset sample rate
	    SampleRate = *(PDWORD) p->dioc_InBuf;

	    // cancel event
	    Cancel_Time_Out( GuiTimeoutHandle );
	    GuiTimeoutHandle = Set_Global_Time_Out( SampleRate, 0, 
				       GuiTimeout, &GuiTimeThunk );
	    return 0;

	default:
	    return 1;
    }
}


//----------------------------------------------------------------------
//
// OnSysCriticalExit
//
// Unhook everything we hooked.
//
//----------------------------------------------------------------------
VOID OnSysCriticalExit( void )
{
  DoUnhooks();
}


//----------------------------------------------------------------------
//
// OnSysDynamicExit
//
// Unhook everything we hooked.
//
//----------------------------------------------------------------------
BOOL OnSysDynamicExit( void )
{
  DoUnhooks();
  return TRUE;
}


//----------------------------------------------------------------------
//
// ControlDispatcher
//
// Multiplex VxD control messages.
//
//----------------------------------------------------------------------
BOOL __cdecl ControlDispatcher(
	DWORD dwControlMessage,
	DWORD EBX,
	DWORD EDX,
	DWORD ESI,
	DWORD EDI,
	DWORD ECX)
{
  START_CONTROL_DISPATCH

    ON_SYS_DYNAMIC_DEVICE_INIT(OnSysDynamicInit);
    ON_SYS_DYNAMIC_DEVICE_EXIT(OnSysDynamicExit);
    ON_SYS_CRITICAL_INIT(OnSysCriticalInit);
    ON_SYS_CRITICAL_EXIT(OnSysCriticalExit);
    ON_W32_DEVICEIOCONTROL(OnW32Deviceiocontrol);

  END_CONTROL_DISPATCH
	  
  return TRUE;
}


