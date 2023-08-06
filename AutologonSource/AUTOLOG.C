//======================================================================
//
// Autoreset.c
//
// Copyright (C) 1996 Mark Russinovich
//
// This is a simple dialog applet that just manages the automatic
// logon entries in the registry.
// 
//======================================================================
#include <windows.h>
#include "resource.h"


// registry key names
TCHAR		LoginKeyName[] = "SOFTWARE\\Microsoft\\Windows NT\\"
							"CurrentVersion\\Winlogon";
TCHAR		AutoLogin[] = "AutoAdminLogon";
TCHAR		DefaultUserName[]  = "DefaultUserName";
TCHAR		DefaultDomain[]    = "DefaultDomainName";
TCHAR		DefaultPassword[]  = "DefaultPassword";

// application instance
HINSTANCE	hInst;



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
	MessageBox( hWnd, Msg, "Autologon", MB_ICONERROR|MB_OK );
	if( hWnd ) EndDialog( hWnd, 1 );
	PostQuitMessage( 1 );
}


//----------------------------------------------------------------------
//  
// About
//
// Pop about dialog.
//
//----------------------------------------------------------------------
BOOL APIENTRY About( HWND hDlg, UINT message, UINT wParam, LONG lParam )
{
	switch ( message )  {
	   case WM_INITDIALOG:
		  return TRUE;

	   case WM_COMMAND:              
		  if ( LOWORD( wParam ) == IDOK )	 {
			  EndDialog( hDlg, TRUE );
			  return TRUE;
		  }
		  break;

		case WM_CLOSE:	
			EndDialog (hDlg, 0);
			break;

	}
	return FALSE;   
}


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
// MainDialog
//
// This is the main window. 
//
//----------------------------------------------------------------------
LONG APIENTRY MainDialog( HWND hDlg, UINT message, UINT wParam,
                       		LONG lParam ) {
	TCHAR		UserName[256], Domain[256], Password[256];
	HKEY		hKey;
	DWORD		valuetype, len;
	static TCHAR szZero[] = "0";
 	static TCHAR szOne[] = "1";

	switch (message) {
	case WM_INITDIALOG :

		//
		// Init - read the current winlogon values from the registry and
		// put them in the dialog
		//

		CenterWindow( hDlg );

		// read the current values and put them in the dialog
		// reset auto-login key
		if( RegOpenKey( HKEY_LOCAL_MACHINE, LoginKeyName, &hKey ) != ERROR_SUCCESS) {
			Abort( NULL, TEXT("Could not open Winlogon registry key"));
			return TRUE;
		}

		// read user name
		len = 256;
		if( RegQueryValueEx( hKey, DefaultUserName, NULL, &valuetype, UserName, &len ) 
			!= ERROR_SUCCESS ) {
			UserName[0] = 0;
		}

		// read domain
		len = 256;
		if( RegQueryValueEx( hKey, DefaultDomain, NULL, &valuetype, Domain, &len ) 
			!= ERROR_SUCCESS ) {
			Domain[0] = 0;
		}

		// read password
		len = 256;
		if( RegQueryValueEx( hKey, DefaultPassword, NULL, &valuetype, Password, &len ) 
			!= ERROR_SUCCESS ) {
			Password[0] = 0;
		}

		// done. close key
		RegCloseKey( hKey );

		// initialize dialog
		SetDlgItemText( hDlg, IDC_LOGINNAME, UserName );
		SetDlgItemText( hDlg, IDC_DOMAINNAME, Domain );
		SetDlgItemText( hDlg, IDC_PASSWORD, Password );

		break;

	case WM_COMMAND :
		switch (LOWORD( wParam )) {
	
		case IDOK :

			//
			// OK - update the registry with the values present in the dialog
			//

			// read the values out of the dialog
			GetDlgItemText( hDlg, IDC_LOGINNAME, UserName, 256 );
			GetDlgItemText( hDlg, IDC_DOMAINNAME, Domain, 256 );
			GetDlgItemText( hDlg, IDC_PASSWORD, Password, 256 );

			// stick it all in the registry
			if( RegOpenKey( HKEY_LOCAL_MACHINE, LoginKeyName, &hKey ) != ERROR_SUCCESS) {
				Abort( NULL, TEXT("Could not open Winlogon registry key"));
				return TRUE;
			}

			// write user name
			if( RegSetValueEx( hKey, DefaultUserName, 0, REG_SZ, UserName, 
				strlen(UserName)+1 ) != ERROR_SUCCESS ) {
				Abort( NULL, TEXT("Could not write DefaultUserName"));
				return TRUE;
			}

			// write domain
			if( RegSetValueEx( hKey, DefaultDomain, 0, REG_SZ, Domain, 
				strlen(Domain)+1 ) != ERROR_SUCCESS ) {
				Abort( NULL, TEXT("Could not write DefaultDomain"));
				return TRUE;
			}

			// write password
			if( RegSetValueEx( hKey, DefaultPassword, 0, REG_SZ, Password, 
				strlen(Password)+1 ) != ERROR_SUCCESS ) {
				Abort( NULL, TEXT("Could not write DefaultPassword"));
				return TRUE;
			}

			// enable auto-logon
			if( RegSetValueEx( hKey, AutoLogin, 0, REG_SZ, szOne, strlen(szOne)+1 ) 
				!= ERROR_SUCCESS ) {
				RegCloseKey( hKey );
				Abort( NULL, TEXT("Could not reset AutoAdminLogon"));
				return TRUE;
			}
			MessageBox( NULL, "Autologon successfully configured.",
						 "AutoLogon", MB_ICONINFORMATION );

			EndDialog (hDlg, 0) ;
			PostQuitMessage (0) ;
			break;

		case IDCANCEL:

			//
			// Cancel - disable the autologon
			//

			// disable auto-login key
			if( RegOpenKey( HKEY_LOCAL_MACHINE, LoginKeyName, &hKey ) != ERROR_SUCCESS) {
				Abort( NULL, TEXT("Could not open Winlogon registry key"));
				return TRUE;
			}

			if( RegSetValueEx( hKey, AutoLogin, 0, REG_SZ, szZero, strlen(szZero)+1 ) 
				!= ERROR_SUCCESS ) {
				RegCloseKey( hKey );
				Abort( NULL, TEXT("Could not reset AutoAdminLogon"));
				return TRUE;
			}

			// done. close key
			RegCloseKey( hKey );
			MessageBox( NULL, "AutoLogon is disabled.",
							"Autologon", MB_ICONINFORMATION );
			EndDialog (hDlg, 0) ;
			PostQuitMessage (0) ;
			break ;
			
		case IDABOUT:
			DialogBox( hInst, TEXT("About"), hDlg, (DLGPROC)About );
			break;
		}
		break; 

	case WM_CLOSE:	
		EndDialog (hDlg, 0);
		PostQuitMessage( 0 );
		break;
	}
    return DefWindowProc ( hDlg, message, wParam, lParam);
}


//----------------------------------------------------------------------
//
// WinMain
//
// Initialize a dialog window class and pop the autologon dialog.
//
//----------------------------------------------------------------------
int WINAPI WinMain(	HINSTANCE hInstance, 
				   HINSTANCE hPrevInstance,	
				   LPSTR lpCmdLine,
				   int nCmdShow )
{
	static TCHAR	szAppName [] = TEXT("AUTOLOG") ;
	MSG				msg ;	   
	HWND			hMainDlg;
	WNDCLASSEX		wndclass ;
	int				NTVersion;

	hInst = hInstance;

	// get NT version
	NTVersion = GetVersion();
	if( NTVersion >= 0x80000000 ) {
		Abort( NULL, TEXT("Not running on Windows NT"));
		return TRUE;
	}

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
	hMainDlg = CreateDialog( hInstance, "AUTOLOG", NULL, (DLGPROC)MainDialog) ;
	ShowWindow( hMainDlg, nCmdShow) ;
 
	while (GetMessage (&msg, NULL, 0, 0)) {
		if( !IsDialogMessage( hMainDlg, &msg )) {
          TranslateMessage (&msg) ;
          DispatchMessage (&msg) ;
		}
	}
	return msg.wParam ;
}