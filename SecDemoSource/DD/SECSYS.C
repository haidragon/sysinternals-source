//======================================================================
// 
// Security Fix Demo
//
// Copyright (C) 1998 Mark Russinovich 
//
// This demo driver works in conjunction with the secdemo GUI. It
// demonstrates how device objects are by-default assigned world r/w
// access, and then removes world r/w to close a potential security
// hole.
//
//======================================================================
#include "ntddk.h"
#include "stdarg.h"
#include "stdio.h"
#include "ioctlcmd.h"
#include "Secsys.h"

//======================================================================
//			     D E F I N E S
//======================================================================-
// print macro that only turns on when debugging is on
#if DBG
#define DbgPrint(arg) DbgPrint arg
#else
#define DbgPrint(arg) 
#endif


//======================================================================
//			    E X T E R N S 
//======================================================================

//======================================================================
//			    F O R W A R D S  
//======================================================================
NTSTATUS SecsysDispatch( IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp );
VOID	 SecsysUnload( IN PDRIVER_OBJECT DriverObject );

//======================================================================
//			     G L O B A L S 
//======================================================================

//======================================================================
//			  F U N C T I O N S  
//=====================================================================

//----------------------------------------------------------------------
//
// NTIDeleteAce
//
// The NT Kernel does not implement a DeleteAce function so we
// have to create our own.
//
//----------------------------------------------------------------------
BOOLEAN NTIDeleteAce( PACL Acl, USHORT AceIndex )
{
  PACE_HEADER     aceHeader;
  WORD            aceSize;
  USHORT          i;

  //
  // Sanity check: does the ACL contain at least AceIndex ACEs?
  //
  if( AceIndex > Acl->AceCount ) return FALSE;

  aceHeader = (PACE_HEADER) ((PUCHAR) Acl + sizeof(ACL));
  for( i = 0; i < AceIndex; i++ ) {
     
     aceHeader = (PACE_HEADER) ((PUCHAR) aceHeader + aceHeader->AceSize);
  }

  //
  // Now subtract the ace size from the header, and shift
  // all aces to the right of this one over to fill the hole
  //
  aceSize = aceHeader->AceSize;

  RtlMoveMemory( aceHeader, ((PUCHAR) aceHeader + aceSize), 
		 Acl->AclSize - (((PUCHAR) aceHeader - (PUCHAR) Acl) + aceSize));
  Acl->AclSize -= aceSize;
  Acl->AceCount--;
  return TRUE;
}


//----------------------------------------------------------------------
//
// NTIGetAce
//
// The NT Kernel does not implement a GetAce function so we
// have to create our own.
//
//----------------------------------------------------------------------
PACE_HEADER NTIGetAce( PACL Acl, USHORT AceIndex )
{
  PACE_HEADER     aceHeader;
  WORD            aceSize;
  USHORT          i;

  //
  // Sanity check: does the ACL contain at least AceIndex ACEs?
  //
  if( AceIndex > Acl->AceCount ) return NULL;

  aceHeader = (PACE_HEADER) ((PUCHAR) Acl + sizeof(ACL));
  for( i = 0; i < AceIndex; i++ ) {
     
     aceHeader = (PACE_HEADER) ((PUCHAR) aceHeader + aceHeader->AceSize);
  }

  return aceHeader;
}


