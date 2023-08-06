//======================================================================
// 
// Tokenmon.c
//
// Copyright (C) 2000 Mark Russinovich 
//
// Device driver that monitors token creation and deletion. Since a
// user is considered logged-on whenever they a token with their
// security credentials exists, this is effectively a logon/logoff
// monitor.
//
//======================================================================
#include "ntddk.h"
#include "ksecdd.h"
#include "stdarg.h"
#include "stdio.h"
#include "ioctlcmd.h"
#include "ntsec.h"
#include "tokenm.h"

//----------------------------------------------------------------------
//                     F O R E W A R D S
//----------------------------------------------------------------------

VOID 
LogRecord( 
    ULONG seq, 
    PLARGE_INTEGER time, 
    PLARGE_INTEGER perfTime, 
    PWCHAR Text 
    );
PWCHAR 
TokenmonCheckFilters(
    PWCHAR OutputText
    );
PWCHAR 
TokenmonGetProcess( 
    PEPROCESS Process, 
    PWCHAR Name 
    );
VOID 
TokenmonGetTimeStamp( 
    PLARGE_INTEGER TimeStamp,
    PLARGE_INTEGER PerfTime
    );

//----------------------------------------------------------------------
//                         GLOBALS
//---------------------------------------------------------------------- 

//
// The GUI's token. We ignore operations on it to avoid infinite
// token-query loops the driver would get into with the GUI.
//
PVOID               GUIToken;

//
// These are system call indexes that are passed to us by the GUI
//
ULONG               NtCreateTokenIndex;
ULONG               NtAdjustPrivilegesTokenIndex;
ULONG               NtSetInformationThreadIndex;
ULONG               NtImpersonateClientOfPortIndex;
ULONG               NtFsControlFileIndex;
ULONG               NtQueryInformationTokenIndex;

//
// Indicates if the GUI wants activity to be logged
//
BOOLEAN             FilterOn = FALSE;

//
// Global filter (sent to us by the GUI)
//
FILTER              FilterDef;

//
// This lock protects access to the filter array
//
KMUTEX              FilterMutex;

//
// Array of process and path filters 
//
ULONG               NumIncludeFilters = 0;
PWCHAR              IncludeFilters[MAXFILTERS];
ULONG               NumExcludeFilters = 0;
PWCHAR              ExcludeFilters[MAXFILTERS];

//
// Time reference point
//
LARGE_INTEGER       StartTime;

//
// This is the offset into a KPEB of the current process name. 
// This is determined dynamically by scanning the process block 
// belonging to the GUI for the name of the system process, in 
// who's context we execute in DriverEntry.
//
ULONG               ProcessNameOffset;


//
// The current output buffer
//
PLOG_BUFFER         LogBuffer = NULL;

//
// Each IRP is given a sequence number. This allows the return status
// of an IRP, which is obtained in the completion routine, to be 
// associated with the IRPs parameters that were extracted in the Dispatch
// routine.
//
ULONG               Sequence = 0;

//
// This mutex protects the output buffer
//
KMUTEX              LogBufferMutex;

//
// This is a hash table for keeping names around for quick lookup.
//
PHASH_ENTRY         HashTable[NUMHASH];

//
// Hash table lookaside list
//
PAGED_LOOKASIDE_LIST HashLookaside;

//
// Mutex for hash table accesses
//
KMUTEX              HashMutex;

//
// Tokenmon keeps track of the number of distinct output buffers that
// have been allocated, but not yet uploaded to the GUI, and caps
// the amount of memory (which is in non-paged pool) it takes at
// 1MB.
//
ULONG               NumLogBuffer = 0;
ULONG               MaxLogBuffer = 1000000/MAX_STORE;

//
// This is the real NtCreateToken entry point. I determined the parameter
// list by looking at the behavior of the Win32 GetTokenInformation
// and SetTokenInformation Apis with respect to what gets placed
// where in a new token constructed with this API.
//
NTSTATUS (*RealNtCreateToken)(
    PHANDLE TokenHandle, 
    ACCESS_MASK DesiredAccess, 
    POBJECT_ATTRIBUTES ObjectAttributes, 
    TOKEN_TYPE TokenType, 
    PLUID SessionId, 
    PLARGE_INTEGER ExpirationTime, 
    PTOKEN_USER User, 
    PTOKEN_GROUPS Groups, 
    PTOKEN_PRIVILEGES Privileges, 
    PTOKEN_OWNER Owner, 
    PTOKEN_PRIMARY_GROUP PrimaryGroup, 
    PTOKEN_DEFAULT_DACL DefaultDacl, 
    PTOKEN_SOURCE TokenSource );

//
// This is the real NtAdjustPrivilegesToken entry point
//
NTSTATUS (*RealNtAdjustPrivilegesToken)(
    HANDLE TokenHandle, 
    BOOLEAN DisableAllPrivileges, 
    PTOKEN_PRIVILEGES NewState, 
    ULONG BufferLength, 
    PTOKEN_PRIVILEGES PreviousState, 
    PULONG ReturnLength 
    );

//
// This is the real NtSetInformationThread
//
NTSTATUS (*RealNtSetInformationThread)(
    IN HANDLE ThreadHandle,
    IN THREADINFOCLASS ThreadInformationClass,
    IN PVOID ThreadInformation,
    IN ULONG ThreadInformationLength
    );

//
// The real NtImpersonateClientOfPort entry point
//
NTSTATUS (*RealNtImpersonateClientOfPort)(
    IN HANDLE PortHandle,
    IN PPORT_MESSAGE Message
    );

NTSTATUS (*RealNtFsControlFile)(
    IN HANDLE FileHandle,
    IN HANDLE Event OPTIONAL,
    IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
    IN PVOID ApcContext OPTIONAL,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN ULONG IoControlCode,
    IN PVOID InputBuffer OPTIONAL,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer OPTIONAL,
    IN ULONG OutputBufferLength
    );

//
// The real NtQueryInformationToken entry point
//
NTSTATUS (*RealNtQueryInformationToken)(
    HANDLE TokenHandle,
    ULONG TokenInformationClass,
    PVOID TokenInformation,
    ULONG TokenInformationLength,
    PULONG ReturnLength
    );

//----------------------------------------------------------------------
//                              S T R I N G S
//----------------------------------------------------------------------

//
// System account strings
//
WCHAR SystemUser[] = L"SYSTEM";
WCHAR SystemDomain[] = L"NT AUTHORITY";
WCHAR SystemServer[] = L"";

//
// Privilege strings
//
#define MAX_PRIVILEGE (sizeof(PrivilegeNames) / sizeof(PrivilegeNames[0]) - 1)
WCHAR PrivilegeNames[][48] = {
    L"",
    L"",
	L"CREATE_TOKEN",
	L"ASSIGN_PRIMARY_TOKEN", 
	L"LOCK_MEMORY",        
	L"INCREASE_QUOTA",     
	L"MACHINE_ACCOUNT",    
	L"TCB",                
	L"SECURITY",           
	L"TAKE_OWNERSHIP",     
	L"LOAD_DRIVER",        
	L"SYSTEM_PROFILE",     
	L"SYSTEMTIME",         
	L"PROF_SINGLE_PROCESS",
	L"INC_BASE_PRIORITY", 
	L"CREATE_PAGEFILE",   
	L"CREATE_PERMANENT",  
	L"BACKUP",            
	L"RESTORE",           
	L"SHUTDOWN",          
	L"DEBUG",             
	L"AUDIT",             
	L"SYSTEM_ENVIRONMENT",
	L"CHANGE_NOTIFY",  
	L"REMOTE_SHUTDOWN",
    L"UNDOCK",
    L"SYNC_AGENT",
    L"ENABLE_DELEGATION",
    L"<Unknown Privilege",
};

//
// Enable or disable of attribute
//
WCHAR PrivilegeAttribute[][32] = {
    L"DISABLED",
    L"ENABLED"
};

//
// Impersonation levels
//
WCHAR ImpersonationLevel[][32] = {
    L"IMPERSONATE-ANONYMOUS",
    L"IMPERSONATE-IDENTIFICATION",
    L"IMPERSONATE",
    L"IMPERSONATE-DELEGATION"
};


//----------------------------------------------------------------------
//      P A T T E R N   M A T C H I N G   R O U T I N E S
//----------------------------------------------------------------------


//----------------------------------------------------------------------
//
// MatchOkay
//
// Only thing left after compare is more mask. This routine makes
// sure that its a valid wild card ending so that its really a match.
//
//----------------------------------------------------------------------
BOOLEAN 
MatchOkay( 
    PWCHAR Pattern 
    )
{
    //
    // If pattern isn't empty, it must be a wildcard
    //
    if( *Pattern && *Pattern != L'*' ) {
 
        return FALSE;
    }

    //
    // Matched
    //
    return TRUE;
}


//----------------------------------------------------------------------
//
// MatchWithPattern
//
// Performs nifty wildcard comparison.
//
//----------------------------------------------------------------------
BOOLEAN 
MatchWithPattern( 
    PWCHAR Pattern, 
    PWCHAR Name 
    )
{
    WCHAR   upcase;

    //
    // End of pattern?
    //
    if( !*Pattern ) {

        return FALSE;
    }

    //
    // If we hit a wild card, do recursion
    //
    if( *Pattern == '*' ) {

        Pattern++;

        while( *Name && *Pattern ) {

            if( *Name >= L'a' && *Name <= L'z' )
                upcase = *Name - L'a' + L'A';
            else
                upcase = *Name;

            //
            // See if this substring matches
            //
            if( *Pattern == upcase || *Name == L'*' ) {

                if( MatchWithPattern( Pattern+1, Name+1 )) {

                    return TRUE;
                }
            }

            //
            // Try the next substring
            //
            Name++;
        }

        //
        // See if match condition was met
        //
        return MatchOkay( Pattern );
    } 

    //
    // Do straight compare until we hit a wild card
    //
    while( *Name && *Pattern != L'*' ) {

        if( *Name >= L'a' && *Name <= L'z' )
            upcase = *Name - L'a' + L'A';
        else
            upcase = *Name;

        if( *Pattern == upcase ) {

            Pattern++;
            Name++;

        } else {

            return FALSE;
        }
    }

    //
    // If not done, recurse
    //
    if( *Name ) {

        return MatchWithPattern( Pattern, Name );
    }

    //
    // Make sure its a match
    //
    return MatchOkay( Pattern );
}

