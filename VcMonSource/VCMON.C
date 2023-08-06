//----------------------------------------------------------------------
//
// VCMON.C: Disk Cache Monitor for Windows 95.
//
// Copyright (c) 1996 by Mark Russinovich and Bryce Cogswell
//
//----------------------------------------------------------------------
#include <windows.h>
#include <stdio.h>
#include <process.h>
#include <commctrl.h>
#include "resource.h"  
#include "vxd\stats.h"

//----------------------------------------------------------------------
//
// typedefs
//
//----------------------------------------------------------------------

// this data structure contains all the relevant information about
// a graph
typedef struct {
	int			graphstat;		// is graph on or off?

	// related to our parent and our checkbox
	HWND		graphdlg;		// our parent dialog
	int			buttonid;		// control button id for us

	// our stuff
	HWND		graphwnd;		// window handle
	int			graphwidth;		// width of graph (in pixels)
	int			graphheight;	// height of graph (in pixels)
	int			halfheight[2];	// height of subgraphs (in pixels)
	BITMAP		graphbmp;		// graph bitmap
	HBITMAP		hgraphbmp;
	BITMAP		legendbmp;		// legend bitmap
	HBITMAP		hlegendbmp;
	BITMAP		axisbmp;		// axis bitmap
	HBITMAP		haxisbmp;	
	int			numplots;		// number of things plotted on graph
	char		graphtitle[64]; // window title
	char		titles[2][64];	// line titles

	// numeric values
	double		highpty[2];		// highest value on graph
	long		highptx[2];		// x-coord of highest value
	double		xaxis[2];		// real value of x-axis
	long		xval;			// current x value
	long		yprev[2];		// previous y value
	double		*yval[2];		// y values on entire graph
} graphdata;


//----------------------------------------------------------------------
//
// defines
//
//----------------------------------------------------------------------

// name of vxd to connect with
#define	VXD_NAME		"\\\\.\\VCMON.VXD"

// color definitions
#define MYBLACK 		PALETTERGB( 0, 0, 0 )
#define MYGRAY			PALETTERGB( 0x80, 0x80, 0x80 )
#define MYWHITE			PALETTERGB( 0xFF, 0xFF, 0xFF )
#define MYBLUE  		PALETTERGB( 0, 0, 0xFF )
#define MYRED			PALETTERGB( 0xFF, 0, 0 )

// graph background color
#define GRAPHBKGRND		GetStockObject( LTGRAY_BRUSH )

// initial size of graph windows
#define GRAPHWIDTH 		450
#define GRAPHHEIGHT		300

// number of points ahead of current one that we clear
#define CLEARAHEAD		20

// distance away from checkbox that graph appears
#define XGRAPHSPACE		20
#define YGRAPHSPACE		20

// indexes into the graph array
#define NUMGRAPHS		6
#define SIZEINDEX		0
#define HITINDEX		1
#define	HOLDINDEX		2
#define	NEWINDEX		3
#define PGFLTINDEX		4
#define REPINDEX		5


//----------------------------------------------------------------------
//
// globals
//
//----------------------------------------------------------------------

// sample rate for getting info from VxD
DWORD				SampleRate = 500; // default == 0.5 second


// VxD related globals
HANDLE				VxDHandle = INVALID_HANDLE_VALUE;
OVERLAPPED  		Ovrlp = {0,0,0,0,0};
VcmonStatus_s		*Stats_p;
VcmonStatus_s		CurStats, LastStats;

// general windows things
static char			msgbuf[ 2048 ];
static HINSTANCE	hInst;

// pens for graph drawing
HPEN				BluePen, RedPen, BlackPen, WhitePen, GrayPen;

// indicates id of new graph being created
int					NewGraph;

// window handle of main dialog
HWND				hMainDlg;

// graphs				 
graphdata			Graph[NUMGRAPHS];
HANDLE				DrawCrit;

// font info
TEXTMETRIC 			FontTM;
LOGFONT    			LogFont;	
HFONT 				hFont = (HFONT) NULL;


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
	MessageBox( hWnd, Msg, "VCache Monitor", MB_OK );
	EndDialog( hWnd, 1 );
	PostQuitMessage( 1 );
}


//----------------------------------------------------------------------
//  
// centerWindow
// 
// Centers the Window on the screen.
//
//----------------------------------------------------------------------
VOID centerWindow( HWND hDlg )
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
// AboutDlgProc
//
// Pops up the standard About dialog box.
//
//----------------------------------------------------------------------
BOOL CALLBACK AboutDlgProc (HWND hDlg, UINT message, UINT wParam,
                       		LONG lParam) {
	switch (message) {
	case WM_INITDIALOG :
		// center the dialog
		centerWindow( hDlg );
		return TRUE ;
 
	case WM_COMMAND :
		switch (wParam) {

		case IDOK :
			EndDialog (hDlg, 0) ;
			return TRUE ;
		}
		break; 

	case WM_CLOSE:	
		EndDialog (hDlg, 0);
		return TRUE;
	}
	return FALSE ;
}


//----------------------------------------------------------------------
//  
// SliderDlgProc
//
// Pops up a dialog with a slider that is used to set the sample
// frequency.
//
//----------------------------------------------------------------------
BOOL CALLBACK SliderDlgProc (HWND hDlg, UINT message, UINT wParam,
                       		LONG lParam) {
	DWORD			NewRate;
	static HWND		hwndTrack;

	switch (message) {
	case WM_INITDIALOG :
		// center the dialog
		centerWindow( hDlg );

		// initialize the trackbar
		hwndTrack = GetDlgItem( hDlg, IDC_SLIDE ); 

		// 10 ticks so 1 == .5 seconds
		SendMessage(hwndTrack, TBM_SETRANGE, 
	        (WPARAM) TRUE,                
	        (LPARAM) MAKELONG(0, 10));

		// place one tick every 1 second
		SendMessage(hwndTrack, TBM_SETTICFREQ, 
	        (WPARAM) 2,                  
	        (LPARAM) 0 );

		// move slider to current sample rate
		SendMessage(hwndTrack, TBM_SETPOS, 
        	(WPARAM) TRUE,                   
        	(LPARAM) SampleRate/500); 
		return TRUE ;

	case WM_HSCROLL:
		switch( LOWORD(wParam) ) {
		case TB_ENDTRACK:
			// don't let an illegal value get through
            NewRate = SendMessage(hwndTrack, TBM_GETPOS, 0, 0);
            if( NewRate < 1 )
				SendMessage(hwndTrack, TBM_SETPOS, 
                    (WPARAM) TRUE,  
                    (LPARAM) 1); 
			break;
		}
		break;
 
	case WM_COMMAND :
		switch (wParam) {
		case IDOK:
			// set new sample rate
			SampleRate = SendMessage(hwndTrack, TBM_GETPOS, 0, 0) * 500;

			// tell VxD to change rate
			if ( ! DeviceIoControl(	VxDHandle, VCMON_SETRATE,
						&SampleRate, sizeof( SampleRate ), NULL, 0,  
						NULL, NULL ) ) {
				wsprintf( msgbuf, "Can't set rate with %s.", VXD_NAME );
				Abort( hDlg, msgbuf );
				return FALSE;
			}
			EndDialog( hDlg, 0);
			return TRUE;

		case IDCANCEL:
			EndDialog( hDlg, 0 );
			return TRUE ;			
		}
		break; 

	case WM_CLOSE:	
		EndDialog (hDlg, 0);
		return TRUE;
	}
	return FALSE ;
}