//----------------------------------------------------------------------
//
// NTIMakeAbsoluteSD
//
// Takes a self-relative security descriptor and returns an allocated
// absolute version. Caller is responsibile for freeing the allocated
// buffer on success.
// 
//----------------------------------------------------------------------
NTSTATUS NTIMakeAbsoluteSD( PSECURITY_DESCRIPTOR RelSecurityDescriptor, 
			    PSECURITY_DESCRIPTOR *pAbsSecurityDescriptor )
{
   NTSTATUS		status;
   BOOLEAN		DaclPresent, DaclDefaulted, OwnerDefaulted, GroupDefaulted;
   PACL			Dacl;
   PSID			Owner, Group;
   PSECURITY_DESCRIPTOR	 absSecurityDescriptor;

   //
   // Initialize buffer pointers
   //
   absSecurityDescriptor = (PSECURITY_DESCRIPTOR) ExAllocatePool( NonPagedPool, 1024 );
   *pAbsSecurityDescriptor = absSecurityDescriptor;

   //
   // Create an absolute-form security descriptor for manipulation.
   // The one on the security descriptor is in self-relative form.
   //
   status = RtlCreateSecurityDescriptor( absSecurityDescriptor,
					 SECURITY_DESCRIPTOR_REVISION );
   if( !NT_SUCCESS( status ) ) {

	  DbgPrint(("Secsys: Unable to initialize security descriptor\n"));
	  goto cleanup;
   }

   //
   // Locate the descriptor's DACL and apply the DACL to the new 
   // descriptor we're going to modify
   //
   status = RtlGetDaclSecurityDescriptor( RelSecurityDescriptor,
					  &DaclPresent,
					  &Dacl,
					  &DaclDefaulted );
   if( !NT_SUCCESS( status ) || !DaclPresent ) {

     if( !NT_SUCCESS( status )) {

       DbgPrint(("Secsys: Error obtaining security descriptor's DACL: %x\n", status ));

     } else {

       DbgPrint(("Secsys: Security descriptor does not have a DACL\n" ));
     }

     goto cleanup;
   }

   status = RtlSetDaclSecurityDescriptor( absSecurityDescriptor,
					  DaclPresent,
					  Dacl,
					  DaclDefaulted );
   if( !NT_SUCCESS( status )) {

     DbgPrint(("Secsys: Coult not set new security descriptor DACL: %x\n", status ));
     goto cleanup;
   }

   //
   // We would get and apply the SACL at this point, but NT does not export 
   // the appropriate function, RtlGetSaclSecurityDescriptor :-(
   //

   // 
   // Get and apply the owner
   //
   status = RtlGetOwnerSecurityDescriptor( RelSecurityDescriptor,
					   &Owner,
					   &OwnerDefaulted );
   if( !NT_SUCCESS( status )) {

     DbgPrint(("Secsys: Could not security descriptor owner: %x\n", Owner ));
     goto cleanup;
   }

   status = RtlSetOwnerSecurityDescriptor( absSecurityDescriptor,
					   Owner,
					   OwnerDefaulted );

   if( !NT_SUCCESS( status )) {

     DbgPrint(("Secsys: Could not set owner: %x\n", status ));
     goto cleanup;
   }

   //
   // Get and apply group
   //
   status = RtlGetGroupSecurityDescriptor( RelSecurityDescriptor,
					   &Group,
					   &GroupDefaulted );
   if( !NT_SUCCESS( status )) {

     DbgPrint(("Secsys: Could not security descriptor group: %x\n", Owner ));
     goto cleanup;
   }

   status = RtlSetGroupSecurityDescriptor( absSecurityDescriptor,
					   Group,
					   GroupDefaulted );

   if( !NT_SUCCESS( status )) {

     DbgPrint(("Secsys: Could not set group: %x\n", status ));
     goto cleanup;
   }

   //
   // Finally, make sure that what we made is valid
   //
   if( !RtlValidSecurityDescriptor( absSecurityDescriptor )) {

     DbgPrint(("Secsys: absolute descriptor not valid!\n"));
     status = STATUS_UNSUCCESSFUL;
   }

   //
   // Done! Return.
   //
   
  cleanup:

   if( !NT_SUCCESS( status ) ) {

     ExFreePool( absSecurityDescriptor );
   }
   return status;
}


