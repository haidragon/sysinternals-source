//======================================================================
//
// Chkdskx
//
// By Mark Russinovich
// Systems Internals
// http://www.sysinternals.com
//
// Chkdsk clone that demonstrates the use of the FMIFS file system
// utility library.
//
//======================================================================
#include <windows.h>
#include <stdio.h>
#include "..\fmifs.h"
#define _UNICODE 1
#include "tchar.h"

//
// Globals
//
BOOL	Error = FALSE;

// switches
BOOL	FixErrors = FALSE;
BOOL	SkipClean = FALSE;
BOOL	ScanSectors = FALSE;
BOOL	Verbose = FALSE;
PWCHAR  Drive = NULL;
WCHAR	  CurrentDirectory[1024];

//
// FMIFS function
//
PCHKDSK   Chkdsk;


//----------------------------------------------------------------------
//
// PrintWin32Error
//
// Takes the win32 error code and prints the text version.
//
//----------------------------------------------------------------------
void PrintWin32Error( PWCHAR Message, DWORD ErrorCode )
{
	LPVOID lpMsgBuf;
 
	FormatMessageW( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
					NULL, ErrorCode, 
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					(PWCHAR) &lpMsgBuf, 0, NULL );
	_tprintf(L"%s: %s\n", Message, lpMsgBuf );
	LocalFree( lpMsgBuf );
}

//--------------------------------------------------------------------
//
// CtrlCIntercept
//
// Intercepts Ctrl-C's so that the program can't be quit with the
// disk in an inconsistent state.
//
//--------------------------------------------------------------------
BOOL WINAPI CtrlCIntercept( DWORD dwCtrlType )
{
	//
	// Handle the event so that the default handler doesn't
	//
	return TRUE;
}


//----------------------------------------------------------------------
// 
// Usage
//
// Tell the user how to use the program
//
//----------------------------------------------------------------------
VOID Usage( PWCHAR ProgramName )
{
	_tprintf(L"Usage: %s [drive:] [-F] [-V] [-R] [-C]\n\n");
	_tprintf(L"  [drive:]    Specifies the drive to check.\n");
	_tprintf(L"  -F          Fixes errors on the disk.\n");
	_tprintf(L"  -V          Displays the full path of every file on the disk.\n");
	_tprintf(L"  -R          Locates bad sectors and recovers readable information.\n");
	_tprintf(L"  -C          Checks the drive only if it is dirty.\n");
	_tprintf(L"\n");
}


//----------------------------------------------------------------------
//
// ParseCommandLine
//
// Get the switches.
//
//----------------------------------------------------------------------
int ParseCommandLine( int argc, WCHAR *argv[] )
{
	int i;
	BOOLEAN gotFix = FALSE;
	BOOLEAN gotVerbose = FALSE;
	BOOLEAN gotClean = FALSE;
	BOOLEAN gotScan = FALSE;


	for( i = 1; i < argc; i++ ) {

		switch( argv[i][0] ) {

		case '-':
		case '/':

			switch( argv[i][1] ) {

			case L'F':
			case L'f':

				if( gotFix ) return i;
				FixErrors = TRUE;
				gotFix = TRUE;
				break;

			case L'V':
			case L'v':

				if( gotVerbose) return i;
				Verbose = TRUE;
				gotVerbose = TRUE;
				break;

			case L'R':
			case L'r':

				if( gotFix ) return i;
				ScanSectors = TRUE;
				gotFix = TRUE;
				break;

			case L'C':
			case L'c':

				if( gotClean ) return i;
				SkipClean = TRUE;
				gotClean = TRUE;
				break;

			default:
				return i;
			}
			break;

		default:

			if( Drive ) return i;
			if( argv[i][1] != L':' ) return i;

			Drive = argv[i];
			break;
		}
	}
	return 0;
}