//----------------------------------------------------------------------
//
// FillStock
// 
// Fills a rectangle with the specified color.
//
//----------------------------------------------------------------------
void FillStock( HDC hDC, RECT *rc, int color )
{
	HBRUSH		hbrBkGnd;

	hbrBkGnd 	= GetStockObject( color );
    FillRect( hDC, rc, hbrBkGnd);
    DeleteObject( hbrBkGnd );
}


//----------------------------------------------------------------------
//  
// DrawSeperator
//
// Draws the 3-d lines seperating the graph from the upper and lower
// parts of the window
//
//----------------------------------------------------------------------
void DrawSeperator( HDC hDC, long top, long width )
{
	// white line
	SelectObject( hDC, WhitePen );
	MoveToEx( hDC, 0, top + 1, NULL );
	LineTo( hDC, width, top + 1 );

	// gray line
	SelectObject( hDC, GrayPen );
	MoveToEx( hDC, 0, top, NULL );
	LineTo( hDC, width , top );
}


//----------------------------------------------------------------------
//  
// DrawBitmap
//
// Draws the logo onto the dialog box. We have bitmaps for large and
// small font cases, but if the system is using an odd size we stretch
// the appropriate bitmap. Looks great for all the cases I tried.
//
//----------------------------------------------------------------------
void DrawBitmap (HDC hdc, HBITMAP hBitmap, short xStart, short yStart,
	int xLen, int yLen, int stretch )
{
     BITMAP    bm ;
     HDC       hdcMem ;
     POINT     ptSize, ptOrg , ptBSize;

     hdcMem = CreateCompatibleDC (hdc) ;
     SelectObject (hdcMem, hBitmap) ;
     SetMapMode (hdcMem, GetMapMode (hdc)) ;

     GetObject (hBitmap, sizeof (BITMAP), (LPSTR) &bm) ;
     ptSize.x = xLen; 
     ptSize.y = yLen; 
     DPtoLP (hdc, &ptSize, 1) ;

     ptOrg.x = 0 ;
     ptOrg.y = 0 ;
     DPtoLP (hdcMem, &ptOrg, 1) ;

	 ptBSize.x = bm.bmWidth;
	 ptBSize.y = bm.bmHeight;
	 DPtoLP (hdcMem, &ptBSize, 1);

	 if((ptSize.x < ptBSize.x - 10) || (ptSize.x > ptBSize.x) || stretch)
	 	StretchBlt (hdc, xStart, yStart, ptSize.x, ptSize.y,
	 		hdcMem, ptOrg.x, ptOrg.y, ptBSize.x, ptBSize.y, SRCCOPY);
	 else
	 	BitBlt (hdc, xStart, yStart, ptSize.x, ptSize.y,
     		hdcMem, ptOrg.x, ptOrg.y, SRCCOPY) ;

     DeleteDC (hdcMem) ;
}


//----------------------------------------------------------------------
//
// NewHightPt
//
// If we get a new high point for the graph, we have to reset the
// label bitmap
//
//----------------------------------------------------------------------
void NewHighPt( graphdata *mygraph )
{
	HDC				hdcMem, hDC;
	HBITMAP			oldbitmap;
	HFONT      		hOldFont = NULL;
	RECT			rc;
	char			tmp[64];

   	// prepare for drawing on bitmap
	hDC = GetDC( mygraph->graphwnd );
	hdcMem = CreateCompatibleDC (hDC) ;

	DeleteObject( mygraph->haxisbmp );
	mygraph->haxisbmp = CreateCompatibleBitmap( hDC, 
								mygraph->graphwidth,
								FontTM.tmHeight );
	GetObject( mygraph->haxisbmp, sizeof (BITMAP), 
							(LPSTR) &mygraph->axisbmp);
	oldbitmap = SelectObject( hdcMem, mygraph->haxisbmp );

	// fill bitmap with grey background	and draw seperator
	rc.top = 0;
	rc.left = 0;
	rc.bottom = mygraph->axisbmp.bmHeight;
	rc.right  = mygraph->axisbmp.bmWidth;
	FillStock( hdcMem, &rc, LTGRAY_BRUSH );
	DrawSeperator( hdcMem, mygraph->axisbmp.bmHeight - 2, 
					mygraph->axisbmp.bmWidth );

	// divide things up into different colors
	SetBkColor( hdcMem, GetSysColor(COLOR_BTNFACE));
	if (hFont) hOldFont = SelectObject( hdcMem, hFont);

	if( mygraph->numplots == 1) {
		SetTextColor( hdcMem, MYBLUE );
		sprintf( tmp, "%g/%g", mygraph->yval[0][ mygraph->xval ], mygraph->highpty[0] ); 
		TextOut( hdcMem, 0, 0, tmp, strlen( tmp ) );
	} else {
		SetTextColor( hdcMem, MYBLUE );
		sprintf( tmp, mygraph->titles[0] );
		TextOut( hdcMem, 0, 0, tmp, strlen( tmp ) );

		sprintf( tmp, "%g/%g", mygraph->yval[0][mygraph->xval], mygraph->highpty[0] );
		TextOut( hdcMem, mygraph->graphbmp.bmWidth/2, 0,
						tmp, strlen( tmp ));
	}
	if( hOldFont ) SelectObject( hdcMem, hOldFont);
	mygraph->haxisbmp = SelectObject( hdcMem, oldbitmap );
	DeleteDC (hdcMem) ;	

	// update lower graph legend (if there is one)
	if( mygraph->numplots == 2 ) {
 		hdcMem = CreateCompatibleDC (hDC) ;

		DeleteObject( mygraph->hlegendbmp );
		mygraph->hlegendbmp = CreateCompatibleBitmap( hDC, 
								mygraph->graphwidth,
								FontTM.tmHeight );
		GetObject( mygraph->hlegendbmp, sizeof (BITMAP), 
							(LPSTR) &mygraph->legendbmp);
		oldbitmap = SelectObject( hdcMem, mygraph->hlegendbmp );

		// fill bitmap with grey background	and draw seperator
		rc.top = 0;
		rc.left = 0;
		rc.bottom = mygraph->legendbmp.bmHeight;
		rc.right  = mygraph->legendbmp.bmWidth;
		FillStock( hdcMem, &rc, LTGRAY_BRUSH );
		DrawSeperator( hdcMem, 0, mygraph->legendbmp.bmWidth );

		// divide things up into different colors
		SetBkColor( hdcMem, GetSysColor(COLOR_BTNFACE));
		if (hFont) hOldFont = SelectObject( hdcMem, hFont);

		SetTextColor( hdcMem, MYRED );
		sprintf( tmp, mygraph->titles[1] );
		TextOut( hdcMem, 0, 3, tmp, strlen( tmp ) );

		sprintf( tmp, "%g/%g", mygraph->yval[1][mygraph->xval], mygraph->highpty[1] );
		TextOut( hdcMem, mygraph->graphbmp.bmWidth/2, 3,
						tmp, strlen( tmp ));

		if( hOldFont ) SelectObject( hdcMem, hOldFont);
		mygraph->hlegendbmp = SelectObject( hdcMem, oldbitmap );
		DeleteDC (hdcMem) ;	
	}

	// release the DC
	ReleaseDC( mygraph->graphwnd, hDC ); 
}


