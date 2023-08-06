//----------------------------------------------------------------------
//
// NewSID
//
// Copyright (c) 1997-2002 Mark Russinovich and Bryce Cogswell
//
// Random number generation routine.
//
//----------------------------------------------------------------------
#include <windows.h>
#include <process.h>
#include <commctrl.h>

//----------------------------------------------------------------------
//
// Random
//
// 32-bit pseudo-random numbers
//
//----------------------------------------------------------------------
volatile BOOL RandomExpired;

void __cdecl RandomTrigger( void * arg )
{
	// Wait until main thread is ready to go
	while ( RandomExpired )
		Sleep( 1 );
	// Now sleep while main thread counts
	Sleep( 1 );
	// Notify main thread that time is up
	RandomExpired = TRUE;
	// Exit
	_endthread();
}

DWORD RandomBits( HWND ProgressBar )
{
	register DWORD cnt = 0;

	// Tell trigger we're not ready yet
	RandomExpired = TRUE;
	// Start trigger
	_beginthread( RandomTrigger, 0, NULL );
	// Okay, tell it to start sleeping
	RandomExpired = FALSE;
	// Count as fast as we can
	while ( !RandomExpired )  {
		++cnt;
	}
	// Only low bits are accurate!
	PostMessage( ProgressBar, PBM_STEPIT, 0, 0 );
	return cnt;
}


DWORD Random( HWND ProgressBar )
{
	return	RandomBits(ProgressBar) << 28	^
			RandomBits(ProgressBar) << 24	^
			RandomBits(ProgressBar) << 20	^
			RandomBits(ProgressBar) << 16	^
			RandomBits(ProgressBar) << 12	^
			RandomBits(ProgressBar) << 8	^
			RandomBits(ProgressBar) << 4	^
			RandomBits(ProgressBar) << 0;
}
