//======================================================================
// 
// Undelete.c
//
// Copyright (C) 1996-1999 Mark Russinovich and Bryce Cogswell
//
// Preserves files that are deleted by using the built-in Recycle-Bin
// functionality of NT 4.0.
// 
// This module implements the file undelete specific part of the filter
// driver and demonstrates the following driver techniques and APIs:
//
//   - creating custom IRPs 
//   - using KeAttachProcess, KeDetachProcess
//   - using directory-query IRPs to list the contents of a directory
//   - sharing a synchronization between a GUI and a driver
//   - stalling an IRP in a completion routine 
//   - obtaining the SID of the current process
//   - referencing a process token
//
//======================================================================
#include "ntddk.h"
#include "undelete.h"
#include "ioctlcmd.h"



//----------------------------------------------------------------------
//
// UndeleteFastIoDeviceControl
//
//----------------------------------------------------------------------
BOOLEAN  UndeleteFastIoDeviceControl( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait,
                                      IN PVOID InputBuffer, IN ULONG InputBufferLength, 
                                      OUT PVOID OutputBuffer, IN ULONG OutputBufferLength, IN ULONG IoControlCode,
                                      OUT PIO_STATUS_BLOCK IoStatus, IN PDEVICE_OBJECT DeviceObject ) {
    BOOLEAN             retval = FALSE;
    PHOOK_EXTENSION     hookExt;
    PCHAR               curDirPtr;
    NTSTATUS            ntStatus;
    ULONG               i;
    HANDLE              mutexHandle;

    if ( DeviceObject == GUIDevice )  {

        //
        // Its a message from our GUI!
        //
        IoStatus->Status      = STATUS_SUCCESS; 
        IoStatus->Information = 0;      

        switch ( IoControlCode ) {

        case UNDELETE_FILTER_UPDATE:

            //
            // Update of extension filters
            //
            if( FilterList ) ExFreePool( FilterList );

            FilterList = ExAllocatePool( NonPagedPool, InputBufferLength );
            if( FilterList ) {

                NumFilters = InputBufferLength/sizeof(FILTERENTRY);
                memcpy( FilterList, InputBuffer, InputBufferLength );

            } else {
                
                NumFilters = 0;
            }
            DbgPrint(("Undelete: %d filters in filter update\n", NumFilters ));
            break;

        case UNDELETE_DIR_FILTER_UPDATE:

            //
            // Update of directory filters
            //
            if( DirFilterArray ) ExFreePool( DirFilterArray );
            if( DirFilterBuffer) ExFreePool( DirFilterBuffer );

            SizeDirFilters = InputBufferLength;
            DirFilterBuffer = (PCHAR) ExAllocatePool( NonPagedPool, InputBufferLength );
            if( DirFilterBuffer ) {

                memcpy( DirFilterBuffer, InputBuffer, InputBufferLength );

                //
                // First, determine how many filters are here
                //
                curDirPtr = DirFilterBuffer;
                NumDirFilters = 0;
                while( curDirPtr < DirFilterBuffer + InputBufferLength ) {
                    
                    NumDirFilters++;
                    curDirPtr += strlen( curDirPtr )+1;
                }
                
                //
                // Allocate the array and fill it in
                //
                DirFilterArray = (PCHAR *) ExAllocatePool( NonPagedPool, NumDirFilters );
                if( DirFilterArray ) {

                    curDirPtr = DirFilterBuffer;
                    for( i = 0; i < NumDirFilters; i++ ) {
	      
                        DirFilterArray[i] = curDirPtr;
                        curDirPtr += strlen( curDirPtr )+1;	    
                    }
                } else {

                    ExFreePool( DirFilterBuffer );
                    DirFilterBuffer = NULL;
                    NumDirFilters = 0;
                }

            } else {

                NumDirFilters = 0;
            }
            break;

        case UNDELETE_MUTEX:

            //
            // The GUI has been started so we need to start worrying about synchronizing
            // access to the undelete bin. We only need to get the handle once; ignore
            // subsequent calls
            //
            if( !UndeleteMutex ) {

                ntStatus = ObReferenceObjectByHandle( (HANDLE) *(PULONG)InputBuffer, GENERIC_ALL,
                                                  NULL, KernelMode, &UndeleteMutex, NULL );
                if( !NT_SUCCESS( ntStatus )) {
                    
                    UndeleteMutex = NULL;
                }
                
                // 
                // We need to open this in the system process to that it won't go away. We could
                // do this by queuing a work item, but I use KeAttachProcess for demonstration
                // and because its more convenient.
                //
                KeAttachProcess( SystemProcess );
                ntStatus = ObOpenObjectByPointer( UndeleteMutex, 0, NULL,
                                                  MUTANT_ALL_ACCESS, NULL, KernelMode, &mutexHandle );
                KeDetachProcess( );
            }
            break;
 
        default:

            //
            // Unknown control
            // 
            DbgPrint (("Undelete: unknown IRP_MJ_DEVICE_CONTROL\n"));
            IoStatus->Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        retval = TRUE;

    } else {

        //
        // Its a call for a file system, so pass it through
        //
        hookExt = DeviceObject->DeviceExtension;

        if( hookExt->FileSystem->DriverObject->FastIoDispatch->FastIoDeviceControl ) {
        
            retval = hookExt->FileSystem->DriverObject->FastIoDispatch->FastIoDeviceControl( 
                FileObject, Wait, InputBuffer, InputBufferLength, OutputBuffer, 
                OutputBufferLength, IoControlCode, IoStatus, 
                hookExt->FileSystem );
        }
    }

    return retval;
}


//----------------------------------------------------------------------
//           F I L E   U N D L E T E   R O U T I N E S
//----------------------------------------------------------------------


//----------------------------------------------------------------------
//
// UndeleteInitInfoFile
//
// Initialize a new INFO file.
//
//----------------------------------------------------------------------
VOID UndeleteInitInfoHeader( BOOLEAN IsAnsi, PINFO_HEADER InfoHeader )
{

    InfoHeader->Entries   = 1;  // anticipating the new entry
    InfoHeader->NextID    = 1;  
    InfoHeader->Reserved2 = 0;

    //
    // If the volume is marked as ANSI, init an ANSI bin
    //
    if( IsAnsi ) {

        InfoHeader->VersionSignature = ANSI_VERSION_SIG;
        InfoHeader->Version = ANSI_RECYCLE_TYPE;

    } else {

        InfoHeader->VersionSignature = UNICODE_VERSION_SIG;
        InfoHeader->Version = UNICODE_RECYCLE_TYPE;
    }
}


//----------------------------------------------------------------------
//
// UndeleteReadInfoFile
//
// Opens the INFO file in the directory passed in, and reads out
// the header to get the file ID that should be assigned to the next
// file to be moved to the recycle bin.
//
//----------------------------------------------------------------------
ULONG UndeleteReadInfoFile( BOOLEAN IsAnsi,
                            PWCHAR RecycleBinPath, 
                            PINFO_HEADER InfoHeader,
                            PINFO_LIST *EntryList )
{
    UNICODE_STRING          infoFileName;
    HANDLE                  infoFile;
    PWCHAR                  infoFileNameBuffer;
    OBJECT_ATTRIBUTES       objectAttributes;
    IO_STATUS_BLOCK  	    ioStatus;
    NTSTATUS                ntStatus;
    ULONG                   fileId;
    ULONG                   i;
    PINFO_LIST              nextEntry, prevEntry;

    //
    // Assume no entries in file
    //
    *EntryList = NULL;
    fileId = 0;

    //
    // Make the name of the INFO file
    //
    infoFileNameBuffer = ExAllocatePool( NonPagedPool, FILE_NAME_LENGTH );
    if( !infoFileNameBuffer ) return (ULONG) -1;

    swprintf( infoFileNameBuffer, L"%s\\INFO", RecycleBinPath );
    RtlInitUnicodeString( &infoFileName, infoFileNameBuffer );

    //
    // Open it.
    //
    InitializeObjectAttributes( &objectAttributes, &infoFileName,
                                OBJ_CASE_INSENSITIVE, NULL, NULL );

    ntStatus = ZwCreateFile( &infoFile, FILE_READ_DATA|SYNCHRONIZE,
                             &objectAttributes, &ioStatus, NULL, 
                             FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM, 
                             FILE_SHARE_READ,
                             FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0 );
    if( !NT_SUCCESS( ntStatus )) {

        //
        // For now, assume INFO file doesn't exist yet. Go ahead and initialize the header
        //
        UndeleteInitInfoHeader( IsAnsi, InfoHeader );        
        DbgPrint(("Undelete: no info file yet.\n"));
        return 0;
    } 

    //
    // Read the header out of the file
    //
    ntStatus = ZwReadFile( infoFile, NULL, NULL, NULL, &ioStatus, InfoHeader, 
                           sizeof( INFO_HEADER ), NULL, NULL );

    if( !NT_SUCCESS( ntStatus ) || !InfoHeader->Entries) {

        //
        // Hmmm...well, initialize a new header
        //
        UndeleteInitInfoHeader( IsAnsi, InfoHeader );
        ZwClose( infoFile );
        return 0;
    }

    //
    // Read each entry out of the file
    //
    prevEntry = NULL;
    for( i = 0; i < InfoHeader->Entries; i++ ) {

        nextEntry = ExAllocatePool( PagedPool, sizeof( INFO_LIST ));
        if( !nextEntry ) {

            //
            // Really bad if no more paged pool
            //
            DbgPrint(("Undelete: OUT OF PAGED POOL!\n"));
            break;
        }

        nextEntry->Next = NULL;

        //
        // Suck it in
        //
        if( InfoHeader->Version == UNICODE_RECYCLE_TYPE ) 
            ntStatus = ZwReadFile( infoFile, NULL, NULL, NULL, &ioStatus, &nextEntry->UniEntry,
                                   sizeof( UNIINFO_ENTRY), NULL, NULL );
        else 
            ntStatus = ZwReadFile( infoFile, NULL, NULL, NULL, &ioStatus, &nextEntry->AnsiEntry,
                                   sizeof( ANSIINFO_ENTRY), NULL, NULL );
        if( !NT_SUCCESS( ntStatus )) {

            //
            // File is shorter than we thought
            //
            DbgPrint(("Undelete: file is only %d entries instead of %d\n", i, InfoHeader->Entries ));
            InfoHeader->Entries = i;
            break;
        }
        
        //
        // Stick the entry on the end of the list
        //
        if( prevEntry ) {
   
            prevEntry->Next = nextEntry;
            prevEntry = nextEntry;

        } else {

            *EntryList = nextEntry;
            prevEntry = nextEntry;

        }

        //
        // Keep track of the highest iD
        //
        if( fileId <= nextEntry->UniEntry.FileID ) {

            fileId = nextEntry->UniEntry.FileID+1;
        }
    }

    //
    // Update the header to reflect the new addition.
    //
    InfoHeader->Entries++;
    InfoHeader->NextID = fileId+1;
    ZwClose( infoFile );
    return fileId;
}


//----------------------------------------------------------------------
//
// UndeleteWriteInfoFile
//
// Opens the INFO file in the directory passed in, and reads out
// the header to get the file ID that should be assigned to the next
// file to be moved to the recycle bin.
//
//----------------------------------------------------------------------
VOID UndeleteWriteInfoFile( PWCHAR RecycleBinPath, PINFO_HEADER InfoHeader,
                            PINFO_LIST EntryList, PINFO_LIST NewEntry )
{
    UNICODE_STRING          infoFileName;
    PWCHAR                  infoFileNameBuffer;
    OBJECT_ATTRIBUTES       objectAttributes;
    IO_STATUS_BLOCK  	    ioStatus;
    NTSTATUS                ntStatus;
    HANDLE                  infoFile;
    ULONG                   fileId;
    ULONG                   i;
    PINFO_LIST              nextEntry;

    //
    // Make the name of the INFO file
    //
    infoFileNameBuffer = ExAllocatePool( NonPagedPool, FILE_NAME_LENGTH );
    if( !infoFileNameBuffer ) return;

    swprintf( infoFileNameBuffer, L"%s\\INFO", RecycleBinPath );
    RtlInitUnicodeString( &infoFileName, infoFileNameBuffer );

    //
    // Open it.
    //
    InitializeObjectAttributes( &objectAttributes, &infoFileName,
                                OBJ_CASE_INSENSITIVE, NULL, NULL );

    ntStatus = ZwCreateFile( &infoFile, FILE_WRITE_DATA|SYNCHRONIZE,
                             &objectAttributes, &ioStatus, NULL, 
                             FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM, 
                             FILE_SHARE_READ,
                             FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0 );
    if( !NT_SUCCESS( ntStatus )) {

        //
        // Bad if we can't open or create it
        //
        DbgPrint(("Undelete: Could not open/create INFO file for writing:%x\n", ntStatus));
        return;
    }

    //
    // Write the header
    //
    ntStatus = ZwWriteFile( infoFile, NULL, NULL, NULL, &ioStatus, InfoHeader, 
                            sizeof( INFO_HEADER ), NULL, NULL );

    if( !NT_SUCCESS( ntStatus )) {

        //
        // Hmmm...that's bad
        //
        DbgPrint(("Undelete: could not update INFO file\n"));
        ZwClose( infoFile );
        return;
    }

    //
    // Write each entry to the file, freeing it when done
    //
    nextEntry = EntryList;
    for( i = 0; i < InfoHeader->Entries - 1; i++ ) {

        //
        // We ran out of entries early
        //
        if( !nextEntry ) break;

        if( InfoHeader->Version == UNICODE_RECYCLE_TYPE )
            ntStatus = ZwWriteFile( infoFile, NULL, NULL, NULL, &ioStatus, &nextEntry->UniEntry,
                                    sizeof( UNIINFO_ENTRY), NULL, NULL );
        else
            ntStatus = ZwWriteFile( infoFile, NULL, NULL, NULL, &ioStatus, &nextEntry->AnsiEntry,
                                    sizeof( ANSIINFO_ENTRY), NULL, NULL );
        if( !NT_SUCCESS( ntStatus )) {

            //
            // That's bad
            //
            DbgPrint(("Undelete: could not write to INFO file:%x\n", ntStatus ));
        }
        
        //
        // Free the entry
        //
        EntryList = nextEntry;
        nextEntry = nextEntry->Next;
        ExFreePool( EntryList );
    }

    //
    // Write out the new entry, if there is one (it would fit in the bin)
    // 
    if( NewEntry ) {

        if( InfoHeader->Version == UNICODE_RECYCLE_TYPE ) 
            ntStatus = ZwWriteFile( infoFile, NULL, NULL, NULL, &ioStatus, &NewEntry->UniEntry,
                                    sizeof( UNIINFO_ENTRY), NULL, NULL );
        else
            ntStatus = ZwWriteFile( infoFile, NULL, NULL, NULL, &ioStatus, &NewEntry->AnsiEntry,
                                    sizeof( ANSIINFO_ENTRY), NULL, NULL );
        if( !NT_SUCCESS( ntStatus )) {

            //
            // That's bad
            //
            DbgPrint(("Undelete: could not write new entry to INFO file:%x\n", ntStatus ));
        }
        ExFreePool( NewEntry );
    }

    ZwClose( infoFile );
    return;
}


//----------------------------------------------------------------------
//
// UndeleteRecycleCurrentSize
//
// Does a directory enumeration of the recycle bin in order to get
// its current size.
//
//----------------------------------------------------------------------
ULONGLONG UndeleteGetRecycleBinSize( UCHAR LogicalDrive, PWCHAR RecycleBinPath ) 
{
    UNICODE_STRING          recycleUnicodeString;
    OBJECT_ATTRIBUTES       objectAttributes;
    HANDLE                  dirHandle;
    NTSTATUS                ntStatus;
    IO_STATUS_BLOCK         ioStatus;
    BOOLEAN                 firstQuery = TRUE;
    ULONGLONG               binSize = 0;
    PFILE_DIRECTORY_INFORMATION dirInfo, curEntry;

    RtlInitUnicodeString( &recycleUnicodeString, RecycleBinPath );
    
    InitializeObjectAttributes( &objectAttributes, &recycleUnicodeString,
                                OBJ_CASE_INSENSITIVE, NULL, NULL );

    ntStatus = ZwCreateFile( &dirHandle, FILE_LIST_DIRECTORY,
                             &objectAttributes, &ioStatus, NULL, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, 
                             FILE_OPEN, 
                             FILE_SYNCHRONOUS_IO_NONALERT|FILE_DIRECTORY_FILE, 
                             NULL, 0 );
    if( !NT_SUCCESS( ntStatus ) ) {

        DbgPrint(("Undelete: Could not open recycle bin: %x\n", ntStatus ));
        return (ULONGLONG) 0;
    }

    //
    // Allocate space for directory entry information
    //
    dirInfo = (PFILE_DIRECTORY_INFORMATION) ExAllocatePool( NonPagedPool, 4096 );
    if( !dirInfo ) return (ULONGLONG) 0;

    while( 1 ) {

        //
        // Get the contents of the directory, adding up the size as we go
        //
        ntStatus = ZwQueryDirectoryFile( dirHandle, NULL, NULL, 0, &ioStatus, 
                                         dirInfo, 4096, FileDirectoryInformation,
                                         FALSE, NULL, firstQuery );
        if( !NT_SUCCESS( ntStatus )) {

            //
            // Got what we wanted
            //
            DbgPrint(("Undelete: bin dir enum terminated with %x\n", ntStatus ));

            ZwClose( dirHandle );
            ExFreePool( dirInfo );
            return binSize;
        }

        //
        // Add up the sizes
        //
        curEntry = dirInfo;
  
        while( 1 ) {

            binSize += (ULONGLONG) curEntry->AllocationSize.QuadPart;

            //
            // End of list is a 0 for the next offset
            //
            if( curEntry->NextEntryOffset == 0 ) break;

            curEntry = (PFILE_DIRECTORY_INFORMATION) ((PCHAR) curEntry + curEntry->NextEntryOffset);

        } 

        firstQuery = FALSE;
    }

    //
    // Never get here
    //
    return 0;
}

//----------------------------------------------------------------------
//
// UndeleteGetAnsiExtension
//
// Given a name of a file embedded in an INFO file entry, this routine
// seeks out the file extension.
//
//----------------------------------------------------------------------
PCHAR UndeleteGetAnsiExtension( PCHAR FileName )
{
    int           i = 0;
    int           length = 0;
    PCHAR        extPointer;

    //
    // Go to the end of the file name
    //
    length = strlen( FileName );
    while( FileName[i++] ) length++;
    extPointer = &FileName[length-1];

    //
    // Seek back looking for the extension
    //
    for(i = 0; i < length - 1; i++ ) {

        if( *extPointer == L'\\' || *extPointer == L'.' ) break;
        extPointer--;
    }

    //
    // Return the pointer
    //
    return extPointer;
}


//----------------------------------------------------------------------
//
// UndeleteGetUnicodeExtension
//
// Given a name of a file embedded in an INFO file entry, this routine
// seeks out the file extension.
//
//----------------------------------------------------------------------
PWCHAR UndeleteGetUnicodeExtension( PWCHAR FileName )
{
    int           i = 0;
    int           length = 0;
    PWCHAR        extPointer;

    //
    // Go to the end of the file name
    //
    while( FileName[i++] ) length++;
    extPointer = &FileName[length-1];

    //
    // Seek back looking for the extension
    //
    for(i = 0; i < length - 1; i++ ) {

        if( *extPointer == L'\\' || *extPointer == L'.' ) break;
        extPointer--;
    }

    //
    // Return the pointer
    //
    return extPointer;
}

//----------------------------------------------------------------------
//
// UndeleteGetAsciiExtension
//
// Given a name of a file, seeks out the ascii extension.
//
//----------------------------------------------------------------------
VOID UndeleteGetAsciiExtension( PWCHAR FileName, PCHAR AsciiName, PCHAR Extension )
{
    int           i;
    int           length = 0;
    PWCHAR        extPointer;

    //
    // Determine the length of the name
    //
    length = wcslen( FileName );

    //
    // First, make the ascii name
    //
    for( i = 0; i < length; i++ ) {
      
        AsciiName[i] = (CHAR) FileName[i];
    }
    AsciiName[i] = 0;

    //
    // Go to the end of the file name
    //
    extPointer = &FileName[length-1];

    //
    // Seek back looking for the extension
    //
    for(i = 0; i < length - 1; i++ ) {

        if( *extPointer == L'\\' || *extPointer == L'.' ) break;
        extPointer--;
    }

    i = 0;
    if( *extPointer == L'.' ) {

        while( *extPointer ) {

            Extension[i++] = *(PCHAR) extPointer;
            extPointer++;
        }
    }
    Extension[i] = 0;
}


//----------------------------------------------------------------------
//
// UndeleteDeleteSpillage
//
// This routine makes sure that the Recycle Bin hasn't overflowed
// its limits. If it has, then we have to really delete things out
// of it.
//
//----------------------------------------------------------------------
VOID UndeleteDeleteSpillage( PDEVICE_OBJECT FileSystem, UCHAR Volume, 
                             PWCHAR RecycleBinPath,
                             PPURGE_INFORMATION PurgeInfo,
                             PINFO_HEADER InfoHeader,
                             PINFO_LIST *EntryList, PINFO_LIST *NewEntry )
{
    WCHAR                   volumeBuffer[] = L"\\DosDevices\\X:\\";
    UNICODE_STRING          volumeBufferUnicodeString;
    HANDLE 		    volumeHandle;   
    OBJECT_ATTRIBUTES       objectAttributes;
    IO_STATUS_BLOCK  	    ioStatus;
    FILE_FS_SIZE_INFORMATION fsSizeInformation;
    ULONGLONG               recycleMaxSize;
    ULONGLONG		    recycleCurrentSize;
    NTSTATUS                ntStatus;
    PWCHAR                  fileNameBuffer;
    PWCHAR                  extPointer;
    ANSI_STRING             ansiExtension;
    UNICODE_STRING          extString;
    UNICODE_STRING          fileName;
    PINFO_LIST              deleteEntry, entryList;

    //
    // Open the volume
    //
    RtlInitUnicodeString (&volumeBufferUnicodeString,
                          volumeBuffer );    
    volumeBufferUnicodeString.Buffer[12] = Volume;

    InitializeObjectAttributes( &objectAttributes, &volumeBufferUnicodeString, 
                                OBJ_CASE_INSENSITIVE, NULL, NULL );

    ntStatus = ZwCreateFile( &volumeHandle, FILE_ANY_ACCESS, 
                             &objectAttributes, &ioStatus, NULL, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, 
                             FILE_OPEN, 
                             FILE_SYNCHRONOUS_IO_NONALERT|FILE_OPEN_FOR_BACKUP_INTENT|FILE_DIRECTORY_FILE, 
                             NULL, 0 );
    if( !NT_SUCCESS( ntStatus ) ) {

        DbgPrint(("Undelete: Could not open drive %c: %x\n", Volume, ntStatus ));
        return;

    }
    
    //
    // Opened the volume. Now ask how much total space is on the drive
    //
    ntStatus = ZwQueryVolumeInformationFile( volumeHandle, &ioStatus, &fsSizeInformation,
                                             sizeof(fsSizeInformation), 
                                             FileFsSizeInformation );
    if( !NT_SUCCESS( ntStatus )) {

        DbgPrint(("Undelete: Could not get volume size: %x\n", ntStatus));
        return;
    }
    ZwClose( volumeHandle );

    //
    // Caclulate the max size of the bin
    //
    if( PurgeInfo->EnforceGlobalSetting ) {

        recycleMaxSize = ((ULONGLONG)fsSizeInformation.TotalAllocationUnits.QuadPart * 
                          (ULONGLONG)fsSizeInformation.BytesPerSector *
                          (ULONGLONG)fsSizeInformation.SectorsPerAllocationUnit *
                          (ULONGLONG)(PurgeInfo->ReservedForRecycle[GLOBAL_SETTING] & 0xFF)) / (ULONGLONG) 100;

    } else {

        recycleMaxSize = ((ULONGLONG)fsSizeInformation.TotalAllocationUnits.QuadPart * 
                          (ULONGLONG)fsSizeInformation.BytesPerSector *
                          (ULONGLONG)fsSizeInformation.SectorsPerAllocationUnit *
                          (ULONGLONG)(PurgeInfo->ReservedForRecycle[Volume - 'A'] & 0xFF)) 
            / (ULONGLONG) 100;
    }

    //
    // See how much space the Bin is currently taking up.
    //
    recycleCurrentSize = UndeleteGetRecycleBinSize( Volume, RecycleBinPath );
 
    DbgPrint(("%C Bin Max: %d current: %d\n", Volume, recycleMaxSize, recycleCurrentSize ));

    //
    // Allocate space for a filename
    //
    fileNameBuffer = (PWCHAR) ExAllocatePool( NonPagedPool, FILE_NAME_LENGTH );

    //
    // Okay, empty files out of the bin until we drop below the size limit
    //
    while( recycleCurrentSize > recycleMaxSize ) {
 
        DbgPrint(("Undelete: trimming a file (%d present)\n", InfoHeader->Entries ));

        //
        // If there are old entries, delete one
        //
        if( InfoHeader->Entries - 1 ) {

            deleteEntry = *EntryList;
            *EntryList = (*EntryList)->Next;

        } else {

            deleteEntry = *NewEntry;
            *NewEntry = NULL;
        }

        //
        // Make the filename. Here it depends on whether we're dealing with 
        // a unicode or ansi recycle bin. Ansi recycle bins are present if
        // the user left things in the bin in Win95 before dual booting into NT
        //
        if( InfoHeader->Version == UNICODE_RECYCLE_TYPE ) {

            extPointer = UndeleteGetUnicodeExtension( deleteEntry->UniEntry.UnicodeName );
            swprintf( fileNameBuffer, L"%s\\D%C%d%s",
                      RecycleBinPath, Volume, deleteEntry->UniEntry.FileID,
                      *extPointer == L'.' ? extPointer : L"" );
            RtlInitUnicodeString( &fileName, fileNameBuffer );

        } else {

            ansiExtension.Buffer = UndeleteGetAnsiExtension( deleteEntry->AnsiEntry.AsciiName );
            ansiExtension.MaximumLength = 2*strlen(ansiExtension.Buffer)+2;
            ansiExtension.Length = ansiExtension.MaximumLength;

            RtlAnsiStringToUnicodeString( &extString, &ansiExtension, TRUE );
            swprintf( fileNameBuffer, L"%s\\D%C%d%s",
                      RecycleBinPath, Volume, deleteEntry->AnsiEntry.FileID,
                      *extString.Buffer == L'.' ? extString.Buffer : L"" );
            RtlInitUnicodeString( &fileName, fileNameBuffer );
        }

        //
        // Delete the file, getting the size subtracted
        //
        recycleCurrentSize -= UndeleteDeleteFile( FileSystem, &fileName );

        //
        // Free the entry
        //
        ExFreePool( deleteEntry );

        //
        // Anything more to delete?
        //
        if( !(--InfoHeader->Entries) ) {

            break;
        }
    }

    //
    // Free the buffer
    //
    ExFreePool( fileNameBuffer );
}


//----------------------------------------------------------------------
//
// UndeleteFile
//
// This is the work routine for moving stuff into the recycle bin.
//
//----------------------------------------------------------------------
VOID UndeleteFile( PUNDELETE_COMPLETE_CONTEXT CompleteContext )
{
    PHOOK_EXTENSION         hookExt;
    NTSTATUS                ntStatus;
    OBJECT_ATTRIBUTES       objectAttributes;
    ULONG                   index;
    HANDLE                  hKey;
    ULONG                   length;
    WCHAR                   HKUName[] = L"\\Registry\\USER";
    UNICODE_STRING          sidString;
    ULONG                   requiredLength;
    PCHAR                   sidStringBuffer[512];
    UNICODE_STRING          HKUUnicodeString;
    UNICODE_STRING          binName;
    PTOKEN_USER             tokenInfoBuffer;
    HANDLE                  tokenHandle;
    PVOID                   token;
    PWCHAR                  binNameBuffer;
    HANDLE                  dirHandle;
    IO_STATUS_BLOCK  	    ioStatus;    
    PINFO_HEADER            infoHeader;
    PINFO_LIST              infoList;
    PINFO_LIST              newEntry;
    PINFO_LIST              nextEntry;
    PWCHAR                  newNameBuffer;
    LONG                    i;
    PWCHAR                  extPointer;

    //
    // Initialize buffer pointers
    //
    newNameBuffer = NULL;
    binNameBuffer = NULL;
    infoHeader    = NULL;

    //
    // Get the hook extension
    //
    hookExt = CompleteContext->DeviceObject->DeviceExtension;

    //
    // Allocate the name
    //
    binNameBuffer = ExAllocatePool( NonPagedPool, FILE_NAME_LENGTH );
    if( !binNameBuffer ) return;

    //
    // Construct the path to the recycle directory this file is going into.
    // We have to get user's SID if this is a delete on an NTFS volume
    //
    if( hookExt->IsNTFS ) {

        //
        // Make sure the top-level recycle directory exists
        //
        swprintf( binNameBuffer, L"\\??\\%C:\\NTRECYCLER", hookExt->LogicalDrive );
        RtlInitUnicodeString( &binName, binNameBuffer );
        InitializeObjectAttributes( &objectAttributes, &binName, 
                                    OBJ_CASE_INSENSITIVE, NULL, NULL );
        ntStatus = ZwCreateFile( &dirHandle, GENERIC_WRITE,
                                 &objectAttributes, &ioStatus, NULL, 
                                 FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM, 
                                 FILE_SHARE_READ|FILE_SHARE_WRITE,
                                 FILE_CREATE, FILE_DIRECTORY_FILE, NULL, 0 );
        if( !NT_SUCCESS( ntStatus )) {

            if( ntStatus != STATUS_OBJECT_NAME_COLLISION ) {

                //
                // Error that prevents us from backing up files
                //
                DbgPrint(("Undelete: Could not make RECYCLER subdirectory for volume %C\n", hookExt->LogicalDrive ));
                ExFreePool( binNameBuffer );
                return;
            } 

        } else {

            ZwClose( dirHandle );
        }

        //
        // Get a handle for the token
        //
        ntStatus = ObOpenObjectByPointer( CompleteContext->Token, 0, NULL,
                                          TOKEN_QUERY, NULL, KernelMode, &tokenHandle );
        ObDereferenceObject( CompleteContext->Token );
        if( !NT_SUCCESS( ntStatus )) {

            DbgPrint(("Undelete: Could not open process token: %x\n", ntStatus ));
            ExFreePool( binNameBuffer );
            return;
        }        
        
        //
        // Pull out the SID
        //
        ntStatus = NtQueryInformationToken( tokenHandle, TokenUser, NULL, 0, &requiredLength );
        if(  ntStatus != STATUS_BUFFER_TOO_SMALL ) {

            DbgPrint(("Undelete: Error getting token information: %x\n", ntStatus));
            ExFreePool( binNameBuffer );
            ZwClose( tokenHandle );
            return;
        }
        tokenInfoBuffer = (PTOKEN_USER) ExAllocatePool( NonPagedPool, requiredLength );
        if( tokenInfoBuffer ) {

            ntStatus = NtQueryInformationToken( tokenHandle, TokenUser, tokenInfoBuffer, requiredLength, &requiredLength );
        }
        if( !NT_SUCCESS( ntStatus ) || !tokenInfoBuffer ) {

            DbgPrint(("Undelete: Error getting token information: %x\n", ntStatus));
            if( tokenInfoBuffer ) ExFreePool( tokenInfoBuffer );
            ExFreePool( binNameBuffer );
            ZwClose( tokenHandle );
            return;
        }
        ZwClose( tokenHandle );

        //
        // Got it, now convert to text representation
        //
        memset( sidStringBuffer, 0, sizeof(sidStringBuffer ));
        sidString.Buffer = (PWCHAR) sidStringBuffer;
        sidString.MaximumLength = sizeof(sidStringBuffer);
        ntStatus = RtlConvertSidToUnicodeString( &sidString, tokenInfoBuffer->User.Sid, FALSE );
        ExFreePool( tokenInfoBuffer );
        if( !NT_SUCCESS( ntStatus )) {

            DbgPrint(("Undelete: Unable to convert SID to text: %x\n", ntStatus ));
            ExFreePool( binNameBuffer );
            return;
        }            

        //
        // We can make the name now
        //
        swprintf( binNameBuffer, L"\\??\\%C:\\NTRECYCLER\\%s", hookExt->LogicalDrive, 
                  sidString.Buffer );

        //
        // Make sure the SID sub-directory exists.
        // FIX: We need to capture the subject security context here 
        //
        RtlInitUnicodeString( &binName, binNameBuffer );
        InitializeObjectAttributes( &objectAttributes, &binName, 
                                    OBJ_CASE_INSENSITIVE, NULL, NULL );
        ntStatus = ZwCreateFile( &dirHandle, GENERIC_WRITE,
                                 &objectAttributes, &ioStatus, NULL, 
                                 FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM, 
                                 FILE_SHARE_READ|FILE_SHARE_WRITE,
                                 FILE_CREATE, FILE_DIRECTORY_FILE, NULL, 0 );
        if( !NT_SUCCESS( ntStatus )) {

            if( ntStatus != STATUS_OBJECT_NAME_COLLISION ) {

                //
                // Error that prevents us from backing up files
                //
                DbgPrint(("Undelete: Could not make SID subdirectory for volume %C\n", hookExt->LogicalDrive ));
                ExFreePool( binNameBuffer );
                return;
            }
        } else {

            ZwClose( dirHandle );
        }

    } else {
   
        //
        // Make sure the top-level recycle directory exists (its also the full path of the bin)
        //
        swprintf( binNameBuffer, L"\\??\\%C:\\NTRECYCLED", hookExt->LogicalDrive );
        RtlInitUnicodeString( &binName, binNameBuffer );
        InitializeObjectAttributes( &objectAttributes, &binName, 
                                    OBJ_CASE_INSENSITIVE, NULL, NULL );
        ntStatus = ZwCreateFile( &dirHandle, GENERIC_WRITE,
                                 &objectAttributes, &ioStatus, NULL, 
                                 FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM, 
                                 FILE_SHARE_READ|FILE_SHARE_WRITE,
                                 FILE_CREATE, FILE_DIRECTORY_FILE, NULL, 0 );
        if( !NT_SUCCESS( ntStatus )) {

            if( ntStatus != STATUS_OBJECT_NAME_COLLISION ) {

                //
                // Error that prevents us from backing up files
                //
                DbgPrint(("Undelete: Could not make RECYCLED subdirectory for volume %C\n", hookExt->LogicalDrive ));
                ExFreePool( binNameBuffer );
                return;
            }
        } else {

            ZwClose( dirHandle );
        }
    }

    DbgPrint(("Moving this guy to the recycle bin\n"));

    //
    // Create the new entry and get the INFO header so that we can see
    //  what ID this file is getting
    //
    newEntry = ExAllocatePool( PagedPool, sizeof( INFO_LIST ));
    infoHeader = (PINFO_HEADER) ExAllocatePool( NonPagedPool, sizeof( INFO_HEADER ));
    if( !newEntry || !infoHeader ) goto cleanup;

    memset( newEntry, 0, sizeof( INFO_LIST ));
    newEntry->UniEntry.FileID = UndeleteReadInfoFile( hookExt->IsAnsi, binNameBuffer, infoHeader, &infoList );
    if( newEntry->UniEntry.FileID == (ULONG) -1 ) {

        //
        // We couldn't read the header because of insufficient resources
        //
        goto cleanup;
    }

    newEntry->UniEntry.Drive  = hookExt->LogicalDrive - 'A';
    KeQuerySystemTime( &newEntry->UniEntry.TimeDeleted );
    DbgPrint(("   file id is: %d\n", newEntry->UniEntry.FileID ));

    //
    // Remember if the bin is ansi
    //
    hookExt->IsAnsi = (infoHeader->Version == ANSI_RECYCLE_TYPE);

    //
    // If this is simply a check to see what type of recycle bin we're dealing with
    // (which happens during initialization) return now.
    //
    if( !CompleteContext->Irp ) {

        DbgPrint(("%C: checked recycle bin and its type is %s\n",
                  hookExt->LogicalDrive, hookExt->IsAnsi ? "ANSI" : "UNICODE" ));
        while( infoList ) {

            nextEntry = infoList->Next;
            ExFreePool( infoList );
            infoList = nextEntry;
        }
        goto cleanup;
    }    

    //
    // Construct the new name for the file - we have to preserve the extension if the file
    // has one
    //
    newNameBuffer = ExAllocatePool( PagedPool, FILE_NAME_LENGTH );
    if( !newNameBuffer ) goto cleanup;

    extPointer = &CompleteContext->FileName.Buffer[CompleteContext->FileName.Length/2-1];
    for(i = 0; i < CompleteContext->FileName.Length/2; i++ ) {

        if( *extPointer == L'\\' || *extPointer == L'.' ) break;
        extPointer--;
    }
    swprintf( newNameBuffer, L"%s\\D%C%d%s", binNameBuffer, hookExt->LogicalDrive,
              newEntry->UniEntry.FileID, *extPointer == L'.'  ? extPointer : L"" );

    //
    // Stick the old in the new entry (except for the leading \??\) (note pointer arithmetic)
    // Only do Unicode for the unicode version
    //
    extPointer = CompleteContext->FileName.Buffer + 4;

    if( infoHeader->Version == UNICODE_RECYCLE_TYPE ) {

        wcscpy( newEntry->UniEntry.UnicodeName, extPointer );
    }

    i = 0;
    while( *extPointer ) {

        newEntry->UniEntry.AsciiName[i++] = (UCHAR) *extPointer;
        extPointer++;
    }

    //
    // Do the rename
    //
    UndeleteRenameFile( &CompleteContext->FileName, newNameBuffer );

    //
    // Delete stuff out of the Recycle Bin if necessary in order to 
    // limit its size to what the user specified.
    //
    UndeleteDeleteSpillage( hookExt->FileSystem, hookExt->LogicalDrive, 
                            binNameBuffer, 
                            &CompleteContext->PurgeInfo, infoHeader,
                            &infoList, &newEntry );
 

    //
    // Dump out the updated file. The entry list is freed in this routine.
    //
    UndeleteWriteInfoFile( binNameBuffer, infoHeader, infoList, newEntry );

    //	
    // Clean up
    //
cleanup:
    if( newNameBuffer ) ExFreePool( newNameBuffer );
    if( binNameBuffer ) ExFreePool( binNameBuffer );
    if( infoHeader )ExFreePool( infoHeader );
}



//----------------------------------------------------------------------
//
// UndeleteCheckFileForUndelete
//
// See if this file is marked for delete. If so, get its name 
// and see if this is a file that should be protected.
//
//----------------------------------------------------------------------
BOOLEAN UndeleteCheckFileForUndelete( PDEVICE_OBJECT DeviceObject, PIRP Irp,
                                      PUNDELETE_COMPLETE_CONTEXT CompleteContext )
{
    PIO_STACK_LOCATION      currentIrpStack = IoGetCurrentIrpStackLocation(Irp);    
    PHOOK_EXTENSION         hookExt;
    NTSTATUS                ntStatus;
    CHAR                    asciiExtension[256];
    RTL_QUERY_REGISTRY_TABLE QueryTable[2];
    OBJECT_ATTRIBUTES       objectAttributes;
    PURGE_INFORMATION	    purgeInfo;
    ULONG                   i;
    HANDLE                  hKey;
    PCHAR                   asciiName = NULL;
    PCHAR                   asciiPtr;
    PCHAR                   fileNameBuffer = NULL;
    UNICODE_STRING          fileNameUnicodeString;
    UNICODE_STRING          upcaseFileName;
    UNICODE_STRING          purgeInfoName;

    //
    // Initialize buffer pointers
    //
    fileNameBuffer               = NULL;
    fileNameUnicodeString.Buffer = NULL;
    upcaseFileName.Buffer        = NULL;
    asciiName                    = NULL;

    //
    // Get the hook extension
    //
    hookExt = DeviceObject->DeviceExtension;

    //
    // First, make sure its not a directory
    //
    if( UndeleteIsDirectory( hookExt->FileSystem, currentIrpStack->FileObject ) ) {
    
        return FALSE;
    }

    //
    // Has the application been uninstalled?
    //
    RtlInitUnicodeString( &purgeInfoName, 
                          L"\\Registry\\Machine\\SOFTWARE\\Systems Internals\\Fundelete" );
    InitializeObjectAttributes( &objectAttributes, &purgeInfoName,
                                OBJ_CASE_INSENSITIVE, NULL, NULL );
    if( !NT_SUCCESS( ZwOpenKey( &hKey, KEY_READ, &objectAttributes ))) {

        return FALSE;
    }
    ZwClose( hKey );

    //
    // Read the recycle bin configuration from the registry
    //
    *(ULONG *) &purgeInfo = sizeof(PURGE_INFORMATION);
    memset( QueryTable, 0, sizeof(RTL_QUERY_REGISTRY_TABLE) * 2);
    QueryTable[0].QueryRoutine = NULL;
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
    QueryTable[0].EntryContext = &purgeInfo;
    QueryTable[0].Name = L"PurgeInfo";
    QueryTable[0].DefaultType = REG_BINARY;
    QueryTable[0].DefaultData = &purgeInfo;
    QueryTable[0].DefaultLength = sizeof(PURGE_INFORMATION);
    ntStatus = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
                                       L"\\Registry\\Machine\\SOFTWARE\\Systems Internals\\Fundelete", 
                                       QueryTable, NULL, NULL );
    
    //
    // If we can't read the Purge info, it means that everything is undeleted
    //
    if( !NT_SUCCESS( ntStatus )) {

        purgeInfo.EnforceGlobalSetting = 1;
        purgeInfo.RecycleDisableMask = 0;
        purgeInfo.ReservedForRecycle[GLOBAL_SETTING] = 100; 
    }

    //
    // Are we doing undeletes for this volume?
    //
    if( (purgeInfo.EnforceGlobalSetting && (purgeInfo.RecycleDisableMask & (1 << GLOBAL_SETTING))) ||
        (!purgeInfo.EnforceGlobalSetting && (purgeInfo.RecycleDisableMask & (1 << (hookExt->LogicalDrive - 'A')) ))) {

        DbgPrint(("Undelete: volume %C is not being protected\n", hookExt->LogicalDrive ));
        return FALSE;
    }

    //
    // Get the name of the file. We have to make sure this isn't a recycle bin
    // file getting deleted (we don't want to undelete those!)
    //
    fileNameBuffer = ExAllocatePool( PagedPool, FILE_NAME_LENGTH );
    if( !fileNameBuffer ) goto cleanup;

    if( !UndeleteGetFileName( hookExt->IsNTFS, hookExt->FileSystem, currentIrpStack->FileObject,
                              fileNameBuffer, 1024)) {

        //
        // Couldn't get the name for some reason
        //
        goto cleanup;
    }

    //
    // Is it in the Recycle Bin already. We are conservative about this as we 
    // only check the first component of the name. If its "recycler" or "recycled"
    // we go ahead and assume that its the user (or Explorer) emptying something from the bin.
    // When we allocate the buffer, leave room for the '\??\X:' (6 wide characters equals 12 bytes)
    // and the terminating null.
    //
    fileNameUnicodeString.Length = (USHORT) ((PFILE_NAME_INFORMATION) fileNameBuffer)->FileNameLength + 12;
    fileNameUnicodeString.MaximumLength = fileNameUnicodeString.Length;
    fileNameUnicodeString.Buffer = ExAllocatePool( PagedPool, fileNameUnicodeString.Length+2+12);
    if( !fileNameUnicodeString.Buffer ) goto cleanup;

    swprintf( fileNameUnicodeString.Buffer, L"\\??\\%C:", hookExt->LogicalDrive );
    RtlCopyMemory( &fileNameUnicodeString.Buffer[6], ((PFILE_NAME_INFORMATION) fileNameBuffer)->FileName, 
                   fileNameUnicodeString.Length - 12 );
    fileNameUnicodeString.Buffer[fileNameUnicodeString.Length/2] = 0;

    upcaseFileName.Length = 0;
    upcaseFileName.MaximumLength = fileNameUnicodeString.MaximumLength;
    upcaseFileName.Buffer = ExAllocatePool( NonPagedPool, upcaseFileName.MaximumLength );
    if( !upcaseFileName.Buffer ) goto cleanup;

    RtlUpcaseUnicodeString( &upcaseFileName, &fileNameUnicodeString, FALSE );

    //
    // Do the comparison - remember to ignore the leading '\??\X:' (6 wide characters)
    //
    if( (hookExt->IsNTFS && !wcsncmp( &upcaseFileName.Buffer[6], L"\\NTRECYCLER", 9 )) ||
        ( !wcsncmp( &upcaseFileName.Buffer[6], L"\\NTRECYCLED", 9 ))) {

        //
        // Its in the recycler - don't do anything with it.
        //
        DbgPrint(("Undelete: its trash being emptied\n"));
        goto cleanup;
    }

    //
    // Convert the name to ascii
    //
    asciiName = ExAllocatePool( NonPagedPool, upcaseFileName.Length );
    if( !asciiName ) goto cleanup;

    UndeleteGetAsciiExtension( fileNameUnicodeString.Buffer, asciiName, asciiExtension );
    
    //
    // See if path is being filtered (ignore the leading '\??\' (
    //
    asciiPtr = &asciiName[4];
    for( i = 0; i < NumDirFilters; i++ ) {

        if( strlen( asciiPtr ) >= strlen( DirFilterArray[i]) ) {

            if( !_strnicmp( asciiPtr, DirFilterArray[i], strlen( DirFilterArray[i]) ) &&
                ( asciiPtr[ strlen(DirFilterArray[i])] == 0 || 
                  asciiPtr[ strlen(DirFilterArray[i])] == '\\')) {

                DbgPrint(("This directory (%s) is being filtered\n", DirFilterArray[i] ));
                goto cleanup;
            }
        }
    }

    //
    // Finally, see if its extension is being filtered. 
    //
    if( asciiExtension[0] ) {

        for( i = 0; i < NumFilters; i++ ) {

            if( !_stricmp( asciiExtension, FilterList[i].filter )) {

                //
                // We're not undeleting these files
                //
                DbgPrint(("This file (%s) extension is being filtered\n",
                          asciiExtension ));
                goto cleanup;
            }
        }  
    }

    //
    // Free buffers
    //
    ExFreePool( asciiName );
    ExFreePool( fileNameBuffer );
    ExFreePool( upcaseFileName.Buffer );

    //
    // Set up the completion context, where the rest of the undelete will take place
    //
    CompleteContext->FileName  = fileNameUnicodeString;
    CompleteContext->PurgeInfo = purgeInfo;
    return TRUE;

cleanup:
    //
    // Error path for insufficient resources
    //
    if( fileNameBuffer ) ExFreePool( fileNameBuffer );
    if( fileNameUnicodeString.Buffer ) ExFreePool( fileNameUnicodeString.Buffer );
    if( asciiName ) ExFreePool( asciiName );
    if( upcaseFileName.Buffer ) ExFreePool( upcaseFileName.Buffer );
    return FALSE;
}

