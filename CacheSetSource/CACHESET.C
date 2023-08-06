/******************************************************************************
*
*       Cacheset - Cache working set controller
*		
*		Copyright (c) 1997 Mark Russinovich
*		http://www.ntinternals.com
*
*    	PROGRAM: Cacheset.c
*
******************************************************************************/
#include <windows.h>
#include <stdio.h>
#include <process.h>
#include <commctrl.h>
#include <winioctl.h>
#include "resource.h"

//
// System cache information class
//
#define SYSTEMCACHEINFORMATION 0x15

//
// Used to get cache information
//
typedef struct {
	ULONG    	CurrentSize;
	ULONG    	PeakSize;
	ULONG    	PageFaultCount;
	ULONG    	MinimumWorkingSet;
	ULONG    	MaximumWorkingSet;
	ULONG    	Unused[4];
} SYSTEM_CACHE_INFORMATION;

//
// Calls to get and set the information
//
ULONG (__stdcall *NtQuerySystemInformation)( 
				ULONG SystemInformationClass, 
				PVOID SystemInformation, 
				ULONG SystemInformationLength,
				PULONG ReturnLength 
				);

ULONG (__stdcall *NtSetSystemInformation)( 
				ULONG SystemInformationClass, 
				PVOID SystemInformation, 
				ULONG SystemInformationLength
				); 

//
// instance
//
HINSTANCE			hInst;

//
// main window
//
HWND				hMainDlg;

//
// General buffer for storing temporary strings
//
static TCHAR		msgbuf[ 257 ];

//
// original cache settings
//
SYSTEM_CACHE_INFORMATION	ResetCache;

//
// forward define
//
void Abort( HWND hWnd, char * Msg );

//----------------------------------------------------------------------
//  
// CenterWindow
// 
// Centers the Window on the screen.
//
//----------------------------------------------------------------------
VOID CenterWindow( HWND hDlg )
{
	RECT            aRt;

	// center the dialog box
	GetWindowRect( hDlg, &aRt );
	OffsetRect( &aRt, -aRt.left, -aRt.top );
	MoveWindow( hDlg,
			((GetSystemMetrics( SM_CXSCREEN ) -
				aRt.right ) / 2 + 4) & ~7,
  			(GetSystemMetrics( SM_CYSCREEN ) -
				aRt.bottom) / 2,
			aRt.right, aRt.bottom, 0 );
}


//----------------------------------------------------------------------
//
// SetPrivilege
//
// Adjusts the token privileges.
//
//----------------------------------------------------------------------
BOOL SetPrivilege( HANDLE hToken, LPCTSTR Privilege, BOOL bEnablePrivilege  )
{
    TOKEN_PRIVILEGES tp;
    LUID luid;
    TOKEN_PRIVILEGES tpPrevious;
    DWORD cbPrevious=sizeof(TOKEN_PRIVILEGES);

    if(!LookupPrivilegeValue( NULL, Privilege, &luid )) return FALSE;

    //
    // first pass.  get current privilege setting
    //
    tp.PrivilegeCount           = 1;
    tp.Privileges[0].Luid       = luid;
    tp.Privileges[0].Attributes = 0;

    if( !AdjustTokenPrivileges(
            hToken,
            FALSE,
            &tp,
            sizeof(TOKEN_PRIVILEGES),
            &tpPrevious,
            &cbPrevious
            )) {

		return FALSE;
	}

    //
    // second pass.  set privilege based on previous setting
    //
    tpPrevious.PrivilegeCount       = 1;
    tpPrevious.Privileges[0].Luid   = luid;
	tpPrevious.Privileges[0].Attributes  = 0;

    if(bEnablePrivilege) {
        tpPrevious.Privileges[0].Attributes |= (SE_PRIVILEGE_ENABLED);
    }
    else {
        tpPrevious.Privileges[0].Attributes ^= (SE_PRIVILEGE_ENABLED &
            tpPrevious.Privileges[0].Attributes);
    }

    return AdjustTokenPrivileges(
            hToken,
            FALSE,
            &tpPrevious,
            cbPrevious,
            NULL,
            NULL
            );
}


//----------------------------------------------------------------------
//  
// Abort
//
// If there is a fatal problem, pop up a message box and die. Assumes
// that it is called from the dialog box.
//
//----------------------------------------------------------------------
void Abort( HWND hWnd, char * Msg )
{
	MessageBox( hWnd, Msg, "Cacheset", MB_ICONERROR|MB_OK );
	if( hWnd ) EndDialog( hWnd, 1 );
	PostQuitMessage( 1 );
}


