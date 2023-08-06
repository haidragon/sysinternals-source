//======================================================================
// 
// Irp.c
//
// Copyright (C) 1996-1998 Mark Russinovich and Bryce Cogswell
//
// Preserves files that are deleted by using the built-in Recycle-Bin
// functionality of NT 4.0. This file contains custom IRP routines.
//
//======================================================================
#include "ntddk.h"
#include "undelete.h"

//----------------------------------------------------------------------
//        N O N - I R P    R E L A T E D   F U N C T I O N S 
//----------------------------------------------------------------------

//----------------------------------------------------------------------
//
// UndeleteDeleteFile
//
// Deletes a file (from the recycle bin).
//
//----------------------------------------------------------------------
ULONGLONG UndeleteDeleteFile( PDEVICE_OBJECT FileSystem, PUNICODE_STRING FileName )
{
    NTSTATUS                ntStatus;
    OBJECT_ATTRIBUTES       objectAttributes;
    HANDLE                  fileHandle;
    IO_STATUS_BLOCK  	    ioStatus;    
    PFILE_OBJECT            fileObject;
    FILE_STANDARD_INFORMATION fsStandardInformation;
    FILE_DISPOSITION_INFORMATION fsDispositionInformation;

    //
    // Open the file
    //
    InitializeObjectAttributes( &objectAttributes, FileName,
				OBJ_CASE_INSENSITIVE, NULL, NULL );
    ntStatus = ZwCreateFile( &fileHandle, GENERIC_WRITE,
			     &objectAttributes, &ioStatus, NULL, 0, 0,
			     FILE_OPEN, FILE_NON_DIRECTORY_FILE, NULL, 0 );
    if( !NT_SUCCESS( ntStatus )) {

        //
        // Couldn't open it.
        //
        DbgPrint(("Undelete: could not open file to delete it: %x\n", ntStatus ));
        return 0;
    }

    //
    // Get the file's size
    //
    ntStatus = ZwQueryInformationFile( fileHandle, &ioStatus, &fsStandardInformation,
				       sizeof(FILE_STANDARD_INFORMATION), 
				       FileStandardInformation );
    if( !NT_SUCCESS( ntStatus )) {

        //
        // Couldn't get size
        //
        ZwClose( fileHandle );
        return 0;
    }

    //
    // Got the file handle, so now look-up the file-object it refers to
    //
    ntStatus = ObReferenceObjectByHandle( fileHandle, FILE_WRITE_DATA, 
                                          NULL, KernelMode, &fileObject, NULL );
    if( !NT_SUCCESS( ntStatus )) {

        DbgPrint(("Undelete: Could not get fileobject from handle: %x\n", ntStatus ));
        ZwClose( fileHandle );
        return 0;
    }

    //
    // Delete the file 
    //
    UndeleteSetDispositionFile( FileSystem, fileObject, TRUE );

    //
    // Clean up
    //
    ObDereferenceObject( fileObject );
    ZwClose( fileHandle );

    //
    // Return the amount we deleted (0 if the delete failed)
    //
    if( NT_SUCCESS( ntStatus ) ) {

        return (ULONGLONG) fsStandardInformation.AllocationSize.QuadPart;

    } else {

        return 0;
    }
}


//----------------------------------------------------------------------
//
// UndeleteRenameFile
//
// Renames a file.
//
//----------------------------------------------------------------------
BOOLEAN UndeleteRenameFile( PUNICODE_STRING OldName, PWCHAR NewName )
{
    NTSTATUS                ntStatus;
    OBJECT_ATTRIBUTES       objectAttributes;
    HANDLE                  fileHandle;
    IO_STATUS_BLOCK  	    ioStatus;    
    PCHAR                   renameInformationBuffer;
    ULONG                   newNameLength;
    PFILE_RENAME_INFORMATION renameInformation;

    //
    // Open the file
    //
    InitializeObjectAttributes( &objectAttributes, OldName,
				OBJ_CASE_INSENSITIVE, NULL, NULL );
    ntStatus = ZwCreateFile( &fileHandle, GENERIC_WRITE,
			     &objectAttributes, &ioStatus, NULL, 0, 0,
			     FILE_OPEN, FILE_NON_DIRECTORY_FILE, NULL, 0 );
    if( !NT_SUCCESS( ntStatus )) {

        //
        // Couldn't open it.
        //
        DbgPrint(("Undelete: could not open file to rename it: %x\n", ntStatus ));
        return FALSE;
    }

    //
    // Rename it
    //
    newNameLength = wcslen( NewName ) * 2;
    renameInformationBuffer = ExAllocatePool( PagedPool, sizeof( FILE_RENAME_INFORMATION) + newNameLength);
    if( !renameInformationBuffer ) return FALSE;

    renameInformation = (PFILE_RENAME_INFORMATION) renameInformationBuffer;
    renameInformation->ReplaceIfExists = FALSE;
    renameInformation->RootDirectory = 0;
    renameInformation->FileNameLength = newNameLength;
    wcscpy( renameInformation->FileName, NewName );
    ntStatus = ZwSetInformationFile( fileHandle, &ioStatus, renameInformation, 
                                     sizeof( FILE_RENAME_INFORMATION) + newNameLength,
                                     FileRenameInformation );
    if( !NT_SUCCESS(ntStatus )) {

        DbgPrint(("Undelete: could not rename file: %x\n", ntStatus ));
    }

    //
    // Close it
    //
    ZwClose( fileHandle );
    ExFreePool( renameInformationBuffer );
    return NT_SUCCESS( ntStatus );
}

