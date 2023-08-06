//----------------------------------------------------------------------
//
// NewSID
//
// Copyright (c) 1997-2002 Mark Russinovich and Bryce Cogswell
//
// Registry-related functions.
//
//----------------------------------------------------------------------
#include <windows.h>
#include <lm.h>
#include <commctrl.h>
#include <stdio.h>
#include <tchar.h>
#include <shlobj.h>
#include <process.h>
#include "resource.h"
#include "wizard.h"
#include "sid.h"


//===================================================================
//
// REGISTRY SECURITY CONVENIENCE ROUTNES 
//
//===================================================================


//----------------------------------------------------------------------
//
// GetRegAccess
//
// Returns the original security descriptor of a key.
//
//----------------------------------------------------------------------
PSECURITY_DESCRIPTOR GetRegAccess (HKEY hKey)
{
	DWORD		nb = 0;
	PSECURITY_DESCRIPTOR SecDesc;
	//
	// Get access
	//
	if (RegGetKeySecurity (hKey, DACL_SECURITY_INFORMATION, NULL, &nb) != ERROR_INSUFFICIENT_BUFFER)
		return NULL;
	SecDesc = (PSECURITY_DESCRIPTOR) malloc (nb);
	if (RegGetKeySecurity (hKey, DACL_SECURITY_INFORMATION, SecDesc, &nb) != ERROR_SUCCESS)
		free (SecDesc);
	return (SecDesc);
}


//----------------------------------------------------------------------
//
// SetRegAccess
//
// Sets the key with the specified security descriptor. If the key's
// name is passed (Rooot != NULL), then the key is re-opened to give
// the caller updated access rights that reflect the changed security.
//
//----------------------------------------------------------------------
LONG SetRegAccess (HKEY hKey, LPCTSTR lpSubKey,
					PSECURITY_DESCRIPTOR SecDesc, PHKEY phKey)
{
	//
	// Grant requested access
	//
	if (RegSetKeySecurity (*phKey, DACL_SECURITY_INFORMATION, SecDesc) 
		!= ERROR_SUCCESS) {

		return FALSE;
	}

	//
	// Re-open the key if requested
	//
	if (! hKey) {

		return TRUE;
	}
	RegCloseKey (*phKey);
	return (RegOpenKey (hKey, lpSubKey, phKey) == ERROR_SUCCESS);
}


