//====================================================================
//
// Defrag.h
//
// Copyright (C) 1997 Mark Russinovich
//
// Header file for defragmentation demonstration program. This file
// includes definitions for defragmentation File System Control 
// commands, as well as the undocumented NtFsControl call.
//
//====================================================================

//--------------------------------------------------------------------
//                     D E F I N E S 
//--------------------------------------------------------------------


//
// File System Control commands related to defragging
//
#define FSCTL_GET_VOLUME_INFORMATION	0x90064
#define	FSCTL_READ_MFT_RECORD			0x90068
#define FSCTL_GET_VOLUME_BITMAP			0x9006F
#define FSCTL_GET_RETRIEVAL_POINTERS	0x90073
#define FSCTL_MOVE_FILE					0x90074

//
// return code type
//
typedef UINT NTSTATUS;


//
// Error codes returned by NtFsControlFile (see NTSTATUS.H)
//
#define STATUS_SUCCESS			         ((NTSTATUS)0x00000000L)
#define STATUS_BUFFER_OVERFLOW           ((NTSTATUS)0x80000005L)
#define STATUS_INVALID_PARAMETER         ((NTSTATUS)0xC000000DL)
#define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023L)
#define STATUS_ACCESS_DENIED	         ((NTSTATUS)0xC0000011L)
#define STATUS_ALREADY_COMMITTED         ((NTSTATUS)0xC0000021L)
#define STATUS_INVALID_DEVICE_REQUEST    ((NTSTATUS)0xC0000010L)


//--------------------------------------------------------------------
//       F S C T L  S P E C I F I C   T Y P E D E F S  
//--------------------------------------------------------------------


//
// This is the definition for a VCN/LCN (virtual cluster/logical cluster)
// mapping pair that is returned in the buffer passed to 
// FSCTL_GET_RETRIEVAL_POINTERS
//
typedef struct {
	ULONGLONG			Vcn;
	ULONGLONG			Lcn;
} MAPPING_PAIR, *PMAPPING_PAIR;

//
// This is the definition for the buffer that FSCTL_GET_RETRIEVAL_POINTERS
// returns. It consists of a header followed by mapping pairs
//
typedef struct {
	ULONG				NumberOfPairs;
	ULONGLONG			StartVcn;
	MAPPING_PAIR		Pair[1];
} GET_RETRIEVAL_DESCRIPTOR, *PGET_RETRIEVAL_DESCRIPTOR;


//
// This is the definition of the buffer that FSCTL_GET_VOLUME_BITMAP
// returns. It consists of a header followed by the actual bitmap data
//
typedef struct {
	ULONGLONG			StartLcn;
	ULONGLONG			ClustersToEndOfVol;
	BYTE				Map[1];
} BITMAP_DESCRIPTOR, *PBITMAP_DESCRIPTOR; 


//
// This is the definition for the data structure that is passed in to
// FSCTL_MOVE_FILE
//
typedef struct {
     HANDLE            FileHandle; 
     ULONG             Reserved;   
     ULONGLONG     StartVcn; 
     ULONGLONG     TargetLcn;
     ULONG             NumVcns; 
     ULONG             Reserved1;	
} MOVEFILE_DESCRIPTOR, *PMOVEFILE_DESCRIPTOR;


//
// NTFS volume information
//
typedef struct {
	ULONGLONG    	SerialNumber;
	ULONGLONG    	NumberOfSectors;
	ULONGLONG    	TotalClusters;
	ULONGLONG    	FreeClusters;
	ULONGLONG    	Reserved;
	ULONG    		BytesPerSector;
	ULONG    		BytesPerCluster;
	ULONG    		BytesPerMFTRecord;
	ULONG    		ClustersPerMFTRecord;
	ULONGLONG    	MFTLength;
	ULONGLONG    	MFTStart;
	ULONGLONG    	MFTMirrorStart;
	ULONGLONG    	MFTZoneStart;
	ULONGLONG    	MFTZoneEnd;
} NTFS_VOLUME_DATA_BUFFER, *PNTFS_VOLUME_DATA_BUFFER;



//--------------------------------------------------------------------
//     N T F S C O N T R O L F I L E   D E F I N I T I O N S
//--------------------------------------------------------------------

//
// Prototype for NtFsControlFile and data structures
// used in its definition
//

//
// Io Status block (see NTDDK.H)
//
typedef struct _IO_STATUS_BLOCK {
    NTSTATUS Status;
    ULONG Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;


//
// Apc Routine (see NTDDK.H)
//
typedef VOID (*PIO_APC_ROUTINE) (
				PVOID ApcContext,
				PIO_STATUS_BLOCK IoStatusBlock,
				ULONG Reserved
			);


//
// The undocumented NtFsControlFile
//
// This function is used to send File System Control (FSCTL)
// commands into file system drivers. Its definition is 
// in ntdll.dll (ntdll.lib), a file shipped with the NTDDK.
//
NTSTATUS (__stdcall *NtFsControlFile)( 
					HANDLE FileHandle,
					HANDLE Event,					// optional
					PIO_APC_ROUTINE ApcRoutine,		// optional
					PVOID ApcContext,				// optional
					PIO_STATUS_BLOCK IoStatusBlock,	
					ULONG FsControlCode,
					PVOID InputBuffer,				// optional
					ULONG InputBufferLength,
					PVOID OutputBuffer,				// optional
					ULONG OutputBufferLength
			);


ULONG (__stdcall *RtlNtStatusToDosError) (
		IN NTSTATUS Status
		);