//----------------------------------------------------------------------
//
// ChkdskCallback
//
// The file system library will call us back with commands that we
// can interpret. If we wanted to halt the chkdsk we could return FALSE.
//
//----------------------------------------------------------------------
BOOLEAN __stdcall ChkdskCallback( CALLBACKCOMMAND Command, DWORD Modifier, PVOID Argument )
{
	PDWORD percent;
	PBOOLEAN status;
	PTEXTOUTPUT output;

	// 
	// We get other types of commands, but we don't have to pay attention to them
	//
	switch( Command ) {

	case PROGRESS:
		percent = (PDWORD) Argument;
		_tprintf(L"%d percent completed.\r", *percent);
		break;

	case OUTPUT:
		output = (PTEXTOUTPUT) Argument;
		fprintf(stdout, "%s", output->Output);
		break;

	case DONE:
		status = (PBOOLEAN) Argument;
		if( *status == TRUE ) {

			_tprintf(L"Chkdsk was unable to complete successfully.\n\n");
			Error = TRUE;
		}
		break;
	}
	return TRUE;
}


//----------------------------------------------------------------------
//
// LoadFMIFSEntryPoints
//
// Loads FMIFS.DLL and locates the entry point(s) we are going to use
//
//----------------------------------------------------------------------
BOOLEAN LoadFMIFSEntryPoints()
{
	LoadLibrary( "fmifs.dll" );

	if( !(Chkdsk = (void *) GetProcAddress( GetModuleHandle( "fmifs.dll"),
			"Chkdsk" )) ) {

		return FALSE;
	}
	return TRUE;
}


//----------------------------------------------------------------------
// 
// WMain
//
// Engine. Just get command line switches and fire off a chkdsk. This 
// could also be done in a GUI like Explorer does when you select a 
// drive and run a check on it.
//
// We do this in UNICODE because the chkdsk command expects PWCHAR
// arguments.
//
//----------------------------------------------------------------------
int wmain( int argc, WCHAR *argv[] )
{
	int badArg;
	HANDLE volumeHandle;
	WCHAR fileSystem[1024];
	WCHAR volumeName[1024];
	DWORD serialNumber;
	DWORD flags, maxComponent;

	_tprintf(L"\nChkdskx v1.0 by Mark Russinovich\n");
	_tprintf(L"Systems Internals - http://www.sysinternals.com\n\n");

	//
	// Get function pointers
	//
	if( !LoadFMIFSEntryPoints()) {

		_tprintf(L"Could not located FMIFS entry points.\n\n");
		return -1;
	}

	//
	// Parse command line
	//
	if( (badArg = ParseCommandLine( argc, argv ))) {

		_tprintf(L"Unknown argument: %s\n", argv[badArg] );

		Usage(argv[0]);
		return -1;
	}

	// 
	// Get the drive's format
	//
	if( !Drive ) {

		if( !GetCurrentDirectoryW( sizeof(CurrentDirectory), CurrentDirectory )) {

			PrintWin32Error( L"Could not get current directory", GetLastError());
			return -1;
		}

	} else {

		wcscpy( CurrentDirectory, Drive );
	}
	CurrentDirectory[2] = L'\\';
	CurrentDirectory[3] = (WCHAR) 0;
	Drive = CurrentDirectory;

	//
	// Determine the drive's file system format, which we need to 
	// tell chkdsk
	//
	if( !GetVolumeInformationW( Drive, 
						volumeName, sizeof(volumeName), 
						&serialNumber, &maxComponent, &flags, 
						fileSystem, sizeof(fileSystem))) {

		PrintWin32Error( L"Could not query volume", GetLastError());
		return -1;
	}

	//
	// If they want to fix, we need to have access to the drive
	//
	if( FixErrors ) {

		swprintf( volumeName, L"\\\\.\\%C:", Drive[0] );
		volumeHandle = CreateFileW( volumeName, GENERIC_WRITE, 
						0, NULL, OPEN_EXISTING, 
						0, 0 );
		if( volumeHandle == INVALID_HANDLE_VALUE ) {

			_tprintf(L"Chdskx cannot run because the volume is in use by another process.\n\n");
			return -1;
		}
		CloseHandle( volumeHandle );

		//
		// Can't let the user break out of a chkdsk that can modify the drive
		//
		SetConsoleCtrlHandler( CtrlCIntercept, TRUE );
	}

	//
	// Just do it
	//
	_tprintf(L"The type of file system is %s.\n", fileSystem );
	Chkdsk( Drive, fileSystem, FixErrors, Verbose, SkipClean, ScanSectors,
				NULL, NULL, ChkdskCallback );

	if( Error ) return -1;
	return 0;
}