//----------------------------------------------------------------------
//             I R P    R E L A T E D   F U N C T I O N S 
//----------------------------------------------------------------------

//----------------------------------------------------------------------
//
// UndeleteIoComplete
//
// This routine is used to handle I/O (read OR write) completion.
//
//----------------------------------------------------------------------
static NTSTATUS UndeleteIoComplete(PDEVICE_OBJECT DeviceObject,
				    PIRP Irp,
				    PVOID Context)
{
    //
    // Copy the status information back into the "user" IOSB.
    //
    *Irp->UserIosb = Irp->IoStatus;

    if( !NT_SUCCESS(Irp->IoStatus.Status) ) {

        DbgPrint(("   ERROR ON IRP: %x\n", Irp->IoStatus.Status ));
    }
    
    //
    // Set the user event - wakes up the mainline code doing this.
    //
    KeSetEvent(Irp->UserEvent, 0, FALSE);
    
    //
    // Free the IRP now that we are done with it.
    //
    IoFreeIrp(Irp);
    
    //
    // We return STATUS_MORE_PROCESSING_REQUIRED because this "magic" return value
    // tells the I/O Manager that additional processing will be done by this driver
    // to the IRP - in fact, it might (as it is in this case) already BE done - and
    // the IRP cannot be completed.
    //
    return STATUS_MORE_PROCESSING_REQUIRED;
}

//----------------------------------------------------------------------
//
// UndeleteIsDirectory
//
// Returns TRUE if its a directory or we can't tell, FALSE if its a file.
//
//----------------------------------------------------------------------
BOOLEAN UndeleteIsDirectory(PDEVICE_OBJECT DeviceObject, 
                            PFILE_OBJECT FileObject )
{
    PIRP irp;
    KEVENT event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION ioStackLocation;
    FILE_STANDARD_INFORMATION fileInfo;

    //
    // First, start by initializing the event
    //
    KeInitializeEvent(&event, SynchronizationEvent, FALSE);

    //
    // Allocate an irp for this request.  This could also come from a 
    // private pool, for instance.
    //
    irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);

    if (!irp) {

        //
        // Failure!
        //
        return FALSE;
    }

    irp->AssociatedIrp.SystemBuffer = &fileInfo;

    irp->UserEvent = &event;

    irp->UserIosb = &IoStatusBlock;

    irp->Tail.Overlay.Thread = PsGetCurrentThread();

    irp->Tail.Overlay.OriginalFileObject = FileObject;

    irp->RequestorMode = KernelMode;
  
    irp->Flags = 0;

    //
    // Set up the I/O stack location.
    //
    ioStackLocation = IoGetNextIrpStackLocation(irp);

    ioStackLocation->MajorFunction = IRP_MJ_QUERY_INFORMATION;

    ioStackLocation->DeviceObject = DeviceObject;

    ioStackLocation->FileObject = FileObject;

    ioStackLocation->Parameters.QueryVolume.Length = sizeof(FILE_STANDARD_INFORMATION);
    
    ioStackLocation->Parameters.QueryVolume.FsInformationClass = FileStandardInformation;

    //
    // Set the completion routine.
    //
    IoSetCompletionRoutine(irp, UndeleteIoComplete, 0, TRUE, TRUE, TRUE);

    //
    // Send the request to the lower layer driver.
    //
    (void) IoCallDriver(DeviceObject, irp);

    //
    // Wait for the I/O
    //
    KeWaitForSingleObject(&event, Executive, KernelMode, TRUE, 0);

    //
    // Return whether its a directory or not
    //
    if( !NT_SUCCESS( IoStatusBlock.Status ) || fileInfo.Directory) {

        return TRUE;

    } else {

        return FALSE; 
    }
}