//----------------------------------------------------------------------
// 
// Undeleteworkitem
//
// Called by the system, this is initiated in the completion callback.
// We use it to ensure we''re running at passive level when we do
// an undelete
//
//----------------------------------------------------------------------
VOID UndeleteWorkItem( PVOID Context )
{
    PHOOK_EXTENSION     hookExt;
    BOOLEAN             mutexGrabbed = FALSE;

    //
    // Grab our mutex
    //
    if( UndeleteMutex ) {

        KeWaitForSingleObject( UndeleteMutex, Executive, KernelMode,
                               FALSE, NULL );
        mutexGrabbed = TRUE;
        DbgPrint(("Got mutex\n"));
    }

    //
    // Call the undelete routine
    //
    UndeleteFile( (PUNDELETE_COMPLETE_CONTEXT) Context );

    //
    // Release Mutex
    //
    if( mutexGrabbed ) {

        DbgPrint(("Releasing mutex\n"));        
        KeReleaseMutex( UndeleteMutex, FALSE );
    }

    //
    // Complete the IRP
    //
    ((PUNDELETE_COMPLETE_CONTEXT) Context)->Irp->IoStatus.Status = STATUS_SUCCESS;

    IoCompleteRequest( ((PUNDELETE_COMPLETE_CONTEXT) Context)->Irp, 
                       IO_NO_INCREMENT );

    //
    // Free memory
    //
    if( ((PUNDELETE_COMPLETE_CONTEXT) Context)->FileName.Buffer ) {

        ExFreePool( ((PUNDELETE_COMPLETE_CONTEXT) Context)->FileName.Buffer );
    }
    ExFreePool( Context );
}