//----------------------------------------------------------------------
//
// GetRegSecDesc
//
// Gets the security descriptor from the specified key.
//
//----------------------------------------------------------------------
PSECURITY_DESCRIPTOR GetRegSecDesc (HKEY Root, TCHAR *Path, 
									SECURITY_INFORMATION Information)
{
	HKEY					hKey;
	LONG					Status;
	DWORD					nb = 0;
	PSECURITY_DESCRIPTOR	SecDesc;

	//
	// Open the key with no access requests, since we don't need
	// any.
	// SECURITY_DESCRIPTOR
	if (RegOpenKeyEx (Root, Path, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {

		return NULL;
	}

	//
	// Grab a copy of the security for key
	//
	if (RegGetKeySecurity (hKey, Information, NULL, &nb) 
		!= ERROR_INSUFFICIENT_BUFFER) {

		return NULL;
	}
	SecDesc = malloc (nb);
	Status = RegGetKeySecurity (hKey, Information, SecDesc, &nb);

	//
	// Close the key anyway
	//
	RegCloseKey (hKey);

	if ((Status) != ERROR_SUCCESS) {

		free (SecDesc);
	}
	return SecDesc;
}




//===================================================================
//
// REGISTRY SECURITY DESCRIPTOR UPDATING 
//
//===================================================================

//----------------------------------------------------------------------
//
// CheckKeyOwnershipForSID
//
// Sees if the file ownership SID contains the old computer SID, and
// if so, replaces it.
//
//----------------------------------------------------------------------
void CheckKeyOwnershipForSID(HKEY hKey, PSECURITY_DESCRIPTOR psdRegSD )
{
	PSID		psidRegOwnerSID;
	PSID		psidRegGroupSID;
	BOOL		bOwnerDefaulted;

	//
	// Get the owner SID
	//
	if (!GetSecurityDescriptorOwner(psdRegSD,
		(PSID *)&psidRegOwnerSID, (LPBOOL)&bOwnerDefaulted) ||
		!psidRegOwnerSID) { 

		return;
	}

	//
	// If the old SID is in the owner, we have to write
	// the updated owner to the key
	//
    if( SecurityReplaceSID( psidRegOwnerSID ) ) {
		if (!RegSetKeySecurity(hKey,
				(SECURITY_INFORMATION)OWNER_SECURITY_INFORMATION,
				psdRegSD)){
			
			return;
		}
    }

	//
	// Now get the Group SID and do the same thing
	//
	if (!GetSecurityDescriptorGroup(psdRegSD,
		(PSID *)&psidRegGroupSID, (LPBOOL)&bOwnerDefaulted) ||
		!psidRegOwnerSID) { 

		return;
	}

	//
	// If the old SID is in the owner, we have to write
	// the updated owner to the key
	//
    if( SecurityReplaceSID( psidRegGroupSID ) ) {
		if (!RegSetKeySecurity(hKey,
				(SECURITY_INFORMATION)GROUP_SECURITY_INFORMATION,
				psdRegSD)){
			
			return;
		}
    }
	return;
}


//----------------------------------------------------------------------
//
// CheckKeyACLForSID
//
// Scan's the security descriptor's ACEs, looking for instances
// of the old computer SID.
//
//----------------------------------------------------------------------
void CheckKeyACLForSID( HKEY hKey, BOOLEAN Dacl, 
					   PSECURITY_DESCRIPTOR psdRegSD )
{
	PACL       paclReg;
	BOOL       bHasACL;
	BOOL       bOwnerDefaulted;
	DWORD      dwAcl_i;
	BOOLEAN    descriptorModified;
	DWORD      dwLastError   = NO_ERROR;
	ACL_SIZE_INFORMATION asiAclSize;
	DWORD      dwBufLength = sizeof(asiAclSize);
	ACCESS_ALLOWED_ACE   *paaAllowedAce;

	if( Dacl ) {
		if (!GetSecurityDescriptorDacl(psdRegSD,
							 (LPBOOL)&bHasACL, (PACL *)&paclReg,
							 (LPBOOL)&bOwnerDefaulted)) {
			return;
		}
	} else {
		if (!GetSecurityDescriptorSacl(psdRegSD,
							 (LPBOOL)&bHasACL, (PACL *)&paclReg,
							 (LPBOOL)&bOwnerDefaulted)) {
			return;
		}
	}

	//
	// If no ACL to process, so OK, return
	//
	if (!bHasACL || !paclReg )
		return;

	if (!GetAclInformation(paclReg, (LPVOID)&asiAclSize,
				 (DWORD)dwBufLength, (ACL_INFORMATION_CLASS)AclSizeInformation)){
		return;
	}

	// 
	// Look through the ACEs
	//
	descriptorModified = FALSE;
	for (dwAcl_i = asiAclSize.AceCount-1;  ((int)dwAcl_i) >= 0;  dwAcl_i--) {

		//
		// If we can't get an ACE, bail
		//
		if (!GetAce(paclReg, dwAcl_i, (LPVOID *)&paaAllowedAce)) {

			return;
		}

		//
		// Make sure we're dealing with an ACE we know
		//
		if (!( (paaAllowedAce->Header.AceType == ACCESS_ALLOWED_ACE_TYPE)
				||(paaAllowedAce->Header.AceType == ACCESS_DENIED_ACE_TYPE )
				||(paaAllowedAce->Header.AceType == SYSTEM_AUDIT_ACE_TYPE  )
				||(paaAllowedAce->Header.AceType == SYSTEM_ALARM_ACE_TYPE  ))) {
			continue;
		}

		//
		// Look at the SID's subauthorities to see if there's a match
		// with the old computer SID
		//
		descriptorModified |= SecurityReplaceSID((PSID)&(paaAllowedAce->SidStart)); 
	}

	//
	// If the security descriptor was modified because an
	// old computer SID was converted to the new one, write
	// the new descriptor to disk.
	//
	if( descriptorModified ) {

		//
		// Modify the SD on the hard disk
		//
		if( Dacl ) {

			//
			// Modify the SD in virtual memory
			//
			if (!SetSecurityDescriptorDacl(psdRegSD,
								TRUE, paclReg, FALSE)) {

				return;
			}

			if (!RegSetKeySecurity(hKey,
							(SECURITY_INFORMATION)DACL_SECURITY_INFORMATION,
							psdRegSD)){

				return;
			}
		} else {

			//
			// Modify the SD in virtual memory
			//
			if (!SetSecurityDescriptorSacl(psdRegSD,
								TRUE, paclReg, FALSE)) {

				return;
			}

			if (!RegSetKeySecurity(hKey,
							(SECURITY_INFORMATION)SACL_SECURITY_INFORMATION,
							psdRegSD)){

				return;
			}
		}
	}
}


//----------------------------------------------------------------------
//
// CheckKeySID
//
// Reads the key's security descriptor and converts it to absolute
// format. Then it calls a function to check the ownership SIDs and
// finally one to check the DACL SIDs, to see if they need to be
// updated.
//
//----------------------------------------------------------------------
BOOL CheckKeySID( HKEY hKey, HKEY Root, LPTSTR lpszFullName,
				 PSECURITY_DESCRIPTOR psdSrelRegSD )
{
	UCHAR       ucBufAbs[SZ_ABS_SD_BUF];
	UCHAR       ucBufCtrl[sizeof(PSECURITY_DESCRIPTOR_CONTROL)];
	DWORD       dwSDLength = SZ_REL_SD_BUF;
	PSECURITY_DESCRIPTOR psdAbsRegSD = (PSECURITY_DESCRIPTOR)&ucBufAbs;
	PSECURITY_DESCRIPTOR_CONTROL psdcCtrl = (PSECURITY_DESCRIPTOR_CONTROL)&ucBufCtrl;
	PACL        paclDacl;
	PACL        paclSacl;
	PSID        psidSidOwn;
	PSID        psidSidPG;
	BOOL        bDaclPresent;
	BOOL        bDaclDefaulted;
	BOOL        bSaclPresent;
	BOOL        bSaclDefaulted;
	BOOL        bOwnerDefaulted;
	BOOL        bGroupDefaulted;
	BOOL        bSDSelfRelative;
	DWORD       dwRevision;

	//
	// Build File SD in absolute format for potential later modification
	//
	if (!InitializeSecurityDescriptor(psdAbsRegSD,
		 SECURITY_DESCRIPTOR_REVISION)) { 
		return FALSE;
	}

	//
	// Get Control from relative format File SD
	//
	if (!GetSecurityDescriptorControl(psdSrelRegSD,
			psdcCtrl,
			&dwRevision)) { 
		return FALSE;
	} 
	bSDSelfRelative = (SE_SELF_RELATIVE & *psdcCtrl);

	//
	// Set DACL into absolute format File SD
	//
	if (bDaclPresent = (SE_DACL_PRESENT   & *psdcCtrl)) {

		bDaclDefaulted = (SE_DACL_DEFAULTED & *psdcCtrl);
	}

	if (!GetSecurityDescriptorDacl(psdSrelRegSD,
							&bDaclPresent,      // fDaclPresent flag
							&paclDacl,
							&bDaclDefaulted)) {

		return FALSE;
	}
	if (!SetSecurityDescriptorDacl(psdAbsRegSD,
							bDaclPresent,       // fDaclPresent flag
							paclDacl,
							bDaclDefaulted)) {

		return FALSE;
	}

	//  
	// Set SACL into absolute format File SD
	//
	if (bSaclPresent = (SE_SACL_PRESENT   & *psdcCtrl)){

		bSaclDefaulted = (SE_SACL_DEFAULTED & *psdcCtrl);
	}

	if (!GetSecurityDescriptorSacl(psdSrelRegSD,
						&bSaclPresent,      // fSaclPresent flag
						&paclSacl,
						&bSaclDefaulted)) {

		return FALSE;
	}
	if (!SetSecurityDescriptorSacl(psdAbsRegSD,
						bSaclPresent,       // fSaclPresent flag
						paclSacl,
						bSaclDefaulted)) {

		return FALSE;
	} 

	//
	// Set Owner into absolute format File SD
	//
	bOwnerDefaulted = (SE_OWNER_DEFAULTED & *psdcCtrl);
	if (!GetSecurityDescriptorOwner(psdSrelRegSD,
					&psidSidOwn,
					&bOwnerDefaulted)) {

		return FALSE;
	}
	if (!SetSecurityDescriptorOwner(psdAbsRegSD,
					psidSidOwn,
					bOwnerDefaulted)) {

		return FALSE;
	}

	//
	// Set Group into absolute format File SD
	//
	bGroupDefaulted = (SE_GROUP_DEFAULTED & *psdcCtrl);
	if (!GetSecurityDescriptorGroup(psdSrelRegSD,
					&psidSidPG,
					&bGroupDefaulted)) {

		return FALSE;
	}
	if (!SetSecurityDescriptorGroup(psdAbsRegSD,
					psidSidPG,
					bGroupDefaulted)) {

		return FALSE;
	}

	//
	// Now we can see if the old computer SID is embedded
	// in the security descriptor, and change it if necessary
	//
	CheckKeyOwnershipForSID(hKey, psdAbsRegSD );
	
	//
	// Check to see if SID is embedded in the DACL and then
	// SACL
	//
	CheckKeyACLForSID( hKey, TRUE, psdAbsRegSD );
	CheckKeyACLForSID( hKey, FALSE, psdAbsRegSD );

	//
	// Always reset the DACL and close the key
	//
	SetRegAccess( NULL, NULL, psdAbsRegSD, &hKey );
	RegCloseKey( hKey );
	free( psdSrelRegSD );

	return(TRUE);
}


//----------------------------------------------------------------------
//
// UpdateKeySID
//
// Updates the SIDs for the specified registry key and recurses
// into subkeys.
//
//----------------------------------------------------------------------
void UpdateKeySID( HWND hDlg, HKEY Root, PTCHAR RootName, PTCHAR PathName,
				  PSECURITY_DESCRIPTOR newSecDesc )
{
	TCHAR			subKeyName[ 1024 ];
	TCHAR			subRootName[ 1024 ];
	TCHAR			subPathName[ 1024 ];
	HKEY			hKey;
	FILETIME		Time;
	DWORD			subKeys;
	int				idx;
	DWORD			status, nb;
	PSECURITY_DESCRIPTOR	origDesc;

	//
	// Update the progress dialog
	//
	UpdateProgressDialog( hDlg, IDC_PATHNAME, RootName );

	//
	// Change the security on the key so that we can enumerate it
	//
	status = RegOpenKeyEx( Root, PathName, 0, 
						WRITE_DAC|READ_CONTROL|
						ACCESS_SYSTEM_SECURITY,
						&hKey );
	if( status != ERROR_SUCCESS ) {
		return;
	}

	//
	// Get the source security and set the new descriptor
	//
	origDesc = GetRegAccess( hKey );
	SetRegAccess( Root, PathName, newSecDesc, &hKey );

	//
	// See if there are subkeys to enumerate
	//
	status = RegQueryInfoKey( hKey, NULL, NULL, NULL,
							&subKeys, NULL,
							NULL, NULL, NULL, NULL,
							NULL, NULL );
	if( status == ERROR_SUCCESS && subKeys ) {

		//
		// Allocate buffers 
		//
		idx = 0;
		while(1)  {

			nb = sizeof subKeyName;
			status = RegEnumKeyEx( hKey, idx, subKeyName, &nb, NULL, NULL, NULL, &Time );
			if ( status == ERROR_NO_MORE_ITEMS )
				break;
			if ( status != ERROR_SUCCESS )
				break;

			//
			// Construct the names
			//
			wsprintf( subRootName, L"%s\\%s", RootName, subKeyName );
			if( wcscmp( PathName, L"" )) 
				wsprintf( subPathName, L"%s\\%s", PathName, subKeyName );
			else
				wcscpy( subPathName, subKeyName );

			//
			// Process the subkey - leave the producttype alone, since
			// we would otherwise getting an annoying popup about
			// messing with our license.
			//
			if( !( !wcsnicmp( subPathName, PRODUCTTYPEPATH, wcslen(PRODUCTTYPEPATH)) &&
					!wcsicmp( subKeyName, PRODUCTTYPENAME))) 
			{
				UpdateKeySID( hDlg, Root,  subRootName, subPathName,
							  newSecDesc );
			}
			idx++;
		}
	}

	//
	// Now process the security on this key. This
	// will restore the security either to updated
	// (SID converted) settings, or the original
	// settings of the key. It will also close
	// the key.
	//
	CheckKeySID( hKey, Root, PathName, origDesc );
}

//----------------------------------------------------------------------
//
// UnloadProfileHives
//
// Unloads the user hives that were loaded into the Registry.
//
//----------------------------------------------------------------------
void UnloadProfileHives( DWORD NumLoadedHives )
{
	TCHAR		hiveName[256];
	DWORD		i;

	for( i = 0; i < NumLoadedHives; i++ ) {

		wsprintf( hiveName, L"User%d", i );
		RegUnLoadKey( HKEY_LOCAL_MACHINE, hiveName );
	}
}

//----------------------------------------------------------------------
//
// LoadProfileHives
//
// Loads all the user profile hives into the Registry, so that
// their security descriptors can be updated.
//
//----------------------------------------------------------------------
void LoadProfileHives( DWORD *NumLoadedHives )
{
	int				idx;
	DWORD			status, nb, type;
	HKEY			hKey, hProfileKey;
	TCHAR			profileKeyPath[1024], hiveName[256];
	TCHAR			subKeyName[1024], imagePath[1024], hivePath[1024];

	//
	// First, locate the user hives and load them
	//
	*NumLoadedHives = 0;
	status = RegOpenKey( HKEY_LOCAL_MACHINE, PROFILELISTPATH, &hKey );
	if( status == ERROR_SUCCESS ) {

		idx = 0;
		while(1)  {

			nb = 1024;
			status = RegEnumKeyEx( hKey, idx, subKeyName, &nb, NULL, NULL, NULL, NULL );
			if ( status == ERROR_NO_MORE_ITEMS )
				break;
			if ( status != ERROR_SUCCESS )
				break;

			//
			// Open the subkey and get the location of the registry hive
			//
			wsprintf( profileKeyPath, L"%s\\%s", PROFILELISTPATH, subKeyName );
			status = RegOpenKey( HKEY_LOCAL_MACHINE, profileKeyPath, &hProfileKey );
			if( status == ERROR_SUCCESS ) {
				
				nb = 1024;
				status = RegQueryValueEx( hProfileKey, L"ProfileImagePath",
										NULL, &type, (PBYTE) imagePath, &nb );
				if( status == ERROR_SUCCESS ) {

					//
					// Expand the system root
					//
					ExpandEnvironmentStrings( imagePath, hivePath, 1024 );

					//
					// Append the hive file name if the path isn't a file
					//
					if( GetFileAttributes( hivePath ) & FILE_ATTRIBUTE_DIRECTORY ) {
  					
						wcscat( hivePath, L"\\ntuser.dat" );
					}

					//
					// Now load the hive
					//
					wsprintf( hiveName, L"User%d", *NumLoadedHives );
					status = RegLoadKey( HKEY_LOCAL_MACHINE, hiveName, hivePath );
					if( status == ERROR_SUCCESS ) {

						(*NumLoadedHives)++;
					}	
				}
			}
			idx++;
		}
		RegCloseKey( hKey );
	}
}


//----------------------------------------------------------------------
//
// UpdateRegistrySID
//
// Scans all the Registry hives looking for SIDs
// that need to be changed in the security descriptors.
//
//----------------------------------------------------------------------
void UpdateRegistrySID( HWND hDlg, PSECURITY_DESCRIPTOR newSecDesc )
{
	TCHAR			startPath[16];
	DWORD			NumLoadedHives;

	//
	// First, load all the user profile hives
	//
	SetDlgItemText( hDlg, IDC_OPERATION, _T("Updating Registry keys:"));

	LoadProfileHives( &NumLoadedHives );

	//
	// Update all the loaded root keys
	//
	wcscpy( startPath, L"");
	UpdateKeySID( hDlg, HKEY_LOCAL_MACHINE, L"HKLM", startPath, newSecDesc ); 	
	wcscpy( startPath, L"");
	UpdateKeySID( hDlg, HKEY_USERS, L"HKEY_USERS", startPath, newSecDesc );

	//
	// Unload user profile hives
	//
	UnloadProfileHives( NumLoadedHives );
}


//===================================================================
//
// SAM AND SECURITY SID CHANGING ROUTINES
//
//===================================================================

//----------------------------------------------------------------------
//
// CopyKey2
//
// Copies a subtree - this is the core of our recursive subkey copy
// functions.
//
//----------------------------------------------------------------------
VOID CopyKey2( HKEY Root, TCHAR *Source, TCHAR *Destination, PSECURITY_DESCRIPTOR newSecDesc )
{
	HKEY					srcKey, dstKey;
	DWORD					status;
	PSECURITY_DESCRIPTOR	origDesc;
	TCHAR					*SourceTail;
	TCHAR					*DestTail;
	FILETIME				Time;
	int						idx;
	TCHAR					valName[1024];
	DWORD					valNameLen, valDataLen, valType, valDataBufsize = 1024;
	BYTE					*valData = (PBYTE) malloc( valDataBufsize );
	BOOLEAN					firstPass = TRUE;
	DWORD					nb;

	//
	// Open the source
	//
	status = RegOpenKey( Root, Source, &srcKey );
	if( status != ERROR_SUCCESS ) {
		return;
	}

	//
	// Get the source security and set the new descriptor
	//
	origDesc = GetRegAccess( srcKey );
	SetRegAccess( Root, Source, newSecDesc, &srcKey );

	//
	// Create a copy
	//
	status = RegCreateKey( Root, Destination, &dstKey );
	if( status != ERROR_SUCCESS ) {
		return;
	}

	//
	// Enumerate source values and create copies in the destination
	//
	wcscpy( valName, L"");
	idx = 0;
	valDataLen = 0;

	do {

		//
		// Copy to the destination 
		//
		if( !firstPass ) {
			if( valType == (DWORD) -1 ) valDataLen = 0;
			(void) RegSetValueEx( dstKey, valName, 0, valType, valData, valDataLen );
		} else {
			firstPass = FALSE;
		}
		valNameLen = sizeof(valName);
		valDataLen = valDataBufsize;
		status = RegEnumValue( srcKey, idx++, valName, &valNameLen,
					NULL, &valType,	valData, &valDataLen  );
		while ( status == ERROR_MORE_DATA )  {
			valNameLen = sizeof(valName);
			valDataBufsize = valDataLen += 1024;
			valData = (PBYTE) realloc( valData, valDataBufsize );
			status = RegEnumValue( srcKey, idx-1, valName, &valNameLen,
									NULL, &valType,	valData, &valDataLen  );
		}

	} while ( status == ERROR_SUCCESS );

	//
	// Enumerate source subkeys and create copies in the destination
	//	
	SourceTail = Source + wcslen(Source);
	*SourceTail++ = L'\\';
	DestTail = Destination + wcslen(Destination);
	*DestTail++ = L'\\';

	idx = 0;
	while(1)  {

		nb = 1024;
		status = RegEnumKeyEx( srcKey, idx, SourceTail, &nb, NULL, NULL, NULL, &Time );
		if ( status == ERROR_NO_MORE_ITEMS )
			break;
		if ( status != ERROR_SUCCESS )
			break;

		// Copy recursively
		wcscpy( DestTail, SourceTail );
		CopyKey2( Root, Source, Destination, newSecDesc );

		//
		// Restart the emumeration
		//
		RegCloseKey( srcKey );
		SourceTail[-1] = L'\0';
		status = RegOpenKey( Root, Source, &srcKey );
		if( status != ERROR_SUCCESS ) {
			break;
		}
		wcscpy( SourceTail-1, L"\\" );
		idx = 0;
	}	

	//
	// Set copy of access on destination(which closes the keys)
	//
	SetRegAccess( NULL, NULL, newSecDesc, &dstKey );
	*SourceTail = 0;
	RegDeleteKey( Root, Source );
	RegCloseKey( srcKey );
	RegCloseKey( dstKey );
	free( origDesc );
}


//----------------------------------------------------------------------
//
// CopyKey
//
// Top level function for recursive key copy routine.
//
//----------------------------------------------------------------------
VOID CopyKey( HKEY Root, TCHAR *Source, TCHAR *Destination, PSECURITY_DESCRIPTOR newSecDesc )
{
	TCHAR	fullSource[ 1024 ];
	TCHAR	fullDest[ 1024 ];

	wcscpy( fullSource, Source );
	wcscpy( fullDest, Destination );
	CopyKey2( Root, fullSource, fullDest, newSecDesc );
}


//----------------------------------------------------------------------
//
// ChangeComputerSID
//
// Recursively dive into the key, changing all occurrences of OldSid
// to NewSid, whether its in a value, or in a key name.
//
//----------------------------------------------------------------------
BOOL ChangeComputerSID( HKEY Root, TCHAR *path, PSECURITY_DESCRIPTOR newSecDesc )
{
	LONG		status;
	HKEY		hKey;
	int         idx;
	TCHAR       Name[ 1024 ];
	TCHAR       *Tail, *newSubPtr;
	FILETIME	Time;
	TCHAR		newSubName[1024], textNewSid[256];
	TCHAR		textSubAuth[256], textSubAuthTmp[256];
	TCHAR		valName[1024];
	DWORD		valNameLen, valDataLen, valType, valDataBufsize = 1024;
	BYTE	*	valData = (PBYTE) malloc( valDataBufsize );
	DWORD		nb, j;	
	BOOLEAN		valChanged;
	PBYTE		sidloc;
	PSECURITY_DESCRIPTOR oldSecDesc;

	//
	// Open key 
	//
	status = RegOpenKey( Root, path, &hKey );
	if ( status != ERROR_SUCCESS ) {

		//
		// Work around a bug in MTS where it creates an unopenable
		// key
		//
		if( status == ERROR_FILE_NOT_FOUND ) 
			return TRUE;
		else
			return FALSE;
	}

	//
	// Get the key's security descriptor and set the new one
	//
	if( !(oldSecDesc = GetRegAccess( hKey ))) 
		return FALSE;
	if( !SetRegAccess( Root, path, newSecDesc, &hKey ) ) 
		return FALSE;

	//
	// Scan the values looking for the old SID and change it
	// to the new SID.
	//
	wcscpy( valName, L"");
	idx = 0;
	valNameLen = 1024;
	valDataLen = valDataBufsize;
	RegQueryValue( hKey, path, (PTCHAR) valData, (PLONG) &valDataLen );
	valDataBufsize = valDataLen;
	valData = (PBYTE) realloc( valData, valDataBufsize );
	RegQueryValue( hKey, path, (PTCHAR) valData, (PLONG) &valDataLen );
	do {
		BYTE * tail = valData + valDataLen - (NewSidLength - sizeof(DWORD)) + 1;

		//
		// If its large enough, scan for the old SID
		//
		valChanged = FALSE;

		for ( sidloc = valData;
			  (sidloc < tail)  &&  (sidloc = (PBYTE) memchr( sidloc, OldSid[sizeof(DWORD)], tail - sidloc ));
			  sidloc++ )
		{
			if ( memcmp( sidloc, &OldSid[sizeof(DWORD)], NewSidLength - sizeof(DWORD)) == 0 ) {
				memcpy( sidloc, &NewSid[sizeof(DWORD)], NewSidLength - sizeof(DWORD));
				sidloc += NewSidLength - sizeof(DWORD) - 1;
				valChanged = TRUE;
			}
		}

		//
		// If we performed a replacement, update the value
		//
		if( valChanged ) {
			if( RegSetValueEx( hKey, valName, 0, valType, valData, valDataLen )	!= ERROR_SUCCESS ) 
				return FALSE;
		}

		valNameLen = 1024;
		valDataLen = valDataBufsize;
		status = RegEnumValue( hKey, idx++, valName, &valNameLen,
								NULL, &valType,	valData, &valDataLen  );
		while ( status == ERROR_MORE_DATA )  {
			valDataBufsize = valDataLen += 1024;
			valData = (PBYTE) realloc( valData, valDataBufsize );
			status = RegEnumValue( hKey, idx-1, valName, &valNameLen,
									NULL, &valType,	valData, &valDataLen  );
		}
	
	} while ( status == ERROR_SUCCESS );


	//
	// Now we enumerate subkeys, because if the tail has 
	// the SID embedded in it, we have to copy the subtree
	//
	wcscpy( Name, path );
	Tail = Name + wcslen(Name);
	*Tail++ = L'\\';

	wsprintf( textSubAuth, L"" );
	for( j = 2; j < NewSidLength / sizeof(DWORD); j++ ) {
		wsprintf( textSubAuthTmp, L"-%u", ((PDWORD) OldSid)[j] );
		wcscat( textSubAuth, textSubAuthTmp );
	}

	wsprintf( textNewSid, L"" );
	for( j = 2; j < NewSidLength / sizeof(DWORD); j++ ) {
		wsprintf( textSubAuthTmp, L"-%u", ((PDWORD) NewSid)[j] );
		wcscat( textNewSid, textSubAuthTmp );
	}

	//
	// Get subkey names
	//
	for ( idx = 0;; idx ++ )  {

		nb = sizeof Name;
		status = RegEnumKeyEx( hKey, idx, Tail, &nb, NULL, NULL, NULL, &Time );
		if ( status == ERROR_NO_MORE_ITEMS )
			break;
		if ( status != ERROR_SUCCESS )
			break;		

		if( !ChangeComputerSID( Root, Name, newSecDesc ) ) {

			SetRegAccess( NULL, NULL, oldSecDesc, &hKey );
			RegCloseKey( hKey );
			free( oldSecDesc);
			return FALSE;
		}

		for( j = 0; j < wcslen( Tail ); j++ ) {
			if( !wcsncmp( textSubAuth, Tail+j, wcslen( textSubAuth ) ) ) {

				//
				// Got a match - copy the tree to the new name
				//
				wsprintf( newSubName, L"%s", Name );
				newSubPtr = (TCHAR *) ((DWORD) newSubName + (DWORD) Tail - (DWORD) Name + j*2); 
				wcscpy( newSubPtr, textNewSid );
				wcscat( newSubPtr, Tail + j + wcslen( textSubAuth ));

				CopyKey( Root, Name, newSubName, newSecDesc );

				//
				// Restart the scan of the key
				//
				idx = -1;
				break;
			}
		}
	}
	
	//
	// Change scurity back and close key
	//
	SetRegAccess( NULL, NULL, oldSecDesc, &hKey );
	RegCloseKey( hKey );
	free( oldSecDesc);
	free( valData );
	return TRUE;
}