//----------------------------------------------------------------------
//                   H A S H  R O U T I N E S 
//----------------------------------------------------------------------

//----------------------------------------------------------------------
//
// TokenmonHashCleanup
//
// Called when we are unloading to free any memory that we have 
// in our possession.
//
//----------------------------------------------------------------------
VOID 
TokenmonHashCleanup(
    VOID
    )
{
    PHASH_ENTRY             hashEntry, nextEntry;
    ULONG                   i;

    MUTEX_WAIT( HashMutex );    

    //
    // First free the hash table entries
    //       
    for( i = 0; i < NUMHASH; i++ ) {

        hashEntry = HashTable[i];
        while( hashEntry ) {

            nextEntry = hashEntry->Next;
            ExFreePool( hashEntry->DomainName );
            ExFreePool( hashEntry->UserName );
            ExFreeToPagedLookasideList( &HashLookaside, hashEntry );
            hashEntry = nextEntry;
        }
    }
    ExDeletePagedLookasideList( &HashLookaside );

    MUTEX_RELEASE( HashMutex );
}


//----------------------------------------------------------------------
//
// TokenmonStoreHash
//
// Stores the key and associated fullpath in the hash table.
//
//----------------------------------------------------------------------
PHASH_ENTRY 
TokenmonStoreHash( 
    BOOLEAN LogonPending,
    PLUID LogonId, 
    PWCHAR ProcessName
    )
{
    PHASH_ENTRY     newEntry;
    LARGE_INTEGER   time, perfTime;

    MUTEX_WAIT( HashMutex );

    newEntry = ExAllocateFromPagedLookasideList( &HashLookaside );
    if( newEntry ) {

        newEntry->GotSecurityInfo = FALSE;
        newEntry->LogonPending = LogonPending;
        newEntry->LogonId = *LogonId;
        newEntry->ThreadId = PsGetCurrentThreadId();
        KeQueryTickCount( &newEntry->LogonTimeStamp );
        TokenmonGetTimeStamp( &time, &perfTime );
        newEntry->Time = time;
        newEntry->PerfTime = perfTime;
        wcscpy( newEntry->ProcessName, ProcessName );

        newEntry->Next = HashTable[ HASHOBJECT(LogonId->LowPart) ];
        HashTable[ HASHOBJECT(LogonId->LowPart) ] = newEntry;     
    }
    MUTEX_RELEASE( HashMutex );
    return newEntry;
}


//----------------------------------------------------------------------
//
// TokenmonFreeHashEntry
//
// When we see a key close, we can free the string we had associated
// with the fileobject being closed since we know it won't be used
// again.
//
//----------------------------------------------------------------------
VOID 
TokenmonFreeHashEntry( 
    PLUID LogonId 
    )
{
    PHASH_ENTRY             hashEntry, prevEntry;

    MUTEX_WAIT( HashMutex );

    //
    // look-up the entry
    //
    hashEntry = HashTable[ HASHOBJECT( LogonId->LowPart ) ];
    prevEntry = NULL;
    while( hashEntry && !RtlEqualLuid( &hashEntry->LogonId, LogonId )) {
        prevEntry = hashEntry;
        hashEntry = hashEntry->Next;
    }
  
    //
    // If we fall off (didn''t find it), just return
    //
    if( !hashEntry ) {

        MUTEX_RELEASE( HashMutex );
        return;
    }

    //
    // Remove it from the hash list 
    //
    if( prevEntry ) 
        prevEntry->Next = hashEntry->Next;
    else 
        HashTable[ HASHOBJECT( LogonId->LowPart )] = hashEntry->Next;

    //
    // Free the memory associated with it
    //
    if( hashEntry->GotSecurityInfo ) {

        ExFreePool( hashEntry->DomainName );
        ExFreePool( hashEntry->UserName );
    }
    ExFreeToPagedLookasideList( &HashLookaside, hashEntry );

    MUTEX_RELEASE( HashMutex );
}


//----------------------------------------------------------------------
//
// TokenmonUpdateHashEntry
//
// Given a logon id, searches for a corresponding hash table
// entry. We call this from every Logon id lookup because
// we might have a pending logon (NtCreateToken) that we need
// to look up the information for and log.
//
//----------------------------------------------------------------------
PHASH_ENTRY 
TokenmonUpdateHashEntry( 
    PLUID LogonId,
    PWCHAR UserName,
    PWCHAR DomainName
    )
{
    PHASH_ENTRY   hashEntry;
    PWCHAR userName, domainName, logonServer;
    WCHAR          output[MAXBUFENTRYLEN];
    WCHAR          name[MAXPROCNAMELEN];

    //
    // look-up the entry
    //
    MUTEX_WAIT( HashMutex );
    hashEntry = HashTable[ HASHOBJECT( LogonId->LowPart ) ];

    while( hashEntry && !RtlEqualLuid( &hashEntry->LogonId, LogonId )) {

        hashEntry = hashEntry->Next;
    }

    //
    // If we didn't find an entry, create one
    //
    if( !hashEntry ) {

        TokenmonGetProcess( PsGetCurrentProcess(), name );
        hashEntry = TokenmonStoreHash( FALSE, LogonId, name );
    }

    //
    // See if we can update the information 
    //
    if( hashEntry && 
        !hashEntry->GotSecurityInfo ) {

        hashEntry->DomainName = ExAllocatePool( PagedPool,
                                               (wcslen( DomainName )+1) * sizeof(WCHAR));
        if( hashEntry->DomainName ) {

            wcscpy( hashEntry->DomainName, DomainName );
        }
        hashEntry->UserName = ExAllocatePool( PagedPool,
                                             (wcslen( UserName )+1) * sizeof(WCHAR));
        if( hashEntry->UserName ) {

            wcscpy( hashEntry->UserName, UserName );
        }
        if( hashEntry->LogonPending ) {

            swprintf( output, L"%s\t%d\tLOGON\t%08I64X: \\\\%s\\%s", 
                      hashEntry->ProcessName,
                      hashEntry->ThreadId,
                      *(PULONGLONG) LogonId,
                      DomainName,
                      UserName );
            LogRecord( InterlockedIncrement( &Sequence ),
                       &hashEntry->Time,
                       &hashEntry->PerfTime,
                       output );
            hashEntry->LogonPending = FALSE;
        }        
        hashEntry->GotSecurityInfo = TRUE;
    }
    MUTEX_RELEASE( HashMutex );
    return hashEntry;
}


//----------------------------------------------------------------------
//
// TokenmonLookupHashEntry
//
// Given a logon id, searches for a corresponding hash table
// entry. 
//
//----------------------------------------------------------------------
PHASH_ENTRY 
TokenmonLookupHashEntry( 
    PLUID LogonId 
    )
{
    PHASH_ENTRY   hashEntry;
    PWCHAR userName, domainName, logonServer;

    //
    // look-up the entry
    //
    MUTEX_WAIT( HashMutex );
    hashEntry = HashTable[ HASHOBJECT( LogonId->LowPart ) ];

    while( hashEntry && !RtlEqualLuid( &hashEntry->LogonId, LogonId )) {

        hashEntry = hashEntry->Next;
    }
    MUTEX_RELEASE( HashMutex );
    return hashEntry;
}


//----------------------------------------------------------------------
//            B U F F E R   M A N A G E M E N T
//----------------------------------------------------------------------

//----------------------------------------------------------------------
//
// TokenmonFreeLogBuffer
//
// Frees all the data output buffers that we have currently allocated.
//
//----------------------------------------------------------------------
VOID TokenmonFreeLogBuffer()
{
    PLOG_BUFFER      next;
    
    while( LogBuffer ) {
        next = LogBuffer->Next;
        ExFreePool( LogBuffer );
        LogBuffer = next;
    }
}       


//----------------------------------------------------------------------
//
// TokenmonNewLogBuffer
//
// Called when the current buffer has filled up. This moves us to the
// pre-allocated buffer and then allocates another buffer.
//
//----------------------------------------------------------------------
void 
TokenmonNewLogBuffer( 
    VOID 
    )
{
    PLOG_BUFFER prev = LogBuffer, newLogBuffer;
    WORK_QUEUE_ITEM  workItem;

    //
    // If we have maxed out or haven't accessed the current LogBuffer
    // just return
    //
    if( MaxLogBuffer == NumLogBuffer ) {
        LogBuffer->Len = 0;
        return; 
    }

    //
    // See if we can re-use a LogBuffer
    //
    if( !LogBuffer->Len ) {

        return;
    }

    //
    // Move to the next buffer and allocate another one
    //
    newLogBuffer = ExAllocatePool( PagedPool, sizeof(*LogBuffer) );
    if( newLogBuffer ) { 

        LogBuffer   = newLogBuffer;
        LogBuffer->Len  = 0;
        LogBuffer->Next = prev;
        NumLogBuffer++;

    } else {

        LogBuffer->Len = 0;
    }
}


//----------------------------------------------------------------------
//
// TokenmonOldestLogBuffer
//
// Goes through the linked list of storage buffers and returns the 
// oldest one.
//
//----------------------------------------------------------------------
PLOG_BUFFER 
TokenmonOldestLogBuffer( 
    VOID 
    )
{
    PLOG_BUFFER  ptr = LogBuffer, prev = NULL;

    while ( ptr->Next ) {

        ptr = (prev = ptr)->Next;
    }
    if ( prev ) {

        prev->Next = NULL;    
    }
    NumLogBuffer--;
    return ptr;
}


