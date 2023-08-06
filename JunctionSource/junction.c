//--------------------------------------------------------------------
//
// Junction
// Copyright (C) 1999 Mark Russinovich
// Systems Internals - http://www.sysinternals.com
//
// This program demonstrates the use of reparse point query functions
// to list reparse points in directories/files specified as a 
// command-line parameter. In addition, it supports the creation
// of NTFS directory junctions.
//
//--------------------------------------------------------------------
#define _WIN32_WINNT  0x500
#include <windows.h>
#include <winioctl.h>
#include <tchar.h>
#include <mbstring.h>
#include <stdio.h>
#include "reparse.h"


//--------------------------------------------------------------------
//                         D E F I N E S
//--------------------------------------------------------------------

//
// Our maximum reparse point name size
//
#define MAX_NAME_LENGTH		1024

//--------------------------------------------------------------------
//                        G L O B A L S
//--------------------------------------------------------------------

//
// Global variables
//
ULONG     FilesProcessed = 0;
ULONG     DotsPrinted = 0;
BOOLEAN   Recurse = FALSE;
BOOLEAN   Quiet = FALSE;
BOOLEAN   PrintFileName = FALSE;
BOOLEAN   ReparseFound = FALSE;


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
// ProcessFile
//
// Determines if the file is a reparse point, and if so prints out
// some information about it.
//
//--------------------------------------------------------------------
VOID ProcessFile( PWIN32_FIND_DATA FindData, TCHAR *FileName )
{
    HANDLE   fileHandle;
	BYTE	 reparseBuffer[MAX_REPARSE_SIZE];
	PBYTE	 reparseData;
	PREPARSE_GUID_DATA_BUFFER reparseInfo = (PREPARSE_GUID_DATA_BUFFER) reparseBuffer;
	PREPARSE_DATA_BUFFER msReparseInfo = (PREPARSE_DATA_BUFFER) reparseBuffer;
	DWORD	returnedLength;
	TCHAR	name[MAX_NAME_LENGTH];
	TCHAR	name1[MAX_NAME_LENGTH];

	//
	// Directories are deleted by ProcessDirectory
	//
	if( FindData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {

		//
		// Open the directory
		//
		fileHandle = CreateFile( FileName, 0,
						FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, 
						OPEN_EXISTING, 
						FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT , 0 );

	} else {

		//
		// Open the file
		//
		fileHandle = CreateFile( FileName, 0,
						FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, 
						OPEN_EXISTING, 
						FILE_FLAG_OPEN_REPARSE_POINT, 0 );
	}
    if( fileHandle == INVALID_HANDLE_VALUE && Quiet == FALSE) {

        _tprintf(_T("\rFailed to open %s: "), FileName );
        PrintWin32Error( GetLastError() );
        return;
    }

    if( !(++FilesProcessed % 500) && Quiet == FALSE) {

        if( DotsPrinted == 3 ) {

            _tprintf(_T("\r     \r"));
            DotsPrinted = 0;

        } else {
            DotsPrinted++;
            _tprintf(_T("."));
        }
        fflush( stdout );
    }
    
    if( GetFileAttributes( FileName ) & FILE_ATTRIBUTE_REPARSE_POINT ) {

		if( DeviceIoControl( fileHandle, FSCTL_GET_REPARSE_POINT,
					NULL, 0, reparseInfo, sizeof( reparseBuffer ),
					&returnedLength, NULL )) {

			if( IsReparseTagMicrosoft( reparseInfo->ReparseTag )) {

				switch( reparseInfo->ReparseTag ) {

				case 0x80000000|IO_REPARSE_TAG_SYMBOLIC_LINK:

					//
					// I don't know if this is a valid reparse point
					//
					reparseData = (PBYTE) &msReparseInfo->SymbolicLinkReparseBuffer.PathBuffer;
					_tcsncpy( name, 
							(PWCHAR) (reparseData + msReparseInfo->SymbolicLinkReparseBuffer.PrintNameOffset),
							msReparseInfo->SymbolicLinkReparseBuffer.PrintNameLength );
					name[msReparseInfo->SymbolicLinkReparseBuffer.PrintNameLength] = 0;
					_tcsncpy( name1, 
							(PWCHAR) (reparseData + msReparseInfo->SymbolicLinkReparseBuffer.SubstituteNameOffset),
							msReparseInfo->SymbolicLinkReparseBuffer.SubstituteNameLength );
					name1[msReparseInfo->SymbolicLinkReparseBuffer.SubstituteNameLength] = 0;
					_tprintf(_T("%s: SYMBOLIC LINK\n"), FileName );
					if( msReparseInfo->SymbolicLinkReparseBuffer.PrintNameLength ) 
						_tprintf(_T("   Print Name     : %s\n"), name );
					if( msReparseInfo->SymbolicLinkReparseBuffer.SubstituteNameLength ) 
						_tprintf(_T("   Substitute Name: %s\n"), name1 );
					break;

				case IO_REPARSE_TAG_MOUNT_POINT:

					//
					// Mount points and junctions
					//
					reparseData = (PBYTE) &msReparseInfo->MountPointReparseBuffer.PathBuffer;
					_tcsncpy( name, 
							(PWCHAR) (reparseData + msReparseInfo->MountPointReparseBuffer.PrintNameOffset),
							msReparseInfo->MountPointReparseBuffer.PrintNameLength );
					name[msReparseInfo->MountPointReparseBuffer.PrintNameLength] = 0;
					_tcsncpy( name1, 
							(PWCHAR) (reparseData + msReparseInfo->MountPointReparseBuffer.SubstituteNameOffset),
							msReparseInfo->MountPointReparseBuffer.SubstituteNameLength );
					name1[msReparseInfo->MountPointReparseBuffer.SubstituteNameLength] = 0;

					//
					// I use a simple heuristic to differentiate mount points and junctions: a junction
					// specifies a drive-letter based target, so I look for the drive letter colon
					// in the target, which should be in the form \??\D:\target
					//
					if( _tcsstr( name1, _T(":") )) {

						_tprintf(_T("%s: JUNCTION\n"), FileName );
					} else {

						_tprintf(_T("%s: MOUNT POINT\n"), FileName );
					}
					if( msReparseInfo->MountPointReparseBuffer.PrintNameLength ) 
						_tprintf(_T("   Print Name     : %s\n"), name );
					if( msReparseInfo->MountPointReparseBuffer.SubstituteNameLength ) {

						if( !_tcsncmp( name1, _T("\\??\\"), _tcslen(_T("\\??\\")))) {

							_tprintf(_T("   Substitute Name: %s\n"), &name1[4] );
							
						} else {

							_tprintf(_T("   Substitute Name: %s\n"), name1 );
						}
					}
					break;

				case IO_REPARSE_TAG_HSM:
					_tprintf(_T("%s: HIERARCHICAL STORAGE MANAGEMENT REPARSE\n"), FileName );				
					break;

				case IO_REPARSE_TAG_SIS:

					//
					// Sinlge Instance Store installs and executes as part
					// of the Remote Installation Service (RIS)
					//
					_tprintf(_T("%s: SINGLE INSTANCE STORE POINT\n"), FileName );
					break;

				case IO_REPARSE_TAG_DFS:
					_tprintf(_T("%s: DISTRIBUTED FILE SYSTEM POINT\n"), FileName );
					break;

				default:
					_tprintf(_T("%s: UNKNOWN MICROSOFT REPARSE POINT\n"), FileName );
					break;
				}
				_tprintf(_T("\n"));
			} else {

				_tprintf(_T("%s: THIRD-PARTY REPARSE POINT\n"), FileName );
				_tprintf(_T("   Tag         : 0x%X\n"), reparseInfo->ReparseTag );
				_tprintf(_T("   Surrogate   : %s\n"), 
					IsReparseTagNameSurrogate(reparseInfo->ReparseTag) ? _T("TRUE") : _T("FALSE"));
#if 0
				_tprintf(_T("   High Latency: %s\n"),
					IsReparseTagHighLatency(reparseInfo->ReparseTag) ?  _T("TRUE") : _T("FALSE"));
#endif
				_tprintf(_T("   Size of data: %d bytes\n\n"), 
						reparseInfo->ReparseDataLength );
			}
		} else if( Quiet == FALSE ) {

	        _tprintf(_T("\rFailed to query reparse info for %s: "), FileName );
		    PrintWin32Error( GetLastError() );
		}
		ReparseFound++;
    }

    CloseHandle( fileHandle );
}


//--------------------------------------------------------------------
//
// ProcessDirectory
// 
// Recursive routine that passes files to the reparse query function.
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

            PrintFileName = TRUE;
	
            if( _tcsrchr( PathName, '\\' ) ) {

                _stprintf( SearchPattern, _tcsrchr( PathName, '\\' )+1 );
                _tcscpy( searchName, PathName );
                _tcscpy( _tcsrchr( searchName, '\\')+1, _T("*.*") );

            } else {
                
                _stprintf( SearchPattern, PathName );
                _tcscpy( searchName, PathName );
            }
            _stprintf( fileSearchName, _T("%s"), PathName );

		} else {

			_stprintf( SearchPattern, _T("*.*") );
            if( Recurse ) {

                PrintFileName = TRUE;
                _stprintf( searchName, _T("%s\\*.*"), PathName );
                _stprintf( fileSearchName, _T("%s\\*.*"), PathName );
            } else {

                _stprintf( searchName, _T("%s"), PathName );
                _stprintf( fileSearchName, _T("%s"), PathName );
            }
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
			}

		} while( FindNextFile( dirHandle, &foundFile ));
		FindClose( dirHandle );
	}
}