//----------------------------------------------------------------------
//  
// MainDialog
//
// This is the main window. 
//
//----------------------------------------------------------------------
LONG APIENTRY MainDialog( HWND hDlg, UINT message, UINT wParam,
                       		LONG lParam ) {
	TCHAR					text[16];
	SYSTEM_CACHE_INFORMATION	newCacheSize;
	ULONG					returnLength;

	switch (message) {
	case WM_INITDIALOG :

		// center the window
		CenterWindow( hDlg );

		// get the current settings from the driver
		if( NtQuerySystemInformation( SYSTEMCACHEINFORMATION,
									   &ResetCache, sizeof(ResetCache),
									   &returnLength ) ) {
			MessageBox( hDlg, TEXT("Could not obtain cache settings"), "Cacheset", MB_ICONWARNING|MB_OK );
			return 0;
		}

		wsprintf( text, TEXT("%d"), ResetCache.MinimumWorkingSet *4 );
		SetDlgItemText( hDlg, IDC_MINIMUM, text );
		wsprintf( text, TEXT("%d"), ResetCache.MaximumWorkingSet *4 );
		SetDlgItemText( hDlg, IDC_MAXIMUM, text );

		wsprintf( text, TEXT("%d KB"), ResetCache.CurrentSize / 1024);
		SetDlgItemText( hDlg, IDC_CURRENT, text );
		wsprintf( text, TEXT("%d KB"), ResetCache.PeakSize / 1024 );
		SetDlgItemText( hDlg, IDC_PEAK, text );


		// Start up timer to periodically update screen
		SetTimer( hDlg,	1, 500/*ms*/, NULL );
		return 0;

	case WM_COMMAND:
		switch (LOWORD( wParam )) {
		
		case IDDEFAULT:

			newCacheSize.MinimumWorkingSet = 4* ResetCache.MinimumWorkingSet * 1024;
			newCacheSize.MaximumWorkingSet = 4* ResetCache.MaximumWorkingSet * 1024;
			if( NtSetSystemInformation( SYSTEMCACHEINFORMATION,
					&newCacheSize, sizeof( newCacheSize)) ) {

				MessageBox( hDlg, TEXT("Default settings were out of valid ranges"), "Cacheset", MB_ICONWARNING|MB_OK );
				return 0;
			}

			// get the current settings from the driver
			if( NtQuerySystemInformation( SYSTEMCACHEINFORMATION,
										   &newCacheSize, sizeof(newCacheSize),
										   &returnLength ) ) {
				MessageBox( hDlg, TEXT("Could not obtain cache settings"), "Cacheset", MB_ICONWARNING|MB_OK );
				return 0;
			}

			wsprintf( text, TEXT("%d"), newCacheSize.MinimumWorkingSet *4 );
			SetDlgItemText( hDlg, IDC_MINIMUM, text );
			wsprintf( text, TEXT("%d"), newCacheSize.MaximumWorkingSet *4 );
			SetDlgItemText( hDlg, IDC_MAXIMUM, text );

			wsprintf( text, TEXT("%d KB"), newCacheSize.CurrentSize / 1024);
			SetDlgItemText( hDlg, IDC_CURRENT, text );
			wsprintf( text, TEXT("%d KB"), newCacheSize.PeakSize / 1024 );
			SetDlgItemText( hDlg, IDC_PEAK, text );

			MessageBox( hDlg, TEXT("Previous cache settings applied"), "Cacheset", MB_ICONINFORMATION|MB_OK );
			break;

		case IDCLEAR:

			// clear the cache working set
			newCacheSize.MinimumWorkingSet = (ULONG) -1;
			newCacheSize.MaximumWorkingSet = (ULONG) -1;
			if( NtSetSystemInformation( SYSTEMCACHEINFORMATION,
					&newCacheSize, sizeof( newCacheSize)) ) {

				MessageBox( hDlg, TEXT("Cache clear settings were out of valid ranges"), "Cacheset", MB_ICONWARNING|MB_OK );
				return 0;
			}

			// get the current settings from the driver
			if( NtQuerySystemInformation( SYSTEMCACHEINFORMATION,
										   &newCacheSize, sizeof(newCacheSize),
										   &returnLength ) ) {
				MessageBox( hDlg, TEXT("Could not obtain cache settings"), "Cacheset", MB_ICONWARNING|MB_OK );
				return 0;
			}

			wsprintf( text, TEXT("%d"), newCacheSize.MinimumWorkingSet *4 );
			SetDlgItemText( hDlg, IDC_MINIMUM, text );
			wsprintf( text, TEXT("%d"), newCacheSize.MaximumWorkingSet *4 );
			SetDlgItemText( hDlg, IDC_MAXIMUM, text );

			wsprintf( text, TEXT("%d KB"), newCacheSize.CurrentSize / 1024);
			SetDlgItemText( hDlg, IDC_CURRENT, text );
			wsprintf( text, TEXT("%d KB"), newCacheSize.PeakSize / 1024 );
			SetDlgItemText( hDlg, IDC_PEAK, text );

			MessageBox( hDlg, TEXT("Cache working set cleared"), "Cacheset", MB_ICONINFORMATION|MB_OK );
			break;

		case IDAPPLY:	

			GetDlgItemText( hDlg, IDC_MINIMUM, text, 16 );
			newCacheSize.MinimumWorkingSet = atoi( text ) * 1024;
			GetDlgItemText( hDlg, IDC_MAXIMUM, text, 16 );
			newCacheSize.MaximumWorkingSet = atoi( text ) * 1024;

			if( NtSetSystemInformation( SYSTEMCACHEINFORMATION,
					&newCacheSize, sizeof( newCacheSize)) ) {
				
				MessageBox( hDlg, TEXT("New settings were out of valid ranges"), "Cacheset", MB_ICONWARNING|MB_OK );
				return 0;
			}

			// get the current settings from the driver
			if( NtQuerySystemInformation( SYSTEMCACHEINFORMATION,
										   &newCacheSize, sizeof(newCacheSize),
										   &returnLength ) ) {

				MessageBox( hDlg, TEXT("Could not obtain cache settings"), "Cacheset", MB_ICONWARNING|MB_OK );
				return 0;
			}

			wsprintf( text, TEXT("%d"), newCacheSize.MinimumWorkingSet *4 );
			SetDlgItemText( hDlg, IDC_MINIMUM, text );
			wsprintf( text, TEXT("%d"), newCacheSize.MaximumWorkingSet *4 );
			SetDlgItemText( hDlg, IDC_MAXIMUM, text );

			wsprintf( text, TEXT("%d KB"), newCacheSize.CurrentSize / 1024);
			SetDlgItemText( hDlg, IDC_CURRENT, text );
			wsprintf( text, TEXT("%d KB"), newCacheSize.PeakSize / 1024 );
			SetDlgItemText( hDlg, IDC_PEAK, text );

			MessageBox( hDlg, TEXT("New cache settings applied"), "Cacheset", MB_ICONINFORMATION|MB_OK );
			break;

		case IDCANCEL:
			EndDialog (hDlg, 0) ;
			PostQuitMessage (0) ;
			break ;
		}
		break; 

	case WM_TIMER:

		// get the current settings 
		if( NtQuerySystemInformation( SYSTEMCACHEINFORMATION,
									   &newCacheSize, sizeof(newCacheSize),
									   &returnLength ) ) {
			MessageBox( hDlg, TEXT("Could not obtain cache settings"), "Cacheset", MB_ICONWARNING|MB_OK );
			return 0;
		}

		wsprintf( text, TEXT("%d KB"), newCacheSize.CurrentSize / 1024);
		SetDlgItemText( hDlg, IDC_CURRENT, text );
		wsprintf( text, TEXT("%d KB"), newCacheSize.PeakSize / 1024 );
		SetDlgItemText( hDlg, IDC_PEAK, text );
		break;


	case WM_CLOSE:	
		EndDialog (hDlg, 0);
		PostQuitMessage( 0 );
		break;
	}
    return DefWindowProc ( hDlg, message, wParam, lParam);
}