//----------------------------------------------------------------------
//
// NTIRemoveWorldAce
// 
// Scans the passed security descriptor's DACL, looking for
// the World SID's ACE (its first because the of the way device object
// security descriptors are created) and removes it.
//
// If successful, the original security descriptor is deallocated
// and a new one is returned.
//
//----------------------------------------------------------------------
NTSTATUS NTIRemoveWorldAce( PSECURITY_DESCRIPTOR SecurityDescriptor,
			    PSECURITY_DESCRIPTOR *NewSecurityDescriptor )
{
   PSECURITY_DESCRIPTOR	     absSecurityDescriptor;
   PSECURITY_DESCRIPTOR	     relSecurityDescriptor;
   PACE_HEADER               aceHeader;
   NTSTATUS		     status;
   PACL			     Dacl;
   BOOLEAN		     DaclPresent, DaclDefaulted;
   USHORT                    aceIndex;
   ULONG                     worldSidLength;
   SID_IDENTIFIER_AUTHORITY  worldSidAuthority = SECURITY_WORLD_SID_AUTHORITY;
   PULONG                    worldSidSubAuthority;
   ULONG                     relLength;
   PSID                      worldSid;
   PSID                      aceSid;

   //
   // First, get an absolute version of the self-relative descriptor
   //
   relLength = RtlLengthSecurityDescriptor( SecurityDescriptor );
   status = NTIMakeAbsoluteSD( SecurityDescriptor, 
			       &absSecurityDescriptor );
   if( !NT_SUCCESS( status )) {
	  
     return status;
   }

   //
   // Pull the DACL out so that we can scan it
   //
   status = RtlGetDaclSecurityDescriptor( absSecurityDescriptor,
					  &DaclPresent,
					  &Dacl,
					  &DaclDefaulted );
   if( !NT_SUCCESS( status ) || !DaclPresent ) {

     DbgPrint(("Secsys: strange - couldn't get DACL from our absolute SD: %x\n", 
	       status ));
     ExFreePool( absSecurityDescriptor );
     return status;
   }

   //
   // Initialize a SID that identifies the world-authority so
   // that we can recognize it in the ACL
   //
   worldSidLength = RtlLengthRequiredSid( 1 );
   worldSid = (PSID) ExAllocatePool( PagedPool, worldSidLength );
   RtlInitializeSid( worldSid, &worldSidAuthority, 1 );
   worldSidSubAuthority = RtlSubAuthoritySid( worldSid, 0 );
   *worldSidSubAuthority = SECURITY_WORLD_RID;

   //
   // Now march through the ACEs looking for the World ace. We could 
   // do one of two things: 
   //
   //	- remove the ACE
   //	- convert it into a grant-nothing ACE
   //
   // For demonstration purposes I'll remove the ACE. In addition,
   // this requires that I implement kernel-mode GetAce and DeleteAce functions,
   // since they are not implemented by the NT kernel.
   //
   DbgPrint(("Secsys: %d ACEs in DACL\n", Dacl->AceCount ));

   for( aceIndex = 0; aceIndex < Dacl->AceCount; aceIndex++ ) {

     aceHeader = NTIGetAce( Dacl, aceIndex );
     
     DbgPrint(("  ACE: type: %s mask: %x\n", 
	       (aceHeader->AceType & ACCESS_DENIED_ACE_TYPE ? "Deny" : "Allow"),
	       *(PULONG) ((PUCHAR) aceHeader + sizeof(ACE_HEADER))));

     //
     // Get the SID in this ACE and see if its the WORLD (Everyone) SID
     //
     aceSid    = (PSID) ((PUCHAR) aceHeader + sizeof(ACE_HEADER) + sizeof(ULONG));
     if( RtlEqualSid( worldSid, aceSid )) {

       //
       // We found it: remove it.
       //
       DbgPrint(("Secsys: Deleting ace %d\n", aceIndex ));
       NTIDeleteAce( Dacl, aceIndex );
       break;
     }
   }

   // 
   // Write new DACL back to security descriptor
   //
   status = RtlSetDaclSecurityDescriptor( absSecurityDescriptor,
					  TRUE,
					  Dacl,
					  FALSE );
   if( !NT_SUCCESS( status )) {

      DbgPrint(("Secsys: Could not update SD Dacl: %x\n", status ));
      goto cleanup;
   }

   // 
   // Make sure its valid
   //
   if( !RtlValidSecurityDescriptor( absSecurityDescriptor )) {

      DbgPrint(("Secsys: SD after remove is invalid!\n"));
      status = STATUS_UNSUCCESSFUL;
      goto cleanup;
   }

   //
   // Now convert the security descriptor back to 
   // self-relative
   //
   relSecurityDescriptor = ExAllocatePool( PagedPool, relLength );
   status = RtlAbsoluteToSelfRelativeSD( absSecurityDescriptor,
					 relSecurityDescriptor, &relLength );
   if( !NT_SUCCESS( status )) {

     DbgPrint(("Could not convert absolute SD to relative: %x\n", status ));
   }

   //
   // Final step, free the original security descriptor and return the new one
   //
   ExFreePool( SecurityDescriptor );
   *NewSecurityDescriptor = relSecurityDescriptor;

cleanup:
   ExFreePool( worldSid );
   ExFreePool( absSecurityDescriptor );
   return status;
}