//----------------------------------------------------------------------
//
// UndeleteGetFileName
//
// This function retrieves the "standard" information for the
// underlying file system, asking for the filename in particular.
//
//----------------------------------------------------------------------
BOOLEAN UndeleteGetFileName(BOOLEAN IsNTFS,
                            PDEVICE_OBJECT DeviceObject, 
                            PFILE_OBJECT FileObject,
                            PUCHAR FileName, ULONG FileNameLength )
{
    PIRP irp;
    KEVENT event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION ioStackLocation;
    PVOID fsContext2;

    DbgPrint(("Getting file name for %x\n", FileObject));

    //
    // Initialize the event
    //
    KeInitializeEvent(&event, SynchronizationEvent, FALSE);

    //
    // Allocate an irp for this request.  This could also come from a 
    // private pool, for instance.
    //
    irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);

    if (!irp) {

        //
        // Failure!
        //
        return FALSE;
    }
  
    //
    // Zap Fscontext2 (the CCB) so that NTFS will give us the long name
    //
    if( IsNTFS ) {

        fsContext2 = FileObject->FsContext2;
  
        FileObject->FsContext2 = NULL;
    }

    irp->AssociatedIrp.SystemBuffer = FileName;

    irp->UserEvent = &event;

    irp->UserIosb = &IoStatusBlock;

    irp->Tail.Overlay.Thread = PsGetCurrentThread();

    irp->Tail.Overlay.OriginalFileObject = FileObject;

    irp->RequestorMode = KernelMode;
  
    irp->Flags = 0;

    //
    // Set up the I/O stack location.
    //
    ioStackLocation = IoGetNextIrpStackLocation(irp);

    ioStackLocation->MajorFunction = IRP_MJ_QUERY_INFORMATION;

    ioStackLocation->DeviceObject = DeviceObject;

    ioStackLocation->FileObject = FileObject;

    ioStackLocation->Parameters.QueryFile.Length = FileNameLength;
    
    ioStackLocation->Parameters.QueryFile.FileInformationClass = FileNameInformation;

    //
    // Set the completion routine.
    //
    IoSetCompletionRoutine(irp, UndeleteIoComplete, 0, TRUE, TRUE, TRUE);

    //
    // Send it to the FSD
    //
    (void) IoCallDriver(DeviceObject, irp);

    //
    // Wait for the I/O
    //
    KeWaitForSingleObject(&event, Executive, KernelMode, TRUE, 0);

    //
    // Restore the fscontext
    //
    if( IsNTFS ) {

        FileObject->FsContext2 = fsContext2;
    }

    //
    // Done!
    //
    return NT_SUCCESS( IoStatusBlock.Status );
}


//----------------------------------------------------------------------
//
// UndeleteSetDispositionFile
//
// Changes the delete status on a file
//
//----------------------------------------------------------------------
BOOLEAN UndeleteSetDispositionFile(PDEVICE_OBJECT DeviceObject, 
			           PFILE_OBJECT FileObject, BOOLEAN Delete )
{
    PIRP irp;
    KEVENT event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION ioStackLocation;
    FILE_DISPOSITION_INFORMATION disposition;

    //
    // Change the delete status
    //
    disposition.DeleteFile = Delete;

    //
    // Initialize the event
    //
    KeInitializeEvent(&event, SynchronizationEvent, FALSE);

    //
    // Allocate an irp for this request.  This could also come from a 
    // private pool, for instance.
    //
    irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);

    if (!irp) {

        //
        // Failure!
        //
        return FALSE;
    }

    irp->AssociatedIrp.SystemBuffer = &disposition;

    irp->UserEvent = &event;

    irp->UserIosb = &IoStatusBlock;

    irp->Tail.Overlay.Thread = PsGetCurrentThread();

    irp->Tail.Overlay.OriginalFileObject = FileObject;

    irp->RequestorMode = KernelMode;
  
    irp->Flags = 0;

    //
    // Set up the I/O stack location.
    //
    ioStackLocation = IoGetNextIrpStackLocation(irp);

    ioStackLocation->MajorFunction = IRP_MJ_SET_INFORMATION;

    ioStackLocation->DeviceObject = DeviceObject;

    ioStackLocation->FileObject = FileObject;

    ioStackLocation->Parameters.SetFile.FileInformationClass = FileDispositionInformation;

    //
    // Set the completion routine.
    //
    IoSetCompletionRoutine(irp, UndeleteIoComplete, 0, TRUE, TRUE, TRUE);

    //
    // Send it to the FSD
    //
    (void) IoCallDriver(DeviceObject, irp);

    //
    // Wait for the I/O
    //
    KeWaitForSingleObject(&event, Executive, KernelMode, TRUE, 0);

    //
    // Done!
    //
    return TRUE;

}