//----------------------------------------------------------------------
//
// TokenmonResetLogBuffer
//
// When a GUI is no longer communicating with us, but we can't unload,
// we reset the storage buffers.
//
//----------------------------------------------------------------------
VOID TokenmonResetLogBuffer()
{
    PLOG_BUFFER  current, next;

    MUTEX_WAIT( LogBufferMutex );

    //
    // Traverse the list of output buffers
    //
    current = LogBuffer->Next;
    while( current ) {

        //
        // Free the buffer
        //
        next = current->Next;
        ExFreePool( current );
        current = next;
    }

    // 
    // Move the output pointer in the buffer that's being kept
    // the start of the buffer.
    // 
    LogBuffer->Len = 0;
    LogBuffer->Next = NULL;

    MUTEX_RELEASE( LogBufferMutex );
}


//----------------------------------------------------------------------
//
// LogRecord
//
// This "printfs" a string into an output buffer.
//
//----------------------------------------------------------------------
VOID 
LogRecord( 
    ULONG seq, 
    PLARGE_INTEGER time, 
    PLARGE_INTEGER perfTime,
    PWCHAR Text 
    )
{   
    PENTRY             Entry;
    int                len;
    va_list            arg_ptr;
    KIRQL              oldirql;

    //
    // If no GUI is there to receive the output or if no filtering is desired,
    // don't bother. 
    //
    if( !TokenmonCheckFilters( Text )) {

        return;
    }

    // 
    // Lock the output buffer.
    // 
    MUTEX_WAIT( LogBufferMutex );

    //
    // Get the length of text
    //
    len = wcslen( Text ) * sizeof( WCHAR ) + sizeof(WCHAR);
    
    //
    // If the current output buffer is near capacity, move to a new 
    // output buffer
    //
    if ( LogBuffer->Len + len + sizeof(ENTRY) >= MAX_STORE ) {

        TokenmonNewLogBuffer();
    }

    //
    // Extract the sequence number and store it
    //
    Entry = (PENTRY) (LogBuffer->Data + LogBuffer->Len);
    Entry->seq = seq;
    Entry->time = *time;
    Entry->perfTime = *perfTime;
    wcscpy( Entry->text, Text );
 
    //
    // Store the length of the string, plus 1 for the terminating
    // NULL  
    //   
    LogBuffer->Len += ((PCHAR) Entry->text - (PCHAR) Entry ) + len;

    //
    // Release the output buffer lock
    //
    MUTEX_RELEASE( LogBufferMutex );
}

//----------------------------------------------------------------------
//       P A T H  A N D  P R O C E S S  N A M E  R O U T I N E S
//----------------------------------------------------------------------

//----------------------------------------------------------------------
//
// TokenmonFreeFilters
//
// Fress storage we allocated for filter strings.
//
//----------------------------------------------------------------------
VOID 
TokenmonFreeFilters(
    VOID
    )
{
    ULONG   i;
    
    for( i = 0; i < NumIncludeFilters; i++ ) {

        ExFreePool( IncludeFilters[i] );
    }
    for( i = 0; i < NumExcludeFilters; i++ ) {

        ExFreePool( ExcludeFilters[i] );
    }
    NumIncludeFilters = 0;
    NumExcludeFilters = 0;
}

//----------------------------------------------------------------------
//
// MakeFilterArray
//
// Takes a filter string and splits into components (a component
// is seperated with a ';')
//
//----------------------------------------------------------------------
VOID 
MakeFilterArray( 
    PWCHAR FilterString,
    PWCHAR FilterArray[],
    PULONG NumFilters 
    )
{
    PWCHAR filterStart;
    ULONG filterLength;


    // Scan through the process filters
    //
    filterStart = FilterString;
    while( *filterStart ) {

        filterLength = 0;
        while( filterStart[filterLength] &&
               filterStart[filterLength] != L';' ) {

            filterLength++;
        }

        //
        // Ignore zero-length components
        //
        if( filterLength ) {

            FilterArray[ *NumFilters ] = 
                ExAllocatePool( PagedPool, (filterLength + 1) * sizeof(WCHAR)  );
            wcsncpy( FilterArray[ *NumFilters ],
                     filterStart, filterLength );
            FilterArray[ *NumFilters ][filterLength] = 0;
            (*NumFilters)++;
        }
    
        //
        // Are we done?
        //
        if( !filterStart[filterLength] ) break;

        //
        // Move to the next component (skip over ';')
        //
        filterStart += filterLength + 1;
    }
}


//----------------------------------------------------------------------
//
// TokenmonUpdateFilters
//
// Takes a new filter specification and updates the filter
// arrays with them.
//
//----------------------------------------------------------------------
VOID 
TokenmonUpdateFilters(
    VOID
    )
{
    //
    // Free old filters (if any)
    //
    MUTEX_WAIT( FilterMutex );

    TokenmonFreeFilters();

    //
    // Create new filter arrays
    //
    MakeFilterArray( FilterDef.includefilter,
                     IncludeFilters, &NumIncludeFilters );
    MakeFilterArray( FilterDef.excludefilter,
                     ExcludeFilters, &NumExcludeFilters );

    MUTEX_RELEASE( FilterMutex );
}


//----------------------------------------------------------------------
//
// TokenmonGetProcessNameOffset
//
// In an effort to remain version-independent, rather than using a
// hard-coded into the KPEB (Kernel Process Environment Block), we
// scan the KPEB looking for the name, which should match that
// of the system process. This is because we are in the system process'
// context in DriverEntry, where this is called.
//
//----------------------------------------------------------------------
ULONG 
TokenmonGetProcessNameOffset(
    VOID
    )
{
    PEPROCESS       curproc;
    int             i;

    curproc = PsGetCurrentProcess();

    //
    // Scan for 12KB, hoping the KPEB never grows that big!
    //
    for( i = 0; i < 3*PAGE_SIZE; i++ ) {
     
        if( !strncmp( SYSNAME, (PCHAR) curproc + i, strlen(SYSNAME) )) {

            return i;
        }
    }

    //
    // Name not found - oh, well
    //
    return 0;
}


//----------------------------------------------------------------------
//
// TokenmonCheckFilters
//
// Given a process name, applies filters to it.
//
//----------------------------------------------------------------------
PWCHAR 
TokenmonCheckFilters( 
    PWCHAR OutputText
    )
{
    ULONG           i;

    //
    // If we aren't capturing, its the same as being filtered
    //
    if( !FilterOn ) return NULL;

    //
    // Apply process name filters
    //
    MUTEX_WAIT( FilterMutex );

    for( i = 0; i < NumExcludeFilters; i++ ) {

        if( MatchWithPattern( ExcludeFilters[i], OutputText )) {

            MUTEX_RELEASE( FilterMutex );
            return NULL;
        }
    }
    for( i = 0; i < NumIncludeFilters; i++ ) {

        if( MatchWithPattern( IncludeFilters[i], OutputText ) ) {

            MUTEX_RELEASE( FilterMutex );
            return OutputText;
        }
    }
    MUTEX_RELEASE( FilterMutex );
    return NULL;
}


//----------------------------------------------------------------------
//
// TokenmonGetTimeStamp
//
// Obtains a time stamp, which has a format dependent on what mode
// the GUI is currently in.
//
//----------------------------------------------------------------------
VOID 
TokenmonGetTimeStamp( 
    PLARGE_INTEGER TimeStamp,
    PLARGE_INTEGER PerfTime
    )
{
    *PerfTime = KeQueryPerformanceCounter( NULL );
    PerfTime->QuadPart -= StartTime.QuadPart;

    KeQuerySystemTime( TimeStamp );
}


//----------------------------------------------------------------------
//
// TokenmonGetProcess
//
// Uses undocumented data structure offsets to obtain the name of the
// currently executing process.
//
//----------------------------------------------------------------------
PWCHAR 
TokenmonGetProcess( 
    PEPROCESS Process, 
    PWCHAR Name 
    )
{
    char    *nameptr;
    int     i;    

    //
    // We only do this if we determined the process name offset
    //
    if( ProcessNameOffset ) {
      
        //
        // Dig into it to extract the name 
        //
        i = 0;
        nameptr  = (PCHAR) Process + ProcessNameOffset;
        while( nameptr[i] && i < NT_PROCNAMELEN-1 ) {

            Name[i] = (WCHAR) nameptr[i++];
        }
        Name[i] = 0;
        swprintf( Name + wcslen(Name), L":%d", PsGetCurrentProcessId());

    } else {

        wcscpy( Name, L"???" );
    }
    return Name;
}


//----------------------------------------------------------------------
//
// TokenmonOpenToken
//
// Opens a handle to the current thread's primary token.
//
//----------------------------------------------------------------------
HANDLE 
TokenmonOpenToken(
    VOID
    )
{
    NTSTATUS     status;
    PVOID        token;
    HANDLE       tokenHandle;
    BOOLEAN      tokenCopy;
    BOOLEAN      tokenEffective;
    SECURITY_IMPERSONATION_LEVEL tokenLevel;

    token = PsReferencePrimaryToken( 
                       PsGetCurrentProcess()
                       );

    //
    // Now that we have a token reference, get a handle to it
    // so that we can query it.
    //
    status = ObOpenObjectByPointer( token, 
                                    0, 
                                    NULL,
                                    TOKEN_QUERY, 
                                    NULL, 
                                    KernelMode, 
                                    &tokenHandle 
                                    );

    ObDereferenceObject( token );
    if( !NT_SUCCESS( status )) {

        //
        // We coudn't open the token!!
        //
        return NULL;
    }    
    return tokenHandle;
}


