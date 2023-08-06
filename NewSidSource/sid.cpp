//----------------------------------------------------------------------
//
// NewSID
//
// Copyright (c) 1997-2002 Mark Russinovich and Bryce Cogswell
//
// Changes the computer SID. 
//
// This code is protected under copyright law. You do not have 
// permission to use this code in a commercial SID-changing product.
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

//
// Are we synchronizing with another computer?
//
BOOLEAN					SyncSID = FALSE;
TCHAR					SyncComputerName[MAX_COMPUTERNAME_LENGTH+1];
BOOLEAN					AutoReboot = FALSE;

//
// The binary representations of the old and the
// new subauthority fields of the computer SID
//
const DWORD				NewSidLength = 6 * sizeof(DWORD);
CHAR					NewSid[ 6 * sizeof(DWORD) ];
CHAR					OldSid[ 6 * sizeof(DWORD) ];

//
// Security descriptor we use to open keys
//
PSECURITY_DESCRIPTOR	AdminSecDesc;

//
// Application instance handle
//
HINSTANCE				hInst;

//
// The wizard
//
CWizardWindow			*Wizard;

//
// New computer name
//
BOOLEAN					RenameComputer = FALSE;
TCHAR					NewComputerName[MAX_PATH] = _T("");
SETCOMPUTERNAMEEX		pSetComputerNameEx;


//
// SID generation choice
//
typedef enum {
	SID_RANDOM,
	SID_COMPUTER,
	SID_SPECIFIC
} SIDCHOICE;

SIDCHOICE				SidChoice = SID_RANDOM;
TCHAR					SidComputer[MAX_PATH] = _T("");
TCHAR					SidSpecific[MAX_PATH] = _T("");




//----------------------------------------------------------------------
//
// DisplayError
//
// Formats an error message for the last error and pops a message box.
//
//----------------------------------------------------------------------
void DisplayError( TCHAR *ErrorTitle )
{
	TCHAR	*errMsg;
	TCHAR	message[256];
	
	FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			NULL, GetLastError(), 
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 
			(LPTSTR) &errMsg, 0, NULL );
	swprintf( message, L"%s:\n\n%s", ErrorTitle, errMsg );

	MessageBox( NULL, message, APPNAME,
				MB_ICONERROR|MB_OK );
	
	LocalFree( errMsg );
}


//----------------------------------------------------------------------
//
// UpdateProgressDialog
//
// Make the pathname fit into the dialog box
//
//----------------------------------------------------------------------
void UpdateProgressDialog( HWND hDlg, DWORD Control, PTCHAR PathName )
{
	TCHAR		shortName[MAX_DLG_PATHLEN];
	DWORD		i, j, k;

	//
	// When run in auto mode there's no dialog
	//
	if( hDlg == NULL ) {

		return;
	}

	if( (i = wcslen( PathName )) > MAX_DLG_PATHLEN ) {

		j = MAX_DLG_PATHLEN-2;
		shortName[MAX_DLG_PATHLEN-1] = 0;
		while( j > (MAX_DLG_PATHLEN/2+3) ) {

			shortName[j] = PathName[i];
			j--;
			i--;
		}

		for( k = 0; k < MAX_DLG_PATHLEN/2; k++ ) {

		   shortName[k] = PathName[k];
		}

		while( k <= j ) shortName[k++] = L'.';

		SetDlgItemText( hDlg, Control, shortName );

	} else {

		SetDlgItemText( hDlg, Control, PathName );
	}
}

//----------------------------------------------------------------------
//  
// AboutDlgProc
//
// Pops up the standard About dialog box.
//
//----------------------------------------------------------------------
BOOL CALLBACK AboutDlgProc (HWND hDlg, UINT message, UINT wParam, LPARAM lParam) 
{
	RECT	parentRc, childRc;
	static HWND		hLink;
	static BOOL		underline_link;
	static HFONT	hFontNormal = NULL;
	static HFONT	hFontUnderline = NULL;
	static HCURSOR	hHandCursor = NULL;
	static HCURSOR	hRegularCursor;
	LOGFONT			logfont;

	switch ( message )  {
	case WM_INITDIALOG:

		GetWindowRect( GetParent(hDlg), &parentRc );
		GetWindowRect( hDlg, &childRc );
		parentRc.left += 70;
		parentRc.top  += 60;
		MoveWindow( hDlg, parentRc.left, parentRc.top, childRc.right - childRc.left, childRc.bottom - childRc.top, TRUE );
		underline_link = TRUE;
		hLink = GetDlgItem( hDlg, IDC_LINK );

		// get link fonts
		hFontNormal = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
		GetObject( hFontNormal, sizeof logfont, &logfont); 
		logfont.lfUnderline = TRUE;
		hFontUnderline = CreateFontIndirect( &logfont );

		// get hand
		hHandCursor = LoadCursor( hInst, TEXT("HAND") );
		hRegularCursor = LoadCursor( NULL, IDC_ARROW );
		return TRUE;

	case WM_CTLCOLORSTATIC:
		if ( (HWND)lParam == hLink )  {
			HDC	hdc = (HDC)wParam;
			SetBkMode( hdc, TRANSPARENT );
			if ( GetSysColorBrush(26/*COLOR_HOTLIGHT*/) )
				SetTextColor( hdc, GetSysColor(26/*COLOR_HOTLIGHT*/) );
			else
				SetTextColor( hdc, RGB(0,0,255) );
			SelectObject( hdc, underline_link ? hFontUnderline : hFontNormal );
			return (LONG)GetSysColorBrush( COLOR_BTNFACE );
		}
		break;

	case WM_MOUSEMOVE: {
		POINT	pt = { LOWORD(lParam), HIWORD(lParam) };
		HWND	hChild = ChildWindowFromPoint( hDlg, pt );
		if ( underline_link == (hChild == hLink) )  {
			underline_link = !underline_link;
			InvalidateRect( hLink, NULL, FALSE );
		}
		if ( underline_link )
			SetCursor( hRegularCursor );
		else
			SetCursor( hHandCursor );
		break;
	}

	case WM_LBUTTONDOWN: {
		POINT		pt = { LOWORD(lParam), HIWORD(lParam) };
		HWND		hChild = ChildWindowFromPoint( hDlg, pt );
		if ( hChild == hLink )  {
			ShellExecute( hDlg, TEXT("open"), TEXT("http://www.winternals.com"), NULL, NULL, SW_SHOWNORMAL );
		} 
		break;
	}

	case WM_COMMAND:
		switch ( wParam ) {
		case IDOK:
		case IDCANCEL:
			EndDialog( hDlg, 0 );
			return TRUE;
		}
		break; 

	case WM_CLOSE:
		EndDialog( hDlg, 0 );
		return TRUE;

	default:
		break;
	}
    return FALSE;
}