//======================================================================
//	   D E V I C E - D R I V E R  R O U T I N E S 
//======================================================================

//----------------------------------------------------------------------
//
// SecsysDeviceControl
//
//----------------------------------------------------------------------
NTSTATUS  SecsysDeviceControl( IN PFILE_OBJECT FileObject, IN BOOLEAN Wait,
		IN PVOID InputBuffer, IN ULONG InputBufferLength, 
		OUT PVOID OutputBuffer, IN ULONG OutputBufferLength, 
		IN ULONG IoControlCode, OUT PIO_STATUS_BLOCK IoStatus, 
		IN PDEVICE_OBJECT DeviceObject ) {

	IoStatus->Status      = STATUS_SUCCESS; // Assume success
	IoStatus->Information = 0;		// Assume nothing returned
	
	switch ( IoControlCode ) {

	   case SECDEMO_IOCTL:

	     //
	     // Fix up the device object's security descriptor by removing
	     // access to non-administrators. Don't check the return code because
	     // in the bizarre case this would fail, we end up with default security.
             //
             // Here I reach right into the device object to pull out and 
	     // replace its security descriptor. An alternate way to accomplish
             // the equivalent is to call NtQuerySecurityObject and NtSetSecurityObject.
             // Both these functions require a handle to the device object, however.
             //
	     IoStatus->Status = NTIRemoveWorldAce( DeviceObject->SecurityDescriptor, 
                                                   &DeviceObject->SecurityDescriptor );
	     return IoStatus->Status;

	   default: 

	     IoStatus->Information = 0;
	     IoStatus->Status = STATUS_NOT_SUPPORTED;
	     return IoStatus->Status;
	}
	return STATUS_SUCCESS;
}


//----------------------------------------------------------------------
//
// SecsysDispatch
//
// In this routine we Secsys requests to our own device. The only 
// requests we care about handling explicitely are IOCTL commands that
// we will get from the GUI. We also expect to get Create and Close 
// commands when the GUI opens and closes communications with us.
//
//----------------------------------------------------------------------
NTSTATUS SecsysDispatch( IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp )
{
	PIO_STACK_LOCATION	irpStack;
	PVOID			inputBuffer;
	PVOID			outputBuffer;
	ULONG			inputBufferLength;
	ULONG			outputBufferLength;
	ULONG			ioControlCode;
	NTSTATUS		status;

	// go ahead and set the request up as successful
	Irp->IoStatus.Status	  = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	// Get a pointer to the current location in the Irp. This is where
	//     the function codes and parameters are located.
	irpStack = IoGetCurrentIrpStackLocation (Irp);

	// Get the pointer to the input/output buffer and its length
	inputBuffer		= Irp->AssociatedIrp.SystemBuffer;
	inputBufferLength	= irpStack->Parameters.DeviceIoControl.InputBufferLength;
	outputBuffer		= Irp->AssociatedIrp.SystemBuffer;
	outputBufferLength	= irpStack->Parameters.DeviceIoControl.OutputBufferLength;
	ioControlCode		= irpStack->Parameters.DeviceIoControl.IoControlCode;

	switch (irpStack->MajorFunction) {

	case IRP_MJ_DEVICE_CONTROL:
		DbgPrint (("Secsys: IRP_MJ_DEVICE_CONTROL\n"));

		// Its a request from the GUI
		status = SecsysDeviceControl( irpStack->FileObject, TRUE,
			     inputBuffer, inputBufferLength, 
			     outputBuffer, outputBufferLength,
			     ioControlCode, &Irp->IoStatus, DeviceObject );
		break;
	}
	IoCompleteRequest( Irp, IO_NO_INCREMENT );
	return status;
}