//----------------------------------------------------------------------
//     D I S P A T C H   A N D   H O O K   E N T R Y   P O I N T S
//----------------------------------------------------------------------


//----------------------------------------------------------------------
// 
// UndeleteHookDone
//
// Gets control on the way back from a delete operation. At this point
// the file cannot be opened by anything else, so we can move it into
// the recycle bin.
//
//----------------------------------------------------------------------
NTSTATUS UndeleteHookDone( IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp,
                           IN PVOID Context )
{
    if( NT_SUCCESS(Irp->IoStatus.Status)) {

        ExInitializeWorkItem( &((PUNDELETE_COMPLETE_CONTEXT) Context)->WorkItem, 
                              UndeleteWorkItem, Context );
        ExQueueWorkItem( &((PUNDELETE_COMPLETE_CONTEXT) Context)->WorkItem, 
                         CriticalWorkQueue );

        //
        // We have to complete the IRP later
        //
        return STATUS_MORE_PROCESSING_REQUIRED;

    } else {

        //
        // Now we have to mark Irp as pending if necessary
        //
        if( Irp->PendingReturned ) {

            IoMarkIrpPending( Irp );
        }

        // 
        // Free the completion context
        //
        if( ((PUNDELETE_COMPLETE_CONTEXT) Context)->FileName.Buffer ) {

            ExFreePool( ((PUNDELETE_COMPLETE_CONTEXT) Context)->FileName.Buffer );
        }
        ExFreePool( Context );
        return Irp->IoStatus.Status;
    }
}