//----------------------------------------------------------------------
//
// RescaleGraph
//
// Takes a graph, new axis information and rescales it.
//
//----------------------------------------------------------------------
void RescaleGraph( graphdata *mygraph, long prevx, double newxaxis1, double newxaxis2 )
{
	HDC				hdcMem, hDC;
	HBRUSH			hbrBkGnd;
	HBITMAP			oldbitmap;
	HFONT      		hOldFont = NULL;
	RECT			rc;
	BOOL			noprev;
	int				i;
	long			grheight, firstheight;

	// update axis
	if( newxaxis1 )
		mygraph->xaxis[0] = newxaxis1;
	if( newxaxis2 )
		mygraph->xaxis[1] = newxaxis2; 

 	// prepare for drawing on bitmap
	hDC = GetDC( mygraph->graphwnd );
	hdcMem 	= CreateCompatibleDC( hDC ) ;
	oldbitmap = SelectObject( hdcMem, mygraph->hgraphbmp );

	// fill with black
	rc.left 	= 0;
	rc.top 		= 0;
	rc.bottom	= mygraph->graphbmp.bmHeight;
	rc.right	= mygraph->graphbmp.bmWidth;
	hbrBkGnd 	= GRAPHBKGRND; 
	FillRect(hdcMem, &rc, hbrBkGnd);
	DeleteObject(hbrBkGnd);

	// now, time to redraw and update all the y-values for the first plot
	SelectObject( hdcMem, BluePen );
	grheight = mygraph->halfheight[0];
	noprev = TRUE;
	mygraph->yprev[0] = -1;
	for( i = 0;	i < mygraph->graphwidth ; i++ ) {
		if( noprev && mygraph->yval[0][i] != -1.0 )  {
			noprev = FALSE;
			MoveToEx( hdcMem, i, (long) (grheight - mygraph->yval[0][i] * grheight/mygraph->xaxis[0]), 
						NULL );
		} else if( mygraph->yval[0][i] == -1.0 )
			noprev = TRUE;

		if( mygraph->yval[0][i] != -1.0) {
			LineTo( hdcMem, i, (long) (grheight - (mygraph->yval[0][i] * grheight)/
						mygraph->xaxis[0] ));
			if( i == prevx )
				mygraph->yprev[0] = (long) (grheight - (mygraph->yval[0][i] * 
						grheight)/mygraph->xaxis[0] );
		}
	}

	// do the second plot (if any)
	if( mygraph->numplots == 2 ) {
		SelectObject( hdcMem, RedPen );
		grheight = mygraph->halfheight[1];
		noprev = TRUE;
		mygraph->yprev[1] = -1;
		firstheight = mygraph->halfheight[0] + 3;
		for( i = 0;	i < mygraph->graphwidth ; i++ ) {
			if( noprev && mygraph->yval[1][i] != -1.0 ) {
				noprev = FALSE;
				MoveToEx( hdcMem, i, (long) (grheight - mygraph->yval[1][i] * 
						grheight/mygraph->xaxis[1] + firstheight ), NULL );
			} else if( mygraph->yval[1][i] == -1.0 )
				noprev = TRUE;

			if( mygraph->yval[1][i] != -1.0 ) {
				LineTo ( hdcMem, i, (long) (grheight - (mygraph->yval[1][i] * grheight)/
					 mygraph->xaxis[1] + firstheight ));
				if( i == prevx ) 
					mygraph->yprev[1] = (long) (grheight - (mygraph->yval[1][i] * grheight)/ 
						mygraph->xaxis[1] );
			}
		}

		// draw the graph seperator
		DrawSeperator( hdcMem, firstheight - 2, rc.right - rc.left );	
	}

	// release the bitmap
	mygraph->hgraphbmp = SelectObject( hdcMem, oldbitmap );
	DeleteDC (hdcMem) ;	
	ReleaseDC( mygraph->graphwnd, hDC );
}


//----------------------------------------------------------------------
//
// ScanGraph
//
// Looks through a graph and extracts the highest point, remembering its
// position and returning its value.
//
//----------------------------------------------------------------------
double ScanGraph( graphdata *mygraph, int graphnum)
{
 	int i;
	
	mygraph->highpty[ graphnum ] = 0.0;
	mygraph->highptx[ graphnum ] = 0;
	for( i = 0; i< mygraph->graphwidth; i++) {
		if( mygraph->yval[ graphnum ][ i ] > mygraph->highpty[ graphnum ] ) {
			mygraph->highpty[ graphnum ] = mygraph->yval[ graphnum][ i ];
			mygraph->highptx[ graphnum ] = i;
		}
	}
	return mygraph->highpty[ graphnum ];
}


