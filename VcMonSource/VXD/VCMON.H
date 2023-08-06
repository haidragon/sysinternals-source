//======================================================================
//
// VCMON.h - include file for VxD VCMON
//
// Copyright (c) 1996 Mark Russinovich and Bryce Cogswell
//
//======================================================================
#include <vtoolsc.h>

#define VCMON_Major		1
#define VCMON_Minor		0
#define VCMON_DeviceID		UNDEFINED_DEVICE_ID
#define VCMON_Init_Order	UNDEFINED_INIT_ORDER

DWORD _cdecl VCMON_Get_Version( void );

Begin_VxD_Service_Table( VCMON )
     VxD_Service( VCMON_Get_Version )
End_VxD_Service_Table


//----------------------------------------------------------------------
//
// PagingFile service definitions
//
//----------------------------------------------------------------------

// pagefile command block defn
#define READ                    0
#define WRITE                   1

// this is the pagefile request data structure - note: it is 
// undocumented and the DDK has WRONG documentation about the
// pagfile interface
typedef struct {
  BYTE                fill1[4];
  BYTE                cmd;
  BYTE                fill2;
  BYTE                result;
  BYTE                numpages;
  PBYTE               vmaddress;
  DWORD               pfoffset;
} pagefilecmd_s;

// Pagefile service definition table. The services and their
// order were obtained from the pagefile.inc file in the win95
// DDK.
#define PAGEFILE_Service  Declare_Service

Begin_Service_Table(PAGEFILE)
PAGEFILE_Service    (VCPageFile_Get_Version)
PAGEFILE_Service    (VCPageFile_Init_File)
PAGEFILE_Service    (VCPageFile_Clean_Up)
PAGEFILE_Service    (VCPageFile_Grow_File)
PAGEFILE_Service    (VCPageFile_Read_Or_Write)
PAGEFILE_Service    (VCPageFile_Cancel)
PAGEFILE_Service    (VCPageFile_Test_IO_Valid)
PAGEFILE_Service    (VCPageFile_Get_Size_Info)
PAGEFILE_Service    (VCPageFile_Set_Async_Manager)
PAGEFILE_Service    (VCPageFile_Call_Async_Manager)

End_Service_Table(PAGEFILE)



