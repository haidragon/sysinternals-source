//--------------------------------------------------------------------
//
// SDelete - Secure Delete
// Copyright (C) 1999 Mark Russinovich
// Systems Internals - http://www.sysinternals.com
//
// This program implements a secure delete function for 
// Windows NT/2K. It even works on WinNT compressed, encrypted 
// and sparse files.
//
// This program is copyrighted. You may not use the source, or 
// derived version of the source, in a secure delete application.
// You may use the source or techniques herein in applications
// with a purpose other than secure delete.
//
//--------------------------------------------------------------------
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <time.h>
#include "defrag.h"


//--------------------------------------------------------------------
//                         D E F I N E S
//--------------------------------------------------------------------

//
// Invalid longlong number
//
#define LLINVALID		((ULONGLONG) -1)

//
// Size of the buffer we read file mapping information into.
// The buffer is big enough to hold the 16 bytes that 
// come back at the head of the buffer (the number of entries 
// and the starting virtual cluster), as well as 512 pairs
// of [virtual cluster, logical cluster] pairs.
//
#define	FILEMAPSIZE		(16384+2)


//--------------------------------------------------------------------
//                        G L O B A L S
//--------------------------------------------------------------------

//
// Global variables
//
BOOLEAN		Silent = FALSE;
BOOLEAN		Recurse = FALSE;
BOOLEAN		ZapFreeSpace = FALSE;
BOOLEAN		ZeroFreeSpace = FALSE;
BOOLEAN		CleanCompressedFiles = FALSE;
DWORD		NumPasses = 1;
DWORD		FilesFound = 0;


BOOL (__stdcall *pGetDiskFreeSpaceEx)(
	  LPCTSTR lpDirectoryName,                 // pointer to the directory name
	  PULARGE_INTEGER lpFreeBytesAvailableToCaller, // receives the number of bytes on
													// disk available to the caller
	  PULARGE_INTEGER lpTotalNumberOfBytes,    // receives the number of bytes on disk
	  PULARGE_INTEGER lpTotalNumberOfFreeBytes // receives the free bytes on disk
);
 


//----------------------------------------------------------------------
//
// PrintNtError
//
// Formats an error message for the last native error.
//
//----------------------------------------------------------------------
void PrintNtError( NTSTATUS status )
{
	TCHAR *errMsg;

	FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			NULL, RtlNtStatusToDosError( status ), 
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 
			(LPTSTR) &errMsg, 0, NULL );
	_tprintf(_T("%s\n"), errMsg );
	LocalFree( errMsg );
}


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


//--------------------------------------------------------------------
//
// OverwriteFileName
//
// Securely deletes a file's original name by renaming it several
// times. This works by changing each non-'.' character in the file's
// name to successive alphabetic characters, thus overwriting the
// name 26 times.
//
//--------------------------------------------------------------------
VOID OverwriteFileName( PTCHAR FileName, PTCHAR LastFileName )
{
	TCHAR		newName[MAX_PATH];
	PTCHAR		lastSlash;
	DWORD		i, j, index;

	_tcscpy( LastFileName, FileName );
	lastSlash = _tcsrchr( LastFileName, _T('\\'));
	index = (lastSlash - LastFileName)/sizeof(TCHAR);

	//
	// Loop through each letter in the English alphabet
	//
	_tcscpy( newName, FileName );
	for( i = 0; i < 26; i++ ) {

		//
		// Replace each non-'.' character with the same letter
		//
		for( j = index+1 ; j < _tcsclen( FileName ); j++ ) {

			if( FileName[j] != _T('.')) newName[j] = (TCHAR) i + _T('A');
		}

		//
		// Got a new name so rename
		//
		if( !MoveFile( LastFileName, newName )) {

			//
			// Bail on error
			//
			return;
		}

		_tcscpy( LastFileName, newName );
	}
}