//----------------------------------------------------------------------
//
// PlotPoints
// 
// Draw the indicated point(s) on the graph of the indicated graph 
// bitmap and tells the graph it needs to update
//
//----------------------------------------------------------------------
void PlotPoints( graphdata *mygraph, double newy1, double newy2 )
{
	HDC  		hdcMem, hDC;
	RECT		rc;
	HBITMAP		oldbitmap;
	HBRUSH		hbrBkGnd;
	long		prevx, yval1, yval2;
	BOOL		rescale = FALSE;
	int			i, j, numclear;
	double		oldhigh, scale1 = 0, scale2 = 0;

	// do each point twice for better look
	for(j=0;j<2;j++ ) {

		// first, calculate what coordinate the new points represent
		yval1 = (long) (mygraph->halfheight[0] - (newy1 * mygraph->halfheight[0])/mygraph->xaxis[0]);
		yval2 = (long) (mygraph->halfheight[1] - (newy2 * mygraph->halfheight[1])/mygraph->xaxis[1]);

		// see if we've wrapped
		prevx = mygraph->xval;
		if( mygraph->xval == mygraph->graphwidth ) { 
			mygraph->xval = 0;
			mygraph->yprev[0] = yval1;
			mygraph->yprev[1] = yval2;
		} else if( !mygraph->xval ) {
			mygraph->yprev[0] = yval1;
			mygraph->yprev[1] = yval2;
			mygraph->xval++;
		} else {
			mygraph->xval++;
		}

		// see if we need to rescale because of running off the top
		if( newy1 >= mygraph->xaxis[0] )	{
			rescale = TRUE;
			scale1 = newy1 * 3.0/2.0;
		}
		if( newy2 >= mygraph->xaxis[1] )	{
			rescale = TRUE;
			scale2 = newy2 * 3.0/2.0;
		}
	
		// zero-out the y-values for points we are going to clear
		// do it now, because we may be getting rid of points that
		// might result in us needing to down-size or adjust highest point
		if( mygraph->xval > mygraph->graphbmp.bmWidth - CLEARAHEAD ) 
			numclear = mygraph->graphbmp.bmWidth - mygraph->xval;
		else
			numclear = CLEARAHEAD;
		for( i=0; i < numclear; i++) {
			mygraph->yval[0][ mygraph->xval+i ] = -1.0;
			if( mygraph->xval + i == mygraph->highptx[0] ) {
	 			oldhigh = mygraph->highpty[0];
				if( (mygraph->highpty[0] = ScanGraph( mygraph, 0 )) != oldhigh ) 
					NewHighPt( mygraph );
			}
			if( mygraph->numplots == 2 ) {
				if( mygraph->xval + i == mygraph->highptx[1] ) {
					oldhigh = mygraph->highpty[1];
					if( (mygraph->highpty[1] = ScanGraph( mygraph, 1 )) != oldhigh )
						NewHighPt( mygraph );
				}
				mygraph->yval[1][ mygraph->xval+i ] = -1.0;
			}
		}	

		// see if we need to down-size 
		if( mygraph->highpty[0] < mygraph->xaxis[0]/3 )	{
			rescale = TRUE;
			scale1 = mygraph->highpty[0] * 3/2;
		} 
		if( mygraph->numplots == 2 && mygraph->highpty[1] < mygraph->xaxis[1]/3 ) {
			rescale = TRUE;
			scale2 = mygraph->highpty[1] * 3/2; 
		}

		// do any rescaling
		if( rescale ) {
			RescaleGraph( mygraph, prevx, scale1, scale2 );

			// need to recalculate new points
			if( scale1 )
				yval1 = (long) (mygraph->halfheight[0] - (newy1 * mygraph->halfheight[0])/
								mygraph->xaxis[0]);
			if( scale2 )
				yval2 = (long) (mygraph->halfheight[1] - (newy2 * mygraph->halfheight[1])/
								mygraph->xaxis[1]);
		}

		// do we have a new high point?
		if( newy1 > mygraph->highpty[0] ) {
			mygraph->highpty[0] = newy1;
			mygraph->highptx[0] = mygraph->xval;
			NewHighPt( mygraph );
		}
		if( newy2 > mygraph->highpty[1] ) {
			mygraph->highpty[1] = newy2;
			mygraph->highptx[1] = mygraph->xval;
			NewHighPt( mygraph );
		}

	  	// prepare for drawing on bitmap
		hDC = GetDC( mygraph->graphwnd );
		hdcMem = CreateCompatibleDC (hDC) ;
		ReleaseDC( mygraph->graphwnd, hDC ); 
		oldbitmap = SelectObject( hdcMem, mygraph->hgraphbmp ) ;
				 
		// clear area ahead of point
		rc.left  = mygraph->xval;
		rc.top   = 0;
		rc.bottom = mygraph->graphbmp.bmHeight;
		rc.right = (mygraph->xval > mygraph->graphbmp.bmWidth - CLEARAHEAD) ? 
					mygraph->graphbmp.bmWidth : mygraph->xval + CLEARAHEAD;
		hbrBkGnd = GRAPHBKGRND;
		FillRect( hdcMem, &rc, hbrBkGnd );
	   	DeleteObject( hbrBkGnd );

		// draw the first point
		SelectObject( hdcMem, BluePen );
		if( mygraph->yprev[0] == -1 ) mygraph->yprev[0] = yval1;
		MoveToEx( hdcMem, mygraph->xval-1, mygraph->yprev[0], NULL );
		LineTo ( hdcMem, mygraph->xval, yval1 );

		// update previous point value and graph point value
		mygraph->yprev[0] = yval1;	
		mygraph->yval[0][mygraph->xval] = newy1;

		// draw the second point
		if( mygraph->numplots == 2 ) {
			SelectObject( hdcMem, RedPen );
			if( mygraph->yprev[1] == -1 ) mygraph->yprev[1] = yval2;
			MoveToEx( hdcMem, mygraph->xval-1, mygraph->yprev[1] + mygraph->halfheight[0] + 3, NULL );
			LineTo( hdcMem, mygraph->xval, yval2 + mygraph->halfheight[0] + 3 );

			// update previous point value
			mygraph->yprev[1] = yval2;
			mygraph->yval[1][mygraph->xval] = newy2;

			// draw the graph seperator
			DrawSeperator( hdcMem, mygraph->halfheight[0] + 1, mygraph->graphbmp.bmWidth ); 
		}

		// update labels
		NewHighPt( mygraph );

		// release the bitmap
		mygraph->hgraphbmp = SelectObject( hdcMem, oldbitmap );
		DeleteDC (hdcMem) ;
	}

	// force a paint of the graph
	GetClientRect( mygraph->graphwnd, &rc ); 
	rc.top 		= mygraph->axisbmp.bmHeight;
	rc.bottom 	-= mygraph->legendbmp.bmHeight; 
	InvalidateRect( mygraph->graphwnd, NULL, FALSE );
}


//----------------------------------------------------------------------
//
// CloseGraphs
//
// When the info box is closing, we close all the graph windows.
//
//----------------------------------------------------------------------
void CloseGraphs( HWND hDlg )
{
	int 	i;

	for( i = 0; i< NUMGRAPHS; i++ ) {
		if( Graph[i].graphstat ) {
			DestroyWindow( Graph[i].graphwnd );
			Graph[i].graphstat = 0;
		}
	}
}


//----------------------------------------------------------------------
// 
// InitializeGraphs
// 
// Each graph has its own title, legend, etc. that must be initialized
// once at application start-up.
//
//----------------------------------------------------------------------
void InitializeGraphs( HDC hDC )
{
	int			i;
	
	// create pens
	BluePen = CreatePen( PS_SOLID, 1, MYBLUE );
	RedPen 	= CreatePen( PS_SOLID, 1, MYRED );
	BlackPen = CreatePen( PS_SOLID, 1, MYBLACK );
	WhitePen = CreatePen( PS_SOLID, 1, MYWHITE );
	GrayPen	= CreatePen( PS_SOLID, 1, MYGRAY );

	// create font for use in graphs
	SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LogFont),
			 (PVOID) &LogFont, FALSE);

	// this is the height for 8 point size font in pixels
	LogFont.lfHeight = 8 * GetDeviceCaps( hDC, LOGPIXELSY ) / 72;

	hFont = CreateFontIndirect(&LogFont);
	GetTextMetrics(hDC, &FontTM);

	// intialize graph data structures
	for( i=0; i < NUMGRAPHS; i++ ) {
	 	Graph[i].graphstat  = 0;	
		Graph[i].buttonid	= 0;
		Graph[i].xaxis[0]	= 1.0;
		Graph[i].xaxis[1]	= 1.0;
		Graph[i].highpty[0]	= 0.0;
		Graph[i].highpty[1]	= 0.0;
		switch( i ) {
		case SIZEINDEX:
			Graph[i].numplots = 2;
			sprintf(Graph[i].graphtitle,"VCache Size" );
			sprintf(Graph[i].titles[0], "VCache Size (KB)" );
			sprintf(Graph[i].titles[1], "Free Memory (KB)" );
			break;
		case HITINDEX:
			Graph[i].numplots = 2;
			sprintf(Graph[i].graphtitle, "VCache Hit/Miss Rate" );
			sprintf(Graph[i].titles[0], "VCache Hits" );
			sprintf(Graph[i].titles[1], "VCache Misses" );
			break;
		case HOLDINDEX:
			Graph[i].numplots = 1;
			sprintf(Graph[i].graphtitle, "Block Holds" );
			sprintf(Graph[i].titles[0], "Holds" );
			break;
		case NEWINDEX:
			Graph[i].numplots = 1;
			sprintf(Graph[i].graphtitle, "Created Blocks" );
			sprintf(Graph[i].titles[0], "Created Blocks" );
			break;
		case REPINDEX:
			Graph[i].numplots = 2;
			sprintf(Graph[i].graphtitle, "Reported Statistics" );
			sprintf(Graph[i].titles[0], "Reported Hits" );
			sprintf(Graph[i].titles[1], "Reported Misses" );
			break;
		case PGFLTINDEX:
			Graph[i].numplots = 2;
			sprintf(Graph[i].graphtitle, "Paging Activity" );
			sprintf(Graph[i].titles[0], "Pagefile Reads" );
			sprintf(Graph[i].titles[1], "Pagefile Writes" );
			break; 
		}
	}
}


