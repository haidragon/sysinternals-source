//======================================================================
// 
//  Secsys.h
//
//  Copyright (C) 1998 Mark Russinovich
//
//  Security-related definitions and APIs not included in NTDDK.H.
//  Note that only the definitions required for Secsys are included
//  here.
//
//======================================================================

// 
// Types for Win32 header definition conversion
//
typedef UCHAR           BYTE;
typedef ULONG           DWORD;
typedef USHORT          WORD;


//
// Security definitions needed for SecSys
//

typedef struct _ACE_HEADER {
    BYTE  AceType;
    BYTE  AceFlags;
    WORD   AceSize;
} ACE_HEADER;
typedef ACE_HEADER *PACE_HEADER;

//
//  The following are the predefined ace types that go into the AceType
//  field of an Ace header.
//

#define ACCESS_ALLOWED_ACE_TYPE          (0x0)
#define ACCESS_DENIED_ACE_TYPE           (0x1)
#define SYSTEM_AUDIT_ACE_TYPE            (0x2)
#define SYSTEM_ALARM_ACE_TYPE            (0x3)


typedef struct _SID_IDENTIFIER_AUTHORITY {
    BYTE  Value[6];
} SID_IDENTIFIER_AUTHORITY, *PSID_IDENTIFIER_AUTHORITY;

#define SECURITY_WORLD_SID_AUTHORITY      {0,0,0,0,0,1}
#define SECURITY_WORLD_RID                (0x00000000L)

#if 0 // in NTDDK.H

// Win32: InitializeSecurityDescriptor
NTSTATUS 
NTAPI 
RtlCreateSecurityDescriptor(
   PSECURITY_DESCRIPTOR SecurityDescriptor, 
   ULONG Revision 
   );

// Win32: IsValidSecurityDescriptor
BOOLEAN 
NTAPI 
RtlValidSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor 
    );

// Win32: GetSecurityDescriptorLength
ULONG 
NTAPI 
RtlLengthSecurityDescriptor(
   PSECURITY_DESCRIPTOR SecurityDescriptor 
   );

// Win32: SetSecurityDescriptorDacl
NTSTATUS 
NTAPI 
RtlSetDaclSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor, 
    BOOLEAN DaclPresent, 
    PACL Dacl, 
    BOOLEAN DaclDefaulted 
    );

#endif // in NTDDK.H

// Win32: GetSecurityDescriptorDacl
NTSTATUS 
NTAPI 
RtlGetDaclSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor, 
    PBOOLEAN DaclPresent, 
    PACL *Dacl, 
    PBOOLEAN DaclDefaulted 
    );

// Win32: SetSecurityDescriptorSacl
//
// NOTE: NT does not export RtlGetSaclSecurityDescriptor!
NTSTATUS 
NTAPI 
RtlSetSaclSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor, 
    BOOLEAN SaclPresent, 
    PACL Sacl, 
    BOOLEAN SaclDefaulted 
    );

// Win32: SetSecurityDescriptorOwner
NTSTATUS 
NTAPI 
RtlSetOwnerSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor, 
    PSID Owner, 
    BOOLEAN OwnerDefaulted 
    );

// Win32: GetSecurityDescriptorOwner
NTSTATUS 
NTAPI 
RtlGetOwnerSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor, 
    PSID *Owner, 
    PBOOLEAN OwnerDefaulted 
);

// Win32: SetSecurityDescriptorGroup
NTSTATUS 
NTAPI 
RtlSetGroupSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor, 
    PSID Group, 
    BOOLEAN GroupDefaulted 
    );

// Win32: GetSecurityDescriptorGroup
NTSTATUS 
NTAPI 
RtlGetGroupSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor, 
    PSID *Group, 
    PBOOLEAN GroupDefaulted 
);


// Win32: GetSidLengthRequired 
ULONG 
NTAPI 
RtlLengthRequiredSid(
     UCHAR SubAuthorityCount 
     );


// Win32: InitializeSid
NTSTATUS 
NTAPI 
RtlInitializeSid(
    PSID Sid, 
    PSID_IDENTIFIER_AUTHORITY pIdentifierAuthority, 
    UCHAR nSubAuthorityCount 
    );


// Win32: GetSidSubAuthority
PULONG 
NTAPI 
RtlSubAuthoritySid(
    PSID pSid, 
    ULONG nSubAuthority 
    );


// Win32: IsEqualSid
BOOLEAN 
NTAPI 
RtlEqualSid(
    PSID Sid1, 
    PSID Sid2 
    );


// Win32: MakeSelfRelativeSD
NTSTATUS 
NTAPI 
RtlAbsoluteToSelfRelativeSD(
    PSECURITY_DESCRIPTOR AbsoluteSecurityDescriptor, 
    PSECURITY_DESCRIPTOR SelfRelativeSecurityDescriptor, 
    PULONG BufferLength 
    );














