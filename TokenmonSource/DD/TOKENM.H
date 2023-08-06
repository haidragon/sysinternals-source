//======================================================================
// 
//  Tokenm.h
//
//  Copyright (C) 2000 Mark Russinovich 
//
//  Typedefs and defines.
// 
//======================================================================

//
// Basic types
//
typedef unsigned int    UINT;
typedef char            CHAR;
typedef char *          PCHAR;
typedef PVOID           POBJECT;


//
// Print macro that only turns on when debugging is on
//
#if DBG
#define DbgPrint(arg) DbgPrint arg
#else
#define DbgPrint(arg) 
#endif

//
// System call hooking macro
//
#define SYSCALL(_index)  ((PSRVTABLE) KeServiceDescriptorTable)->ServiceTable[ _index ]

//
// The name of the System process, in which context we're called 
// in our DriverEntry
//
#define SYSNAME    "System"

//
// Maximum length of output buffer entry
//
#define MAXBUFENTRYLEN  256

//
// Maximum seperate filter components 
//
#define MAXFILTERS     64

//
// Length of an error string
//
#define ERRORLEN       32

//
// Length of other string
//
#define OTHERLEN       64

//
// Maximum length of data that will be copied to the "other" field in the display
//
#define MAXDATALEN     32

//
// Length of process name buffer. Process names are at most 16 characters so
// we take into account a trailing NULL.
//
#define MAXPROCNAMELEN  32

//
// Maximum length of NT process name
//
#define NT_PROCNAMELEN  16

//
// Maximum amount of memory that will be grabbed
//
#define MAXMEM          1000000

//
// Number of entries in hash table
//
#define NUMHASH         256

//
// Undocumented NtSetThreadInformation class
//
#define THREAD_ASSIGN_TOKEN_CLASS 5

//
// System account LUID
//
#define SYSTEMACCOUNT_LOW 999
#define SYSTEMACCOUNT_HIGH 0 

//
// Hash table hash function
//
#define HASHOBJECT(_object)  (((ULONG)_object >> 2) % NUMHASH)

//
// Convenient mutex macros
//
#define MUTEX_INIT(v)      KeInitializeMutex( &v, 0 )
#define MUTEX_WAIT(v)      KeWaitForMutexObject( &v, Executive, KernelMode, FALSE, NULL )
#define MUTEX_RELEASE(v)   KeReleaseMutex( &v, FALSE )

//
// Our work item for getting user information
//
typedef struct {
    WORK_QUEUE_ITEM  Item;
    KEVENT           Event;
    PLUID            LogonId;
    NTSTATUS         Status;
    SecurityUserData UserInformation;
} GETSECINFO_WORK_ITEM, *PGETSECINFO_WORK_ITEM;

//
// Structure for keeping linked lists of output buffers
//
typedef struct _LOG_BUFFER {
    ULONG               Len;
    struct _LOG_BUFFER  *Next;
    char                Data[ MAX_STORE ];
} LOG_BUFFER, *PLOG_BUFFER;

//
// Definition for system call service table
//
typedef struct _SRVTABLE {
	PVOID           *ServiceTable;
	ULONG           LowCall;        
	ULONG           HiCall;
	PVOID		*ArgTable;
} SRVTABLE, *PSRVTABLE;

//
// Definition for hash table entry
//
typedef struct _HASH_ENTRY {
    LUID                 LogonId;
    WCHAR                ProcessName[MAXPROCNAMELEN];
    HANDLE               ThreadId;
    LARGE_INTEGER        LogonTimeStamp;
    LARGE_INTEGER        Time;
    LARGE_INTEGER        PerfTime;

    BOOLEAN              LogonPending;
    BOOLEAN              GotSecurityInfo;
    PWCHAR               DomainName;
    PWCHAR               UserName;

    struct _HASH_ENTRY   *Next;
} HASH_ENTRY, *PHASH_ENTRY;

//
// LPC message structure
//
typedef struct {
    USHORT DataSize;
    USHORT MessageSize;
    USHORT MessageType;
    USHORT VirtualRangesOffset;
    CLIENT_ID ClientId;
    ULONG MessageId;
    ULONG SectionSize;
} PORT_MESSAGE, *PPORT_MESSAGE;


//
// Pointer to the image of the system service table
//
extern PSRVTABLE KeServiceDescriptorTable;

//
// Named pipe impersonate FSCTL
//
#define FSCTL_PIPE_IMPERSONATE           0x11001C

//
// Extract transfer type
//
#define IOCTL_TRANSFER_TYPE( _iocontrol)   (_iocontrol & 0x3)

//
// Prototypes for undocumented kernel functions
//
PIRP IoGetTopLevelIrp();
VOID IoSetTopLevelIrp( PIRP TopLevelIrp );
NTSTATUS SeRegisterLogonSessionTerminatedRoutine(PVOID CallbackRoutine );
NTSTATUS SeUnregisterLogonSessionTerminatedRoutine(PVOID CallbackRoutine );
NTSTATUS SeMarkLogonSessionForTerminationNotification(PLUID LogonId );
NTSTATUS PsLookupProcessByProcessId(PVOID ProcessId, PEPROCESS *Process );
NTSTATUS ObOpenObjectByPointer( POBJECT Object, ULONG HandleAttributes, 
                                PACCESS_STATE PassedAccessState, ACCESS_MASK DesiredAccess, 
                                POBJECT_TYPE ObjectType, 
                                KPROCESSOR_MODE AccessMode, HANDLE *Handle );
NTSTATUS ZwQueryInformationToken (HANDLE TokenHandle,
                                  TOKEN_INFORMATION_CLASS TokenInformationClass,
                                  PVOID TokenInformation,
                                  ULONG TokenInformationLength,
                                  PULONG ReturnLength );