//----------------------------------------------------------------------
//  
// GraphProc
// 
// Windows routine for the logo window.
//
//----------------------------------------------------------------------
LONG APIENTRY  GraphProc( HWND hWnd, UINT message, WPARAM uParam, 
			    LPARAM lParam )
{
	HDC						hDC;
	static int				firstgraph = 1;
	static RECT				origrc;
	graphdata				*mygraph;

	switch( message) {
	case WM_CREATE:	{
		HDC				hdcMem;
		HBITMAP			oldbitmap;
		HBRUSH			hbrBkGnd;
		HFONT      		hOldFont = NULL;
		RECT			rc;
		int				j;
		char			tmp[256];

		// determine which graph we are
		mygraph = &Graph[ NewGraph ];
		mygraph->graphwnd = hWnd;
		SetWindowLong( hWnd, 0, (long) mygraph );

		// save original size of window
		if( firstgraph) {
			GetWindowRect( hWnd, &origrc );
			firstgraph = 0;
		}
		GetClientRect( hWnd, &rc );

		// grab device context
		hDC = GetDC( hWnd );

		// create the legend bitmap
		mygraph->hlegendbmp = CreateCompatibleBitmap( hDC, 
							rc.right-rc.left,
							FontTM.tmHeight );
		GetObject( mygraph->hlegendbmp, sizeof (BITMAP), 
						(LPSTR) &mygraph->legendbmp) ;
		hdcMem   	= CreateCompatibleDC (hDC) ;
		oldbitmap 	= SelectObject( hdcMem, mygraph->hlegendbmp ) ;

		// fill bitmap with grey background	and draw graph border
		FillStock( hdcMem, &rc, LTGRAY_BRUSH );
		DrawSeperator( hdcMem, 0, rc.right - rc.left );		

		// divide things up into different colors
		SetBkColor( hdcMem, GetSysColor(COLOR_BTNFACE));
		if (hFont) hOldFont = SelectObject( hdcMem, hFont);	
		if( mygraph->numplots == 1 ) {
			SetTextColor( hdcMem, MYBLUE );
			TextOut( hdcMem, 0, 3, mygraph->titles[0], strlen(mygraph->titles[0]) );
		} else {
			SetTextColor( hdcMem, MYRED );
			TextOut( hdcMem, 0, 3, mygraph->titles[1], strlen(mygraph->titles[1]) );
			sprintf( tmp, "0/%g", mygraph->highpty[1] );
			TextOut( hdcMem, rc.right/2, 3, tmp, strlen( tmp ));
		}
		if (hOldFont) SelectObject( hdcMem, hOldFont);
		mygraph->hlegendbmp = SelectObject( hdcMem, oldbitmap );
		DeleteDC (hdcMem) ;

		// creat axis label bitmap
		mygraph->haxisbmp = CreateCompatibleBitmap( hDC, 
							rc.right-rc.left,
							FontTM.tmHeight );
		GetObject( mygraph->haxisbmp, sizeof (BITMAP), 
						(LPSTR) &mygraph->axisbmp) ;
		hdcMem   	= CreateCompatibleDC (hDC) ;
		oldbitmap 	= SelectObject( hdcMem, mygraph->haxisbmp ) ;

		// fill bitmap with grey background and draw seperator
		FillStock( hdcMem, &rc, LTGRAY_BRUSH );
		DrawSeperator( hdcMem, mygraph->axisbmp.bmHeight - 2, rc.right - rc.left ); 

		// divide things up into different colors
		SetBkColor( hdcMem, GetSysColor(COLOR_BTNFACE));
		if (hFont) hOldFont = SelectObject( hdcMem, hFont);
		if( mygraph->numplots == 1 ) {
			SetTextColor( hdcMem, MYBLUE );
			sprintf( tmp, "0/%g", mygraph->highpty[0] ); 
			TextOut( hdcMem, 0, 0, tmp, strlen( tmp ) );
		} else { 
			SetTextColor( hdcMem, MYBLUE );
			TextOut( hdcMem, 0, 0, mygraph->titles[0], strlen( mygraph->titles[0] ) );
			sprintf( tmp, "0/%g", mygraph->highpty[0] );
			TextOut( hdcMem, rc.right/2, 0,
					tmp, strlen( tmp ));
		}
		if (hOldFont) SelectObject( hdcMem, hOldFont);
		mygraph->haxisbmp = SelectObject( hdcMem, oldbitmap );
		DeleteDC (hdcMem) ;	
		
		// calculate size of graphic bitmap
		mygraph->graphwidth = rc.right;
		mygraph->graphheight = rc.bottom - mygraph->legendbmp.bmHeight - 
						mygraph->axisbmp.bmHeight;			

		// create graphic bitmap
		mygraph->hgraphbmp = CreateCompatibleBitmap( hDC, 
						mygraph->graphwidth, mygraph->graphheight );
		GetObject( mygraph->hgraphbmp, sizeof (BITMAP), 
					(LPSTR) &mygraph->graphbmp) ;

		// fill with background color
		hdcMem 		= CreateCompatibleDC (hDC) ;
		oldbitmap 	= SelectObject( hdcMem, mygraph->hgraphbmp ) ;
		hbrBkGnd 	= GRAPHBKGRND;
    	FillRect(hdcMem, &rc, hbrBkGnd);
    	DeleteObject(hbrBkGnd);

		// draw the graph seperator	and set bitmap
		if( mygraph->numplots == 2)	{
			mygraph->halfheight[0] = (mygraph->graphheight - 2)/2 - 1;
			mygraph->halfheight[1] = mygraph->graphheight - mygraph->halfheight[0] - 4; 
			DrawSeperator( hdcMem, mygraph->halfheight[0] + 1, rc.right - rc.left );
		} else 
			mygraph->halfheight[0] = mygraph->graphheight - 1;
		mygraph->hgraphbmp	= SelectObject( hdcMem, oldbitmap );
		DeleteDC (hdcMem) ;

		// create and zero the data points associated with the graph
		mygraph->yval[0] = (double *) malloc( (mygraph->graphwidth+1) * sizeof(double) ); 
		if( mygraph->numplots == 2)
			mygraph->yval[1] = (double *) malloc( (mygraph->graphwidth+1) * sizeof(double) );
		for(j=0; j < mygraph->graphwidth; j++ ) {
			mygraph->yval[0][j] = -1.0;
			if( mygraph->numplots == 2 )
				mygraph->yval[1][j] = -1.0;
		}

		// draw	the bitmaps
		DrawBitmap( hDC, mygraph->haxisbmp, 0, 0, 
					rc.right, mygraph->axisbmp.bmHeight, 0 );

		DrawBitmap( hDC, mygraph->hgraphbmp, 0, 
					(short) mygraph->axisbmp.bmHeight, 
					rc.right, rc.bottom - mygraph->legendbmp.bmHeight - 
					mygraph->axisbmp.bmHeight, 0 );

		DrawBitmap( hDC, mygraph->hlegendbmp, 
					0, (short) (rc.bottom - mygraph->legendbmp.bmHeight), 
					(int) mygraph->legendbmp.bmWidth, (int) mygraph->legendbmp.bmHeight, 0 );

		ReleaseDC( hWnd, hDC );	
		
		// flush
		GdiFlush();		
		return 0;
		}
		
	case WM_GETMINMAXINFO:	{

		MINMAXINFO FAR* lpmmi;

		// don't let windows shrink to less than original size
    	lpmmi = (MINMAXINFO FAR*) lParam;
    	lpmmi->ptMinTrackSize.x = origrc.right;
    	lpmmi->ptMinTrackSize.y = origrc.bottom;
		return 0;
	}

	case WM_CLOSE:
		// time to go away
		mygraph = (graphdata *) GetWindowLong( hWnd, 0 );
		mygraph->graphstat = 0;

		// toggle our checkbox, if it exists
		if( mygraph->buttonid ) 
			CheckDlgButton( mygraph->graphdlg, mygraph->buttonid, 0);
		
		// free all resources associated with the graph
		if( mygraph->yval[0] ) free( mygraph->yval[0] );
		if( mygraph->yval[1] ) free( mygraph->yval[1] );
		DeleteObject( mygraph->hgraphbmp );
		DeleteObject( mygraph->haxisbmp );
		DeleteObject( mygraph->hlegendbmp );
		DestroyWindow( hWnd );
		return 0;			 

	case WM_PAINT: {
		RECT			bckrect, rc;
		PAINTSTRUCT		Paint;

		// get drawing critical section
		WaitForSingleObject( DrawCrit, INFINITE );

		// determine which graph I am
		mygraph = (graphdata *) GetWindowLong( hWnd, 0 );

		// get our DC
     	GetClientRect( hWnd, &rc);
		hDC = BeginPaint( hWnd, &Paint );

		// draw axis bitmap	and seperator
 		bckrect 		= rc;
		bckrect.bottom 	= mygraph->axisbmp.bmHeight - 2;
		bckrect.left 	= mygraph->axisbmp.bmWidth;
		FillStock( hDC, &bckrect, LTGRAY_BRUSH );
		DrawBitmap( hDC, mygraph->haxisbmp,
					0, 0,
					(int) mygraph->axisbmp.bmWidth, 
					(int) mygraph->axisbmp.bmHeight, 0 );
		DrawSeperator( hDC, mygraph->axisbmp.bmHeight - 2, 
						rc.right );

		// draw the graph
		DrawBitmap( hDC, mygraph->hgraphbmp, 0, 
					(short) mygraph->axisbmp.bmHeight, 
					rc.right, 
					(int) (rc.bottom - mygraph->legendbmp.bmHeight - 
					mygraph->axisbmp.bmHeight ), 1);

		// do background for text areas
		bckrect = rc;
		bckrect.top 	= rc.bottom - mygraph->legendbmp.bmHeight + 2;
		bckrect.left 	= mygraph->legendbmp.bmWidth;
		FillStock( hDC, &bckrect, LTGRAY_BRUSH );

		// draw legend bitmap and seperator
		DrawSeperator( hDC, rc.bottom - mygraph->legendbmp.bmHeight,
						rc.right );
		DrawBitmap( hDC, mygraph->hlegendbmp, 0, 
					(short) (rc.bottom - mygraph->legendbmp.bmHeight),
					(int) mygraph->legendbmp.bmWidth, 
					(int) mygraph->legendbmp.bmHeight, 0 );

		EndPaint( hWnd, &Paint );	
		
		// release critical section
		ReleaseSemaphore( DrawCrit, 1, NULL );
		break;
		}
	}
	return DefWindowProc ( hWnd, message, uParam, lParam); 
}