//--------------------------------------------------------------------
//
// LocateNDLLCalls
//
// Loads function entry points in NTDLL
//
//--------------------------------------------------------------------
BOOLEAN LocateNTDLLCalls()
{

 	if( !(NtQuerySystemInformation = (void *) GetProcAddress( GetModuleHandle("ntdll.dll"),
			"NtQuerySystemInformation" )) ) {

		return FALSE;
	}

 	if( !(NtSetSystemInformation = (void *) GetProcAddress( GetModuleHandle("ntdll.dll"),
			"NtSetSystemInformation" )) ) {

		return FALSE;
	}
	return TRUE;
}


//----------------------------------------------------------------------
//  
// DummyProc
//
// This is the Wnd proc for a dummy window we use as the parent
// to our real window. This makes it so that we don't get a stupid
// icon on the task bar!
//
//----------------------------------------------------------------------
LONG APIENTRY DummyProc( HWND hDlg, UINT message, UINT wParam,
                       		LONG lParam ) {

	if( message == WM_CREATE || message == WM_DESTROY )
		return 0;
	else 
		return DefWindowProc ( hDlg, message, wParam, lParam);
}


//----------------------------------------------------------------------
//  
// WinMain
//
// Registers a class. The reason I did this is because the application
// doesn't get cleaned up properly upon exit if winmain just calls
// dialogbox. This was manifested by all the system icons disappearing
// after the program exited. See Petzold's hexcalc example for the base
// of this.
//
//----------------------------------------------------------------------
int CALLBACK WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
        				LPSTR lpCmdLine, int nCmdShow )
{	  
	static TCHAR	szAppName [] = TEXT("CACHESET") ;
	MSG				msg ;
	WNDCLASSEX		wndclass ;
	HWND			hDummyWnd;
	int				NTVersion;
	int				len = 256;
	HANDLE			hToken;
	SYSTEM_CACHE_INFORMATION	newCacheSize;
	ULONG			minSize, maxSize;

	// save instance
	hInst = hInstance;

	// init common controls
	InitCommonControls();

	// get NT version
	NTVersion = GetVersion();
	if( NTVersion >= 0x80000000 ) {
		Abort( NULL, TEXT("Not running on Windows NT"));
		return TRUE;
	}

	// Enable increase quota privilege
    if(!OpenProcessToken( GetCurrentProcess(),
				TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
				&hToken )) {
	
		printf("You do not have the necessary privilege to run this program\n");
		return -1;
	}

    // Enable SeDebugPrivilege
    if(!SetPrivilege(hToken, SE_INCREASE_QUOTA_NAME, TRUE))
    {

 		Abort(NULL, TEXT("You must have the INCREASE_QUOTA privilege to run CacheSet"));
        CloseHandle(hToken);
        return -1;
    }
	CloseHandle( hToken );

	// locate the calls we need in NTDLL
	LocateNTDLLCalls();

	// If there are command line arguments, set the cache size
	if( lpCmdLine && (sscanf( lpCmdLine," %d %d", &minSize, &maxSize ) == 2) ) {

		// set the cache size 
		newCacheSize.MinimumWorkingSet = minSize * 1024;
		newCacheSize.MaximumWorkingSet = maxSize * 1024;
		NtSetSystemInformation( SYSTEMCACHEINFORMATION,
									   &newCacheSize, sizeof(newCacheSize) );
		return 0;
	}

	// create a dummy class and window that will hide the task tray icon for us
	wndclass.cbSize			= sizeof( WNDCLASSEX );
	wndclass.style          = 0 ;
 	wndclass.lpfnWndProc    = (WNDPROC) DummyProc ;
	wndclass.cbClsExtra     = 0 ;
	wndclass.cbWndExtra     = 0 ;
	wndclass.hInstance      = hInstance ;
	wndclass.hIcon          = LoadIcon (hInstance, "APPICON") ;
	wndclass.hIconSm		= LoadIcon (hInstance, "APPICON");
	wndclass.hCursor        = LoadCursor (NULL, IDC_ARROW) ;
	wndclass.hbrBackground  = (HBRUSH) (COLOR_BTNFACE+1);
	wndclass.lpszMenuName   = NULL ;
	wndclass.lpszClassName  = "Dummy" ;
	RegisterClassEx (&wndclass) ;

	hDummyWnd = CreateWindowEx( WS_EX_TOOLWINDOW, "Dummy", "",
								0, -1, -1, 0, 0, 0, 0, hInstance, 0 );

	// create the main window class
	wndclass.cbSize			= sizeof( WNDCLASSEX );
	wndclass.style          = CS_HREDRAW | CS_VREDRAW ;
 	wndclass.lpfnWndProc    = (WNDPROC) MainDialog ;
	wndclass.cbClsExtra     = 0 ;
	wndclass.cbWndExtra     = DLGWINDOWEXTRA ;
	wndclass.hInstance      = hInstance ;
	wndclass.hIcon          = LoadIcon (hInstance, "APPICON") ;
	wndclass.hIconSm		= LoadIcon (hInstance, "APPICON");
	wndclass.hCursor        = LoadCursor (NULL, IDC_ARROW) ;
	wndclass.hbrBackground  = (HBRUSH) (COLOR_BTNFACE+1);
	wndclass.lpszMenuName   = NULL ;
	wndclass.lpszClassName  = szAppName ;
	RegisterClassEx (&wndclass) ;
 
 	// create the dialog
	hMainDlg = CreateDialog( hInstance, "CACHESET", hDummyWnd, (DLGPROC)MainDialog) ;
	ShowWindow( hMainDlg, nCmdShow) ;
 
	while (GetMessage (&msg, NULL, 0, 0)) {
		if( !IsDialogMessage( hMainDlg, &msg )) {
          TranslateMessage (&msg) ;
          DispatchMessage (&msg) ;
		}
	}
	return msg.wParam ;
}


