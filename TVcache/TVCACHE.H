//======================================================================
//
// VCACHE.h - header module for VxD VCACHE
//
// Copyright (C) 1995 - Mark Russinovich and Bryce Cogswell
//
//======================================================================
#include  <vtoolsc.h>

// VxD definitions
#define TVCache_Major		3
#define TVCache_Minor		1
#define TVCache_DEVICE_ID	0xDEAD
#define TVCache_Init_Order	0x98

#define TVCache_DeviceID	TVCache_DEVICE_ID


//======================================================================
//
//                            EXTERNS
//
//======================================================================
extern TVCache_Get_Version();
extern TVCache_Register();
extern TVCache_GetSize();
extern TVCache_CheckAvail();
extern TVCache_FindBlock();
extern TVCache_FreeBlock();
extern TVCache_MakeMRU();
extern TVCache_Hold();
extern TVCache_Unhold();
extern TVCache_Enum();
extern TVCache_TestHandle();
extern TVCache_VerifySums();
extern TVCache_RecalcSums();
extern TVCache_TestHold();
extern TVCache_GetStats();
extern TVCache_Deregister();
extern TVCache_AdjustMinimum();
extern TVCache_SwapBuffers();
extern TVCache_RelinquishPage();
extern TVCache_UseThisPage();
extern _TVCache_CreateLookupCache();
extern _TVCache_CloseLookupCache();
extern _TVCache_DeleteLookupCache();
extern _TVCache_Lookup();
extern _TVCache_UpdateLookup();


//======================================================================
//
//                           SERVICE TABLE
//
//======================================================================
Begin_VxD_Service_Table(TVCache)

VxD_Service( TVCache_Get_Version )
VxD_Service( TVCache_Register )
VxD_Service( TVCache_GetSize )
VxD_Service( TVCache_CheckAvail )
VxD_Service( TVCache_FindBlock )
VxD_Service( TVCache_FreeBlock )
VxD_Service( TVCache_MakeMRU )
VxD_Service( TVCache_Hold )
VxD_Service( TVCache_Unhold )
VxD_Service( TVCache_Enum )
VxD_Service( TVCache_TestHandle )
VxD_Service( TVCache_VerifySums )
VxD_Service( TVCache_RecalcSums )
VxD_Service( TVCache_TestHold )
VxD_Service( TVCache_GetStats )
VxD_Service( TVCache_Deregister )
VxD_Service( TVCache_AdjustMinimum )
VxD_Service( TVCache_SwapBuffers )
VxD_Service( TVCache_RelinquishPage )
VxD_Service( TVCache_UseThisPage )
VxD_Service( _TVCache_CreateLookupCache )
VxD_Service( _TVCache_CloseLookupCache )
VxD_Service( _TVCache_DeleteLookupCache )
VxD_Service( _TVCache_Lookup )
VxD_Service( _TVCache_UpdateLookup )

End_VxD_Service_Table
