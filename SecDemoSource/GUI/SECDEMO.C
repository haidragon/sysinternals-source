//====================================================================
//
// SecDemo.c
//
// Copyright (C) 1998 Mark Russinovich
//
// Works with the security demo device driver to show default
// device object security (viewable with WinObj V2.0 - available
// at NTInternals), and then permissions after the driver has
// removed World R/W.
//
//====================================================================
#include <windows.h>
#include <stdio.h>
#include <winioctl.h>
#include <conio.h>
#include "..\dd\ioctlcmd.h"
#include "secdemo.h"

//--------------------------------------------------------------------
//							 G L O B A L S
//--------------------------------------------------------------------

//
// Variables/definitions for the driver that performs 
//
#define				SYS_FILE		TEXT("SECSYS.SYS")
#define				SYS_NAME		TEXT("SECSYS")
static HANDLE		sys_handle		= INVALID_HANDLE_VALUE;



//--------------------------------------------------------------------
//							F U N C T I O N S 
//--------------------------------------------------------------------



//----------------------------------------------------------------------
//
// PrintError
//
// Formats an error message for the last error
//
//----------------------------------------------------------------------
void PrintError()
{
	char *errMsg;

	FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			NULL, GetLastError(), 
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 
			(LPTSTR) &errMsg, 0, NULL );
	printf("%s\n", errMsg );
	LocalFree( errMsg );
}


//----------------------------------------------------------------------
//  
// DriverConnect
//
// If under NT 4.0, we need to establish system call hooking in order
// to make the changeover.
//
//----------------------------------------------------------------------
BOOL DriverConnect()
{
	TCHAR		Path[256];
	TCHAR		*File;

	// open the handle to the device
	if( !SearchPath( NULL, SYS_FILE, NULL, sizeof(Path), Path, &File ) ) {

		// that failed, last ditch - try current directory again.
		GetCurrentDirectory( sizeof Path, Path );
		wsprintf( Path+lstrlen(Path), TEXT("\\%s"), SYS_FILE );
	}
	if ( ! LoadDeviceDriver( SYS_NAME, Path, &sys_handle ) )  {

		// unload it in case the driver moved 
		UnloadDeviceDriver( SYS_NAME );

		if ( ! LoadDeviceDriver( SYS_NAME, Path, &sys_handle ) )  {

			printf("Unable to load driver: ");
			PrintError();
			return FALSE;
		}
	}	
	return TRUE;
}

//----------------------------------------------------------------------
//  
// DriverDisconnect
//
// If under NT 4.0, we need to establish system call hooking in order
// to make the changeover.
//
//----------------------------------------------------------------------
void DriverDisconnect()
{
	if( sys_handle != INVALID_HANDLE_VALUE ) {
		CloseHandle( sys_handle );			
		if ( ! UnloadDeviceDriver( SYS_NAME ) )  {
			printf("Error unloading driver: ");
			PrintError();
		}	
	}
}

//----------------------------------------------------------------------
//  
// CallDriver
//
// Sends a DevIoControl to the driver along with some data if necessary.
//
//----------------------------------------------------------------------
BOOL CallDriver( int Msg, PBYTE InData, int InDataLen, 
				PBYTE OutData, int OutDataLen )
{
	int			nb;
	
	if ( ! DeviceIoControl(	sys_handle, Msg,
							InData, InDataLen, OutData, OutDataLen, &nb, NULL ) )
	{
		return FALSE;
	}
	return TRUE;
}


//--------------------------------------------------------------------
//
// Main
//
//--------------------------------------------------------------------
int main( int argc, char *argv[] )
{
	char	input[256];

	//
	// Print title
	//
	printf("\nDevice Object Security Demo V1.0\n");
	printf("Copyright (C) 1998 Mark Russinovich\n");
	printf("http://www.ntinternals.com\n\n");

	printf("This program demonstrates default device object security and\n"
		"the secsys driver, which removes Everyone R/W access from its\n"
		"device object.\n\n");

	//
	// get NT version
	//
	if( GetVersion() >= 0x80000000 ) {
		printf("Not running on Windows NT.\n");
		return -1;
	}

	//
	// Connect to driver
	//
	if( !DriverConnect()) {

		DriverDisconnect();
		return -1;
	}

	printf("SecSys driver is loaded. View its (\\device\\secsys.sys)\n"
		"permissions with Winobj and then press enter to have\n"
		"Everyone R/W access removed: ");
	fflush( stdout );
	gets( input );

	//
	// Tell the driver to remove world R/W
	//
	CallDriver( SECDEMO_IOCTL, NULL, 0, NULL, 0 );

	printf("\nEveryone R/W permission removed. View new permissions with\n"
		"Winobj (close and reselect the secsys device properties) and\n"
		"then press enter to end: ");
	fflush( stdout );
	gets( input );

	DriverDisconnect();
	return 0;
}