//--------------------------------------------------------------------
//
// CreateJunction
//
// This routine creates a NTFS junction, using the undocumented
// FSCTL_SET_REPARSE_POINT structure Win2K uses for mount points
// and junctions.
//
//--------------------------------------------------------------------
CreateJunction( PTCHAR LinkDirectory, PTCHAR LinkTarget )
{
	char		reparseBuffer[MAX_PATH*3];
	WCHAR		volumeName[] = _T("X:\\");
	WCHAR		directoryFileName[MAX_PATH];
	WCHAR		fileSystem[MAX_PATH] = L"";
	WCHAR		targetFileName[MAX_PATH];
    WCHAR       targetNativeFileName[MAX_PATH];
	PTCHAR		filePart;
	HANDLE		hFile;
	DWORD		returnedLength;
	PREPARSE_MOUNTPOINT_DATA_BUFFER reparseInfo = 
        (PREPARSE_MOUNTPOINT_DATA_BUFFER) reparseBuffer;
	
	//
	// Get the full path referenced by the target
	//
	if( !GetFullPathName( LinkTarget, 
						MAX_PATH, targetFileName,
						&filePart )) {

		_tprintf(_T("%s is an invalid file name:\n"), LinkTarget );
		PrintWin32Error( GetLastError());
		return -1;
	}

	//
	// Get the full path referenced by the directory
	//
	if( !GetFullPathName( LinkDirectory, 
						MAX_PATH, directoryFileName,
						&filePart )) {

		_tprintf(_T("%s is an invalid file name:\n"), LinkDirectory );
		PrintWin32Error( GetLastError());
		return -1;
	}

	//
	// Make sure that directory is on NTFS volume
	//
	volumeName[0] = directoryFileName[0];
	GetVolumeInformation( volumeName, NULL, 0, NULL, NULL, NULL, fileSystem,
		sizeof(fileSystem)/sizeof(WCHAR));
	if( _wcsicmp( L"NTFS", fileSystem)) {
	
		_tprintf(_T("Junctions are only supported on NTFS volumes.\n\n"));
		return -1;
	}

	//
	// Make the native target name
	//
	_stprintf( targetNativeFileName, _T("\\??\\%s"), targetFileName );
	if ( (targetNativeFileName[_tcslen( targetNativeFileName )-1] == _T('\\')) &&
	   (targetNativeFileName[_tcslen( targetNativeFileName )-2] != _T(':'))) {
		  targetNativeFileName[_tcslen( targetNativeFileName )-1] = 0;
	 }  

	//
	// Create the link - ignore errors since it might already exist
	//
	CreateDirectory( LinkDirectory, NULL );
	hFile = CreateFile( LinkDirectory, GENERIC_WRITE, 0,
						NULL, OPEN_EXISTING, 
						FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS, NULL );
	if( hFile == INVALID_HANDLE_VALUE ) {

		_tprintf(_T("Error creating %s:\n"), LinkDirectory );
		PrintWin32Error( GetLastError());
		return -1;
	}
	
	//
	// Build the reparse info
	//
	memset( reparseInfo, 0, sizeof( *reparseInfo ));
	reparseInfo->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;

	reparseInfo->ReparseTargetLength = _tcslen( targetNativeFileName ) * sizeof(WCHAR);
	reparseInfo->ReparseTargetMaximumLength = reparseInfo->ReparseTargetLength + sizeof(WCHAR);
	_tcscpy( reparseInfo->ReparseTarget, targetNativeFileName );
	reparseInfo->ReparseDataLength = reparseInfo->ReparseTargetLength + 12;

	//
	// Set the link
	//
	if( !DeviceIoControl( hFile, FSCTL_SET_REPARSE_POINT,
				reparseInfo, 
				reparseInfo->ReparseDataLength + REPARSE_MOUNTPOINT_HEADER_SIZE,
				NULL, 0, &returnedLength, NULL )) {	

		_tprintf(_T("Error setting junction for %s:\n"), LinkDirectory );
		PrintWin32Error( GetLastError());
		CloseHandle( hFile );
		RemoveDirectory( LinkDirectory );
		return -1;
	}

	_tprintf(_T("Created: %s\nTargetted at: %s\n"),
				directoryFileName, targetFileName );
	CloseHandle( hFile );
	return 0;
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
	_tprintf(_T("The first usage is for displaying reparse point information, and the\n")
			 _T("second usage is for creating or deleting a NTFS junction point:\n\n"));
    _tprintf(_T("usage: %s [-s] [-q] <file or directory>\n"), ProgramName );
    _tprintf(_T("       -q     Don't print error messages (quiet)\n\n"));
    _tprintf(_T("       -s     Recurse subdirectories\n\n"));
    _tprintf(_T("usage: %s [-d] <junction directory> [<junction target>]\n"), ProgramName );
    _tprintf(_T("       -d     Delete the specified junction\n"));
	_tprintf(_T("       example: junction d:\\link c:\\winnt\n\n"));
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
	BOOLEAN		deleteJunction = FALSE;
	int			i;
	int			foundFileArg = 0;
	int			foundSecondFileArg = 0;

    //
    // Print banner and perform parameter check
    //
    _tprintf(_T("\nJunction v1.04 - Windows junction creator and reparse point viewer\n") );
    _tprintf(_T("Copyright (C) 2000-2005 Mark Russinovich\n"));
    _tprintf(_T("Systems Internals - http://www.sysinternals.com\n\n"));

    if( argc < 2 ) return Usage( argv[0] );

    for( i = 1; i < argc; i++ ) {

	    if( !_tcsicmp( argv[i], _T("/s") ) ||
			!_tcsicmp( argv[i], _T("-s") )) {

			Recurse = TRUE;
			if( deleteJunction ) return Usage( argv[0]);

		} else if( !_tcsicmp( argv[i], _T("/q") ) ||
				   !_tcsicmp( argv[i], _T("-q") )) {

			Quiet = TRUE;
			if( deleteJunction ) return Usage( argv[0]);

		} else if( !_tcsicmp( argv[i], _T("/d") ) ||
			!_tcsicmp( argv[i], _T("-D") )) {

			deleteJunction = TRUE;
			if( Recurse ) return Usage( argv[0] );

		} else if( !_tcsicmp( argv[i], _T("/?") ) ||
				   !_tcsicmp( argv[i], _T("-?") )) {
				   
			return Usage( argv[0] );

		} else {

			if( foundSecondFileArg ) return Usage( argv[0] );

			if( foundFileArg )	foundSecondFileArg  = i;
			else				foundFileArg		= i;

			if( foundSecondFileArg && deleteJunction ) return Usage(argv[0]);
		}
    }
	
	//
	// Either create a new junction, or look for reparse points
	//
	if( foundSecondFileArg ) {

		//
		// Create the junction point
		//
		return CreateJunction( argv[foundFileArg], argv[foundSecondFileArg] );

	} else if( deleteJunction ) {

		//
		// Delete a junction
		//
		if( !RemoveDirectory( argv[foundFileArg] )) {

			_tprintf(_T("Error deleting %s: "), argv[foundFileArg] );
			PrintWin32Error( GetLastError() );

		} else {

			_tprintf(_T("Deleted %s.\n"), argv[foundFileArg] );
		}

	} else {

		//
		// Dump reparse points
		//
		if( foundFileArg ) {
			
			//
			// Get canonical path name
			//
			GetFullPathName( argv[foundFileArg], MAX_PATH, searchPath, &filePart );

		} else {

			GetCurrentDirectory( MAX_PATH, searchPath );
		}

		//
		// Now go and process directories
		//
		ProcessDirectory( searchPath, searchPattern );

		if( !FilesProcessed ) {

			_tprintf(_T("No matching files were found.\n\n"));

		} else if( !ReparseFound ) {
        
			_tprintf(_T("No reparse points found.\n\n"));
		}
	}
    return 0;
}