//----------------------------------------------------------------------
//
// UndeleteHookRoutine
//
// This routine is the main hook routine where we figure out what
// calls are being sent to the file system.
//
//----------------------------------------------------------------------
NTSTATUS UndeleteHookRoutine( PDEVICE_OBJECT HookDevice, IN PIRP Irp )
{
    PIO_STACK_LOCATION  currentIrpStack = IoGetCurrentIrpStackLocation(Irp);
    PIO_STACK_LOCATION  nextIrpStack    = IoGetNextIrpStackLocation(Irp);
    PFILE_OBJECT        fileObject;
    PHOOK_EXTENSION     hookExt;
    PUNDELETE_COMPLETE_CONTEXT completeContext;

    //
    // Extract the file object from the IRP
    //
    fileObject    = currentIrpStack->FileObject;

    //
    // Point at the device extension, which contains information on which
    // file system this IRP is headed for
    //
    hookExt = HookDevice->DeviceExtension;

    //
    // Copy parameters down to next level in the stack for the driver below us
    //
    *nextIrpStack = *currentIrpStack;

    //
    // Determine what function we're dealing with
    //
    switch( currentIrpStack->MajorFunction ) {

    case IRP_MJ_CLEANUP:

        //
        // If the file will be deleted, see if we have to undelete it
        //
        completeContext = ExAllocatePool( NonPagedPool, sizeof( UNDELETE_COMPLETE_CONTEXT));
        if( completeContext ) completeContext->FileName.Buffer = NULL;

        if( completeContext &&
            fileObject->DeletePending && (SystemProcess != PsGetCurrentProcess()) &&
            UndeleteCheckFileForUndelete( HookDevice, Irp, completeContext )) {

            //
            // Undelete the file
            //
            UndeleteSetDispositionFile( hookExt->FileSystem, currentIrpStack->FileObject,
                                        FALSE );

            //
            // Finish setting up the completion context. We reference the process' primary
            // token because we don't support deletes from the network, which is typically
            // the only time that the impersonation context instead of the primary context
            // would reflect the user account actually deleting a file.
            //
            completeContext->DeviceObject = HookDevice;
            completeContext->Irp = Irp;
            completeContext->Token = PsReferencePrimaryToken(PsGetCurrentProcess());
            if( !completeContext->Token ) {

                //
                // Couldn't reference the token(?)
                //
                DbgPrint(("Undelete: Could not reference the process token.\n"));
                ExFreePool( completeContext );
                
            } else {

                //
                // Set the completion routine where we finish the job
                //
                DbgPrint(("Undelete: going to move this to the recycle bin\n"));

                IoSetCompletionRoutine( Irp, UndeleteHookDone, (PVOID) completeContext, TRUE, TRUE, TRUE );  
                
                IoMarkIrpPending( Irp );
                IoCallDriver( hookExt->FileSystem, Irp );

                //
                // Return this because we're going to stall the IRP in the 
                // completion routine
                //
                return STATUS_PENDING;
            }

        } else if( completeContext ) {
 
            ExFreePool( completeContext );
        }
        break;

    default:

        //
        // Register a never-called completion routine just so that the IRP stack
        // doesn't have a bogus completion routine in it.
        //
        IoSetCompletionRoutine( Irp, UndeleteHookDone, 0, FALSE, FALSE, FALSE );
        break;
    }

    //
    // Return the results of the call to the caller
    //
    return IoCallDriver( hookExt->FileSystem, Irp );
}