//--------------------------------------------------------------------
//
// SecureOverwrite
//
// This function implements a secure santize of rigid (removable 
// and fixed) disk media as per the Department of Defense clearing 
// and sanitizing standard: DOD 5220.22-M
// 
// The standard states that hard disk media is sanatized by 
// overwriting with a character, then the character's complement,
// and then a random character. Note that the standard specicically
// states that this method is not suitable for TOP SECRET information.
// TOP SECRET data sanatizing is only achievable by a Type 1 or 2 
// degauss of the disk, or by disintegrating, incinerating, 
// pulverizing, shreding, or melting the disk.
//
//--------------------------------------------------------------------
BOOLEAN SecureOverwrite( HANDLE FileHandle, ULONGLONG Length )
{
#define CLEANBUFSIZE 65536
	static PBYTE	cleanBuffer[3];
	static BOOLEAN	buffersAlloced = FALSE;

	DWORD		i, j, passes;
	ULONGLONG	totalWritten;
	ULONG		bytesWritten, bytesToWrite;
	LARGE_INTEGER	seekLength;
	BOOLEAN		status;

	//
	// Allocate our cleaning buffers if necessary (we just let program
	// exit free the buffers).
	//
	if( !buffersAlloced ) {

		//
		// Seed the random number generator
		//
		srand( (unsigned)time( NULL ) );
	
		for( i = 0; i < 3; i++ ) {

			cleanBuffer[i] = VirtualAlloc( NULL, CLEANBUFSIZE, MEM_COMMIT, PAGE_READWRITE );
			if( !cleanBuffer[i] ) {

				for( j = 0; j < i; j++ ) {

					VirtualFree( cleanBuffer[j], 0, MEM_RELEASE );
				}
				return FALSE;
			}

			//
			// Fill each buffer with a different signature
			//
			switch( i ) {

			case 0:
				// do nothing - buffer is zero-filled by Windows
				break;
			case 1:
				// fill with complement of 0 - 0xFF
				memset( cleanBuffer[i], 0xFF, CLEANBUFSIZE );
				break;
			case 2:
				// fill with a random value
				for( j = 0; j < CLEANBUFSIZE; j++ ) cleanBuffer[i][j] = (BYTE) rand();
				break;
			}

			//
			// If zeroing free space we're done
			//
			if( ZeroFreeSpace ) {

				break;
			}
		}	
		buffersAlloced = TRUE;
	}

	//
	// Do the overwrite
	//
	for( passes = 0; passes < NumPasses; passes++ ) {

		if( passes != 0 ) {

			seekLength.QuadPart = -(LONGLONG) Length;
			SetFilePointer( FileHandle, seekLength.LowPart, &seekLength.HighPart, FILE_CURRENT );
		}

		for( i = 0; i < 3; i++ ) {

			//
			// Move back to the start of where we're overwriting
			//
			if( i != 0 ) {

				seekLength.QuadPart = -(LONGLONG) Length;
				SetFilePointer( FileHandle, seekLength.LowPart, &seekLength.HighPart, FILE_CURRENT );
			}

			//
			// Loop and overwrite
			//
			totalWritten = 0;
			while( totalWritten < Length ) {

				if( Length - totalWritten > 1024*1024) {

					bytesToWrite = 1024*1024;
				} else {

					bytesToWrite = (ULONG) (Length - totalWritten );
				}
				if( bytesToWrite > CLEANBUFSIZE ) bytesToWrite = CLEANBUFSIZE;

				status = WriteFile( FileHandle, cleanBuffer[i], bytesToWrite, &bytesWritten, NULL );
				if( !status ) return FALSE;

				//
				// Note: no need to flush since the file is opened with write-through or 
				// no cache buffering
				//

				totalWritten += bytesWritten;
			}
		}
	}
	return TRUE;
}


