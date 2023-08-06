//======================================================================
//
// DiskExt
//
// Copyright (C) 2001 Mark Russinovich
// Sysinternals - www.sysinternals.com
//
// This file demonstrates the use of 
// IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS to obtain a volume's 
// areas (extents) on the disks that it occupies. The Platform SDK 
// says this IOCTL is only available on Whistler (Windows XP), but it
// works on Win2K as well.
//
//======================================================================
#include <windows.h>
#include <tchar.h>
#include <basetyps.h>
#include <stdlib.h>
#include <wtypes.h>
#include <stdio.h>
#include <string.h>
#include <winioctl.h>


//--------------------------------------------------------------------
//
// PrintWin32Error
// 
// Translates a Win32 error into a text equivalent
//
//--------------------------------------------------------------------
void PrintWin32Error( DWORD ErrorCode )
{
	LPVOID lpMsgBuf;
	FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
					NULL, ErrorCode, 
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					(LPTSTR) &lpMsgBuf, 0, NULL );
	_tprintf(_T("%s\n"), lpMsgBuf );
	LocalFree( lpMsgBuf );
}


//----------------------------------------------------------------------
//
// DumpDiskExtents
//
// Dumps the extents for the specified volume.
//
//----------------------------------------------------------------------
VOID DumpDiskExtents( TCHAR DriveLetter )
{
	TCHAR	volumePath[MAX_PATH];
	HANDLE	hVolume;
	ULONG	extent;
	ULONG	bytesWritten;
	UCHAR	DiskExtentsBuffer[0x400];
	PVOLUME_DISK_EXTENTS DiskExtents = (PVOLUME_DISK_EXTENTS)DiskExtentsBuffer;
	
	//
	// Open the volume
	//
	_stprintf( volumePath, _T("\\\\.\\%c:"), DriveLetter );
	hVolume = CreateFile( volumePath,
					GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, 
					NULL, OPEN_EXISTING, 0, NULL );
	if( hVolume == INVALID_HANDLE_VALUE ) {

		_tprintf( _T("Error getting extents for drive %C:\n"), DriveLetter );
		PrintWin32Error( GetLastError());
		return;
	}
    _tprintf(_T("Extents for %C:\n"), DriveLetter );
        
	//
	// Get the extents
	//
    if( DeviceIoControl( hVolume,
                IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                NULL, 0,
                DiskExtents, sizeof(DiskExtentsBuffer),
                &bytesWritten, NULL ) ) {
        
		//
		// Dump the extents
		//
		for( extent = 0; extent < DiskExtents->NumberOfDiskExtents; extent++ ) {

			_tprintf(_T("   Extent [%d]:\n"), extent + 1 );
			_tprintf(_T("       Disk:   %d\n"), 
						DiskExtents->Extents[extent].DiskNumber);
			_tprintf(_T("       Offset: %I64d\n"), 
						DiskExtents->Extents[extent].StartingOffset );
			_tprintf(_T("       Length: %I64d\n"), 
						DiskExtents->Extents[extent].ExtentLength );
		}
	}
	CloseHandle( hVolume );
}


//----------------------------------------------------------------------
//
// Usage
// 
//----------------------------------------------------------------------
DWORD Usage( VOID )
{
	_tprintf(_T("diskext [drive1 [drive2] ...]\n"));
	_tprintf(_T("    Drive1 and drive2 are drive letters.\n"));
	_tprintf(_T("    Ommiting drives dumps extents for all drives.\n\n"));
	return (DWORD) -1;
}


//----------------------------------------------------------------------
//
// Main
//
// Print banner, get arguments, and dump extents.
//
//----------------------------------------------------------------------
DWORD _tmain( DWORD argc, PTCHAR argv[])
{
	DWORD		i;
	TCHAR		driveRoot[] = _T("X:\\");
	TCHAR		driveLetter;

    //
    // Print banner
    //
    wprintf(L"\nDisk Extent Dumper v1.0\n" );
    wprintf(L"Copyright (C) 2001 Mark Russinovich\n");
    wprintf(L"Sysinternals - www.sysinternals.com\n\n");

	//
	// Make sure that we're on Win2k or higher
	//
	if( (DWORD)(LOBYTE(LOWORD(GetVersion()))) < 5 ) {

		_tprintf(_T("DiskExt requires Win2K or higher.\n\n"));
		return (DWORD) -1;
	} 

	//
	// Go through arguments. If none, dump extents for all disks
	//
	if( argc > 1 ) {

		//
		// First, make sure that arguments are valid
		//
		for( i = 1 ; i < argc; i++ ) {

			if( argv[i][1] ||
				(_toupper( argv[i][0]) <= _T('A') &&
				_toupper( argv[i][0]) >= _T('Z'))) {

				return Usage();
			}
		}

		//
		// Now dump the extents
		//
		for( i = 1 ; i < argc; i++ ) {

			DumpDiskExtents( _toupper( argv[i][0] ));
		}

	} else {

		for( driveLetter = _T('A'); driveLetter <= _T('Z'); driveLetter++ ) {

			driveRoot[0] = driveLetter;
			if( GetDriveType( driveRoot ) == DRIVE_FIXED ) { 

				DumpDiskExtents( driveLetter );
			}
		}
	}
	return 0;
}