//----------------------------------------------------------------------
//
// ToggleGraph
//
// Turns graph on or off. If the graph is to be turned on, it means
// creating the graph from scratch and sending it updated information 
// whenever we get new information from the VxD.
//
//----------------------------------------------------------------------
VOID ToggleGraph( HWND hDlg, int buttonid, int index )
{
	HWND		hWnd, checkbox;
	RECT		rc;
	int			screenx, screeny;

	if( Graph[ index ].graphstat && !IsDlgButtonChecked( hDlg, buttonid)) {
	 	// graph is on so turn it off!
		DestroyWindow( Graph[ index ].graphwnd );
	} else if( !Graph[ index].graphstat && IsDlgButtonChecked( hDlg, buttonid )) {
		// set global variable so graph knows which one it is when it 
		// handles WM_CREATE - I can find no other way of sending instance
		// data to a window as it creates!
		NewGraph = index;

		// re-initialize key values
		Graph[index].graphdlg	= hDlg;
		Graph[index].buttonid	= buttonid;
		Graph[index].xval 		= 0;
		Graph[index].xaxis[0] 	= 1.0;
		Graph[index].xaxis[1] 	= 1.0;
		Graph[index].highpty[0]	= 0.0;
		Graph[index].highpty[1]	= 0.0;

		// turn the graph on
		hWnd = CreateWindow( "Graph", Graph[ index ].graphtitle, 
				WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
				WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
				WS_THICKFRAME,
				0, 0, GRAPHWIDTH, GRAPHHEIGHT,
				NULL, NULL, hInst, NULL );

		// determine where to place the graph (just to the right and below the checkbox)
		checkbox = GetDlgItem( hDlg, buttonid );
		GetWindowRect( checkbox, &rc );
		screenx = GetSystemMetrics( SM_CXSCREEN );
		screeny = GetSystemMetrics( SM_CYSCREEN );
		if( rc.right + GRAPHWIDTH >= screenx )
			rc.right = screenx - GRAPHWIDTH;
		if( rc.bottom + GRAPHHEIGHT >= screeny )
			rc.bottom = screeny - GRAPHHEIGHT;
		SetWindowPos( hWnd, 0, rc.right+XGRAPHSPACE, rc.bottom+YGRAPHSPACE,
					0, 0, SWP_NOSIZE );
	
		// Show window
		ShowWindow (hWnd, TRUE) ;
	}
	Graph[ index ].graphstat = !Graph[ index ].graphstat;
}