//--------------------------------------------------------------------
//
// SecureDelete
//
// Performs a secure delete on the specified file.
//
//--------------------------------------------------------------------
VOID SecureDelete( PTCHAR FileName, DWORD FileLengthHi,
					DWORD FileLengthLo ) 
{
	HANDLE	hFile;
	ULONGLONG bytesToWrite, bytesWritten;
	ULARGE_INTEGER fileLength;
	TCHAR   lastFileName[MAX_PATH];

	//
	// First, open the file in overwrite mode
	//
	hFile = CreateFile( FileName, GENERIC_WRITE, 
						FILE_SHARE_READ|FILE_SHARE_WRITE,
						NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, NULL );
	if( hFile == INVALID_HANDLE_VALUE ) {

		_tprintf( _T("\nError opening %s for delete: "), FileName );
		PrintWin32Error( GetLastError());
		return;
	}

	//
	// If the file has a non-zero length, fill it with 0's first in order
	// to preserve is cluster allocation.
	//
	if( FileLengthLo || FileLengthHi ) {

		//
		// Seek to the last byte of the file
		//
		FileLengthLo--;
		if( FileLengthLo == (DWORD) -1 && FileLengthHi ) FileLengthHi--;
		SetFilePointer( hFile, FileLengthLo, &FileLengthHi, FILE_BEGIN );

		//
		// Write one zero byte, which causes the file system to fill the entire
		// file's on-disk contents with 0.
		//
		if( !SecureOverwrite( hFile, 1 )) {

			_tprintf( _T("\nError overwriting %s: "), FileName );
			PrintWin32Error( GetLastError() );
			CloseHandle( hFile );
			return;
		}

		//
		// Now go back to the start of the file and overwrite the rest of the
		// file.
		//
		SetFilePointer( hFile, 0, NULL, FILE_BEGIN );
		fileLength.LowPart = FileLengthLo;
		fileLength.HighPart = FileLengthHi;
		bytesWritten = 0;
		while( bytesWritten < fileLength.QuadPart ) {

			bytesToWrite = min( fileLength.QuadPart - bytesWritten, 65536 );
			if( !SecureOverwrite( hFile, (DWORD) bytesToWrite )) {

				_tprintf( _T("\nError overwriting %s: "), FileName );
				PrintWin32Error( GetLastError() );
				CloseHandle( hFile );
				return;
			}
			bytesWritten += bytesToWrite;
		}
	}

	//
	// Done!
	//
	CloseHandle( hFile );
	
	//
	// Rename the file a few times
	//
	OverwriteFileName( FileName, lastFileName );

	//
	// Now we can delete the file
	//
	if( !DeleteFile( lastFileName ) ) {

		_tprintf( _T("\nError deleting %s: "), FileName );
		PrintWin32Error( GetLastError() );

		//
		// Rename back to original name so as not to confuse the user
		//
		if( !MoveFile( lastFileName, FileName )) {

			_tprintf( _T("\nError renaming file back to original name. File is left as %s\n"),
				lastFileName );
		}
		return;
	}

	if( !Silent ) _tprintf( _T("deleted.\n"));
}



//--------------------------------------------------------------------
//
// ScanFile
//
// This is only invoked for compressed, encrypted or sparse files, 
// which exists only on NTFS drives (WinNT/2K). Thus, we can use
// the defrag API to zap the clusters belonging to the file 
// Determines if the the file is non-resident (outside the MFT), and 
// if so and we were able to open the volume for write access, we zap 
// the clusters.
//
//--------------------------------------------------------------------
BOOLEAN ScanFile( HANDLE VolumeHandle,
				  DWORD ClusterSize,
				  HANDLE FileHandle, 
				  PBOOLEAN ReallyCompressed,
				  PBOOLEAN ZappedFile )
{
	DWORD						status;
	int							i;
	IO_STATUS_BLOCK				ioStatus;
	ULONGLONG					startVcn, prevVcn;
	LARGE_INTEGER				clusterOffset;
	ULONGLONG					endOfPrevRun;
	PGET_RETRIEVAL_DESCRIPTOR	fileMappings;
	ULONGLONG					fileMap[ FILEMAPSIZE ];
	int							lines = 0;

	//
	// Assume file is in an MFT record.
	//
	*ReallyCompressed = FALSE;
	*ZappedFile = FALSE;

	startVcn = 0;
	endOfPrevRun = LLINVALID;
	fileMappings = (PGET_RETRIEVAL_DESCRIPTOR) fileMap;
	while( !(status = NtFsControlFile( FileHandle, NULL, NULL, 0, &ioStatus,
						FSCTL_GET_RETRIEVAL_POINTERS,
						&startVcn, sizeof( startVcn ),
						fileMappings, FILEMAPSIZE * sizeof(ULONGLONG) ) ) ||
			 status == STATUS_BUFFER_OVERFLOW ||
			 status == STATUS_PENDING ) {

		// 
		// If the operation is pending, wait for it to finish
		//
		if( status == STATUS_PENDING ) {
			
			WaitForSingleObject( FileHandle, INFINITE ); 

			//
			// Get the status from the status block
			//
			if( ioStatus.Status != STATUS_SUCCESS && 
				ioStatus.Status != STATUS_BUFFER_OVERFLOW ) {

				return ioStatus.Status == STATUS_SUCCESS;
			}
		}

		//
		// Loop through the buffer of number/cluster pairs, printing them
		// out.
		//
		startVcn = fileMappings->StartVcn;
		prevVcn  = fileMappings->StartVcn;
		for( i = 0; i < (ULONGLONG) fileMappings->NumberOfPairs; i++ ) {	 

			//
			// On NT 4.0, a compressed virtual run (0-filled) is 
			// identified with a cluster offset of -1
			//
			if( fileMappings->Pair[i].Lcn != LLINVALID ) {

				//
				// Its compressed and outside the zone
				//
				*ReallyCompressed = TRUE;

				//
				// Overwrite the clusters if we were able to open the volume
				// for write access.
				//
				if( VolumeHandle != INVALID_HANDLE_VALUE ) {
			

					clusterOffset.QuadPart = fileMappings->Pair[i].Lcn * ClusterSize;
					SetFilePointer( VolumeHandle, clusterOffset.LowPart,
									&clusterOffset.HighPart, FILE_BEGIN );
					if( !SecureOverwrite( VolumeHandle,
									ClusterSize * (DWORD) (fileMappings->Pair[i].Vcn - startVcn) )) {

						//
						// Couldn't zap the clusters, so we'll have to clean the free space
						//
						return TRUE;
					}							

				} else {

					return TRUE;	
				}
			}
			startVcn = fileMappings->Pair[i].Vcn;
		}

		//
		// If the buffer wasn't overflowed, then we're done
		//
		if( !status ) break;
	}

	//
	// Return now if there were any errors
	//
	if( status && status != STATUS_INVALID_PARAMETER && !Silent ) {

		printf("Scanning file: ");
		PrintNtError( status );
	}

	// 
	// If we made through with no errors we've overwritten all the file's
	// clusters.
	//
	if( status == STATUS_SUCCESS ) *ZappedFile = TRUE;
	return status == STATUS_SUCCESS;
}


