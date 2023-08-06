//======================================================================
// 
// Undelete.h
//
// Copyright (C) 1996-1997 Mark Russinovich and Bryce Cogswell
//
// Preserves files that are deleted by using the built-in Recycle-Bin
// functionality of NT 4.0.
//
//======================================================================

//----------------------------------------------------------------------
//                 APIS and structures
//----------------------------------------------------------------------

int __cdecl swprintf( WCHAR *, const WCHAR *, ... );

typedef struct {
    LARGE_INTEGER    TotalAllocationUnits;
    LARGE_INTEGER    AvailableAllocationUnits;
    ULONG            SectorsPerAllocationUnit;
    ULONG            BytesPerSector;
} FILE_FS_SIZE_INFORMATION, *PFILE_FS_SIZE_INFORMATION;

typedef struct _FILE_FS_ATTRIBUTE_INFORMATION {
    ULONG FileSystemAttributes;
    LONG MaximumComponentNameLength;
    ULONG FileSystemNameLength;
    WCHAR FileSystemName[1];
} FILE_FS_ATTRIBUTE_INFORMATION, *PFILE_FS_ATTRIBUTE_INFORMATION;

//
// File rename information
//
typedef struct {
    BOOLEAN ReplaceIfExists;
    HANDLE  RootDirectory;
    ULONG   FileNameLength;
    WCHAR   FileName[1]; 
} FILE_RENAME_INFORMATION, *PFILE_RENAME_INFORMATION;

//
// File name information 
//
typedef struct _FILE_NAME_INFORMATION {
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFORMATION, *PFILE_NAME_INFORMATION;

typedef struct _FILE_DIRECTORY_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_DIRECTORY_INFORMATION, *PFILE_DIRECTORY_INFORMATION;

NTSTATUS NTAPI ZwQueryVolumeInformationFile( HANDLE FileHandle,
					     PIO_STATUS_BLOCK IoStatusBlock,
					     PVOID FsInformation,
					     ULONG Length,
					     FS_INFORMATION_CLASS FsInformationClass );

NTSTATUS NTAPI ZwQueryDirectoryFile( IN HANDLE FileHandle,
				     IN HANDLE Event,
				     IN PIO_APC_ROUTINE ApcRoutine,
				     IN PVOID ApcContext,
				     OUT PIO_STATUS_BLOCK IoStatusBlock,
				     OUT PVOID FileInformation,
				     IN ULONG Length,
				     IN FILE_INFORMATION_CLASS FileInformationClass,
				     IN BOOLEAN ReturnSingleEntry,
				     IN PUNICODE_STRING FileName,
				     IN BOOLEAN RestartScan );

typedef enum _TOKEN_INFORMATION_CLASS {
    TokenUser = 1,
    TokenGroups,
    TokenPrivileges,
    TokenOwner,
    TokenPrimaryGroup,
    TokenDefaultDacl,
    TokenSource,
    TokenType,
    TokenImpersonationLevel,
    TokenStatistics,
    TokenRestrictedSids
} TOKEN_INFORMATION_CLASS, *PTOKEN_INFORMATION_CLASS;

typedef PVOID PSID;     

typedef struct _SID_AND_ATTRIBUTES {
    PSID Sid;
    ULONG Attributes;
} SID_AND_ATTRIBUTES, * PSID_AND_ATTRIBUTES;

typedef struct _TOKEN_USER {
    SID_AND_ATTRIBUTES User;
} TOKEN_USER, *PTOKEN_USER;

#define MUTANT_QUERY_STATE      0x0001
#define MUTANT_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|\
                          MUTANT_QUERY_STATE)
#define TOKEN_QUERY             (0x0008)

HANDLE PsReferencePrimaryToken(PEPROCESS Process );

NTSTATUS RtlConvertSidToUnicodeString(
    PUNICODE_STRING SidString, 
    PVOID Sid, BOOLEAN AllocateString );

NTSTATUS NtQueryInformationToken(
    HANDLE Token, 
    TOKEN_INFORMATION_CLASS TokenInformationClass, PVOID TokenInformation, 
    ULONG TokenInformationLength, PULONG ReturnLength );

NTSTATUS ObOpenObjectByPointer(
    PVOID Object, 
    ULONG Flags, 
    PACCESS_STATE AccessState, 
    ACCESS_MASK DesiredAccess, 
    POBJECT_TYPE ObjectType, 
    KPROCESSOR_MODE AccessMode, 
    PHANDLE Handle );

PVOID KeAttachProcess( PEPROCESS Process );
PVOID KeDetachProcess( VOID );



//----------------------------------------------------------------------
//                           TYPEDEFS
//----------------------------------------------------------------------

//
// File name length (for unicode strings)
//
#define MAXIMUM_PATH_LENGTH   280
#define FILE_NAME_LENGTH      MAXIMUM_PATH_LENGTH * 2

//
// Extension length (3 characters + '.' + terminating null)
//
#define EXT_LEN		10

//
// This is an entry in the filter array for the registry
//
typedef struct {
  char       filter[EXT_LEN];
} FILTERENTRY, *PFILTERENTRY;

//
// Structure for device specific data that keeps track of what
// drive and what filesystem device are hooked 
//
typedef struct {
   PDEVICE_OBJECT   FileSystem;
   UCHAR            LogicalDrive;
   BOOLEAN          IsNTFS;
   BOOLEAN          IsAnsi;
   ERESOURCE        UndeleteLock;
} HOOK_EXTENSION, *PHOOK_EXTENSION;         