//----------------------------------------------------------------------
//
// SecurityReplaceSID
//
// This convenience function scans the passed in SID for an instance
// of the old computer security SID, and if it finds it, it updates
// the instance with the new computer SID.
//
//----------------------------------------------------------------------
BOOLEAN SecurityReplaceSID( PSID pSid )
{
	BOOLEAN		foundSid;
	DWORD		numSubAuths;
	PDWORD		firstSubAuth;
	PDWORD		firstOldSidAubAuth;
	PDWORD		firstNewSidAubAuth;

	foundSid = FALSE;
	numSubAuths = *GetSidSubAuthorityCount(pSid);
	if( numSubAuths >= *GetSidSubAuthorityCount(NewSid)) {

		firstSubAuth = GetSidSubAuthority(pSid, 0); 
		firstOldSidAubAuth = GetSidSubAuthority(OldSid, 0); 
		if( !memcmp( firstSubAuth, firstOldSidAubAuth, 
				NewSidLength - ((PCHAR) firstOldSidAubAuth - &OldSid[0]))) {

			//
			// Found it
			//
			firstNewSidAubAuth = GetSidSubAuthority(NewSid, 0);
			memcpy(firstSubAuth, firstNewSidAubAuth, 
					NewSidLength - ((PCHAR) firstOldSidAubAuth - &OldSid[0]) );
			foundSid = TRUE;
		}
	}
	return foundSid;
}


//----------------------------------------------------------------------
//
// SetCurrentPrivilege
//
// Used to set the privilege necessary for adding computers to domains.
//
//----------------------------------------------------------------------
BOOL SetCurrentPrivilege( LPCTSTR Privilege,  BOOL bEnablePrivilege )
{
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    LUID luid;
    TOKEN_PRIVILEGES tpPrevious;
    DWORD cbPrevious=sizeof(TOKEN_PRIVILEGES);
    BOOL bSuccess=FALSE;

    if(!LookupPrivilegeValue(NULL, Privilege, &luid)) return FALSE;

    if(!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES,
            &hToken
            )) return FALSE;

    //
    // first pass.  get current privilege setting
    //
    tp.PrivilegeCount           = 1;
    tp.Privileges[0].Luid       = luid;
    tp.Privileges[0].Attributes = 0;

    AdjustTokenPrivileges(
            hToken,
            FALSE,
            &tp,
            sizeof(TOKEN_PRIVILEGES),
            &tpPrevious,
            &cbPrevious
            );

    if(GetLastError() == ERROR_SUCCESS) {
        //
        // second pass.  set privilege based on previous setting
        //
        tpPrevious.PrivilegeCount     = 1;
        tpPrevious.Privileges[0].Luid = luid;

        if(bEnablePrivilege) {
            tpPrevious.Privileges[0].Attributes |= (SE_PRIVILEGE_ENABLED);
        }
        else {
            tpPrevious.Privileges[0].Attributes ^= (SE_PRIVILEGE_ENABLED &
                tpPrevious.Privileges[0].Attributes);
        }

        AdjustTokenPrivileges(
                hToken,
                FALSE,
                &tpPrevious,
                cbPrevious,
                NULL,
                NULL
                );

        if(GetLastError() == ERROR_SUCCESS) bSuccess=TRUE;
    }

    CloseHandle(hToken);

    return bSuccess;
}




//----------------------------------------------------------------------
//  
// ShutDown
//
// Reboot the machine
//
//----------------------------------------------------------------------
BOOL ShutDown()
{
	HANDLE				hToken;    
	TOKEN_PRIVILEGES	tkp;      
 
	// 
	// Get the current process token handle 
	// so we can get shutdown privilege. 
	// 
	if (!OpenProcessToken(GetCurrentProcess(), 
		TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
	    
		return FALSE;
	}
 
	// 
	// Get the LUID for shutdown privilege. 
	// 
	LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, 
	        &tkp.Privileges[0].Luid); 
 
	tkp.PrivilegeCount = 1;  
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; 

	// 
	// Get shutdown privilege for this process. 
	// 
 	if( !AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, 
		(PTOKEN_PRIVILEGES) NULL, 0)) {

	    return FALSE; 
	}
 
	// 
	// Actually shutdown
	// 
	if( !InitiateSystemShutdown(  NULL, NULL, 0, TRUE, TRUE )) {

	    return FALSE;
	}
 
	// 
	// Disable shutdown privilege.
	// 
	tkp.Privileges[0].Attributes = 0; 
	if( !AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, 
		(PTOKEN_PRIVILEGES) NULL, 0)) {
 
	    return FALSE;
	}
	return TRUE;
}


//----------------------------------------------------------------------
//
// ComputerBrowse
//
// Standard computer browsing dialog.
//
//----------------------------------------------------------------------
BOOLEAN ComputerBrowse(HWND hwnd, PTCHAR RemoteComputer ) 
{ 
    BROWSEINFO			bi;     
	PTCHAR				lpBuffer; 
    LPITEMIDLIST		pidlComputers;  // PIDL for Programs folder 
    LPITEMIDLIST		pidlBrowse;    // PIDL selected by user  
	LPMALLOC			pMalloc;

	// Get shell malloc interface
	SHGetMalloc(&pMalloc);

    // Allocate a buffer to receive browse information. 
    if ((lpBuffer = (PTCHAR) pMalloc->Alloc( MAX_PATH)) == NULL)         
		return FALSE;  

    // Get the PIDL for the Programs folder. 
    if (!SUCCEEDED(SHGetSpecialFolderLocation( 
            hwnd, CSIDL_NETWORK, &pidlComputers))) { 

        pMalloc->Free( lpBuffer); 
		return FALSE;     
	}  

    // Fill in the BROWSEINFO structure.     
	bi.hwndOwner	= hwnd; 
    bi.pidlRoot		= pidlComputers;     
	bi.pszDisplayName = lpBuffer; 
    bi.lpszTitle	= _T("Select the computer from which you want to copy the SID:");     
	bi.ulFlags		= BIF_BROWSEFORCOMPUTER; // |BIF_EDITBOX; 
    bi.lpfn			= NULL;     
	bi.lParam		= 0;  

    // Browse for a folder and return its PIDL. 
    pidlBrowse = SHBrowseForFolder(&bi);     
	if (pidlBrowse != NULL) {  

        // Copy the name
        _tcscpy( RemoteComputer, lpBuffer); 
		
         // Free the PIDL returned by SHBrowseForFolder. 
        pMalloc->Free( pidlBrowse);     
		pMalloc->Free( pidlComputers); 
		pMalloc->Free( lpBuffer); 
		return TRUE;

	} else {      
	
		// Clean up. 
		pMalloc->Free( pidlComputers); 
		pMalloc->Free(lpBuffer); 
		return FALSE;
	}
} 