//--------------------------------------------------------------------
//
// SecureDeleteCompressed
//
// More complicated than a regular file - we actually try to use
// direct disk access to overwrite the clusters that are used by a 
// compressed file. The function returns FALSE if the file is
// not really compressed (it is stored as resident data in the MFT).
//
//--------------------------------------------------------------------
BOOLEAN SecureDeleteCompressed( PTCHAR FileName ) 
{
	HANDLE			hFile;
	BOOLEAN			reallyCompressed = FALSE;
	BOOLEAN			zappedFile = FALSE;
	TCHAR			lastFileName[MAX_PATH];
	static TCHAR	volumeName[] = _T("\\\\.\\A:");
	static TCHAR	volumeRoot[] = _T("A:\\");
	static HANDLE	hVolume = INVALID_HANDLE_VALUE;
	static DWORD	clusterSize;
	DWORD			sectorsPerCluster, bytesPerSector, freeClusters, totalClusters;

	//
	// If we haven't opened the volume, attempt it now
	//
	if( hVolume == INVALID_HANDLE_VALUE ) {

		volumeName[4] = FileName[0];
		hVolume = CreateFile( volumeName, GENERIC_READ|GENERIC_WRITE,
							FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
							0, 0 );

		volumeRoot[0] = FileName[0];
		GetDiskFreeSpace( volumeRoot, &sectorsPerCluster, &bytesPerSector,
						&freeClusters, &totalClusters );

		clusterSize = bytesPerSector * sectorsPerCluster;
	}

	//
	// Open the file exclusively
	//
	hFile = CreateFile( FileName, GENERIC_READ, 
						0,NULL, OPEN_EXISTING, 0, NULL );
	if( hFile == INVALID_HANDLE_VALUE ) {

		_tprintf( _T("\nError opening %s for compressed file scan: "), FileName );
		PrintWin32Error( GetLastError());
		return TRUE;
	}
	
	//
	// Scan the location of the file
	//
	if( !ScanFile( hVolume, clusterSize, hFile, 
			&reallyCompressed, &zappedFile )) {

		CloseHandle( hFile );
		return TRUE;
	}

	// 
	// Done with the file handle
	//
	CloseHandle( hFile );

	//
	// If the file is really compressed (it is non-resident),
	// we can delete it now.
	//
	if( reallyCompressed ) {

		//
		// Rename the file a few times
		//
		OverwriteFileName( FileName, lastFileName );

		if( !DeleteFile( lastFileName )) {

			//
			// Rename back to the original name on error so as
			// not to confuse the user
			//
			_tprintf( _T("\nError deleting %s: "), FileName );
			PrintWin32Error( GetLastError() );
			if( !MoveFile( lastFileName, FileName )) {

				_tprintf( _T("\nError renaming file back to original name. File is left as %s\n"),
					lastFileName );
			}
			return TRUE;
		}

		//
		// If we couldn't directly overwrite the file's clusters, we'll
		// have to clean free space to overwrite them indirectly
		//
		if( !zappedFile ) CleanCompressedFiles = TRUE;

		if( !Silent ) _tprintf( _T("deleted.\n"));
	}

	//
	// Return TRUE if the file had clusters outside the MFT
	//
	return reallyCompressed;
}