//----------------------------------------------------------------------
//
// TokenmonOpenEffectiveToken
//
// Opens a handle to the current thread's effective token (either
// impersonation token or primary token).
//
//----------------------------------------------------------------------
HANDLE 
TokenmonOpenEffectiveToken(
    VOID
    )
{
    NTSTATUS     status;
    PVOID        token;
    HANDLE       tokenHandle;
    BOOLEAN      tokenCopy;
    BOOLEAN      tokenEffective;
    SECURITY_IMPERSONATION_LEVEL tokenLevel;

    token = PsReferenceImpersonationToken( 
                          PsGetCurrentThread(), 
                          &tokenCopy, 
                          &tokenEffective, 
                          &tokenLevel 
                          );

    if( !token ) {
        token = PsReferencePrimaryToken( 
                          PsGetCurrentProcess()
                          );

    }

    //
    // Now that we have a token reference, get a handle to it
    // so that we can query it.
    //
    status = ObOpenObjectByPointer( token, 
                                    0, 
                                    NULL,
                                    TOKEN_QUERY, 
                                    NULL, 
                                    KernelMode, 
                                    &tokenHandle 
                                    );

    ObDereferenceObject( token );
    if( !NT_SUCCESS( status )) {

        //
        // We coudn't open the token!!
        //
        return NULL;
    }    
    return tokenHandle;
}