//--------------------------------------------------------------------
//
// GetTextualSid
//
// MSDN function for converting a binary SID to text form.
//
//--------------------------------------------------------------------
BOOL GetTextualSid(
    PSID pSid,            // binary Sid
    PTCHAR TextualSid,    // buffer for Textual representation of Sid
    LPDWORD lpdwBufferLen // required/provided TextualSid buffersize
    )
{
    PSID_IDENTIFIER_AUTHORITY psia;
    DWORD dwSubAuthorities;
    DWORD dwSidRev=SID_REVISION;
    DWORD dwCounter;
    DWORD dwSidSize;

    // Validate the binary SID.

    if(!IsValidSid(pSid)) return FALSE;

    // Get the identifier authority value from the SID.

    psia = GetSidIdentifierAuthority(pSid);

    // Get the number of subauthorities in the SID.

    dwSubAuthorities = *GetSidSubAuthorityCount(pSid);

    // Compute the buffer length.
    // S-SID_REVISION- + IdentifierAuthority- + subauthorities- + NULL

    dwSidSize=(15 + 12 + (12 * dwSubAuthorities) + 1) * sizeof(TCHAR);

    // Check input buffer length.
    // If too small, indicate the proper size and set last error.

    if (*lpdwBufferLen < dwSidSize) {

        *lpdwBufferLen = dwSidSize;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    // Add 'S' prefix and revision number to the string.

    dwSidSize= _stprintf(TextualSid, _T("S-%lu-"), dwSidRev );

    // Add SID identifier authority to the string.

    if ( (psia->Value[0] != 0) || (psia->Value[1] != 0) ) {

        dwSidSize += _stprintf(TextualSid + lstrlen(TextualSid),
                    _T("0x%02hx%02hx%02hx%02hx%02hx%02hx"),
                    (USHORT)psia->Value[0],
                    (USHORT)psia->Value[1],
                    (USHORT)psia->Value[2],
                    (USHORT)psia->Value[3],
                    (USHORT)psia->Value[4],
                    (USHORT)psia->Value[5]);

    } else {

        dwSidSize += _stprintf(TextualSid + lstrlen(TextualSid),
                     _T("%lu"),
                    (ULONG)(psia->Value[5]      )   +
                    (ULONG)(psia->Value[4] <<  8)   +
                    (ULONG)(psia->Value[3] << 16)   +
                    (ULONG)(psia->Value[2] << 24)   );
    }

    // Add SID subauthorities to the string.
    //
    for (dwCounter=0 ; dwCounter < dwSubAuthorities ; dwCounter++) {
        dwSidSize+= _stprintf(TextualSid + dwSidSize, _T("-%lu"),
                    *GetSidSubAuthority(pSid, dwCounter) );
    }

    return TRUE;
}



//----------------------------------------------------------------------
//  
// GetBinarySid
//
// Converts a textual SID into binary form. Taken from MS KB article 
// 198907.
//
//----------------------------------------------------------------------
PSID GetBinarySid(
    LPTSTR TextualSid  // Buffer for Textual representation of SID.
    )
{
    PSID  pSid = 0;
    SID_IDENTIFIER_AUTHORITY identAuthority;
    TCHAR buffer[1024];
    int   i;

    LPTSTR ptr, ptr1;


    BYTE  nByteAuthorityCount = 0;
    DWORD dwSubAuthority[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    ZeroMemory(&identAuthority, sizeof(identAuthority));

    lstrcpy(buffer, TextualSid);

    // S-SID_REVISION- + identifierauthority- + subauthorities- + NULL

    // Skip S
    if (!(ptr = _tcschr(buffer, _T('-'))))
    {
        return pSid;
    }

    // Skip -
    ptr++;

    // Skip SID_REVISION
    if (!(ptr = _tcschr(ptr, _T('-'))))
    {
        return pSid;
    }

    // Skip -
    ptr++;

    // Skip identifierauthority
    if (!(ptr1 = _tcschr(ptr, _T('-'))))
    {
        return pSid;
    }
    *ptr1= 0;

    if ((*ptr == '0') && (*(ptr+1) == 'x'))
    {
        _stscanf(ptr, _T("0x%02hx%02hx%02hx%02hx%02hx%02hx"),
            &identAuthority.Value[0],
            &identAuthority.Value[1],
            &identAuthority.Value[2],
            &identAuthority.Value[3],
            &identAuthority.Value[4],
            &identAuthority.Value[5]);
    }
    else
    {
        DWORD value;

        _stscanf(ptr, _T("%lu"), &value);

        identAuthority.Value[5] = (BYTE)(value & 0x000000FF);
        identAuthority.Value[4] = (BYTE)(value & 0x0000FF00) >> 8;
        identAuthority.Value[3] = (BYTE)(value & 0x00FF0000) >> 16;
        identAuthority.Value[2] = (BYTE)(value & 0xFF000000) >> 24;
    }

    // Skip -
    *ptr1 = '-';
    ptr = ptr1;
    ptr1++;

    for (i = 0; i < 8; i++)
    {
        // get subauthority
        if (!(ptr = _tcschr(ptr, '-')))
        {
            break;
        }
        *ptr=0;
        ptr++;
        nByteAuthorityCount++;
    }

    for (i = 0; i < nByteAuthorityCount; i++)
    {
        // Get subauthority.
        _stscanf(ptr1, _T("%lu"), &dwSubAuthority[i]);
        ptr1 += lstrlen(ptr1) + 1;
    }

    if (!AllocateAndInitializeSid(&identAuthority,
        nByteAuthorityCount,
        dwSubAuthority[0],
        dwSubAuthority[1],
        dwSubAuthority[2],
        dwSubAuthority[3],
        dwSubAuthority[4],
        dwSubAuthority[5],
        dwSubAuthority[6],
        dwSubAuthority[7],
        &pSid))
    {
        pSid = 0;
    }

    return pSid;
}

//----------------------------------------------------------------------
//  
// IsSubAuthValid
//
// Verifies that the SID is valid.
//
//----------------------------------------------------------------------
PBYTE IsSubAuthValid( PBYTE SidData, DWORD SidLength )
{
	PBYTE	sidPtr;

	sidPtr = NULL;
	if ( SidLength % sizeof(DWORD) == 0 )  {
		for ( sidPtr = SidData + SidLength - 5*sizeof(DWORD); sidPtr >= SidData; sidPtr -= sizeof(DWORD) )
			if ( ((PDWORD)sidPtr)[1] == 0x05000000  &&  ((PDWORD)sidPtr)[2] == 0x00000015 )
				break;
		if ( sidPtr < SidData )
			sidPtr = NULL;
	}
	return sidPtr;
}


//----------------------------------------------------------------------
//  
// IsValidComputerSid
//
// Verifies that the SID is valid.
//
//----------------------------------------------------------------------
PSID IsValidComputerSid( PTCHAR SidText )
{
	DWORD	sidLength;
	PBYTE	sidPtr;
	PSID	pSid;
	
	pSid = GetBinarySid( SidText );
	if( !IsValidSid( pSid )) {
	
		FreeSid( pSid );
		return NULL;
	}

	sidLength = GetLengthSid( pSid );
	for ( sidPtr = (PBYTE) pSid + sidLength - 5*sizeof(DWORD); sidPtr >= (PBYTE) pSid; sidPtr -= sizeof(DWORD) ) {

		if ( ((PDWORD)sidPtr)[0] == 0x05000000  &&  ((PDWORD)sidPtr)[1] == 0x00000015 )
			break;

	}
	if( sidPtr < (PBYTE) pSid ) {
		
		FreeSid( pSid );
		return NULL;

	} else {

		return pSid;
	}
}


//----------------------------------------------------------------------
//
// GetMachineSid
//
// Gets the sid of the current computer.
//
//----------------------------------------------------------------------
PSECURITY_DESCRIPTOR GetMachineSid()
{
	PSECURITY_DESCRIPTOR	newSecDesc, oldSecDesc;
	DWORD					valType;
	PBYTE					vData;
	HKEY					hKey;
	DWORD					nb;
	PBYTE					sidPtr;
	DWORD					Status;
 
	//
	// Now, get the descriptor of HKLM\SOFTWARE and apply this to SECURITY
	//
	newSecDesc = GetRegSecDesc( HKEY_LOCAL_MACHINE, L"SOFTWARE",
					DACL_SECURITY_INFORMATION );

	//
	// Read the last subauthority of the current computer SID
	//
	if( RegOpenKey( HKEY_LOCAL_MACHINE, L"SECURITY\\SAM\\Domains\\Account", 
		&hKey) != ERROR_SUCCESS ) {

		MessageBox( NULL, L"NewSID was unable to access the\nComputer's SID.",
									APPNAME, MB_ICONERROR|MB_OK );	
		return NULL;
	}
	oldSecDesc = GetRegAccess( hKey );
	SetRegAccess( HKEY_LOCAL_MACHINE, L"SECURITY\\SAM\\Domains\\Account", 
		newSecDesc, &hKey );
	nb = 0;
	vData = NULL;
	RegQueryValueEx( hKey, L"V", NULL, &valType, vData, &nb );
	vData = (PBYTE) malloc( nb );
	Status = RegQueryValueEx( hKey, L"V", NULL, &valType, vData, &nb );
	if( Status != ERROR_SUCCESS ) {
		
		SetRegAccess( HKEY_LOCAL_MACHINE, L"SECURITY\\SAM\\Domains\\Account", 
				oldSecDesc, &hKey );
		MessageBox( NULL, L"NewSID was unable to access the\nComputer's SID.",
			APPNAME, MB_ICONERROR|MB_OK );	
		return NULL;
	}
	SetRegAccess( NULL, NULL, oldSecDesc, &hKey );
	RegCloseKey( hKey );

	//
	// Make sure that we're dealing with a SID we understand
	//
	if( !(sidPtr = IsSubAuthValid( vData, nb ))) {

		MessageBox( NULL, L"NewSID does not understand the format of\nthe Computer's SID.",
						APPNAME, MB_ICONERROR|MB_OK );	
		return NULL;
	}
	memcpy( OldSid, sidPtr, NewSidLength );
	return newSecDesc;
}


//----------------------------------------------------------------------
//
// CompactRegistry
//
// Compacts the Registry.
//
//----------------------------------------------------------------------
void CompactRegistry( HWND hDlg )
{
	TCHAR	backupHivePath[1024];
	TCHAR	backupHiveFile[1024];
	TCHAR	tmpBackupHiveFile[1024];
	HKEY	hKey;
	int		hive;
	static const struct {
		HKEY		RootKey;
		TCHAR *		KeyName;
	} HiveList[] = {
		{	HKEY_LOCAL_MACHINE,	_T("SYSTEM")	},
		{	HKEY_LOCAL_MACHINE,	_T("SOFTWARE")	},
		{	HKEY_LOCAL_MACHINE,	_T("SAM")		},
		{	HKEY_LOCAL_MACHINE,	_T("SECURITY")	},
		{	HKEY_USERS,			_T(".DEFAULT")	},
		{	NULL,				NULL			},
	};


	SetDlgItemText( hDlg, IDC_OPERATION,
			_T("Compacting Registry hives:"));
	SetDlgItemText( hDlg, IDC_PATHNAME, _T(""));

	//
	// Create a backup directory
	//
	ExpandEnvironmentStrings( _T("%SystemRoot%\\System32\\Config\\Newsid Backup"), 
			backupHivePath, sizeof( backupHivePath)/sizeof(TCHAR) );	
	if( CreateDirectory( backupHivePath, NULL )) {

		for( hive = 0; HiveList[hive].RootKey; ++hive ) {

			SetDlgItemText( hDlg, IDC_PATHNAME, HiveList[hive].KeyName );

			_stprintf( backupHiveFile, _T("%s\\%s.bak"),
					backupHivePath, HiveList[hive].KeyName );

			if( RegOpenKey( HiveList[hive].RootKey, HiveList[hive].KeyName, &hKey ) ==
						ERROR_SUCCESS ) {	

				RegFlushKey( hKey );
				RegFlushKey( HKEY_LOCAL_MACHINE );

				if( RegSaveKey( hKey, backupHiveFile, NULL ) == 
							ERROR_SUCCESS ) {	
				
					RegCloseKey( hKey );
					_stprintf( tmpBackupHiveFile, _T("%s\\%s"), 
						backupHivePath, HiveList[hive].KeyName );
					RegReplaceKey( HiveList[hive].RootKey,
									HiveList[hive].KeyName, backupHiveFile, 
									tmpBackupHiveFile );

				} else {

					RegCloseKey( hKey );
				}
			}
		}
	}
}


//----------------------------------------------------------------------
//
// DoSidChange
//
// Actually directs the change of the computer's sid
//
//----------------------------------------------------------------------
void DoSidChange( PVOID Context )
{
	HWND		hDlg = (HWND) Context;
	HKEY		hKey;

	// 
	// Replace the original computer SID with the one we generated
	//
	if( ChangeComputerSID( HKEY_LOCAL_MACHINE, L"SECURITY",
								AdminSecDesc )) {

		//
		// Update SIDs embedded in all service security descriptors, which includes
		// file, port and printer shares
		//
		ChangeComputerSID( HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services",
								AdminSecDesc );

		//
		// Update SIDs embedded in NTFS security descriptors
		//
		UpdateFileSystemSID( hDlg );

		//
		// Update SIDs embedded in Registry keys
		//
		UpdateRegistrySID( hDlg, AdminSecDesc );

		//
		// Change the profile information 
		//
		if( ChangeComputerSID( HKEY_LOCAL_MACHINE, 
					L"Software\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList",
					AdminSecDesc ) ) {

		} else {

			MessageBox( hDlg, L"NewSID has successfully changed the\nComputer's SID but was unable\n"
							  L"to update profile information.",
								APPNAME, MB_ICONWARNING|MB_OK );	
		}

		//
		// Rename the computer
		//
		if( RenameComputer ) {

			//
			// Change the name
			//
			pSetComputerNameEx = (SETCOMPUTERNAMEEX) GetProcAddress( GetModuleHandle( _T("Kernel32.dll")),
							"SetComputerNameExW" );

			if( pSetComputerNameEx ) {

				pSetComputerNameEx( ComputerNamePhysicalDnsHostname, NewComputerName );

			} else {

				SetComputerName( NewComputerName );
			}

			//
			// Change the TCP/IP name
			//
			if( !RegOpenKey( HKEY_LOCAL_MACHINE, 
				_T("System\\CurrentControlSet\\Services\\Tcpip\\Parameters"), &hKey )) {

				RegSetValueEx( hKey, _T("Hostname"), 0, REG_SZ, (PBYTE) NewComputerName, 
								(_tcslen( NewComputerName )+1) * sizeof(TCHAR));
				RegCloseKey( hKey );
			}

			//
			// Change active computer name
			//
			if( !RegOpenKey( HKEY_LOCAL_MACHINE, 
				_T("System\\CurrentControlSet\\Control\\ComputerName\\ActiveComputerName"), &hKey )) {

				RegSetValueEx( hKey, _T("ComputerName"), 0, REG_SZ, (PBYTE) NewComputerName, 
								(_tcslen( NewComputerName )+1) * sizeof(TCHAR));
				RegCloseKey( hKey );
			}
		}

		//
		// Compact Registry
		//
		CompactRegistry( hDlg );

		if( AutoReboot ) {

			SetDlgItemText( hDlg, IDC_OPERATION,
				_T("Rebooting..."));
			SetDlgItemText( hDlg, IDC_PATHNAME, _T(""));		
			ShutDown();
		}

	} else {

		//
		// Something went wrong
		//
		MessageBox( hDlg, L"NewSID was unable to change the\nComputer's SID.\n\n"
							L"It is suggested you restore your system from backup.",
							APPNAME, MB_ICONERROR|MB_OK );	
	}
	if( hDlg ) {

		EndDialog( hDlg, 0);
	}
}


//===================================================================
//
// WIZARD DIALOG FUNCTIONS
//
//===================================================================


//----------------------------------------------------------------------
//  
//  Start page
//
//----------------------------------------------------------------------
bool 
StartPage( 
    CPropPage* page, 
    UINT message, 
    UINT wParam, 
    LONG /*lParam*/ 
    )
{
    switch ( message )  {
	case WM_INITDIALOG:
		break;
    
	case WM_COMMAND:
		switch ( LOWORD(wParam) )  {
		case IDC_ABOUT:
			::DialogBox( hInst, TEXT("ABOUT"), page->GetHwnd(), (DLGPROC)AboutDlgProc );
			break;
		case IDOK:

			AdminSecDesc = GetMachineSid();
			if( AdminSecDesc == NULL ) {

				ExitProcess( 1 );
			}
			break;
		}
		break;
	}
    
    return false;
}


//----------------------------------------------------------------------
//  
// GenerateRandomSid
//
// Thread that generates a SID.
//
//----------------------------------------------------------------------
void GenerateRandomSid( PVOID Context )
{
	HWND hProgressDlg = (HWND) Context;

	PostMessage( GetDlgItem( hProgressDlg, IDC_RANDOMPROGRESS ), PBM_SETSTEP,
					100/24, 0 );
	((PDWORD)NewSid)[0]	= ((PDWORD)OldSid)[0];
	((PDWORD)NewSid)[1]	= ((PDWORD)OldSid)[1];
	((PDWORD)NewSid)[2]	= ((PDWORD)OldSid)[2];
	((PDWORD)NewSid)[3]	= Random( GetDlgItem( hProgressDlg, IDC_RANDOMPROGRESS ));
	((PDWORD)NewSid)[4]	= Random( GetDlgItem( hProgressDlg, IDC_RANDOMPROGRESS ));
	((PDWORD)NewSid)[5]	= Random( GetDlgItem( hProgressDlg, IDC_RANDOMPROGRESS ));
	if( hProgressDlg ) {

		PostMessage( hProgressDlg, WM_CLOSE, 0, 0 );
	}
}


//----------------------------------------------------------------------
//  
// RandomSidDlgProc
//
// Just a dummy proc for dialogs.
//
//----------------------------------------------------------------------
BOOL CALLBACK RandomSidDlgProc (HWND hDlg, UINT message, UINT wParam, LPARAM lParam) 
{
	switch ( message )  {
	case WM_INITDIALOG:
		_beginthread( GenerateRandomSid, 0, (PVOID) hDlg );
		return TRUE;
	case WM_CLOSE:
		EndDialog( hDlg, 0 );
		return TRUE;
	}
	return FALSE;
}



//----------------------------------------------------------------------
//  
//  Choose page
//
//----------------------------------------------------------------------
bool 
ChoosePage( 
    CPropPage* page, 
    UINT message, 
    UINT wParam, 
    LONG /*lParam*/ 
    )
{
	HKEY		hKey, hSidKey;
	HCURSOR		hPrevCursor;
	PSID		pSid;
	PSECURITY_DESCRIPTOR	oldSecDesc;
	DWORD		valType;
	DWORD		nb;
	PBYTE		subAuth;
	DWORD		error;
	TCHAR		computerName[MAX_PATH];
	DWORD		computerNameLength;
	DWORD		oldSidLength;
	TCHAR		oldSidText[MAX_PATH];

    switch ( message )  {

	case WM_SHOWWINDOW:
		if ( wParam )  {
			page->EnableCancel( true );

			oldSidLength = sizeof(oldSidText)/sizeof(oldSidText[0]);
			GetTextualSid( OldSid, oldSidText, &oldSidLength );
			SetDlgItemText( page->GetHwnd(), IDC_CURSID, oldSidText );
			switch( SidChoice ) {
			case SID_RANDOM:
				CheckDlgButton( page->GetHwnd(), IDC_RANDOM , BST_CHECKED);
				break;

			case SID_COMPUTER:
				CheckDlgButton( page->GetHwnd(), IDC_COPY , BST_CHECKED);
				break;

			case SID_SPECIFIC:
				CheckDlgButton( page->GetHwnd(), IDC_SPECIFIC , BST_CHECKED);
				break;
			}
		}
		break;

	case WM_USER:
		//
		// This is invoked when the random SID generator is done
		//
		EnableWindow( page->GetHwnd(), TRUE );
		page->GoNext();
		break;

	case WM_COMMAND :
		//
		// Handle notifications
		//
		switch (HIWORD( wParam )) {
		case EN_CHANGE:

			if( LOWORD( wParam) == IDC_COMPUTER ) {

				GetDlgItemText( page->GetHwnd(), IDC_COMPUTER, SidComputer, sizeof(SidComputer)/sizeof(TCHAR));
				page->EnableNext( _tcscmp( SidComputer, _T("")) != 0 );

			} else {

				GetDlgItemText( page->GetHwnd(), IDC_SID, SidSpecific, sizeof(SidSpecific)/sizeof(TCHAR));
				page->EnableNext( _tcscmp( SidSpecific, _T("")) != 0 );
			}
			break;

		case BN_CLICKED:

			switch( LOWORD( wParam )) {
			case IDC_RANDOM:

				EnableWindow( page->GetDlgItem( IDC_COMPUTER ), false );
				EnableWindow( page->GetDlgItem( IDC_BROWSE ), false );
				EnableWindow( page->GetDlgItem( IDC_SID ), false );	
				page->EnableNext( true );
				break;

			case IDC_COPY:

				EnableWindow( page->GetDlgItem( IDC_COMPUTER ), true );
				EnableWindow( page->GetDlgItem( IDC_BROWSE ), true );
				EnableWindow( page->GetDlgItem( IDC_SID ), false );	
				page->EnableNext( _tcscmp( SidComputer, _T("")) != 0 );
				break;

			case IDC_SPECIFIC:

				EnableWindow( page->GetDlgItem( IDC_COMPUTER ), false );
				EnableWindow( page->GetDlgItem( IDC_BROWSE ), false );
				EnableWindow( page->GetDlgItem( IDC_SID ), true );	
				page->EnableNext( _tcscmp( SidSpecific, _T("")) != 0 );
				break;
			}
			break;
		}

		//
		// Handle commands
		//
		switch (LOWORD( wParam )) {
		case IDC_BROWSE:

			if( ComputerBrowse( page->GetHwnd(), SidComputer ) ) {

				SetDlgItemText( page->GetHwnd(), IDC_COMPUTER, SidComputer );
			} 
			break;

		case IDOK:
		case IDC_BACK:
			if( IsDlgButtonChecked( page->GetHwnd(), IDC_RANDOM ) == BST_CHECKED ) {
			
				SidChoice = SID_RANDOM;

				//
				// Launch the random number generator
				//
				if( LOWORD( wParam ) == IDOK ) {

					DialogBox( hInst, _T("GENERATINGSID"), page->GetHwnd(), 
										RandomSidDlgProc );
				}

			} else if( IsDlgButtonChecked( page->GetHwnd(), IDC_COPY ) == BST_CHECKED ) {

				SidChoice = SID_COMPUTER;

				//
				// Make sure that the computer is accessible by 
				// opening the registry of the target computer
				//
				if( LOWORD( wParam ) == IDOK ) {

					//
					// Make sure its not this computer
					//
					computerNameLength = sizeof( computerName ) / sizeof(TCHAR);
					GetComputerName( computerName, &computerNameLength );
					if( !_tcsicmp( SidComputer, computerName )) {

						MessageBox( page->GetHwnd(), 
								_T("You cannot specify this computer."), 
								APPNAME, MB_ICONERROR|MB_OK );
						SetFocus( page->GetDlgItem( IDC_COMPUTER ));
						return FALSE;
					}


					hPrevCursor = SetCursor( LoadCursor( NULL, IDC_WAIT ));
					if( RegConnectRegistry( SidComputer, HKEY_LOCAL_MACHINE, &hKey ) ) {

						MessageBox( page->GetHwnd(), 
								L"NewSID was unable to connect to the specified computer's Registry.",
								APPNAME, MB_ICONERROR|MB_OK );
						SetFocus( page->GetDlgItem( IDC_COMPUTER ));
						return true;
					}	

					//
					// Read the SID of the target computer
					//
					if( RegOpenKey( hKey, L"SECURITY\\SAM\\Domains\\Account", 
								&hSidKey) != ERROR_SUCCESS ) {

						MessageBox( page->GetHwnd(), 
								L"NewSID was unable to access the\ntarget computer's SID.",
								APPNAME, MB_ICONERROR|MB_OK );	
						return true;
					}

					//
					// Use the Software key's security as a model
					//
					oldSecDesc = GetRegAccess( hSidKey );
					SetRegAccess( hKey, L"SECURITY\\SAM\\Domains\\Account", 
								AdminSecDesc, &hSidKey );

					nb = 0;
					RegQueryValueEx( hSidKey, L"V", NULL, &valType, NULL, &nb );
					pSid = (PBYTE) malloc( nb );
					error = RegQueryValueEx( hSidKey, L"V", NULL, &valType, (PBYTE) pSid, &nb );
					if( error != ERROR_SUCCESS ) {
						
						SetRegAccess( hKey, L"SECURITY\\SAM\\Domains\\Account", 
								oldSecDesc, &hSidKey );
						MessageBox( NULL, L"NewSID was unable to access the\ntarget computer's SID.",
									APPNAME, MB_ICONERROR|MB_OK );
						return true;
					}
					SetRegAccess( NULL, NULL, oldSecDesc, &hSidKey );
					RegCloseKey( hSidKey );
					RegCloseKey( hKey );

					//
					// Make sure that we're dealing with a SID we understand
					//
					if( !(subAuth = IsSubAuthValid( (PBYTE) pSid, nb )))  {

						MessageBox( page->GetHwnd(), 
								L"NewSID does not undetstand the format of\nthe target computer's SID.",
								APPNAME, MB_ICONERROR|MB_OK );	
						return true;
					}
					memcpy( NewSid, subAuth, NewSidLength );
							RegCloseKey( hKey );
							SetCursor( hPrevCursor );
					free( pSid );
				}
				
			} else {

				SidChoice = SID_SPECIFIC;

				//
				// Validate the SID
				//
				if( LOWORD( wParam ) == IDOK ) {

					if( !(pSid = IsValidComputerSid( SidSpecific ))) {

						MessageBox( page->GetHwnd(), 
							L"The SID specified is not a valid computer SID.",
							APPNAME, MB_ICONERROR|MB_OK );

						FreeSid( pSid );
						SetFocus( page->GetDlgItem( IDC_SID ));
						return true;
					} 
					memcpy( NewSid, pSid, NewSidLength );
					FreeSid( pSid );
				}
			}			
			break;
		}
    }    
    return false;
}



//----------------------------------------------------------------------
//  
// ApplySidDlgProc
//
//
//----------------------------------------------------------------------
BOOL CALLBACK ApplySidDlgProc (HWND hDlg, UINT message, UINT wParam, LPARAM lParam) 
{
	switch ( message )  {
	case WM_INITDIALOG:
		
		SetDlgItemText( hDlg, IDC_OPERATION,
				_T("Changing computer SID..."));
		SetDlgItemText( hDlg, IDC_PATHNAME, _T(""));

		_beginthread( DoSidChange, 0, (PVOID) hDlg );
		return TRUE;

	case WM_CLOSE:
		EndDialog( hDlg, 0 );
		return TRUE;
	}
	return FALSE;
}



//----------------------------------------------------------------------
//  
// Rename page
//
//----------------------------------------------------------------------
bool 
RenamePage( 
    CPropPage* page, 
    UINT message, 
    UINT wParam, 
    LONG /*lParam*/ 
    )
{
	TCHAR	computerName[MAX_PATH];
	DWORD	nameLength;

    switch ( message )  {

	case WM_SHOWWINDOW:

		nameLength = sizeof(computerName)/sizeof(computerName[0]);
		GetComputerName( computerName, &nameLength );
		SetDlgItemText( page->GetHwnd(), IDC_CURNAME, computerName );
		break;

	case WM_COMMAND :
		//
		// Handle notifications
		//
		switch (HIWORD( wParam )) {
		case EN_CHANGE:

			GetDlgItemText( page->GetHwnd(), IDC_COMPUTERNAME, NewComputerName, sizeof(NewComputerName)/sizeof(TCHAR));
			if( IsDlgButtonChecked( page->GetHwnd(), IDC_CHECKRENAME ) == BST_CHECKED ) {

				page->EnableNext( _tcscmp( NewComputerName, _T("")) != 0 );

			} else {

				page->EnableNext( true );
			}
			break;

		case BN_CLICKED:

			EnableWindow( page->GetDlgItem( IDC_COMPUTERNAME), 
				IsDlgButtonChecked( page->GetHwnd(), IDC_CHECKRENAME ) == BST_CHECKED );
			SetFocus( page->GetDlgItem( IDC_COMPUTERNAME) );
			GetDlgItemText( page->GetHwnd(), IDC_COMPUTERNAME, NewComputerName, sizeof(NewComputerName)/sizeof(TCHAR));

			if( IsDlgButtonChecked( page->GetHwnd(), IDC_CHECKRENAME ) == BST_CHECKED ) {

				page->EnableNext( _tcscmp( NewComputerName, _T("")) != 0 );

			} else {

				page->EnableNext( true );
			}
			break;
		}

		//
		// Handle commands
		//
		switch (LOWORD( wParam )) {
		case IDOK:

			GetDlgItemText( page->GetHwnd(), IDC_COMPUTERNAME, NewComputerName, sizeof(NewComputerName)/sizeof(TCHAR));
			RenameComputer = (BOOLEAN) (IsDlgButtonChecked( page->GetHwnd(), IDC_CHECKRENAME ) == BST_CHECKED);
			break;
		}
		break;
	}
	return FALSE;
}


//----------------------------------------------------------------------
//  
//  Ready page
//
//----------------------------------------------------------------------
bool 
ReadyPage( 
    CPropPage* page, 
    UINT message, 
    UINT wParam, 
    LONG /*lParam*/ 
    )
{
	TCHAR	sidText[256];
	DWORD	sidLength;

    switch ( message )  {
	case WM_SHOWWINDOW:

		CheckDlgButton( page->GetHwnd(), IDC_REBOOT, BST_CHECKED );
		sidLength = sizeof(sidText)/sizeof(TCHAR);
		GetTextualSid( OldSid, sidText, &sidLength );
		SetDlgItemText( page->GetHwnd(), IDC_CURRENTSID, sidText );

		sidLength = sizeof(sidText)/sizeof(TCHAR);
		GetTextualSid( NewSid, sidText, &sidLength );
		SetDlgItemText( page->GetHwnd(), IDC_NEWSID, sidText );

		if( RenameComputer ) {

			_stprintf( sidText, _T("The computer will be renamed to \"%s\"."), 
				NewComputerName );
			SetDlgItemText( page->GetHwnd(), IDC_RENAME, sidText );

		} else {

			ShowWindow( page->GetDlgItem( IDC_RENAME ), SW_HIDE );
		}
		break;
    
	case WM_COMMAND:
		switch ( LOWORD(wParam) )  {
		case IDOK:
			AutoReboot = 
				(IsDlgButtonChecked( page->GetHwnd(), IDC_REBOOT ) == BST_CHECKED);
			DialogBox( hInst, _T("APPLYINGGSID"), page->GetHwnd(), 
								ApplySidDlgProc );
			break;
		}
		break;
	}
    
    return false;
}


//----------------------------------------------------------------------
//  
//  Finish page
//
//----------------------------------------------------------------------
bool 
FinishPage( 
    CPropPage* page, 
    UINT message, 
    UINT wParam, 
    LONG /*lParam*/ 
    )
{
    switch ( message )  {
	case WM_SHOWWINDOW:
		if( !AutoReboot ) {

			SetDlgItemText( page->GetHwnd(), IDC_FINISHTEXT, _T("Press Finish to exit the wizard."));
		}
		page->EnableBack( false );
		break;

	case WM_COMMAND:
		switch (LOWORD( wParam )) {
		case IDOK:

		  if(AutoReboot == TRUE) {

		    ShutDown();

		  } else {

		    PostQuitMessage(0);
		  }
		}
		break;
    }    
    return false;
}


//----------------------------------------------------------------------
//  
//  InitializeWizardPages
//
//----------------------------------------------------------------------
void InitializeWizardPages()
{
	int nPageIndex;

	// WELCOME
	nPageIndex = Wizard->AddPage( new CPropPage(	
			TEXT("Welcome to the NewSID Wizard"),
			TEXT("NewSID allows you to change the machine SID of a computer."),
			TEXT("PAGE_WELCOME"), 
			StartPage ) );

	Wizard->GetPage( nPageIndex )->SetType( CPropPage::ENTRY );
	Wizard->SetStartPage( nPageIndex );

	// CHOOSE
	nPageIndex = Wizard->AddPage( new CPropPage(	
			TEXT("Choose a SID"),
			TEXT("You can specify the SID that you want NewSID to apply."),
			TEXT("PAGE_CHOOSESID"), 
			ChoosePage ) );
	Wizard->GetPage( nPageIndex )->SetType( CPropPage::INTERNAL );

	// RENAME
	nPageIndex = Wizard->AddPage( new CPropPage(	
			TEXT("Rename the Computer"),
			TEXT("You can have NewSID rename the computer when it changes the SID."),
			TEXT("PAGE_RENAMECOMPUTER"), 
			RenamePage ) );
	Wizard->GetPage( nPageIndex )->SetType( CPropPage::INTERNAL );

	// READY
	nPageIndex = Wizard->AddPage( new CPropPage(	
			TEXT("Ready to Apply SID"),
			TEXT("NewSID is ready to apply the selected SID."),
			TEXT("PAGE_READY"), 
			ReadyPage ) );
	Wizard->GetPage( nPageIndex )->SetType( CPropPage::INTERNAL );
	
	// FINISH
	nPageIndex = Wizard->AddPage( new CPropPage(	
			TEXT("The SID Has Been Applied"),
			TEXT("The NewSID Wizard has completed."),
			TEXT("PAGE_FINISHED"), 
			FinishPage ) );
	Wizard->GetPage( nPageIndex )->SetType( CPropPage::FINISH );
}


//----------------------------------------------------------------------
//  
//  Run
//
//----------------------------------------------------------------------
bool Run()
{
    MSG	msg;
    HWND hwndWizard = NULL;

    // Instantiate wizard 
    Wizard = new CWizardWindow( hInst, TEXT("NewSID - Sysinternals: www.sysinternals.com") );
    
    if ( Wizard == NULL )
        return false;
    
    // Initialize wizard pages
    InitializeWizardPages();

    // Display the wizard
    hwndWizard = Wizard->Run();
    
    while ( GetMessage( &msg, NULL, 0, 0 ) )  
    {
        if ( Wizard->GetCurrentDlg() )  
        {
            if ( IsDialogMessage( Wizard->GetCurrentDlg(), &msg ) )
                continue;
        }
        
        if ( TranslateMessage( &msg ) )
            continue;
        
        DispatchMessage( &msg );
    }
    
    // Destroy wizard
    delete Wizard;
    Wizard = NULL;

    return true;
}

//-------------------------------------------------------------------
//
// Usage
// 
// Displays correct usage syntax.
//
//-------------------------------------------------------------------
BOOLEAN Usage( PTCHAR ImageName )
{
	MessageBox( NULL, 
			_T("Invalid arguments.\n\nUsage: NewSid [/a [[/n]|[/d <reboot delay (in seconds)>]] [<new computer name>]\n")
						_T("  /a    Run without prompts.\n")
						_T("  /n    Don't reboot after automatic run."), 
			APPNAME, MB_ICONERROR|MB_OK );
	return FALSE;
}


//-------------------------------------------------------------------
//
// ProcessArguments
//
// Parses the command-line arguments.
//
//-------------------------------------------------------------------
BOOLEAN ProcessArguments( DWORD argc, TCHAR *argv[],
							PBOOLEAN Auto,
							PBOOLEAN NoReboot,
							PDWORD RebootDelay )
{
	DWORD		i, j;
	BOOLEAN		skipArgument;

	*NoReboot = FALSE;
	*Auto = FALSE;
	*RebootDelay = 0;
	for( i = 1; i < argc; i++ ) {

		skipArgument = FALSE;
		switch( argv[i][0] ) {

		case '-':
		case '/':

			j = 1;
			while( argv[i][j] ) {

				switch( _toupper( argv[i][j]) ) {

				case 'A':
					if( *Auto) return Usage( argv[0] );
					*Auto = TRUE;
					break;

				case 'N':
					if( *NoReboot || *Auto == FALSE ) return Usage( argv[0] );
					*NoReboot = TRUE;
					break;

				case 'D':
					if( *RebootDelay && *Auto == FALSE || *NoReboot == TRUE) return Usage( argv[0] );
					if( i+1 < argc && 
						isdigit( argv[i+1][0] )) {

						*RebootDelay = _ttoi( argv[i+1] );
						i++;
						skipArgument = TRUE;
					}
					break;

				default:

					return Usage( argv[0] );
					break;
				}
				if( skipArgument ) break;
				j++;
			}
			break;

		default:
			
			if( RenameComputer ) return Usage( argv[0] );
			RenameComputer = TRUE;
			_tcscpy( NewComputerName, _tcsupr( argv[i] ));
			break;
		}
	}
	return TRUE;
}

//----------------------------------------------------------------------
//  
// WinMain
//
//----------------------------------------------------------------------
int CALLBACK WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
        				LPSTR lpCmdLine, int nCmdShow )
{
	HANDLE				Client;
	BOOLEAN				autoRun, noReboot;
	DWORD				rebootDelay;
	TOKEN_PRIVILEGES  *	Priv;
	TCHAR				buf[ 1024 ];
	int					j, argc;
	LPWSTR *			argv;	
	static TCHAR	*	P[] = {
		SE_TAKE_OWNERSHIP_NAME,
		SE_SECURITY_NAME,
		SE_BACKUP_NAME,
		SE_RESTORE_NAME,
		SE_MACHINE_ACCOUNT_NAME,
		SE_CHANGE_NOTIFY_NAME 
	};

	// 
	// Give ourselves privileges for manipulating security
	// 
	if( ! OpenProcessToken( GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Client ) ) {

		DisplayError( L"NewSID was unable to obtain necessary privilege to change the computer SID");
		return 1;
	}
	Priv = (PTOKEN_PRIVILEGES) buf;
	Priv->PrivilegeCount = 0;
	for( j = 0; j < sizeof P/sizeof P[0]; ++j )  {

		Priv->PrivilegeCount += 1;
		if ( ! LookupPrivilegeValue( NULL, P[j], &Priv->Privileges[j].Luid ) ) {
			DisplayError( L"NewSID was unable to obtain necessary privilege to change the computer SID");
			return FALSE;
		}
		Priv->Privileges[j].Attributes	= SE_PRIVILEGE_ENABLED;
	}
	if( ! AdjustTokenPrivileges( Client, FALSE, Priv, 0, NULL, NULL ) ) {

		DisplayError(  L"NewSID was unable to obtain necessary privilege to change the computer SID" );
		return 2;
	}

	// 
	// do init stuff
	// 
	InitCommonControls();
	hInst = hInstance;

	argv = CommandLineToArgvW( GetCommandLine(), &argc );
	if( !ProcessArguments( argc, argv, &autoRun, &noReboot, &rebootDelay )) {

		return 3;
	}

	//
	// Run automatically or with a wizard
	//
	if( autoRun ) {

		AdminSecDesc = GetMachineSid();
		if( AdminSecDesc == NULL ) {

			return 4;
		}
		GenerateRandomSid( NULL );
		DoSidChange( NULL );

		if( noReboot == FALSE ) {

			Sleep( rebootDelay * 1000 );
			ShutDown();
		}

	} else {

		//
		// Execute the wizard
		//
		Run();
	}
	return 0;
}