//--------------------------------------------------------------------
//
// ProcessFile
//
// Performs a secure delete on the specified file.
//
//--------------------------------------------------------------------
VOID ProcessFile( PWIN32_FIND_DATA FindData, TCHAR *FileName )
{
	//
	// Directories are deleted by ProcessDirectory
	//
	if( FindData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) return;

	//
	// Got a matching file
	//
	FilesFound++;

	//
	// Print which file we're working on
	//
	if( !Silent ) {
		
		_tprintf( _T("%s..."), FileName );
		fflush( stdout );
	}

	//
	// If the file is compressed, we have to go a different path
	//
	if( FindData->dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED ||
		FindData->dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED  ||
		FindData->dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE ) {

		//
		// We need to determine where the compressed file is located 
		// physically on disk.
		//
		if( SecureDeleteCompressed( FileName )) return;
	} 

	// 
	// Regular path, non-compressed/encrypted/sparse file or one of those
	// types of files with their data resident in an MFT record: perform a 
	// simple secure delete
	//
	SecureDelete( FileName, FindData->nFileSizeHigh,
							FindData->nFileSizeLow );
}


//--------------------------------------------------------------------
//
// ProcessDirectory
// 
// Recursive routine that passes files to the delete function.
//
//--------------------------------------------------------------------
void ProcessDirectory( TCHAR *PathName, TCHAR *SearchPattern )
{
	TCHAR			subName[MAX_PATH], fileSearchName[MAX_PATH], searchName[MAX_PATH];
	HANDLE			dirHandle, patternHandle;
	static BOOLEAN	firstCall = TRUE;
	static BOOLEAN  deleteDirectories = FALSE;
	WIN32_FIND_DATA foundFile;

	//
	// Scan the files and/or directories if this is a directory
	//
	if( firstCall ) {

		if( _tcsrchr( PathName, '*' ) ) {
	
            if( _tcsrchr( PathName, '\\' ) ) {

                _stprintf( SearchPattern, _tcsrchr( PathName, '\\' )+1 );
                _tcscpy( searchName, PathName );
                _tcscpy( _tcsrchr( searchName, '\\')+1, _T("*.*") );
				if( !_tcscmp( SearchPattern, _T("*.*")) ||
					!_tcscmp( SearchPattern, _T("*"))) {

					deleteDirectories = TRUE;
				}

            } else {
                
                _stprintf( SearchPattern, PathName );
                _tcscpy( searchName, PathName );
            }
            _stprintf( fileSearchName, _T("%s"), PathName );

		} else {

			_stprintf( SearchPattern, _T("*.*") );
			_stprintf( searchName, _T("%s"), PathName );
            _stprintf( fileSearchName, _T("%s"), PathName );
			deleteDirectories = TRUE;
		}

	} else {

		_stprintf( searchName, _T("%s\\*.*"), PathName );
		_stprintf( fileSearchName, _T("%s\\%s"), PathName, SearchPattern );
	}

	//
	// Process all the files, according to the search pattern
	//
	if( (patternHandle = FindFirstFile( fileSearchName, &foundFile )) != 
		INVALID_HANDLE_VALUE  ) {

		do {

			if( _tcscmp( foundFile.cFileName, _T(".") ) &&
				_tcscmp( foundFile.cFileName, _T("..") )) {

				_tcscpy( subName, searchName );
				if( _tcsrchr( subName, '\\' ) ) 
					_tcscpy( _tcsrchr( subName, '\\')+1, foundFile.cFileName );
				else
					_tcscpy( subName, foundFile.cFileName );

				//
				// Do this file/directory
				//
				ProcessFile( &foundFile, subName );
			}
		} while( FindNextFile( patternHandle, &foundFile ));
		FindClose( patternHandle );
	}

	//
	// Now recurse if we're supposed to
	//
	if( Recurse ) {

        if( firstCall && !_tcsrchr( searchName, L'\\') ) {

            if( _tcsrchr( searchName, L'*' )) {

                if( (dirHandle = FindFirstFile( _T("*.*"), &foundFile )) == 
                    INVALID_HANDLE_VALUE  ) {

                    //
                    // Nothing to process
                    //
                    return;
                }
            } else {

                if( (dirHandle = FindFirstFile( searchName, &foundFile )) == 
                    INVALID_HANDLE_VALUE  ) {

                    //
                    // Nothing to process
                    //
                    return;
                }
            }
        } else {

            if( (dirHandle = FindFirstFile( searchName, &foundFile )) == 
                INVALID_HANDLE_VALUE  ) {

                //
                // Nothing to process
                //
                return;
            }
        }
        firstCall = FALSE;

		do {

			if( (foundFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				_tcscmp( foundFile.cFileName, _T(".") ) &&
				_tcscmp( foundFile.cFileName, _T("..") )) {

				_tcscpy( subName, searchName );
				if( _tcsrchr( subName, '\\' ) ) 
					_tcscpy( _tcsrchr( subName, '\\')+1, foundFile.cFileName );
				else
					_tcscpy( subName, foundFile.cFileName );

				//
				// Go into this directory
				//
				ProcessDirectory( subName, SearchPattern );

				//
				// Delete the directory if we're supposed to
				//
				if( deleteDirectories ) {

					if( !RemoveDirectory( subName )) {

						_tprintf( _T("\nError deleting %s: "), subName );
						PrintWin32Error( GetLastError() );
					}
				}
			}

		} while( FindNextFile( dirHandle, &foundFile ));
		FindClose( dirHandle );
	}
}


//--------------------------------------------------------------------
//
// CleanFreeSpace
//
// This function overwrites all free-space data areas on a disk, 
// including free space in the MFT. Note that it does not overwrite 
// freespace filename information.
//
//--------------------------------------------------------------------
BOOLEAN CleanFreeSpace( PTCHAR DrivePath )
{
	TCHAR		tempFileName[MAX_PATH];
	ULARGE_INTEGER bytesAvail, totalBytes, freeBytes;
	DWORD		sectorsPerCluster, bytesPerSector, totalClusters, freeClusters;
	ULONGLONG	prevSize, tempSize = 0, cleanSize;
	TCHAR		progress[] = _T("|/-\\|/-\\");
	HANDLE		hTempFile;
	BOOLEAN		createdFile;
	DWORD		percent, mftFilesCreated;
	DWORD		prevPercent = 0;
	USHORT Compression = COMPRESSION_FORMAT_NONE;
	DWORD BytesReturned;	

	if( DrivePath[1] != ':' ) {

		_tprintf( _T("Cannot clean free space for UNC drive\n\n"));
		return FALSE;
	}

	//
	// Drive letter - move to one past slash
	//
	DrivePath[3] = 0;

	if( CleanCompressedFiles ) {

		_tprintf(_T("Cleaning free space to securely delete compressed files: 0%%"));
	} else {

		_tprintf(_T("Cleaning free space on %s: 0%%"), DrivePath );
	}
	fflush( stdout );

	if( !GetDiskFreeSpace( DrivePath, &sectorsPerCluster, &bytesPerSector,
		&freeClusters, &totalClusters )) {

		_tprintf( _T("Could not determine disk cluster size: "));
		PrintWin32Error( GetLastError());
		return FALSE;
	}

#if UNICODE
	if( !(pGetDiskFreeSpaceEx = (PVOID) GetProcAddress( GetModuleHandle( _T("kernel32.dll") ),
											"GetDiskFreeSpaceExW" ))) {
#else
	if( !(pGetDiskFreeSpaceEx = (PVOID) GetProcAddress( GetModuleHandle( _T("kernel32.dll") ),
											"GetDiskFreeSpaceExA" ))) {
#endif

		bytesAvail.QuadPart = sectorsPerCluster * freeClusters * bytesPerSector;
        freeBytes.QuadPart = bytesAvail.QuadPart;

	} else {
	
		if( !pGetDiskFreeSpaceEx( DrivePath, &bytesAvail, &totalBytes, &freeBytes )) {

			_tprintf( _T("Could not determine amount of free space: "));
			PrintWin32Error( GetLastError());
			return FALSE;
		}
	}

	//
	// If the user doesn't have access to all the free space, we can't perform a full clean
	//
	if( bytesAvail.QuadPart != freeBytes.QuadPart ) {

		_tprintf(_T("Your disk quota prevents you from cleaning free space on this drive.\n\n"));
		return FALSE;
	}

	_stprintf( tempFileName, _T("%sSDELTEMP"), DrivePath );
	hTempFile = CreateFile( tempFileName, GENERIC_WRITE|GENERIC_READ, 
					0, NULL, CREATE_ALWAYS, 
					FILE_FLAG_NO_BUFFERING|FILE_FLAG_SEQUENTIAL_SCAN|
					FILE_FLAG_DELETE_ON_CLOSE|FILE_ATTRIBUTE_HIDDEN, NULL );

	if( hTempFile == INVALID_HANDLE_VALUE ) {

		_tprintf( _T("Could not create free-space cleanup file: "));
		PrintWin32Error( GetLastError());
		CloseHandle( hTempFile );
		return FALSE;
	}

	//
	// Turn off compression for the file - ignore errors since this might
	// be a FAT volume
	//
	DeviceIoControl( hTempFile, FSCTL_SET_COMPRESSION, &Compression,
			sizeof(Compression), NULL, 0, &BytesReturned, NULL);

	//
	// Allcate a buffer that is a cluster in size
	//
	cleanSize = (ULONGLONG) sectorsPerCluster * (ULONGLONG) bytesPerSector * 128;

	//
	// Grow the file by cluster size chunks until we fail
	//
	while( cleanSize > bytesPerSector * sectorsPerCluster ) {

		if( SecureOverwrite( hTempFile, cleanSize )) {

			tempSize += cleanSize;

			percent = (DWORD) ((tempSize * 100)/ freeBytes.QuadPart );

			if( percent != prevPercent ) {
				if( CleanCompressedFiles ) {

					_tprintf(_T("\rCleaning free space to securely delete compressed files: %d%%"),
						percent );
				} else {

					_tprintf(_T("\rCleaning free space on %s: %d%%"), DrivePath, percent );
				}
				prevPercent = percent;
			}
		} else {

			cleanSize -= bytesPerSector * sectorsPerCluster;
		}
	}

	//
	// There's less than a full cluster (outside the MFT if this is NTFS) free. 
	// Let's allocate another file to take care of it.
	//
	_stprintf( tempFileName, _T("%sSDELTEMP1"), DrivePath );
	hTempFile = CreateFile( tempFileName, GENERIC_WRITE, 
					0, NULL, CREATE_NEW, 
					FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_DELETE_ON_CLOSE|
					FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_WRITE_THROUGH, NULL );

	if( hTempFile != INVALID_HANDLE_VALUE ) {

		while( cleanSize ) {

			if( !SecureOverwrite( hTempFile, cleanSize )) {

				cleanSize--;
			}
		}
	}

	//
	// If we're just zapping free space, and this is NTFS, we have to take care of
	// deleted files within the MFT. We do this by creating as many of the largest sized
	// files we can (if there is space in the MFT, we'll be able to create non-zero sized
	// files, where the data is resident in the MFT record).
	//
	if( ZapFreeSpace ) {

		mftFilesCreated = 0;
		prevSize = 4096; // max MFT record size
		while( 1 ) {

			_stprintf( tempFileName, _T("%sSDELMFT%06d"), DrivePath, mftFilesCreated++ );
			hTempFile = CreateFile( tempFileName, GENERIC_WRITE, 
							0, NULL, CREATE_NEW, 
							FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_DELETE_ON_CLOSE|
							FILE_ATTRIBUTE_HIDDEN, NULL );
			if( hTempFile == INVALID_HANDLE_VALUE ) {

				break;
			}

			//
			// Mft record can be up to 4K in size
			//
			cleanSize = prevSize;
			createdFile = FALSE;
			while( cleanSize ) {

				if( !SecureOverwrite( hTempFile, cleanSize )) {

					cleanSize--;

				} else {

					prevSize = cleanSize;
					createdFile = TRUE;
				}
			}	
			
			//
			// If the only file we could create is length 0, then this is FAT
			//
			if( !createdFile ) break;

			if( mftFilesCreated == 1 ) {
				_tprintf( _T("\r                                                           "));
				
			} 

			_tprintf( _T("\rCleaning MFT...%c"),
				progress[ mftFilesCreated % 8 ]);

			// Don't close the file, since we want it to keep the space until we're
			// done.
		}
	}

	//
	// Done. No need to close our handles, since they are all delete-on-close.
	//
	_tprintf(_T("\rFree space cleaned on %s                                       \n"),
		DrivePath );
	return TRUE;
}


//--------------------------------------------------------------------
//
// LocateNativeEntryPoints
//
//--------------------------------------------------------------------
VOID LocateNativeEntryPoints()
{
	//
	// If we're on Win9x, just return
	//
	if( GetVersion() >= 0x80000000) return;

    //
    // Load the NTDLL entry point we need
    //
	if( !(NtFsControlFile = (void *) GetProcAddress( GetModuleHandle(_T("ntdll.dll")),
			"NtFsControlFile" )) ) {

		_tprintf(_T("\nCould not find NtFsControlFile entry point in NTDLL.DLL\n"));
		exit(1);
	}
	if( !(RtlNtStatusToDosError = (void *) GetProcAddress( GetModuleHandle(_T("ntdll.dll")),
							"RtlNtStatusToDosError" )) ) {

		_tprintf(_T("\nCould not find RtlNtStatusToDosError entry point in NTDLL.DLL\n"));
		exit(1);
	}
}


//--------------------------------------------------------------------
//
// Usage
//
// Tell user how to use the program.
//
//--------------------------------------------------------------------
int Usage( TCHAR *ProgramName ) 
{
    _tprintf(_T("usage: %s [-p passes] [-s] [-q] <file or directory>\n"), ProgramName );
	_tprintf(_T("       %s [-p passes] [-z|-c] [drive letter]\n"), ProgramName );
	_tprintf(_T("   -c         Zero free space (good for virtual disk optimization)\n"));
	_tprintf(_T("   -p passes  Specifies number of overwrite passes (default is 1)\n"));
    _tprintf(_T("   -q         Don't print errors (Quiet)\n"));
    _tprintf(_T("   -s         Recurse subdirectories\n"));
	_tprintf(_T("   -z         Clean free space\n\n"));
    return -1;
}


//--------------------------------------------------------------------
//
// Main
//
// Runs the show. 
//
//--------------------------------------------------------------------
int _tmain( int argc, TCHAR *argv[] ) 
{
    TCHAR       searchPattern[MAX_PATH];
	TCHAR		searchPath[MAX_PATH];
	PTCHAR		filePart;
	BOOL		foundFileArg = FALSE;
	int			i;

    //
    // Print banner and perform parameter check
    //
    _tprintf(_T("\nSDelete - Secure Delete v1.51\n") );
    _tprintf(_T("Copyright (C) 1999-2005 Mark Russinovich\n"));
    _tprintf(_T("Sysinternals - www.sysinternals.com\n\n"));

    if( argc < 2 ) {

        return Usage( argv[0] );
    }

    for( i = 1; i < argc; i++ ) {

	    if( !_tcsicmp( argv[i], _T("/s") ) ||
			!_tcsicmp( argv[i], _T("-s") )) {

			Recurse = TRUE;

		} else if( !_tcsicmp( argv[i], _T("/q") ) ||
				   !_tcsicmp( argv[i], _T("-q") )) {

			Silent = TRUE;

		} else if( !_tcsicmp( argv[i], _T("/z") ) ||
				   !_tcsicmp( argv[i], _T("-z") )) {

			ZapFreeSpace = TRUE;

		} else if( !_tcsicmp( argv[i], _T("/c") ) ||
				   !_tcsicmp( argv[i], _T("-c") )) {

			ZapFreeSpace = TRUE;
			ZeroFreeSpace = TRUE;

		} else if( !_tcsicmp( argv[i], _T("/p") ) ||
				   !_tcsicmp( argv[i], _T("-p") )) {

			NumPasses = argc > i ? atoi( argv[i+1] ) : 1;
			if( !NumPasses ) return Usage( argv[0] );
			i++;

		} else if( !_tcsicmp( argv[i], _T("/?") ) ||
				   !_tcsicmp( argv[i], _T("-?") )) {
				   
			return Usage( argv[0] );

		} else {

			if( foundFileArg ) return Usage( argv[0] );
			foundFileArg = TRUE;
		}
    }

	//
	// Have to have file if not zapping free space
	//
	if( !ZapFreeSpace && !foundFileArg ) {

		return Usage( argv[0] );
	}

	//
	// Locate Native entry points we need
	//
	LocateNativeEntryPoints();

	if( foundFileArg ) {
		
		//
		// Get canonical path name
		//
		GetFullPathName( argv[argc-1], MAX_PATH, searchPath, &filePart );
	}
	printf("SDelete is set for %d pass%s.\n", NumPasses,
		NumPasses > 1 ? "es" : "");
	
	//
	// Do the deleting
	//
	if( !ZapFreeSpace ) {

		//
		// Now go and process directories
		//
		ProcessDirectory( searchPath, searchPattern );

		if( !FilesFound ) _tprintf(_T("No files found that match %s.\n"), argv[argc-1] );

	} else if( !foundFileArg ) {

		GetCurrentDirectory( MAX_PATH, searchPath );
	} 

	//
	// If we encountered a compressed file along the way, and we couldn't directly
	// zap its on-disk clusters then we have to clean all the free space.
	//
	if( CleanCompressedFiles || ZapFreeSpace ) {

		CleanFreeSpace( searchPath );
	}

	_tprintf(_T("\n"));
    return 0;
}