//----------------------------------------------------------------------
//
// GetSecurityInfoWorkRoutine
//
// Gets security information in the context of the System process.
//
//----------------------------------------------------------------------
VOID 
GetSecurityInfoWorkRoutine(
    PVOID Context
    )
{
    PGETSECINFO_WORK_ITEM workItem = (PGETSECINFO_WORK_ITEM) Context;
    PSecurityUserData userInformation = NULL;
    PUNICODE_STRING userName, domainName, logonServer;

    workItem->Status = GetSecurityUserInfo( workItem->LogonId,
                                             UNDERSTANDS_LONG_NAMES,
                                             &userInformation );
    if( NT_SUCCESS( workItem->Status )) {

        //
        // Get convenient string pointers
        //
        userName = &workItem->UserInformation.UserName;
        domainName = &workItem->UserInformation.LogonDomainName;
        logonServer = &workItem->UserInformation.LogonServer;

        //
        // Copy out the strings to pool
        //
        userName->MaximumLength = userInformation->UserName.Length + sizeof(WCHAR);
        userName->Buffer = ExAllocatePool( PagedPool, userName->MaximumLength );
        if( userName->Buffer ) {

            RtlCopyUnicodeString( userName, &userInformation->UserName );
            userName->Buffer[ userName->Length / sizeof(WCHAR)] = 0;
        }
        domainName->MaximumLength = userInformation->LogonDomainName.Length + sizeof(WCHAR);
        domainName->Buffer = ExAllocatePool( PagedPool, domainName->MaximumLength );
        if( domainName->Buffer ) {

            RtlCopyUnicodeString( domainName, &userInformation->LogonDomainName );
            domainName->Buffer[ domainName->Length / sizeof(WCHAR)] = 0;
        }
        logonServer->MaximumLength = userInformation->LogonServer.Length + sizeof(WCHAR);
        logonServer->Buffer = ExAllocatePool( PagedPool, logonServer->MaximumLength );
        if( logonServer->Buffer ) {

            RtlCopyUnicodeString( logonServer, &userInformation->LogonServer );
            logonServer->Buffer[ logonServer->Length / sizeof(WCHAR)] = 0;
        }

        //
        // Free allocations if any of them failed
        //
        if( !userName->Buffer ||
            !domainName->Buffer ||
            !logonServer->Buffer ) {

            if( userName->Buffer ) ExFreePool( userName->Buffer );
            if( domainName->Buffer ) ExFreePool( domainName->Buffer );
            if( logonServer->Buffer ) ExFreePool( logonServer->Buffer );
            workItem->Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        LsaFreeReturnBuffer( userInformation );
    }
    KeSetEvent( &workItem->Event, 0, FALSE );
}
    

//----------------------------------------------------------------------
//
// TokenmonGetLogonId
//
// Given a token handle, retreives the logonId.
//
//----------------------------------------------------------------------
BOOLEAN 
TokenmonGetLogonId( 
    BOOLEAN Query,
    HANDLE TokenHandle, 
    PLUID LogonId,
    PWCHAR *UserName,
    PWCHAR *DomainName,
    PWCHAR *LogonServer
    )
{
    NTSTATUS     status;
    ULONG        requiredLength;
    TOKEN_STATISTICS tokenStats;
    PSecurityUserData userInformation = NULL;
    GETSECINFO_WORK_ITEM workItem;
    PHASH_ENTRY  hashEntry;
    PIRP         prevTopLevelIrp;

    //
    // Set the top level IRP so that we can track our own reentrancy
    //
    prevTopLevelIrp = IoGetTopLevelIrp();
    IoSetTopLevelIrp( (PIRP) RealNtQueryInformationToken );

    //
    // Get the logon ID from the token
    //
    *UserName = NULL;
    *DomainName = NULL;
    *LogonServer = NULL;
    status = ZwQueryInformationToken( TokenHandle, 
                                      TokenStatistics,
                                      &tokenStats,
                                      sizeof( tokenStats ),
                                      &requiredLength 
                                      );
    IoSetTopLevelIrp( prevTopLevelIrp );

    if( NT_SUCCESS( status )) {

        *LogonId = tokenStats.AuthenticationId;

        hashEntry = TokenmonLookupHashEntry( LogonId );
        if( !hashEntry || !hashEntry->GotSecurityInfo ) {

            //
            // Now get the user and domain names
            //
            if( LogonId->LowPart == SYSTEMACCOUNT_LOW && 
                LogonId->HighPart == SYSTEMACCOUNT_HIGH ) {

                //
                // Special case for system account 
                //
                *UserName = ExAllocatePool( PagedPool, sizeof( SystemUser ) + sizeof(WCHAR));
                if( *UserName ) wcscpy( *UserName, SystemUser );
                *DomainName = ExAllocatePool( PagedPool, sizeof( SystemDomain ) + sizeof(WCHAR));
                if( *DomainName ) wcscpy( *DomainName, SystemDomain );
                *LogonServer = ExAllocatePool( PagedPool, sizeof( SystemServer ) + sizeof(WCHAR));
                if( *LogonServer ) wcscpy( *LogonServer, SystemServer );

                if( !*UserName || !*DomainName || !*LogonServer ) {

                    if( *UserName ) ExFreePool( *UserName );
                    if( *DomainName ) ExFreePool( *DomainName );
                    if( *LogonServer ) ExFreePool( *LogonServer );
                    *UserName = NULL;
                    *DomainName = NULL;
                    *LogonServer = NULL;
                    status = STATUS_INSUFFICIENT_RESOURCES;
                }
            
            } else {

                //
                // If we can't query right now (like we're in the context of LSASS),
                // just return and we'll skip recording this event
                //
                if( !Query ) return FALSE;

                //
                // Other account
                //
                ExInitializeWorkItem( &workItem.Item, GetSecurityInfoWorkRoutine, &workItem );
                KeInitializeEvent( &workItem.Event, SynchronizationEvent, FALSE );
                workItem.LogonId = LogonId;

                ExQueueWorkItem( (PWORK_QUEUE_ITEM) &workItem, CriticalWorkQueue );

                KeWaitForSingleObject( &workItem.Event, Executive,
                                       KernelMode, FALSE, NULL );
        
                if( NT_SUCCESS( workItem.Status )) {

                    DbgPrint(("User: \\\\%Z\\%Z Server: %Z\n",
                              &workItem.UserInformation.UserName,
                              &workItem.UserInformation.LogonDomainName,
                              &workItem.UserInformation.LogonServer ));
                    *UserName    = workItem.UserInformation.UserName.Buffer;
                    *DomainName  = workItem.UserInformation.LogonDomainName.Buffer;
                    *LogonServer = workItem.UserInformation.LogonServer.Buffer;

                } else {

                    status = workItem.Status;
                }
            }
            if( NT_SUCCESS( status )) {

                TokenmonUpdateHashEntry( LogonId, 
                                         *UserName,
                                         *DomainName );
            }
        } else {

            //
            // We've already got the logon information
            //
            *UserName = ExAllocatePool( PagedPool, (wcslen( hashEntry->UserName) + 1 ) * sizeof(WCHAR));
            if( *UserName ) wcscpy( *UserName, hashEntry->UserName );
            *DomainName = ExAllocatePool( PagedPool, (wcslen( hashEntry->DomainName ) + 1) * sizeof(WCHAR));
            if( *DomainName ) wcscpy( *DomainName, hashEntry->DomainName );
            *LogonServer = ExAllocatePool( PagedPool, (wcslen( hashEntry->DomainName ) + 1) * sizeof(WCHAR));
            if( *LogonServer ) wcscpy( *LogonServer, hashEntry->DomainName );

            if( !*UserName || !*DomainName || !*LogonServer ) {

                if( *UserName ) ExFreePool( *UserName );
                if( *DomainName ) ExFreePool( *DomainName );
                if( *LogonServer ) ExFreePool( *LogonServer );
                *UserName = NULL;
                *DomainName = NULL;
                *LogonServer = NULL;
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
    }
    return NT_SUCCESS( status );
}


//----------------------------------------------------------------------
//                   H O O K  R O U T I N E S 
//----------------------------------------------------------------------

//----------------------------------------------------------------------
//
// ProcessNotifyRoutine
//
//----------------------------------------------------------------------
VOID 
ProcessNotifyRoutine( 
    HANDLE ParentId, 
    HANDLE ProcessId, 
    BOOLEAN Create 
    )
{
    PHASH_ENTRY    hashEntry;
    PEPROCESS      Parent;
    PEPROCESS      Child;
    WCHAR          ParentName[32];
    WCHAR          ChildName[32];
    LARGE_INTEGER  time, perfTime;
    LUID           logonId;
    HANDLE         tokenHandle;
    PWCHAR         userName, domainName, logonServer;
    WCHAR          output[MAXBUFENTRYLEN];

    //
    // Get a handle to the current thread's effective token.
    //
    if( !(tokenHandle = TokenmonOpenEffectiveToken()) ) {

        //
        // Couldn't open the token - this should never happen.
        // 
        return;
    }

    //
    // Get the logon Id, user name, and domain name
    //
    if( !TokenmonGetLogonId( TRUE, tokenHandle, 
                             &logonId,
                             &userName,
                             &domainName,
                             &logonServer )) {

        ZwClose( tokenHandle );
        return;
    }        

    //
    // We're done with the token handle
    //
    ZwClose( tokenHandle );

    //
    // Get the time
    //
    TokenmonGetTimeStamp( &time, &perfTime );

    //
    // If its a create call, we can get the names of both the parent
    // and the child. If its a delete, the child process is already deleted
    // and the parent may not exist either.
    //
    if( Create ) {

        //
        // Get the parent name.
        //
        PsLookupProcessByProcessId( ParentId, &Parent );
        ObDereferenceObject( Parent );
        TokenmonGetProcess( Parent, ParentName );

        DbgPrint(("   Parent name: %s\n", ParentName ));

        //
        // Get the child name.
        //
        PsLookupProcessByProcessId( ProcessId, &Child );
        ObDereferenceObject( Child );
        TokenmonGetProcess( Child, ChildName );

        DbgPrint(("   Child name: %s\n", ChildName ));

        //
        // Send the record to the output
        //
        swprintf( output, L"%s\t%d\tCREATE PROCESS\t%08I64X: \\\\%s\\%s\tParent: %s",
                  ChildName, 
                  PsGetCurrentThreadId(),
                  *(PULONGLONG) &logonId,
                  domainName,
                  userName,
                  ParentName );
        LogRecord(  InterlockedIncrement( &Sequence ),
                    &time,
                    &perfTime,
                    output );
    
    } else {

        //
        // Get the current process name
        //
        TokenmonGetProcess( PsGetCurrentProcess(), ParentName );

        //
        // Send the record to the output
        //
        swprintf( output, L"%s\t%d\tEXIT PROCESS\t%08I64X: \\\\%s\\%s",
                  ParentName, 
                  PsGetCurrentThreadId(),
                  *(PULONGLONG) &logonId, 
                  domainName,
                  userName );
        LogRecord(  InterlockedIncrement( &Sequence ),
                    &time,
                    &perfTime,
                    output );
    }

    //
    // Free resources
    //
    if( userName ) {
    
        ASSERT( userName != NULL );
        ExFreePool( userName );
        ASSERT( domainName != NULL );
        ExFreePool( domainName );
        ASSERT( logonServer != NULL );
        ExFreePool( logonServer );
    }
}


//----------------------------------------------------------------------
//
// HookNtSetInformationThread
//
// This hook watches for impersonation, a thread characteristic
// specified by information class 5.
//
//----------------------------------------------------------------------
NTSTATUS 
HookNtSetInformationThread( 
    IN HANDLE ThreadHandle,
    IN THREADINFOCLASS ThreadInformationClass,
    IN PVOID ThreadInformation,
    IN ULONG ThreadInformationLength 
    )
{
    HANDLE tokenHandle, impTokenHandle = (HANDLE) -1;
    NTSTATUS ntStatus;
    LUID logonId, impersonateLogonId;
    POBJECT token;
    WCHAR name[MAXPROCNAMELEN];
    LARGE_INTEGER time, perfTime;
    BOOLEAN tokenCopy;
    BOOLEAN tokenEffective;
    SECURITY_IMPERSONATION_LEVEL tokenLevel;
    POBJECT impToken, primToken;
    PWCHAR userName, impersonateUserName;
    PWCHAR domainName, impersonateDomainName;
    PWCHAR logonServer, impersonateServer;
    WCHAR output[MAXBUFENTRYLEN];

    //
    // Record whether or not this thread is impersonating before
    // letting the call through
    //
    impToken = PsReferenceImpersonationToken( 
                  PsGetCurrentThread(), 
                  &tokenCopy, 
                  &tokenEffective, 
                  &tokenLevel );
    if( impToken ) {

        impTokenHandle = TokenmonOpenEffectiveToken();
        ZwClose( impTokenHandle );
        ObDereferenceObject( impToken );
    }

    //
    // Pass the call through
    //
    ntStatus = RealNtSetInformationThread( 
                            ThreadHandle,
                            ThreadInformationClass,
                            ThreadInformation,
                            ThreadInformationLength );

    //
    // Note that this call can be used to set the token
    // for other threads - we ignore this very rare 
    // usage (where ThreadHandle != currenthread). We also 
    // ignore operations that are taking place by or on behalf
    // of our GUI.
    //
    if( NT_SUCCESS( ntStatus ) && ThreadHandle == (HANDLE) -2 &&
        ThreadInformationClass == THREAD_ASSIGN_TOKEN_CLASS &&
        impToken != GUIToken ) {
        
        //
        // First, see if this is a GUI-related operation
        //
        if( (primToken = 
                PsReferencePrimaryToken( PsGetCurrentProcess())) ==
             GUIToken ) {
            
            ObDereferenceObject( primToken );
            return ntStatus;
        }
        ObDereferenceObject( primToken );

        //
        // Okay, now report the operation to the GUI
        //
        tokenHandle = TokenmonOpenToken();

        if( TokenmonGetLogonId( TRUE, tokenHandle, 
                                &logonId, 
                                &userName, 
                                &domainName,
                                &logonServer )) {

            TokenmonGetProcess( PsGetCurrentProcess(), name );
            TokenmonGetTimeStamp( &time, &perfTime );
                
            ZwClose( tokenHandle );

            if( *(PULONG) ThreadInformation ) {

                if( TokenmonGetLogonId( TRUE, (HANDLE) *(PULONG) ThreadInformation, 
                                        &impersonateLogonId,
                                        &impersonateUserName,
                                        &impersonateDomainName,
                                        &impersonateServer )) {

                    impToken = PsReferenceImpersonationToken( 
                        PsGetCurrentThread(), 
                        &tokenCopy, 
                        &tokenEffective, 
                        &tokenLevel );
                    ObDereferenceObject( impToken );

                    swprintf( output, L"%s\t%d\t%s\t%08I64X: \\\\%s\\%s\t%08I64X: \\\\%s\\%s", 
                              name,
                              PsGetCurrentThreadId(),
                              tokenLevel < sizeof(ImpersonationLevel)/sizeof(ImpersonationLevel[0]) -1 ?
                              ImpersonationLevel[tokenLevel] : L"IMPERSONATE-<unknown level>",
                              *(PULONGLONG) &logonId,
                              domainName,
                              userName,
                              *(PULONGLONG) &impersonateLogonId,
                              impersonateDomainName,
                              impersonateUserName );
                    LogRecord( InterlockedIncrement( &Sequence ),
                               &time,
                               &perfTime,
                               output );

                    if( impersonateUserName ) {

                        ExFreePool( impersonateUserName );
                        ExFreePool( impersonateDomainName );
                        ExFreePool( impersonateServer );
                    }
                }
            
            } else if( impToken ) {
                
                //
                // Only send an output if this thread was impersonating
                //
                swprintf( output, L"%s\t%d\tREVERTTOSELF\t%08I64X: \\\\%s\\%s", 
                          name,
                          PsGetCurrentThreadId(),
                          *(PULONGLONG) &logonId,
                          domainName,
                          userName );
                LogRecord( InterlockedIncrement( &Sequence ),
                           &time,
                           &perfTime,
                           output );
            }
            if( userName ) {

                ASSERT( userName != NULL );
                ExFreePool( userName );
                ASSERT( domainName != NULL );
                ExFreePool( domainName );
                ASSERT( logonServer != NULL );
                ExFreePool( logonServer );
            }
        }
    }
    return ntStatus;
}


//----------------------------------------------------------------------
//
// HookAdjustPrivilegesToken
//
// This is our hook routine for NtAdjustPrivilegesToken. After
// recording each privilege either enabled or disabled (that
// actually changes state) we let the call pass through.
//
//----------------------------------------------------------------------
NTSTATUS 
HookNtAdjustPrivilegesToken(
    HANDLE TokenHandle, 
    BOOLEAN DisableAllPrivileges, 
    PTOKEN_PRIVILEGES NewState, 
    ULONG BufferLength, 
    PTOKEN_PRIVILEGES PreviousState, 
    PULONG ReturnLength 
    )
{
    NTSTATUS ntStatus;
    LARGE_INTEGER time, perfTime;
    LUID logonId;
    ULONG i;
    WCHAR name[MAXPROCNAMELEN];
    PWCHAR privilegeName, attribute;
    WCHAR output[MAXBUFENTRYLEN];
    PWCHAR userName, domainName, logonServer;

    //
    // Pass the call through
    //
    ntStatus = RealNtAdjustPrivilegesToken( 
                      TokenHandle,
                      DisableAllPrivileges,
                      NewState,
                      BufferLength,
                      PreviousState,
                      ReturnLength );

    if( NT_SUCCESS( ntStatus )) {

        //
        // Get the Token SID
        //
        if( TokenmonGetLogonId( TRUE, TokenHandle, 
                                &logonId,
                                &userName,
                                &domainName,
                                &logonServer )) {

            TokenmonGetProcess( PsGetCurrentProcess(), name );
            TokenmonGetTimeStamp( &time, &perfTime );

            if( DisableAllPrivileges ) {

                swprintf( output, L"%s\t%d\tADJUST PRIVILEGES\t%08I64X: \\\\%s\\%s\tDISABLE ALL", 
                          name,
                          PsGetCurrentThreadId(),
                          *(PULONGLONG) &logonId,
                          domainName,
                          userName );
                LogRecord( InterlockedIncrement( &Sequence ),
                           &time,
                           &perfTime,
                           output );

            } else {

                for( i = 0; i < NewState->PrivilegeCount; i++ ) {
                        
                    if( NewState->Privileges[i].Luid.LowPart <= MAX_PRIVILEGE-1 ) {
                            
                        privilegeName = PrivilegeNames[ NewState->Privileges[i].Luid.LowPart ];

                    } else {

                        privilegeName = PrivilegeNames[MAX_PRIVILEGE];
                    }
                    if( DisableAllPrivileges ||
                        !(NewState->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) ) {

                        attribute = PrivilegeAttribute[0];

                    } else {

                        attribute = PrivilegeAttribute[1];
                    }
                    swprintf( output, L"%s\t%d\tADJUST PRIVILEGES\t%08I64X: \\\\%s\\%s\t%s: %s",
                              name, 
                              PsGetCurrentThreadId(),
                              *(PULONGLONG) &logonId,
                              domainName,
                              userName,
                              attribute, 
                              privilegeName );
                    LogRecord( InterlockedIncrement( &Sequence ),
                               &time,
                               &perfTime,
                               output );
                }
            }

            //
            // Free resources
            //
            if( userName ) {

                ASSERT( userName != NULL );
                ExFreePool( userName );
                ASSERT( domainName != NULL );
                ExFreePool( domainName );
                ASSERT( logonServer != NULL );
                ExFreePool( logonServer );
            }
        }
    }

    return ntStatus;
}


//----------------------------------------------------------------------
//
// HookNtCreateToken
//
// This is our hook routine for NtCreateToken. We let the call
// pass through to the real function, and if it is successfull we
// wait for the GUI to pass us down the Domain/User string, which
// we'll associate with the logon session.
//
//----------------------------------------------------------------------
NTSTATUS 
HookNtCreateToken(
    PHANDLE TokenHandle, 
    ACCESS_MASK DesiredAccess, 
    POBJECT_ATTRIBUTES ObjectAttributes, 
    TOKEN_TYPE TokenType, 
    PLUID LogonId, 
    PLARGE_INTEGER ExpirationTime, 
    PTOKEN_USER User, 
    PTOKEN_GROUPS Groups, 
    PTOKEN_PRIVILEGES Privileges, 
    PTOKEN_OWNER Owner, 
    PTOKEN_PRIMARY_GROUP PrimaryGroup, 
    PTOKEN_DEFAULT_DACL DefaultDacl, 
    PTOKEN_SOURCE TokenSource 
    )
{
    NTSTATUS ntStatus;
    LARGE_INTEGER time, perfTime;
    WCHAR name[MAXPROCNAMELEN];
    WCHAR output[MAXBUFENTRYLEN];
    LUID logonId;

    //
    // First pass the call through
    //
    ntStatus = RealNtCreateToken(
        TokenHandle, 
        DesiredAccess, 
        ObjectAttributes, 
        TokenType, 
        LogonId,
        ExpirationTime, 
        User, 
        Groups, 
        Privileges, 
        Owner, 
        PrimaryGroup, 
        DefaultDacl, 
        TokenSource );

    if( NT_SUCCESS( ntStatus )) {
    
        //
        // The token session was successfully created
        //
        DbgPrint(("New session: %d\n", *LogonId ));
        TokenmonGetProcess( PsGetCurrentProcess(), name );
        TokenmonGetTimeStamp( &time, &perfTime );

        //
        // Add this session to the hash table. Note: we don't look up the
        // user's security inforamation yet, since LSASS still doesn't know
        // about this logon session.
        //
        TokenmonStoreHash( TRUE, 
                           LogonId,
                           name );

        //
        // Tell the Security Manager we want to know when this user is logged out
        //
        SeMarkLogonSessionForTerminationNotification( LogonId );
    }
    return ntStatus;
}    


//----------------------------------------------------------------------
//
// HookNtFsControlFile
//
// This is the hook for NtImpersonateClientOfPipe. We wait for the 
// call to complete successfully and then check to see who we
// are impersonating.
//
//----------------------------------------------------------------------
NTSTATUS 
HookNtFsControlFile(
    IN HANDLE FileHandle,
    IN HANDLE Event OPTIONAL,
    IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
    IN PVOID ApcContext OPTIONAL,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN ULONG FsControlCode,
    IN PVOID InputBuffer OPTIONAL,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer OPTIONAL,
    IN ULONG OutputBufferLength
    )
{
    NTSTATUS ntStatus;
    LARGE_INTEGER time, perfTime;
    WCHAR name[MAXPROCNAMELEN];
    WCHAR output[MAXBUFENTRYLEN];
    HANDLE tokenHandle = NULL;
    HANDLE impTokenHandle = NULL;
    LUID logonId, impersonateLogonId;
    PWCHAR userName, domainName, logonServer;
    PWCHAR impersonateUserName, impersonateDomainName, impersonateLogonServer;

    //
    // Get the original security context
    //
    if( FsControlCode == FSCTL_PIPE_IMPERSONATE ) {

        tokenHandle = TokenmonOpenEffectiveToken();
    }

    //
    // Pass the call through
    //
    ntStatus = RealNtFsControlFile(
        FileHandle,
        Event,
        ApcRoutine,
        ApcContext,
        IoStatusBlock,
        FsControlCode,
        InputBuffer,
        InputBufferLength,
        OutputBuffer,
        OutputBufferLength
        );

    //
    // Log if success
    //
    if( NT_SUCCESS( ntStatus ) &&
        tokenHandle &&
        (impTokenHandle = TokenmonOpenEffectiveToken())) {

        //
        // Get the logon Id, user name, and domain name
        //
        if( TokenmonGetLogonId( TRUE, tokenHandle, 
                                 &logonId,
                                 &userName,
                                 &domainName,
                                 &logonServer ) &&
            TokenmonGetLogonId( TRUE, impTokenHandle,
                                &impersonateLogonId,
                                &impersonateUserName,
                                &impersonateDomainName,
                                &impersonateLogonServer )) {

            TokenmonGetProcess( PsGetCurrentProcess(), name );
            TokenmonGetTimeStamp( &time, &perfTime );
            swprintf( output, L"%s\t%d\tIMPERSONATE CLIENT OF PIPE\t%08I64X: \\\\%s\\%s\t%08I64X: \\\\%s\\%s", 
                      name,
                      PsGetCurrentThreadId(),
                      *(PULONGLONG) &logonId,
                      domainName,
                      userName,
                      *(PULONGLONG) &impersonateLogonId,
                      impersonateDomainName,
                      impersonateUserName );
            LogRecord( InterlockedIncrement( &Sequence ),
                       &time,
                       &perfTime,
                       output );
        }   
        
        //
        // Free resources
        //
        if( userName ) {
            
            ExFreePool( userName );
            ExFreePool( domainName );
            ExFreePool( logonServer );
            if( impersonateUserName ) {
                
                ExFreePool( impersonateUserName );
                ExFreePool( impersonateDomainName );
                ExFreePool( impersonateLogonServer );
            }
        }
    }
    if( tokenHandle )    ZwClose( tokenHandle );
    if( impTokenHandle ) ZwClose( impTokenHandle );
    return ntStatus;
}


//----------------------------------------------------------------------
//
// HookNtQueryInformationToken
//
// We don't actually monitor this function, but hook it so that
// we can obtain a logon session's information as soon after
// the session is created as possible. We can't get the information
// in the NtCreateToken call because LSASS is in the process of
// setting up the logon session.
//
//----------------------------------------------------------------------
NTSTATUS 
HookNtQueryInformationToken(
    HANDLE TokenHandle,
    ULONG TokenInformationClass,
    PVOID TokenInformation,
    ULONG TokenInformationLength,
    PULONG ReturnLength
    )
{
    NTSTATUS   ntStatus;
    PWCHAR     userName, domainName, logonServer;
    LUID       logonId;    

    ntStatus = RealNtQueryInformationToken( TokenHandle,
                                            TokenInformationClass,
                                            TokenInformation,
                                            TokenInformationLength,
                                            ReturnLength );
    if( NT_SUCCESS( ntStatus )) {

        //
        // If this isn't us calling ourselves, update the has entry
        //
        if( IoGetTopLevelIrp() != (PIRP) RealNtQueryInformationToken ) {

            //
            // Call this function just so that our hash table entry
            // for this logon session is updated
            //
            if( TokenmonGetLogonId( TRUE, TokenHandle,
                                    &logonId,
                                    &userName,
                                    &domainName,
                                    &logonServer )) {

                ExFreePool( userName );
                ExFreePool( domainName );
                ExFreePool( logonServer );
            }
        }
    }
    return ntStatus;
}


//----------------------------------------------------------------------
//
// HookNtImpersonateClientOfPort
//
// This is the hook for NtImpersonateClientOfPort. We wait for the 
// call to complete successfully and then check to see who we
// are impersonating.
//
//----------------------------------------------------------------------
NTSTATUS
HookNtImpersonateClientOfPort(
    IN HANDLE PortHandle,
    IN PPORT_MESSAGE Message
    )
{
    NTSTATUS ntStatus;
    LARGE_INTEGER time, perfTime;
    WCHAR name[MAXPROCNAMELEN];
    WCHAR output[MAXBUFENTRYLEN];
    HANDLE tokenHandle;
    HANDLE impTokenHandle = NULL;
    LUID logonId, impersonateLogonId;
    PWCHAR userName, domainName, logonServer;
    PWCHAR impersonateUserName, impersonateDomainName, impersonateLogonServer;

    //
    // Get the original security context
    //
    tokenHandle = TokenmonOpenEffectiveToken();
    
    //
    // Pass the call through
    //
    ntStatus = RealNtImpersonateClientOfPort(
        PortHandle,
        Message );

    //
    // Log if success
    //
    if( NT_SUCCESS( ntStatus ) &&
        tokenHandle &&
        (impTokenHandle = TokenmonOpenEffectiveToken())) {

        //
        // Get the logon Id, user name, and domain name
        //
        if( TokenmonGetLogonId( TRUE, tokenHandle, 
                                 &logonId,
                                 &userName,
                                 &domainName,
                                 &logonServer ) &&
            TokenmonGetLogonId( FALSE, impTokenHandle,
                                &impersonateLogonId,
                                &impersonateUserName,
                                &impersonateDomainName,
                                &impersonateLogonServer )) {

            TokenmonGetProcess( PsGetCurrentProcess(), name );
            TokenmonGetTimeStamp( &time, &perfTime );
            swprintf( output, L"%s\t%d\tIMPERSONATE CLIENT OF PORT\t%08I64X: \\\\%s\\%s\t%08I64X: \\\\%s\\%s", 
                      name,
                      PsGetCurrentThreadId(),
                      *(PULONGLONG) &logonId,
                      domainName,
                      userName,
                      *(PULONGLONG) &impersonateLogonId,
                      impersonateDomainName,
                      impersonateUserName );
            LogRecord( InterlockedIncrement( &Sequence ),
                       &time,
                       &perfTime,
                       output );
        }   
        
        //
        // Free resources
        //
        if( userName ) {
            
            ExFreePool( userName );
            ExFreePool( domainName );
            ExFreePool( logonServer );
            if( impersonateUserName ) {
                
                ExFreePool( impersonateUserName );
                ExFreePool( impersonateDomainName );
                ExFreePool( impersonateLogonServer );
            }
        }
    }
    if( tokenHandle )    ZwClose( tokenHandle );
    if( impTokenHandle ) ZwClose( impTokenHandle );
    return ntStatus;
}


//----------------------------------------------------------------------
//
// HookLogoff
//
// This is a callback from the Security Reference Monitor that
// notifies us when all references to a logon session have been
// removed, which means that the session is closed and the user
// is considered logged-off.
// 
//----------------------------------------------------------------------
VOID 
HookLogoff( 
    PLUID LogonId 
    )
{
    WCHAR   name[MAXPROCNAMELEN];
    LARGE_INTEGER time, perfTime;
    ULONGLONG hours, minutes, logonTime;
    LARGE_INTEGER logoffTime;
    PHASH_ENTRY logonEntry;
    WCHAR output[MAXBUFENTRYLEN];

    //
    // Lookup the hash entry
    //
    logonEntry = TokenmonLookupHashEntry( LogonId );
    if( logonEntry ) {

        ASSERT( logonEntry->GotSecurityInfo );

        TokenmonGetProcess( PsGetCurrentProcess(), name );
        TokenmonGetTimeStamp( &time, &perfTime );
        
        //
        // Calculate logon time and convert to seconds
        //
        KeQueryTickCount( &logoffTime );
        logonTime = 
            (logoffTime.QuadPart - logonEntry->LogonTimeStamp.QuadPart)
            * KeQueryTimeIncrement();
        logonTime /= (ULONGLONG) 10000000;

        hours = logonTime / (ULONGLONG) 3600;
        minutes = (logonTime % (ULONGLONG) 3600) / (ULONGLONG) 60;

        swprintf( output, L"%s\t%d\tLOGOFF\t%08I64X: \\\\%s\\%s\t%I64d Hours %I64d Minutes", 
                  name, 
                  PsGetCurrentThreadId(),
                  *(PULONGLONG) &logonEntry->LogonId,
                  logonEntry->DomainName,
                  logonEntry->UserName,
                  hours, 
                  minutes );
        LogRecord( InterlockedIncrement( &Sequence ),
                   &time,
                   &perfTime,
                   output );
        TokenmonFreeHashEntry( &logonEntry->LogonId );
    }
}

//----------------------------------------------------------------------
//          H O O K / U N H O O K   R O U T I N E S
//----------------------------------------------------------------------

//----------------------------------------------------------------------
//
// UnhookTokenApis
//
// Unhooks the APIs that we've hooked.
//
//----------------------------------------------------------------------
VOID 
UnhookTokenApis( 
    VOID 
    )
{
    if( FilterOn ) {

        FilterOn = FALSE;
        SYSCALL( NtCreateTokenIndex )             = RealNtCreateToken;
        SYSCALL( NtAdjustPrivilegesTokenIndex )   = RealNtAdjustPrivilegesToken;
        SYSCALL( NtSetInformationThreadIndex )    = RealNtSetInformationThread;
        SYSCALL( NtImpersonateClientOfPortIndex ) = RealNtImpersonateClientOfPort;
        SYSCALL( NtFsControlFileIndex )           = RealNtFsControlFile;
        SYSCALL( NtQueryInformationTokenIndex )   = RealNtQueryInformationToken;
        SeUnregisterLogonSessionTerminatedRoutine( HookLogoff );
	    PsSetCreateProcessNotifyRoutine( ProcessNotifyRoutine, TRUE );
    }
}


//----------------------------------------------------------------------
//
// HookTokenApis
//
// Two things to hook:
//    NtCreateToken: this represents a logon.
//    FsrtlRegisterLogonSessionTerminationNotification: this represents
//          a logoff.
//
//----------------------------------------------------------------------
VOID 
HookTokenApis( 
    VOID 
    )
{
    if( !FilterOn ) {

        FilterOn = TRUE;

        //
        // Hook NT calls
        //
        RealNtCreateToken = SYSCALL( NtCreateTokenIndex );
        SYSCALL( NtCreateTokenIndex ) = HookNtCreateToken;

        RealNtAdjustPrivilegesToken = SYSCALL( NtAdjustPrivilegesTokenIndex );
        SYSCALL( NtAdjustPrivilegesTokenIndex ) = HookNtAdjustPrivilegesToken;

        RealNtSetInformationThread = SYSCALL( NtSetInformationThreadIndex );
        SYSCALL( NtSetInformationThreadIndex ) = HookNtSetInformationThread;

        RealNtImpersonateClientOfPort = SYSCALL( NtImpersonateClientOfPortIndex );
        SYSCALL( NtImpersonateClientOfPortIndex ) = HookNtImpersonateClientOfPort;

        RealNtFsControlFile = SYSCALL( NtFsControlFileIndex );
        SYSCALL( NtFsControlFileIndex ) = HookNtFsControlFile;

        RealNtQueryInformationToken = SYSCALL( NtQueryInformationTokenIndex );
        SYSCALL( NtQueryInformationTokenIndex ) = HookNtQueryInformationToken;

        //
        // Register process create/delete notify
        //
	    PsSetCreateProcessNotifyRoutine( ProcessNotifyRoutine, FALSE );

        //
        // Register logoff notify
        //
        SeRegisterLogonSessionTerminatedRoutine( HookLogoff );
    }
}


//----------------------------------------------------------------------
//
// TokenmonDeviceControl
//
//----------------------------------------------------------------------
NTSTATUS  
TokenmonDeviceControl( 
    IN PIRP Irp, 
    IN PFILE_OBJECT FileObject, 
    IN BOOLEAN Wait,
    IN PVOID InputBuffer, 
    IN ULONG InputBufferLength, 
    OUT PVOID OutputBuffer, 
    IN ULONG OutputBufferLength, 
    IN ULONG IoControlCode,
    OUT PIO_STATUS_BLOCK IoStatus, 
    IN PDEVICE_OBJECT DeviceObject 
    ) 
{
    NTSTATUS            status = STATUS_SUCCESS;
    PLOG_BUFFER         old;

    //
    // Its a message from our GUI!
    //
    IoStatus->Status      = STATUS_SUCCESS; // Assume success
    IoStatus->Information = 0;      // Assume nothing returned
    
    switch ( IoControlCode ) {

    case IOCTL_TOKENMON_VERSION:
        
        //
        // Version #
        //
        if ( OutputBufferLength < sizeof(ULONG) ||
             OutputBuffer == NULL ) {

            status = STATUS_INVALID_PARAMETER;
            break;
        }
            
        *(ULONG *)OutputBuffer = TOKENMONVERSION;
        IoStatus->Information = sizeof(ULONG);
        break;

    case IOCTL_TOKENMON_STOPFILTER:
            
        //
        // Turn off logging
        //
        DbgPrint(("Tokenmon: stop logging\n"));
        UnhookTokenApis();
        break;

    case IOCTL_TOKENMON_STARTFILTER:
          
        //
        // Turn on logging. The GUI passes us the NtCreateToken
        // index when it makes this call.
        //
        DbgPrint(("Tokenmon: start logging\n"));
        NtCreateTokenIndex           = ((PSYSCALL_INDEX)InputBuffer)->NtCreateTokenIndex;
        NtAdjustPrivilegesTokenIndex = ((PSYSCALL_INDEX)InputBuffer)->NtAdjustPrivilegesTokenIndex;
        NtSetInformationThreadIndex  = ((PSYSCALL_INDEX)InputBuffer)->NtSetInformationThreadIndex;
        NtImpersonateClientOfPortIndex = ((PSYSCALL_INDEX)InputBuffer)->NtImpersonateClientOfPortIndex;
        NtFsControlFileIndex         = ((PSYSCALL_INDEX)InputBuffer)->NtFsControlFileIndex;
        NtQueryInformationTokenIndex = ((PSYSCALL_INDEX)InputBuffer)->NtQueryInformationTokenIndex;
        HookTokenApis();
        break;

    case IOCTL_TOKENMON_SETFILTER:
  
        //
        // Gui is updating the filter functions
        //
        DbgPrint(("Tokenmon: set filter\n"));
        FilterDef = *(PFILTER) InputBuffer;
        TokenmonUpdateFilters();
        break;

    case IOCTL_TOKENMON_ZEROSTATS:

        //
        // Reset all output buffers
        //
        DbgPrint (("Tokenmon: zero stats\n"));

        StartTime = KeQueryPerformanceCounter( NULL );
        MUTEX_WAIT( LogBufferMutex );

        while ( LogBuffer->Next )  {

            //
            // Free all but the first output buffer
            //
            old = LogBuffer->Next;
            LogBuffer->Next = old->Next;
            ExFreePool( old );
            NumLogBuffer--;
        }
 
        //
        // Set the output pointer to the start of the output buffer
        //
        LogBuffer->Len = 0;
        Sequence = 0;

        MUTEX_RELEASE( LogBufferMutex );
        break;

    case IOCTL_TOKENMON_GETSTATS:

        //
        // Copy the oldest output buffer to the caller
        //
        DbgPrint (("Tokenmon: get stats\n"));

        //
        // If the output buffer is too large to fit into the caller's buffer
        //
        if ( MAX_STORE > OutputBufferLength )  {

            status = STATUS_INVALID_PARAMETER;
            break;
        }

        MUTEX_WAIT( LogBufferMutex );

        if ( LogBuffer->Len  ||  LogBuffer->Next ) {

            //
            // Start output to a new output buffer
            //
            TokenmonNewLogBuffer();

            //
            // Fetch the oldest to give to user
            //
            old = TokenmonOldestLogBuffer();

            MUTEX_RELEASE( LogBufferMutex );

            //
            // Copy it to the caller's buffer
            //
            memcpy( OutputBuffer, old->Data, old->Len );

            //
            // Return length of copied info
            //
            IoStatus->Information = old->Len;

            //
            // Deallocate buffer
            //
            ExFreePool( old );

        } else {

            //
            // There is no unread data
            //
            MUTEX_RELEASE( LogBufferMutex );
            IoStatus->Information = 0;
        }
        break;
 
    default:

        //
        // Unknown control
        // 
        DbgPrint (("Tokenmon: unknown IRP_MJ_DEVICE_CONTROL\n"));
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    IoStatus->Status = status;
    IoCompleteRequest( Irp, IO_NO_INCREMENT );
    return status;
}


//----------------------------------------------------------------------
//     D I S P A T C H   A N D   H O O K   E N T R Y   P O I N T S
//----------------------------------------------------------------------

//----------------------------------------------------------------------
//
// TokenmonDispatch
//
// In this routine we handle requests to our own device. The only 
// requests we care about handling explicitely are IOCTL commands that
// we will get from the GUI. We also expect to get Create and Close 
// commands when the GUI opens and closes communications with us.
//
//----------------------------------------------------------------------
NTSTATUS 
TokenmonDispatch( 
    IN PDEVICE_OBJECT DeviceObject, 
    IN PIRP Irp 
    )
{
    PIO_STACK_LOCATION  irpStack;
    PVOID               inputBuffer;
    PVOID               outputBuffer;
    ULONG               inputBufferLength;
    ULONG               outputBufferLength;
    ULONG               ioControlCode;
    HANDLE              tokenHandle;

    //
    // Go ahead and set the request up as successful
    //
    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    //
    // Get a pointer to the current location in the Irp. This is where
    // the function codes and parameters are located.
    //
    irpStack = IoGetCurrentIrpStackLocation (Irp);

    //
    // Get the pointer to the input/output buffer and its length
    //
    inputBuffer         = Irp->AssociatedIrp.SystemBuffer;
    inputBufferLength   = irpStack->Parameters.DeviceIoControl.InputBufferLength;
    outputBuffer        = Irp->AssociatedIrp.SystemBuffer;
    outputBufferLength  = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
    ioControlCode       = irpStack->Parameters.DeviceIoControl.IoControlCode;

    switch (irpStack->MajorFunction) {
    case IRP_MJ_CREATE:

        DbgPrint(("Tokenmon: IRP_MJ_CREATE\n"));

        //
        // Get a reference to the token of the current process, so that
        // we can ignore its token activity 
        //
        GUIToken = PsReferencePrimaryToken( 
                                     PsGetCurrentProcess()
                                     );
        
        // 
        // Start the sequence number at 0
        // 
        Sequence = 0;
        break;

    case IRP_MJ_CLOSE:

        DbgPrint(("Tokenmon: IRP_MJ_CLOSE\n"));

        //
        // A GUI is closing communication
        //
        UnhookTokenApis();

        //
        // If the GUI has no more references to us, reset the output
        // buffers 
        //
        TokenmonResetLogBuffer();
        break;

    case IRP_MJ_DEVICE_CONTROL:

        DbgPrint (("Tokenmon: IRP_MJ_DEVICE_CONTROL\n"));

       	//
        // See if the output buffer is really a user buffer that we
        // can just dump data into.
        //
        if( IOCTL_TRANSFER_TYPE(ioControlCode) == METHOD_NEITHER ) {

            outputBuffer = Irp->UserBuffer;
        }

        //
        // Its a request from the GUI
        //
        return TokenmonDeviceControl( Irp, irpStack->FileObject, TRUE,
                                      inputBuffer, inputBufferLength, 
                                      outputBuffer, outputBufferLength,
                                      ioControlCode, &Irp->IoStatus, DeviceObject );

    default:

        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        IoCompleteRequest( Irp, IO_NO_INCREMENT );        
        return STATUS_INVALID_DEVICE_REQUEST;
   }

    //
    // Complete the IRP
    //
    IoCompleteRequest( Irp, IO_NO_INCREMENT );
    return STATUS_SUCCESS;   
}

//----------------------------------------------------------------------
//
// TokenmonUnload
//
// Our job is done - time to leave.
//
//----------------------------------------------------------------------
VOID 
TokenmonUnload( 
    IN PDRIVER_OBJECT DriverObject 
    )
{
    WCHAR                  deviceLinkBuffer[]  = L"\\DosDevices\\Tokenmon";
    UNICODE_STRING         deviceLinkUnicodeString;

    //
    // Delete the symbolic link for our GUI device
    //
    RtlInitUnicodeString( &deviceLinkUnicodeString, deviceLinkBuffer );
    IoDeleteSymbolicLink( &deviceLinkUnicodeString );

    DbgPrint(("Tokenmon.SYS: unloading\n"));

    //
    // Delete the device object
    //
    if( DriverObject->DeviceObject )  {

        IoDeleteDevice( DriverObject->DeviceObject );

    } 
    DbgPrint(("Tokenmon.SYS: deleted devices\n"));

    //
    // Unhook stuff
    //
    UnhookTokenApis();

    //
    // Now we can free any memory that is allocatedp
    //
    TokenmonFreeLogBuffer();
    TokenmonHashCleanup();

    DbgPrint(("Tokenmon.SYS: freed memory\n"));
}


//----------------------------------------------------------------------
//
// DriverEntry
//
// Installable driver initialization. Here we just set ourselves up.
//
//----------------------------------------------------------------------
NTSTATUS 
DriverEntry(
    IN PDRIVER_OBJECT DriverObject, 
    IN PUNICODE_STRING RegistryPath 
    )
{
    NTSTATUS                ntStatus;
    PDEVICE_OBJECT          deviceObject = NULL;
    WCHAR                   deviceNameBuffer[]  = L"\\Device\\Tokenmon";
    UNICODE_STRING          deviceNameUnicodeString;
    WCHAR                   deviceLinkBuffer[]  = L"\\DosDevices\\Tokenmon";
    UNICODE_STRING          deviceLinkUnicodeString;
    ULONG                   i;

    DbgPrint (("Tokenmon.SYS: entering DriverEntry\n"));

    //    
    // Setup the device name
    //    
    RtlInitUnicodeString (&deviceNameUnicodeString,
                          deviceNameBuffer );

    //
    // Create the device used for GUI communications
    //
    ntStatus = IoCreateDevice ( DriverObject,
                                0,
                                &deviceNameUnicodeString,
                                FILE_DEVICE_TOKENMON,
                                0,
                                TRUE,
                                &deviceObject );

    //
    // If successful, make a symbolic link that allows for the device
    // object's access from Win32 programs
    //
    if (NT_SUCCESS(ntStatus)) {

        //
        // Create a symbolic link that the GUI can specify to gain access
        // to this driver/device
        //
        RtlInitUnicodeString (&deviceLinkUnicodeString,
                              deviceLinkBuffer );
        ntStatus = IoCreateSymbolicLink (&deviceLinkUnicodeString,
                                         &deviceNameUnicodeString );
        if (!NT_SUCCESS(ntStatus)) {

            DbgPrint (("Tokenmon.SYS: IoCreateSymbolicLink failed\n"));
        
        }

        //
        // Create dispatch points for all routines that must be handled. 
        //
        DriverObject->MajorFunction[IRP_MJ_CLOSE]           =
        DriverObject->MajorFunction[IRP_MJ_CREATE]          =
        DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]  = TokenmonDispatch;
#if DBG
        DriverObject->DriverUnload                          = TokenmonUnload;
#endif
    }

    //
    // If something went wrong, cleanup the device object and don't load
    //
    if (!NT_SUCCESS(ntStatus)) {

        DbgPrint(("Tokenmon: Failed to create our device!\n"));

        if( deviceObject ) {

            IoDeleteSymbolicLink( &deviceLinkUnicodeString );
            IoDeleteDevice( deviceObject );
        }
        return ntStatus;
    }

    //
    // Find the process name offset
    //
    ProcessNameOffset = TokenmonGetProcessNameOffset();

    //
    // Initialize the mutexes
    //
    MUTEX_INIT( LogBufferMutex );
    MUTEX_INIT( HashMutex );
    MUTEX_INIT( FilterMutex );

    //
    // Initialize hash lookaside
    //
    ExInitializePagedLookasideList( &HashLookaside,
                                    NULL,
                                    NULL,
                                    0,
                                    sizeof( HASH_ENTRY ),
                                    'mkoT',
                                    256 );

    //
    // Allocate the first output buffer
    //
    LogBuffer   = ExAllocatePool( PagedPool, sizeof(*LogBuffer) );
    if ( !LogBuffer ) {

        // 
        // Oops - we can't do anything without at least one buffer
        // 
        IoDeleteDevice( deviceObject );
        IoDeleteSymbolicLink( &deviceLinkUnicodeString );
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // 
    // Set the buffer pointer to the start of the buffer just allocated
    // 
    LogBuffer->Len  = 0;
    LogBuffer->Next = NULL;
    NumLogBuffer = 1;

    return ntStatus;
}