//----------------------------------------------------------------------
//
// UpdateInfoDialog
//
// Updates all the statistics text fields in the information dialog
// box.
//
//----------------------------------------------------------------------
void UpdateInfoDialog( HWND hDlg, BOOL init )
{
	static 	hitrate = 0;
	int		newrate;

	// cache size
	if( CurStats.size != LastStats.size || init ) { 
		sprintf( msgbuf, "%d KB", CurStats.size );
		SetDlgItemText( hDlg, IDC_VCSIZE, msgbuf );
	}
	// free memory
	if( CurStats.free != LastStats.free || init ) { 
		sprintf( msgbuf, "%d KB", CurStats.free );
		SetDlgItemText( hDlg, IDC_VCFREE, msgbuf );
	}
	// hit rate
	if( CurStats.hits != LastStats.hits || 
		CurStats.misses != LastStats.misses || init ) {
		if( CurStats.hits + CurStats.misses == 0 )	{
			sprintf( msgbuf, "0 %%" );
			SetDlgItemText( hDlg, IDC_VCHITRATE, msgbuf );
		} else {
			newrate = (int) (((double)CurStats.hits/
				((double)CurStats.misses + (double)CurStats.hits))*100.0);
			if( newrate != hitrate ) {
				sprintf( msgbuf, "%d %%", newrate );
				SetDlgItemText( hDlg, IDC_VCHITRATE, msgbuf );
				hitrate = newrate;
			}
		}
	}
	// hits
	if( CurStats.hits != LastStats.hits || init ) { 
		sprintf( msgbuf, "%d", CurStats.hits );
		SetDlgItemText( hDlg, IDC_VCHIT, msgbuf );
	}
	// misses
	if( CurStats.misses != LastStats.misses || init ) { 
		sprintf( msgbuf, "%d", CurStats.misses );
		SetDlgItemText( hDlg, IDC_VCMISS, msgbuf );
	}
	// holds
	if( CurStats.holds != LastStats.holds || init ) { 
		sprintf( msgbuf, "%d", CurStats.holds );
		SetDlgItemText( hDlg, IDC_VCHOLD, msgbuf );
	}
	// new blocks
	if( CurStats.new != LastStats.new || init ) { 
		sprintf( msgbuf, "%d", CurStats.new );
		SetDlgItemText( hDlg, IDC_VCNEW, msgbuf );
	}
	// pagefile reads
	if( CurStats.pagereads != LastStats.pagereads || init ) { 
		sprintf( msgbuf, "%d", CurStats.pagereads );
		SetDlgItemText( hDlg, IDC_VCPGREAD, msgbuf );
	}
	// pagefile writes
	if( CurStats.pagewrites != LastStats.pagewrites || init ) { 
		sprintf( msgbuf, "%d", CurStats.pagewrites );
		SetDlgItemText( hDlg, IDC_VCPGWRITE, msgbuf );
	}
	// reports hits
	if( CurStats.rephits != LastStats.rephits || init ) { 
		sprintf( msgbuf, "%d", CurStats.rephits );
		SetDlgItemText( hDlg, IDC_VCREPHIT, msgbuf );
	}
	// reports misses
	if( CurStats.repmisses != LastStats.repmisses || init ) { 
		sprintf( msgbuf, "%d", CurStats.repmisses );
		SetDlgItemText( hDlg, IDC_VCREPMISS, msgbuf );
	}
}


//----------------------------------------------------------------------
//
// StatsLoop
//
// Thread routine that sits looping waiting for performance statistics
// that are being passed to us by the VxD.
//
//----------------------------------------------------------------------
VOID StatsLoop( VOID *dummy )
{
	int			i;
	DWORD		cbRet;
	static		initialized = FALSE;

	for(;;) {
		// wait for event to be signalled	
		GetOverlappedResult( VxDHandle, &Ovrlp, &cbRet, TRUE);

		// get a snapshot of the current stats
		CurStats = *Stats_p;

		// update the text items
		UpdateInfoDialog( hMainDlg, FALSE );
		
		// update graphs
		WaitForSingleObject( DrawCrit, INFINITE );
		for( i=0; i< NUMGRAPHS; i++ ) {
			if( Graph[i].graphstat ) {
				switch( i ) {	
				case SIZEINDEX: {
				 	PlotPoints( &Graph[i],
						(double) CurStats.size, 
						(double) CurStats.free );
					break;
					}			
				case HITINDEX: {
					PlotPoints( &Graph[i],
				 		(double) (CurStats.hits - LastStats.hits ), 
				 		(double) (CurStats.misses - LastStats.misses) );
					break;
					}
				case HOLDINDEX: {
					PlotPoints( &Graph[i],
				 		(double) (CurStats.holds - LastStats.holds ), 0 );
					break;
					}
				case NEWINDEX: {
					PlotPoints( &Graph[i],
						(double) (CurStats.new - LastStats.new), 0 );
					break;
					}
				case PGFLTINDEX: {
					PlotPoints( &Graph[i],
						(double) (CurStats.pagereads - LastStats.pagereads), 
						(double) (CurStats.pagewrites - LastStats.pagewrites) );
					break;
					}  
				case REPINDEX: {
					PlotPoints( &Graph[i],
						(double) (CurStats.rephits - LastStats.rephits), 
						(double) (CurStats.repmisses - LastStats.repmisses) );
					break;				
					} 
				}
			}
		}
		ReleaseSemaphore( DrawCrit, 1, NULL );

		// update last stats
		LastStats = CurStats;
	}
	// end thread implied 
}


//----------------------------------------------------------------------
// 
// Reset
//
// Resets statistics and has graphs start drawing fresh.
//
//----------------------------------------------------------------------
void Reset( HWND hDlg )
{
	HDC				hdcMem, hDC;
	HBITMAP			oldbitmap;
	HBRUSH			hbrBkGnd;
	RECT			rc;
	int   			index, j;

	// get drawing critical section
	WaitForSingleObject( DrawCrit, INFINITE );

	// zero the data structure
 	LastStats.rephits 	= Stats_p->rephits 		= 0;
	LastStats.repmisses = Stats_p->repmisses	= 0;
	LastStats.holds 	= Stats_p->holds		= 0;
	LastStats.misses 	= Stats_p->misses		= 0;
	LastStats.hits 		= Stats_p->hits			= 0;
	LastStats.pagereads = Stats_p->pagereads	= 0;
	LastStats.pagewrites= Stats_p->pagewrites	= 0;
	LastStats.new 		= Stats_p->new			= 0;
	CurStats = LastStats;

	// grab device context
	hDC = GetDC( hDlg );

	// clear the graphs and restart them
	for( index = 0; index < NUMGRAPHS; index++ ) {
		if( Graph[ index ].graphstat ) {
			Graph[index].xval 		= 0;

			// redraw the graph image as cleared
			hdcMem 		= CreateCompatibleDC (hDC) ;
			oldbitmap 	= SelectObject( hdcMem, Graph[index].hgraphbmp ) ;
			hbrBkGnd 	= GRAPHBKGRND;
			rc.top = rc.left = 0;
			rc.right 	= Graph[index].graphwidth;
			rc.bottom	= Graph[index].graphheight;
	    	FillRect( hdcMem, &rc, hbrBkGnd );
	    	DeleteObject(hbrBkGnd);

			// draw the graph seperator	and set bitmap
			if( Graph[index].numplots == 2)	{
				Graph[index].halfheight[0] = (Graph[index].graphheight - 2)/2 - 1;
				Graph[index].halfheight[1] = Graph[index].graphheight - 
									Graph[index].halfheight[0] - 4; 
				DrawSeperator( hdcMem, Graph[index].halfheight[0] + 1, 
								Graph[index].graphwidth );
			} else 
				Graph[index].halfheight[0] = Graph[index].graphheight - 1;
			Graph[index].hgraphbmp	= SelectObject( hdcMem, oldbitmap );
			DeleteDC (hdcMem) ;

			for(j=0; j < Graph[index].graphwidth; j++ ) {
				Graph[index].yval[0][j] = -1.0;
				if( Graph[index].numplots == 2 )
					Graph[index].yval[1][j] = -1.0;
			}
		}
	}	 

	ReleaseDC( hDlg, hDC );	
	UpdateInfoDialog( hDlg, TRUE );

	// release critical section
	ReleaseSemaphore( DrawCrit, 1, NULL );
}