//
// This is the structure of the PurgeInfo value in the Registry. This
// value directs the behavior of the Recycle bin for each volume.
//
#define GLOBAL_SETTING 27
typedef struct {
        // dummy header to store length and value type returned by RtlQueryRegistryValues
	ULONG       Length;
        ULONG       Type;

        // actual information  
	ULONG	    SizeOfInformation;
	ULONG	    EnforceGlobalSetting;

	// Space for each recycle bin if settings are per-drive
	USHORT	    ReservedForRecycle[28];

	// If settings are not global, mask determines which volumes have
	// recycling disabled
	ULONG	    RecycleDisableMask;
	ULONG	    Unknown;
} PURGE_INFORMATION, *PPURGE_INFORMATION;

//
// Have to pack these
//
#pragma pack(1)

//
// Type of recycle bin
//
#define ANSI_RECYCLE_TYPE    0x0
#define UNICODE_RECYCLE_TYPE 0x2


#define UNICODE_VERSION_SIG  0x320
#define ANSI_VERSION_SIG     0x118

//
// This is the header on the front of the INFO file
//
typedef struct {
        ULONG       Version;   // must be 2
	ULONG       Entries; 
        ULONG       NextID; 
	ULONG       VersionSignature; 
        ULONG       Reserved2;
} INFO_HEADER, *PINFO_HEADER;

//
// NOTE: the undelete code relies on the fact that the
// ANSI info entry structure is an exact subset of the 
// UNICODE info entry structure
//

//
// This is an entry in the unicode INFO file
//
typedef struct {
        UCHAR       AsciiName[0x104];   
        ULONG       FileID; 
        ULONG       Drive;       // 0-based
        LARGE_INTEGER TimeDeleted;
        ULONG       Reserved1;   // don't care
        WCHAR       UnicodeName[0x104];
} UNIINFO_ENTRY, *PUNIINFO_ENTRY;

//
// This is an entry in the ansi INFO file
//
typedef struct {
        UCHAR       AsciiName[0x104];   
        ULONG       FileID; 
        ULONG       Drive;       // 0-based
        LARGE_INTEGER TimeDeleted;
        ULONG       Reserved1;   // don't care
} ANSIINFO_ENTRY, *PANSIINFO_ENTRY;

#pragma pack()

//
// This tracks INFO file entries in a linked list
//
typedef struct _INFO_LIST {
        union {
            UNIINFO_ENTRY          UniEntry;
            ANSIINFO_ENTRY         AnsiEntry;
        };           
        struct _INFO_LIST   *Next;
} INFO_LIST, *PINFO_LIST;

// 
// This is passed to the CLOSE completion routine for an undelete
//
typedef struct {
   PIRP                       Irp;
   WORK_QUEUE_ITEM            WorkItem;
   PVOID                      Token;
   PDEVICE_OBJECT             DeviceObject;
   UNICODE_STRING             FileName;
   PURGE_INFORMATION          PurgeInfo;
} UNDELETE_COMPLETE_CONTEXT, *PUNDELETE_COMPLETE_CONTEXT;



//----------------------------------------------------------------------
//                           DEFINES
//----------------------------------------------------------------------

//
// Define the various device type values.  Note that values used by Microsoft
// Corporation are in the range 0-32767, and 32768-65535 are reserved for use
// by customers.
//
#define FILE_DEVICE_UNDELETE	0x00008320


// 
// NT Final Build number
//
#define NT4FINAL        1381

//
// Print macro that only turns on when debugging is on
//
#if DBG
#define DbgPrint(arg) DbgPrint arg
#else
#define DbgPrint(arg) 
#endif

//----------------------------------------------------------------------
//                         FORWARDS
//----------------------------------------------------------------------

extern PDEVICE_OBJECT      GUIDevice;
extern PKMUTEX             UndeleteMutex;
extern PDEVICE_OBJECT      LDriveDevices[];
extern PEPROCESS           SystemProcess;
extern ULONG               NumFilters;
extern PFILTERENTRY        FilterList;
extern ULONG               SizeDirFilters;
extern ULONG               NumDirFilters;
extern PCHAR               *DirFilterArray;
extern CHAR                *DirFilterBuffer;


ULONGLONG UndeleteDeleteFile( PDEVICE_OBJECT FileSystem, PUNICODE_STRING FileName );
BOOLEAN UndeleteGetFileName(BOOLEAN IsNTFS, PDEVICE_OBJECT DeviceObject, 
                            PFILE_OBJECT FileObject,
                            PUCHAR FileName, ULONG FileNameLength );
BOOLEAN UndeleteRenameFile(PUNICODE_STRING OldName, PWCHAR NewName );
BOOLEAN UndeleteSetDispositionFile(PDEVICE_OBJECT DeviceObject, 
			           PFILE_OBJECT FileObject, BOOLEAN Delete );
BOOLEAN UndeleteIsDirectory(PDEVICE_OBJECT DeviceObject, 
                            PFILE_OBJECT FileObject );
BOOLEAN  UndeleteFastIoDeviceControl( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait,
                                      IN PVOID InputBuffer, IN ULONG InputBufferLength, 
                                      OUT PVOID OutbufBuffer, IN ULONG OutputBufferLength, IN ULONG IoControlCode,
                                      OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject );
NTSTATUS UndeleteHookRoutine( PDEVICE_OBJECT HookDevice, IN PIRP Irp );
VOID UndeleteFile( PUNDELETE_COMPLETE_CONTEXT CompleteContext );