//----------------------------------------------------------------------
//
// SecsysUnload
//
// Our job is done - time to leave.
//
//----------------------------------------------------------------------
VOID SecsysUnload( IN PDRIVER_OBJECT DriverObject )
{
	WCHAR			deviceLinkBuffer[]  = L"\\DosDevices\\Secsys";
	UNICODE_STRING		deviceLinkUnicodeString;

	DbgPrint(("Secsys.sys: unloading\n"));

	// delete the symbolic link for our device
	RtlInitUnicodeString( &deviceLinkUnicodeString, deviceLinkBuffer );
	IoDeleteSymbolicLink( &deviceLinkUnicodeString );

	// Delete the device object
	IoDeleteDevice( DriverObject->DeviceObject );

	DbgPrint(("Secsys.sys deleted devices\n"));
}


//----------------------------------------------------------------------
//
// DriverEntry
//
// Installable driver initialization. Here we just set ourselves up.
//
//----------------------------------------------------------------------
NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath )
{
    PDEVICE_OBJECT            GUIDevice;
    NTSTATUS		    ntStatus;
    WCHAR		    deviceNameBuffer[]	= L"\\Device\\Secsys";
    UNICODE_STRING	    deviceNameUnicodeString;
    WCHAR		    deviceLinkBuffer[]	= L"\\DosDevices\\Secsys";
    UNICODE_STRING	    deviceLinkUnicodeString;  

    DbgPrint (("Secsys.sys: entering DriverEntry\n"));

    //
    // setup our name
    //
    RtlInitUnicodeString (&deviceNameUnicodeString,
			  deviceNameBuffer );

    //
    // set up the device used for GUI communications
    ntStatus = IoCreateDevice ( DriverObject,
				0,
				&deviceNameUnicodeString,
				FILE_DEVICE_SECDEMO,
				0,
				FALSE,
				&GUIDevice );
    if (NT_SUCCESS(ntStatus)) {

	   //
	   // Create a symbolic link that the GUI can specify to gain access
	   // to this driver/device
	   //
	   RtlInitUnicodeString (&deviceLinkUnicodeString,
							 deviceLinkBuffer );
	   ntStatus = IoCreateSymbolicLink (&deviceLinkUnicodeString,
										&deviceNameUnicodeString );
	   if (!NT_SUCCESS(ntStatus))
		  DbgPrint (("Secsys.sys: IoCreateSymbolicLink failed\n"));

	   //
	   // Create dispatch points for all routines that must be Secsysd
	   //
	   DriverObject->MajorFunction[IRP_MJ_CREATE]	       =
	   DriverObject->MajorFunction[IRP_MJ_CLOSE]	      =
	   DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]  = SecsysDispatch;
	   DriverObject->DriverUnload			       = SecsysUnload;

    } else {
	 
	   DbgPrint(("Secsys: Failed to create our device!\n"));

	   //
	   // Something went wrong, so clean up (free resources etc)
	   //
	   if( GUIDevice ) IoDeleteDevice( GUIDevice );
	   return ntStatus;
    }

    return ntStatus;
}
    



