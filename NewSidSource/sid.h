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

//
// Defines for security buffer sizes
//
#define SZ_REL_SD_BUF		8192
#define SZ_ABS_SD_BUF		8192

//
// Max length of path name displayed in
// progress dialogs
//
#define MAX_DLG_PATHLEN		55

//
// ProductType name - this isn't exact, but its
// good enough
//
#define PRODUCTTYPEPATH		L"System\\C"
#define PRODUCTTYPENAME		L"ProductOptions"

//
// Profile list path
//
#define PROFILELISTPATH L"Software\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList"

//
// Application name
//
#define APPNAME				_T("NewSID")

//
// Convenient string sizing macro
//
#define TLEN( _string )		((_tcslen( _string )+1) * sizeof(TCHAR))

//
// Values the main dialog can return, which indicate
// whether to proceed or not.
//
enum {
	DIALOG_OK,
	DIALOG_CANCEL,
};


//
// Undocumented Registry function
//
typedef struct {
	SHORT	Length;
	SHORT	MaximumLength;
	PWCHAR	Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

//
// Only available on Win2K+
//
typedef
BOOL 
(__stdcall *SETCOMPUTERNAMEEX) (
  COMPUTER_NAME_FORMAT NameType,  // name type 
  LPCTSTR lpBuffer                // new name buffer 
);


//
// Externs
//

extern const DWORD		NewSidLength;
extern CHAR				NewSid[];
extern CHAR				OldSid[];
extern HINSTANCE		hInst;

//
// Random.cpp
//
DWORD Random( HWND ProgressBar );

//
// Registry.cpp
//
PSECURITY_DESCRIPTOR GetRegAccess (HKEY hKey);
LONG SetRegAccess (HKEY hKey, LPCTSTR lpSubKey,
					PSECURITY_DESCRIPTOR SecDesc, PHKEY phKey);
PSECURITY_DESCRIPTOR GetRegSecDesc (HKEY Root, TCHAR *Path, 
									SECURITY_INFORMATION Information);
BOOL ChangeComputerSID( HKEY Root, TCHAR *path, PSECURITY_DESCRIPTOR newSecDesc );
void UpdateRegistrySID( HWND hDlg, PSECURITY_DESCRIPTOR newSecDesc );

//
// File system
//
void UpdateFileSystemSID( HWND hDlg );

//
// Sid.cpp
//
BOOLEAN SecurityReplaceSID( PSID pSid );
// remove:
void UpdateProgressDialog( HWND hDlg, DWORD Control, PTCHAR PathName ); 
BOOL CALLBACK UpdateDlgProc (HWND hDlg, UINT message, UINT wParam,
                       		LONG lParam);