//----------------------------------------------------------------------
//  
// VxDConnect
// 
// Initializes connection with the VxD. This includes passing it our
// event handle and obtaining the address of the statistics data structure.
//
//----------------------------------------------------------------------
BOOL VxDConnect( HWND hDlg )
{	
	DWORD 		cbRet;

	// connect with VxD
	VxDHandle = CreateFile( VXD_NAME, 0, 0, NULL,
							0, FILE_FLAG_OVERLAPPED|
							FILE_FLAG_DELETE_ON_CLOSE,
							NULL );
	if ( VxDHandle == INVALID_HANDLE_VALUE )  {
		wsprintf( msgbuf, "%s is not loaded properly.", VXD_NAME );
		Abort( hDlg, msgbuf );
		return FALSE;
	}

	// Create event which will be used by VxD to indicate operation is
	// complete
	if ( !(Ovrlp.hEvent = CreateEvent( 0, FALSE, 0, NULL )) ) {
       	wsprintf( msgbuf, "Windows is out of resources." );
       	Abort( hDlg, msgbuf);
		return FALSE;
	}

	// get the address of the stats structure
	if ( ! DeviceIoControl(	VxDHandle, VCMON_STATUS,
				NULL, 0, &Stats_p, sizeof( &Stats_p ), 
							&cbRet, &Ovrlp ) ) {
		wsprintf( msgbuf, "Can't get %s status.", VXD_NAME );
		Abort( hDlg, msgbuf );
		return FALSE;
	}

	// fire-off the statistics handling thread
	_beginthread( StatsLoop, 0, NULL );
	return TRUE;
}

						
//----------------------------------------------------------------------
//  
// MainDialog
//
// This is the main window. It presents the statistics to the user
// about VCache.
//
//----------------------------------------------------------------------
LONG APIENTRY MainDialog( HWND hDlg, UINT message, UINT wParam,
                       		LONG lParam ) {
	static HDC      hDC;
	static RECT     FrameRect;
	static HBITMAP  hBitmap, hSMBitmap, hBmp;
	static HWND		hFrame;

	switch (message) {
	case WM_INITDIALOG :
		// get control information
		hFrame = GetDlgItem ( hDlg, IDC_FRAME );
		GetClientRect( hFrame, &FrameRect );
		hBitmap   	= LoadBitmap( hInst, MAKEINTRESOURCE(IDB_LOGO));
		hSMBitmap 	= LoadBitmap( hInst, MAKEINTRESOURCE(IDB_SMLOGO));

		// create a semaphore for multi-threading
		DrawCrit = CreateSemaphore( NULL, 1, 1, NULL );

		// establish connection with the VxD
	    if( !VxDConnect( hDlg ))
			return TRUE;

		// set-up graph data structures	and font
		hDC = GetDC( hDlg );
		InitializeGraphs( hDC );
		ReleaseDC( hDlg, hDC );
			
		// center the dialog
		centerWindow( hDlg );

		// intialize text fields
		CurStats = *Stats_p;
		UpdateInfoDialog( hDlg, TRUE );
		LastStats = CurStats;
		break;

	case WM_PAINT: {
		PAINTSTRUCT  Paint;
		POINT		 frametop, windowpt;

		// get location where picture will be drawn
		windowpt.x = windowpt.y = 0;
		frametop.x = frametop.y = 0;
		ClientToScreen( hDlg, 	&windowpt );
		ClientToScreen( hFrame, &frametop );

		// draws the cool bitmap logo onto the dialog box
		hDC = BeginPaint( hDlg, &Paint ); 
		if( FrameRect.bottom < 140 ) 
			hBmp = hSMBitmap;
		else
			hBmp = hBitmap;
		DrawBitmap( hDC, hBmp, (short) (frametop.x - windowpt.x), 
					(short) (frametop.y - windowpt.y), 
					FrameRect.right, 
					FrameRect.bottom, 1 );
		EndPaint( hDlg, &Paint );
		break;
		}
 
	case WM_COMMAND :
		switch (LOWORD( wParam )) {
		
		// monitoring graph check boxes
		case IDC_GRSIZE:
			ToggleGraph( hDlg, IDC_GRSIZE, SIZEINDEX );
			break;

		case IDC_GRHITS:
			ToggleGraph( hDlg, IDC_GRHITS, HITINDEX );
			break;

		case IDC_GRHOLD:
			ToggleGraph( hDlg, IDC_GRHOLD, HOLDINDEX );
			break;

		case IDC_GRNEW:
			ToggleGraph( hDlg, IDC_GRNEW,  NEWINDEX );
			break;

		case IDC_GRPGFLT:
			ToggleGraph( hDlg, IDC_GRPGFLT,  PGFLTINDEX );
			break;

		case IDC_GRREP:
			ToggleGraph( hDlg, IDC_GRREP,  REPINDEX );
			break;

		case IDRESET:
			Reset( hDlg );
			break;

		case IDOK :
			CloseGraphs( hDlg );
			EndDialog (hDlg, 0) ;
			PostQuitMessage (0) ;
			break ;

		case IDABOUT:
			DialogBox( hInst, "ABOUT", hDlg, AboutDlgProc );
			break;
			
		case IDRATE:
			DialogBox( hInst, "SLIDER", hDlg, SliderDlgProc );
			break;			
		}
		break; 

	case WM_DESTROY:
		DeleteObject( RedPen );
		DeleteObject( BluePen );
		DeleteObject( BlackPen );
		DeleteObject( WhitePen );
		DeleteObject( GrayPen );
		if( hFont ) DeleteObject( hFont );
		break;

	case WM_CLOSE:	
		CloseGraphs( hDlg );
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
	static char szAppName [] = "VCMON" ;
	MSG         msg ;
	WNDCLASSEX  wndclass ;

	hInst = hInstance;

	if (!hPrevInstance) {
		// create the main window class
		wndclass.cbSize			= sizeof( WNDCLASSEX );
		wndclass.style          = CS_HREDRAW | CS_VREDRAW ;
 		wndclass.lpfnWndProc    = (WNDPROC) MainDialog ;
		wndclass.cbClsExtra     = 0 ;
		wndclass.cbWndExtra     = DLGWINDOWEXTRA ;
		wndclass.hInstance      = hInstance ;
		wndclass.hIcon          = LoadIcon (hInstance, "AVCICON") ;
		wndclass.hIconSm		= LoadIcon (hInstance, "AVCICON");
		wndclass.hCursor        = LoadCursor (NULL, IDC_ARROW) ;
		wndclass.hbrBackground  = (HBRUSH) (COLOR_BTNFACE+1);
		wndclass.lpszMenuName   = NULL ;
		wndclass.lpszClassName  = szAppName ;
		RegisterClassEx (&wndclass) ;

		// create the graph window class
 		wndclass.lpfnWndProc    = (WNDPROC) GraphProc ;
		wndclass.cbWndExtra     = sizeof( graphdata * ) ;
		wndclass.hInstance      = hInstance ;
		wndclass.hIcon          = LoadIcon( hInstance, "GRAPHICON" );
		wndclass.hIconSm		= LoadIcon( hInstance, "GRAPHICON" );
		wndclass.hCursor        = LoadCursor (NULL, IDC_ARROW) ;
		wndclass.hbrBackground  = NULL; 
		wndclass.lpszClassName  = "Graph" ;
		RegisterClassEx( &wndclass ) ;
	}

	// init common controls
	InitCommonControls();
 
 	// create the dialog
	hMainDlg = CreateDialog( hInstance, "VCMON", NULL, (DLGPROC)MainDialog) ;
	ShowWindow( hMainDlg, nCmdShow) ;
 
	while (GetMessage (&msg, NULL, 0, 0)) {
		if( !IsDialogMessage( hMainDlg, &msg )) {
          TranslateMessage (&msg) ;
          DispatchMessage (&msg) ;
		}
	}
	return msg.wParam ;
}

