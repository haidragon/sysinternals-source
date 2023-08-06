/******************************************************************************
*
*       VxdMon - VxD Device Monitor
*		
*		Copyright (c) 1996 Mark Russinovich and Bryce Cogswell
*
*		You have the right to take this code and use it in whatever way you
*		wish.
*
*    	PROGRAM: VxdMon.c
*
*    	PURPOSE: Communicates with the VxdMon VxD to display performance 
*				information	virtual device activity.
*
******************************************************************************/

#include <windows.h>    // includes basic windows functionality
#include <commctrl.h>   // includes the common control header
#include <stdio.h>
#include <string.h>
#include "resource.h"
#include "../vxd/vxdmon.h"

// Variables/definitions for the VxD that performs the actual monitoring.
#define			VXD_NAME		"\\\\.\\VXDMHLP.VXD"
static HANDLE	vxd_handle		= INVALID_HANDLE_VALUE;

// White space in services.dat and profile.dat
#define ISWHITE(c)	( (c)==' ' || (c)=='\t' || (c)=='\n' || (c)=='\r' )

// Indicate whether a service is hooked or not, or for higher levels of tree, whether
// sub-items are hooked or not.  These values correspond to the ImageList for the 
// selection tree.
enum {
	HOOK_NO		= 0,			// Not hooked
	HOOK_YES	= 1,			// Hooked
	HOOK_PART	= 2,			// Subset of subitems hooked
};

// Structure used to define each hookable service.  The set of services is defined
// by the services.dat file.
struct service {
	DWORD				Ordinal;		// VxD and service number
	char			*	Name;			// Name of service
	HTREEITEM			hTree;			// Pointer to item in service-selection tree
	BOOL				Hooked;			// Whether service is currently hooked
	struct service	*	Next;			// Next service pointer
};
// The global list of hookable services
struct service		*	ServiceList = NULL;


// Variables for reading statistics from our VxD
#define					MAX_SERVICE		2000
// Buffer into which VxD can copy statistics
struct vxdmon_stats		Stats[ MAX_SERVICE ];
// Current fraction of buffer filled
DWORD					numStats;

// Application instance handle
HINSTANCE				hInst;

// General buffer for storing temporary strings
static char				msgbuf[ 200 ];

// Root of selection tree
HTREEITEM				hSelectRoot;
// Image list for selection tree
HIMAGELIST  			hSelectImageList;
// Image list for ancestor/descendant trees
HIMAGELIST				hCallImageList;

// General cursor manipulation
HCURSOR 				hSaveCursor;
HCURSOR 				hHourGlass;

// name of stats file
char					szFileName[256];
BOOL					GotFile 		= FALSE;	// Do we have a valide file name

// name of help file
char					szHelpFileName[] = "VXDMON.HLP";

// name of call graph file
char					CallFileName[256];

// global status variables
BOOL					ProfileChanged	= FALSE; 	// new hook settings
BOOL					IgnoreUncalled	= TRUE;		// Ignore services never called
BOOL					Overhead		= TRUE;		// Compensate for hooking overhead
DWORD					OverheadCycles	= 0;		// Overhead incurred by VxD monitoring code

// Potential states of call-tree items.
// These correspond to the call-tree image list.
enum {
	CALL_NONE	= 0,
	CALL_UP		= 1,
	CALL_DOWN	= 2,
}; 

// Which service are we currently displaying a call tree for?
struct vxdmon_stats *	CallTreeStat;
BOOL					CallTreeDown	= FALSE;	// tree grows up or down?

BOOL					ResetStatsOnUpdate	= TRUE;

// procs
long APIENTRY MainWndProc( HWND, UINT, UINT, LONG );
long APIENTRY TreeWndProc( HWND, UINT, UINT, LONG );
long APIENTRY CallWndProc( HWND, UINT, UINT, LONG );
BOOL APIENTRY About( HWND, UINT, UINT, LONG );
//functions
BOOL InitApplication( HANDLE );
HWND InitInstance( HANDLE, int );
HWND CreateListView( HWND );
LRESULT NotifyHandler( HWND, UINT, WPARAM, LPARAM );
int CALLBACK ListViewCompareProc( LPARAM, LPARAM, LPARAM );
HWND CreateTreeView( HWND hWndParent );
BOOL AddTreeViewItems( HWND hwndTree );
HTREEITEM AddOneTreeViewItem( HWND hwndTree, HTREEITEM hParent, HTREEITEM hInsAfter,
							LPSTR szText, DWORD lParam );
BOOL DefaultTreeSelections( HWND hwndTree );
BOOL HookServices( HWND hTree, HTREEITEM hItem );
POINT ClickPoint( HWND hWnd );
BOOL AddCallTreeItems( HWND hTree, HTREEITEM Parent, struct vxdmon_stats * stats, int depth );
void WriteDefaultTreeSelections( HWND hTree, HTREEITEM hItem );
void CreateCallTree( HWND hWnd, struct vxdmon_stats * item, BOOL calldown );
BOOL GetFileName( HWND hWnd, char *FileName, char *Filter, char *Ext );
void SaveStatisticsData( HWND hWnd, HWND hWndListView );
void RefreshStatistics( HWND hWnd, HWND hWndListView, int SortItem );

/******************************************************************************
*
*	FUNCTION:	Abort:
*
*	PURPOSE:	Handles emergency exit conditions.
*
*****************************************************************************/
void Abort( HWND hWnd, char * Msg )
{
	MessageBox( hWnd, Msg, "VxdMon", MB_OK );
	PostQuitMessage( 1 );
}

/******************************************************************************
*
*	FUNCTION:	StringDup:
*
*	PURPOSE:	Convenience function for duplicating a string into heap-allocated memory.
*
******************************************************************************/
char * StringDup( const char * str )
{
	char * new = LocalAlloc( LPTR, strlen(str)+1 );
	strcpy( new, str );
	return new;
}

/******************************************************************************
*
*	FUNCTION:	LookupService
*
*	PURPOSE:	Searches the list of services defined by services.dat for a particular ordinal.
*
******************************************************************************/
struct service * LookupService( DWORD Ordinal )
{
	struct service * p = ServiceList;
	while ( p  &&  p->Ordinal != Ordinal )
		p = p->Next;
	return p;
}

/******************************************************************************
*
*	FUNCTION:	NewService
*
*	PURPOSE:	Allocates space for and initializes a new service.
*				Used while reading the services.dat file.
*
******************************************************************************/
struct service * NewService( DWORD Ordinal, const char * Name )
{
	struct service *	New	 = LocalAlloc( LPTR, sizeof *New );
	// Initialize info for new service
	New->Ordinal	= Ordinal;
	New->Name		= StringDup( Name );
	// Add to list of services
	New->Next		= ServiceList;
	ServiceList		= New;
	// Return newly created service
	return New;
}

/******************************************************************************
*
*	FUNCTION:	ServiceName
*
*	PURPOSE:	Convenience function for both searching for a service by ordinal, and then
*				returning the name of the service.
*
******************************************************************************/
char * ServiceName( DWORD ord )
{
	struct service * s = LookupService( ord );
	if ( s )
		return s->Name;
	else
		return "???";
}



/****************************************************************************
*
*	FUNCTION: WinMain(HANDLE, HANDLE, LPSTR, int)
*
*	PURPOSE:	calls initialization function, processes message loop
*
****************************************************************************/
int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
						LPSTR lpCmdLine, int nCmdShow )
{
	MSG 	msg;      
	HWND	hWnd;
	HANDLE 	hAccel ;                 

	if ( ! InitApplication( hInstance ) )
		return FALSE;     

	/* Perform initializations that apply to a specific instance */
	if ( (hWnd = InitInstance( hInstance, nCmdShow )) == NULL )
		return FALSE;

  	// load menu accelerators
	hAccel = LoadAccelerators ( hInstance, "Accelerators") ;

	/* Acquire and dispatch messages until a WM_QUIT message is received. */
	while ( GetMessage( &msg, NULL, 0, 0 ) )  {
		if (!TranslateAccelerator (hWnd, hAccel, &msg)) {
			TranslateMessage( &msg );
			DispatchMessage( &msg ); 
		}
	}
	return msg.wParam;										 
}


/****************************************************************************
*
*    FUNCTION: InitApplication(HANDLE)
*
*    PURPOSE: Initializes window data and registers window class
*
****************************************************************************/
BOOL InitApplication( HANDLE hInstance )	/* current instance             */
{
	WNDCLASS  wc;
	
	/* Fill in window class structure with parameters that describe the       */
	/* main (statistics) window.                                                           */
	wc.style			= 0;                     
	wc.lpfnWndProc		= (WNDPROC)MainWndProc; 
	wc.cbClsExtra		= 0;              
	wc.cbWndExtra		= 0;              
	wc.hInstance		= hInstance;       
	wc.hIcon			= LoadIcon( hInstance, "APPICON" );
	wc.hCursor			= LoadCursor( NULL, IDC_ARROW );
	wc.hbrBackground	= GetStockObject( /*WHITE_BRUSH*/ LTGRAY_BRUSH ); 
	wc.lpszMenuName		= "LISTMENU";  
	wc.lpszClassName	= "VxDMonClass";
	if ( ! RegisterClass( &wc ) )
		return FALSE;


	/* Fill in window class structure with parameters that describe the       */
	/* service-selection tree view window.                                                           */
	wc.style			= 0;                     
	wc.lpfnWndProc		= (WNDPROC)TreeWndProc; 
	wc.cbClsExtra		= 0;              
	wc.cbWndExtra		= 0;              
	wc.hInstance		= hInstance;       
	wc.hIcon			= LoadIcon( hInstance, "TREEICON" );
	wc.hCursor			= LoadCursor( NULL, IDC_ARROW );
	wc.hbrBackground	= GetStockObject( /*WHITE_BRUSH*/ LTGRAY_BRUSH ); 
	wc.lpszMenuName		= "TREEMENU";  
	wc.lpszClassName	= "VxDMonTreeClass";
	if ( ! RegisterClass( &wc ) )
		return FALSE;

	/* Fill in window class structure with parameters that describe the       */
	/* call tree window.                                                           */
	wc.style			= 0;                     
	wc.lpfnWndProc		= (WNDPROC)CallWndProc; 
	wc.cbClsExtra		= 0;              
	wc.cbWndExtra		= sizeof(DWORD);	// reserve memory for tree handle
	wc.hInstance		= hInstance;       
	wc.hCursor			= LoadCursor( NULL, IDC_ARROW );
	wc.hbrBackground	= GetStockObject( /*WHITE_BRUSH*/ LTGRAY_BRUSH ); 
	wc.lpszMenuName		= "CALLMENU";  
	// register call-down class
	wc.lpszClassName	= "VxDMonCallDownClass";
	wc.hIcon			= LoadIcon( hInstance, "CALLEE" );
	if ( ! RegisterClass( &wc ) )
		return FALSE;
	// register call-up class
	wc.lpszClassName	= "VxDMonCallUpClass";
	wc.hIcon			= LoadIcon( hInstance, "CALLER" );
 	if ( ! RegisterClass( &wc ) )
		return FALSE;

	return TRUE;
}


/****************************************************************************
*
*    FUNCTION:  InitInstance(HANDLE, int)
*
*    PURPOSE:  Saves instance handle and creates main window
*
****************************************************************************/
HWND InitInstance( HANDLE hInstance, int nCmdShow )
{
	HWND hWndMain;

	hInst = hInstance;

	hWndMain = CreateWindow( "VxDMonClass", "VxD Monitor", 
							WS_OVERLAPPEDWINDOW,
							CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
							NULL, NULL, hInstance, NULL );

	/* If window could not be created, return "failure" */
	if ( ! hWndMain )
		return NULL;
	
	/* Make the window visible; update its client area; and return "success" */
	ShowWindow( hWndMain, nCmdShow );
	UpdateWindow( hWndMain ); 
	return hWndMain;      
}

/****************************************************************************
*
*    FUNCTION: MainWndProc(HWND, unsigned, WORD, LONG)
*
*    PURPOSE:  Processes messages for the statistics window.
*
****************************************************************************/
LONG APIENTRY MainWndProc( HWND hWnd, UINT message, UINT wParam, LONG lParam) 
{
	static HWND		hTree;
	static HWND		hWndListView;
	static int		SortItem = -1;
	DWORD			row, statcmd;
	LV_HITTESTINFO	htInfo;
	DWORD			nb;
	LV_ITEM			Item;

	switch ( message ) {

		case WM_CREATE:

			// get hourglass icon ready
			hHourGlass = LoadCursor( NULL, IDC_WAIT );

			// Create the listview within the main window
			hWndListView = CreateListView( hWnd );
			if ( hWndListView == NULL )
				MessageBox( NULL, "Listview not created!", NULL, MB_OK );

		    // open the handle to the VXD
			vxd_handle = CreateFile( VXD_NAME, 0, 0, NULL,
									0, FILE_FLAG_DELETE_ON_CLOSE, NULL );
			if ( vxd_handle == INVALID_HANDLE_VALUE )  {
				wsprintf( msgbuf, "Opening %s: error %d", VXD_NAME, 
								GetLastError( ) );
				Abort( hWnd, msgbuf );
			}
			// Have VxD compute overhead of service hooking
			// Do this before hooking any services
			if ( ! DeviceIoControl(	vxd_handle, VXDMON_getoverhead,
									NULL, 0, &OverheadCycles, sizeof(DWORD),
									&nb, NULL ) )
			{
				MessageBox( hWnd, "Couldn't get monitoring overhead", NULL, MB_OK );
				OverheadCycles = 0;
			}

			// Create the service selection tree view, but don't show it yet.
			hTree = CreateWindow(
						"VxDMonTreeClass", "Hook Selection", WS_OVERLAPPEDWINDOW,
						CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
						NULL, NULL, hInst, NULL );
			if ( !hTree )  {
				wsprintf( msgbuf, "Treeview not created: Error %d", GetLastError() );
				MessageBox( NULL, msgbuf, NULL, MB_OK );
			}

			// Create and fill image list for call tree view
			hCallImageList = ImageList_Create( 16, 16, ILC_MASK, 2, 1 );
			ImageList_AddIcon( hCallImageList, LoadIcon( hInst, "SELECTED"  ));
			ImageList_AddIcon( hCallImageList, LoadIcon( hInst, "CALLER" ));
			ImageList_AddIcon( hCallImageList, LoadIcon( hInst, "CALLEE" ));

			// Initialize check marks on menu items
			CheckMenuItem( GetMenu( hWnd ), IDM_RESETONUPDATE, 
							MF_BYCOMMAND | (ResetStatsOnUpdate?MF_CHECKED:MF_UNCHECKED) );

			break;

		case WM_NOTIFY:
			// Make sure its intended for us
			if ( wParam == ID_LISTVIEW )  {
				NM_LISTVIEW	* pNm = (NM_LISTVIEW *)lParam;
				switch ( pNm->hdr.code )  {

			        case LVN_BEGINLABELEDIT:
						// Don't allow editing of service information
						return TRUE;

					case LVN_COLUMNCLICK:
						// post hourglass icon
						SetCapture( hWnd );
						hSaveCursor = SetCursor(hHourGlass);

						// The user clicked a column header - sort by this criterion.
						ListView_SortItems( pNm->hdr.hwndFrom, ListViewCompareProc,
											(LPARAM)pNm->iSubItem );
						SortItem = pNm->iSubItem;

						// notify user that operation is done
						SetCursor( hSaveCursor );
						ReleaseCapture();
						break;

					case NM_DBLCLK:
					case NM_RDBLCLK:
						// On double click pull up call-tree view for item
						htInfo.pt = ClickPoint( hWndListView );
						// Determine service clicked upon
						row = ListView_HitTest( hWndListView, &htInfo );
						if ( row != (DWORD)-1  &&  htInfo.flags & LVHT_ONITEMLABEL )  {
							// Create call graph for item
							LV_ITEM		Item;
							// Get additional information about service clicked on
							Item.mask		= LVIF_PARAM;
							Item.iItem		= row;
							Item.iSubItem	= 0;
							if ( ! ListView_GetItem( hWndListView, &Item ) )
								return 0;
							// Create the call tree window
							CreateCallTree( hWnd,
											(struct vxdmon_stats *) Item.lParam,
											pNm->hdr.code == NM_DBLCLK );
						}
						break;
				}
			}
			return 0;

		case WM_COMMAND:

			switch ( LOWORD( wParam ) )	 {

				case IDM_TREEVIEW:
					/* Make the service selection window visible and update its client area */
					ShowWindow( hTree, SW_SHOWNORMAL );
					BringWindowToTop( hTree );
					UpdateWindow( hTree ); 
					return TRUE;      

				// stats related commands to send to VxD
				case IDM_ZEROSTATS:
					// Zero information on screen
					for ( row = 0; row < numStats; ++row )  {
						Stats[row].Enter	= 0;
						Stats[row].Exit		= 0;
						Stats[row].Time		= 0;
					}
					// Have VxD likewise zero information
					if ( ! DeviceIoControl(	vxd_handle, VXDMON_zerostats,
											NULL, 0, NULL, 0, NULL, NULL ) )
					{
						Abort( hWnd, "Couldn't access VxD" );
						return TRUE;
					}
					// Update statistics windows
					RefreshStatistics( hWnd, hWndListView, SortItem );
					return 0;

				// Toggle the reset-on-update variable
				case IDM_RESETONUPDATE:
					ResetStatsOnUpdate = !ResetStatsOnUpdate;
					CheckMenuItem( GetMenu( hWnd ), IDM_RESETONUPDATE, 
									MF_BYCOMMAND | (ResetStatsOnUpdate?MF_CHECKED:MF_UNCHECKED) );
					break;

				// Read statistics from VxD
				case IDM_SAMPLE:
					if ( ResetStatsOnUpdate )
						statcmd = VXDMON_getzerostats;
					else
						statcmd = VXDMON_getstats;
					// Have VxD fill Stats buffer with information
					if ( ! DeviceIoControl(	vxd_handle, statcmd,
											NULL, 0, &Stats, sizeof Stats,
											&numStats, NULL ) )
					{
						Abort( hWnd, "Couldn't access VxD" );
						return TRUE;
					}
					numStats /= sizeof Stats[0];

					// Update statistics windows
					RefreshStatistics( hWnd, hWndListView, SortItem );
					return 0;

				case IDM_ANCESTORS:
				case IDM_DESCENDANTS:
					// Create call graph for currently selected item
					Item.iItem = ListView_GetNextItem( hWndListView, -1, LVNI_ALL|LVNI_SELECTED ); 
					if ( Item.iItem == -1 )
						return 0;
					Item.iSubItem = 0;
					Item.mask = LVIF_PARAM;
					if ( ! ListView_GetItem( hWndListView, &Item ) )
						return 0;

					// Create the window
					CreateCallTree( hWnd,
									(struct vxdmon_stats *) Item.lParam,
									LOWORD(wParam) == IDM_DESCENDANTS );
					return 0;

				case IDM_SAVEAS:
					// Save current selections
 					if( GotFile = GetFileName( hWnd, szFileName, "VxDMon Files (*.VMN)\0*.VMN\0", "VMN" )) 
						SaveStatisticsData( hWnd, hWndListView );
					break;

				case IDM_SAVE:
					// Save current selections
					if( !GotFile )
						GotFile = GetFileName( hWnd, szFileName, "VxDMon Files (*.VMN)\0*.VMN\0", "VMN" );
					if( GotFile ) 
						SaveStatisticsData( hWnd, hWndListView );
					break;

				case IDM_IGNORE:
					// Toggle ignore-uncalled-services variable
					IgnoreUncalled = !IgnoreUncalled;
					CheckMenuItem( GetMenu( hWnd ), IDM_IGNORE, 
									MF_BYCOMMAND | (!IgnoreUncalled?MF_CHECKED:MF_UNCHECKED) );
					// Update statistics windows
					RefreshStatistics( hWnd, hWndListView, SortItem );
					return 0;

				case IDM_OVERHEAD:
					// Toggle compensate-for-monitoring-overhead variable
					Overhead = !Overhead;
					CheckMenuItem( GetMenu( hWnd ), IDM_OVERHEAD, 
									MF_BYCOMMAND | (Overhead?MF_CHECKED:MF_UNCHECKED) );
					// Update statistics windows
					RefreshStatistics( hWnd, hWndListView, SortItem );
   					return 0;

				case IDM_EXIT:
					// see if we should prompt about saving current hook profile
					if( ProfileChanged && 
						MessageBox( NULL, "Save current hook profile before exiting?", 
								"Profile Changed", MB_YESNO ) == IDYES )
						SendMessage( hTree, WM_COMMAND, (WPARAM) IDM_SAVE, (LPARAM) 0 );
					PostQuitMessage( 0 );
					break;

				case IDM_HELP:
					// Pull up on-line help
					WinHelp( hWnd, szHelpFileName, HELP_INDEX, 0L);	  	
					break;

				case IDM_ABOUT:
					// Show the names of the authors
					DialogBox( hInst, "AboutBox", hWnd, (DLGPROC)About );
					break;

				default:
					// Default behavior
					return DefWindowProc( hWnd, message, wParam, lParam );
		}
		break;

		case WM_SIZE:
			// Move or resize the listview
            MoveWindow( hWndListView, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE );
            break;

		case WM_DESTROY:
			// see if we should prompt about saving current hook profile
			if( ProfileChanged && 
				MessageBox( NULL, "Save current hook profile before exiting?", 
						"Profile Changed", MB_YESNO ) == IDYES)
				SendMessage( hTree, WM_COMMAND, (WPARAM) IDM_SAVE, (LPARAM) 0 );
			// Exit application
			PostQuitMessage( 0 );
			break;

		default:
			// Default behavior
			return DefWindowProc( hWnd, message, wParam, lParam );
	}
	return 0;
}

/******************************************************************************
*
*	FUNCTION:	RefreshStatistics
*
*	PURPOSE:	Clear the statistics window and refill it with the current 
*				contents of the statistics buffer.  Does not refresh the 
*				buffer from the VxD.
*
******************************************************************************/
void RefreshStatistics( HWND hWnd, HWND hWndListView, int SortItem )
{
	DWORD row;
	DWORD cnt;

	// post hourglass icon
	SetCapture(hWnd);
	hSaveCursor = SetCursor(hHourGlass);

	// Draw all listview items from Stats[] data
	ListView_DeleteAllItems( hWndListView ); 
	row = 0;
	for ( cnt = 0; cnt < numStats; ++cnt )  {
		LV_ITEM		lvI;	// list view item structure

		// see if we are ignoring uncalled services
		if ( IgnoreUncalled  &&  Stats[cnt].Enter == 0 )
			continue;
			
		lvI.mask		= LVIF_TEXT | LVIF_PARAM;
		lvI.iItem		= row;
		lvI.iSubItem	= 0;

		// Add service name to listview
		lvI.pszText		= ServiceName( Stats[cnt].Ordinal );
		lvI.cchTextMax	= strlen( lvI.pszText ) + 1;
		lvI.lParam		= (LPARAM) &Stats[cnt];	// Passed as data to NOTIFY
		if ( ListView_InsertItem( hWndListView, &lvI ) == -1 )
			Abort( hWnd, "Couldn't add items to list view" );

		// Ordinal
		sprintf( msgbuf, "0x%08X", Stats[cnt].Ordinal );
		ListView_SetItemText( hWndListView,	row, 1, msgbuf );

		// Entries
		sprintf( msgbuf, "%d", Stats[cnt].Enter );
		ListView_SetItemText( hWndListView,	row, 2, msgbuf );

		// Exits
		sprintf( msgbuf, "%d", Stats[cnt].Exit );
		ListView_SetItemText( hWndListView,	row, 3, msgbuf );

		// Avg cycles
		if ( Stats[cnt].Exit )  {
			_int64 n = Stats[cnt].Time / Stats[cnt].Exit;
			if ( Overhead ) n -= OverheadCycles;
			sprintf( msgbuf, "%I64d", n );
		} else {
			sprintf( msgbuf, "n/a" );
		}
		ListView_SetItemText( hWndListView,	row, 4, msgbuf );

		// Total cycles
		sprintf( msgbuf, "%I64d", 
				Stats[cnt].Time - (Overhead?(_int64)OverheadCycles*Stats[cnt].Enter:0) );
		ListView_SetItemText( hWndListView,	row, 5, msgbuf );

		// move to next row
		row++;
	}					

	if ( SortItem != -1 )  {
		// The user clicked a column header - sort by this criterion.
		ListView_SortItems( hWndListView, ListViewCompareProc,
							(LPARAM)SortItem );
	}

	// notify user that operation is done
	SetCursor( hSaveCursor );
	ReleaseCapture();
}

/******************************************************************************
*
*	FUNCTION:	GetFileName
*
*	PURPOSE:	Generic wrapper for GetSaveFileName function.
*
******************************************************************************/
BOOL GetFileName( HWND hWnd, char *filename, char *filter, char *ext )
{
	  OPENFILENAME of;

	  of.lStructSize       = sizeof(OPENFILENAME);
	  of.hwndOwner         = (HWND)hWnd;
	  of.hInstance         = (HANDLE)NULL;
	  of.lpstrFilter       = filter;
	  of.lpstrCustomFilter = (LPSTR)NULL;
	  of.nMaxCustFilter    = 0L;
	  of.nFilterIndex      = 1L;
	  of.lpstrFile         = filename;
	  of.nMaxFile          = 256;
	  of.lpstrFileTitle    = NULL;
	  of.nMaxFileTitle     = 0;
	  of.lpstrInitialDir   = NULL;
	  of.lpstrTitle        = (LPSTR)NULL;
	  of.Flags             = OFN_HIDEREADONLY |
	                         OFN_PATHMUSTEXIST ;
	  of.nFileOffset       = 0;
	  of.nFileExtension    = 0;
	  of.lpstrDefExt       = (LPSTR) ext;
	  of.lCustData         = 0L;
	  of.lpfnHook          = 0L;  // Zero eliminates compiler warnings
	  of.lpTemplateName    = (LPSTR)NULL;
  
	  return GetSaveFileName ( &of );
}


/******************************************************************************
*
*	FUNCTION:	SaveStatisticsData
*
*	PURPOSE:	Writes the contents of the statistics window to a file.
*
******************************************************************************/
void SaveStatisticsData( HWND hWnd, HWND hWndListView )
{
	SECURITY_ATTRIBUTES	security;
	HANDLE				fp;
	DWORD				cnt;
	char				tmp[64], Line[256];
	LV_ITEM				pitem;

	// Open the output file
	memset( &security, 0, sizeof security );
	security.nLength = sizeof security;
	fp = CreateFile( szFileName, GENERIC_WRITE, FILE_SHARE_WRITE, 
					 &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( fp == INVALID_HANDLE_VALUE )  {
		wsprintf( msgbuf, "Can't open \"%s\": Error %d", szFileName, GetLastError() );
		MessageBox( NULL, msgbuf, NULL, MB_OK );
		return;
	}
 
	// set up item to use for looping through listview items
	pitem.mask 		= LVIF_TEXT; 
	pitem.iItem		= 0;
    pitem.iSubItem 	= 0;
	pitem.pszText	= msgbuf;
	pitem.cchTextMax= sizeof msgbuf;

	// write header
	sprintf( msgbuf, "Name" );
	sprintf( Line, "%32s\tOrdinal\t\tEntries\tExits\tAvg\tTotal\r\n\n", msgbuf);
	WriteFile( fp, Line, strlen(Line), &cnt, NULL );

	// Iterate over all services in listview
	while ( ListView_GetItem( hWndListView, &pitem ) ) {
		// service name
		sprintf(Line,"%32s\t", msgbuf );

		// ordinal
		ListView_GetItemText( hWndListView,	pitem.iItem, 1, msgbuf, sizeof msgbuf );
		sprintf( tmp, "%s\t", msgbuf );
		strcat( Line, tmp );
		
		// entries
		ListView_GetItemText( hWndListView,	pitem.iItem, 2, msgbuf, sizeof msgbuf );
		sprintf( tmp, "%s\t", msgbuf );
		strcat( Line, tmp );

		// exits
		ListView_GetItemText( hWndListView,	pitem.iItem, 3, msgbuf, sizeof msgbuf );
		sprintf( tmp, "%s\t", msgbuf );
		strcat( Line, tmp );

		// avg cycles
		ListView_GetItemText( hWndListView,	pitem.iItem, 4, msgbuf, sizeof msgbuf );
		sprintf( tmp, "%s\t", msgbuf );
		strcat( Line, tmp );

		// total cycles
		ListView_GetItemText( hWndListView,	pitem.iItem, 5, msgbuf, sizeof msgbuf );
		sprintf( tmp, "%s\r\n", msgbuf );
		strcat( Line, tmp );

		// Write output line to file
	   	WriteFile( fp, Line, strlen(Line), &cnt, NULL );

		// Advance to next service
		pitem.iItem++;
	}

	CloseHandle( fp );
	return;
}


/****************************************************************************
* 
*    FUNCTION: CreateListView(HWND)
*
*    PURPOSE:  Creates the statistics list view window and initializes it
*
****************************************************************************/
HWND CreateListView( HWND hWndParent )                                     
{
	HWND		hWndList;    	  	// handle to list view window
	RECT		rc;         	  	// rectangle for setting size of window
	LV_COLUMN	lvC;				// list view column structure
	DWORD		j;
	static struct {
		char *	Label;	// title of column
		DWORD	Width;	// width of column in pixels
	} column[] = {
		{	"VxD Service",	150		},
		{	"Ordinal",		80		},
		{	"Entries",		50		},
		{	"Exits",		50		},
		{	"Avg Cycles",	80		},
		{	"Total Cycles",	80		},
	};

	// Ensure that the common control DLL is loaded.
	InitCommonControls();

	// Get the size and position of the parent window.
	GetClientRect( hWndParent, &rc );

	// Create the list view window
	hWndList = CreateWindowEx( 0L, WC_LISTVIEW, "", 
								WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT |
								    WS_EX_CLIENTEDGE,	// styles
								0, 0, rc.right - rc.left, rc.bottom - rc.top,
								hWndParent,	(HMENU)ID_LISTVIEW, hInst, NULL );
	if ( hWndList == NULL )
		return NULL;

	// Now initialize the columns you will need.
	// Initialize the LV_COLUMN structure.
	// The mask specifies that the fmt, width, pszText, and subitem members 
	// of the structure are valid,
	lvC.mask	= LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvC.fmt		= LVCFMT_LEFT;	// left-align column

	// Add the columns.
	for ( j = 0; j < sizeof column/sizeof column[0]; ++j )  {
		lvC.iSubItem	= j;
		lvC.cx			= column[j].Width;
	 	lvC.pszText		= column[j].Label;
		if ( ListView_InsertColumn( hWndList, j, &lvC ) == -1 )
			return NULL;
	}

	return hWndList;
}



/****************************************************************************
* 
*	FUNCTION:	ListViewCompareProc(LPARAM, LPARAM, LPARAM)
*
*	PURPOSE:	Callback function that sorts the statistics listview based on
*				the column clicked.
*
****************************************************************************/
int CALLBACK ListViewCompareProc( LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort )
{
	struct vxdmon_stats	*	left	= (void *) lParam1;
	struct vxdmon_stats	*	right	= (void *) lParam2;
	_int64					diff;

	if ( left && right )  {
		switch ( lParamSort)  {
			case 0:     // VxD Service
				return strcmpi( ServiceName( left->Ordinal ), ServiceName( right->Ordinal ) );

			case 1:     // Ordinal
				return left->Ordinal - right->Ordinal;

			case 2:     // Entries
				return right->Enter - left->Enter;

			case 3:     // Exits
				return right->Exit - left->Exit;

			case 4:     // Avg cycles
				if ( left->Enter == 0  ||  right->Enter == 0 )
					return right->Enter - left->Enter;
				diff = right->Time/right->Enter - left->Time/left->Enter;
				return diff < 0 ? -1 : diff > 0 ? 1 : 0; 

			case 5:		// Total cycles
				diff = right->Time - left->Time;
				return diff < 0 ? -1 : diff > 0 ? 1 : 0; 

			default:
				return 0;
		}
	} else {
		return 0;
	}
}


/******************************************************************************
*
*	FUNCTION:	SetTreeParentState
*
*	PURPOSE:	Determine whether a parent node in service selection tree should be marked
*				selected, unselected, or partially selected, based on states of child nodes.
*
******************************************************************************/
void SetTreeParentState( HWND hTree, HTREEITEM Parent )
{
	UINT		State;
	TV_ITEM		tvi;
	HTREEITEM	Child;
	BOOL		allsel, nonesel, partial;

	allsel  = TRUE;
	nonesel = TRUE;
	partial = FALSE;
	for ( Child = TreeView_GetChild( hTree, Parent );
		  Child != NULL;
		  Child = TreeView_GetNextSibling( hTree, Child ) )
	{		
		// if at least one, but not all children are selected, then the parent is partial   
		tvi.mask = TVIF_IMAGE;
		tvi.hItem = Child;
		TreeView_GetItem( hTree, &tvi );
		switch ( tvi.iImage )  {
			case HOOK_YES:	nonesel = FALSE;	break;
			case HOOK_NO:	allsel 	= FALSE;	break;
			case HOOK_PART: allsel  = FALSE; nonesel = FALSE; break;
		}		
		// see if partial condition is satisfied
		if( !allsel && !nonesel ) {
			partial = TRUE;
			break;
		}
	}

	// set parent partial if necessary
	if ( partial ) 
		State 			= HOOK_PART;
	else if( allsel )
		State			= HOOK_YES;
	else if( nonesel )
		State			= HOOK_NO;

	tvi.hItem 			= Parent;
	tvi.mask			= TVIF_IMAGE | TVIF_SELECTEDIMAGE; 
	tvi.iImage			= State;
	tvi.iSelectedImage	= State;
	TreeView_SetItem( hTree, &tvi );	
}


/******************************************************************************
*
*	FUNCTION:	SetTreeItemState
*
*	PURPOSE:	Set the selected/unselected state of a service-selection tree 
*				item, simultaneously recursing on all children to ensure their 
*				state matches the state of the parent.
*
******************************************************************************/
UINT SetTreeItemState( HWND hTree, HTREEITEM hItem, UINT State, BOOL recurse )
{
	TV_ITEM		tvi;
	HTREEITEM	Parent, Child;

	// Set item we're modifying
	tvi.hItem = hItem;

	// Check if this is a toggle operation
	if ( State == HOOK_PART )  {
		// Toggle current image info
		tvi.mask = TVIF_IMAGE;
		TreeView_GetItem( hTree, &tvi );
		switch ( tvi.iImage )  {
			case HOOK_YES:	State = HOOK_NO;	break;
			case HOOK_NO:	State = HOOK_YES;	break;
			case HOOK_PART:	State = HOOK_NO;	break;
		}
	}

	// Set new info
	tvi.mask			= TVIF_IMAGE | TVIF_SELECTEDIMAGE; 
	tvi.iImage			= State;
	tvi.iSelectedImage	= State;
	TreeView_SetItem( hTree, &tvi );

	// Set any children via recursive descent
	for ( Child = TreeView_GetChild( hTree, hItem );
		  Child != NULL;
		  Child = TreeView_GetNextSibling( hTree, Child ) )
	{		   
		SetTreeItemState( hTree, Child, State, TRUE );
	}

	// Actually, check to see if this is a child toggle- if so, then set the parent
	// to partial, unless all its children are selected - in that case set to partial
	if ( !recurse && (Parent = TreeView_GetParent( hTree, hItem)) ) {
		SetTreeParentState( hTree, Parent );
		// do the parent
		if( Parent = TreeView_GetParent( hTree, Parent) ) 
			SetTreeParentState( hTree, Parent );
	}

	return State;
}

/****************************************************************************
*
*    FUNCTION: TreeWndProc(HWND, unsigned, WORD, LONG)
*
*    PURPOSE:  Processes messages for service-selection treeview
*
****************************************************************************/

LONG APIENTRY TreeWndProc( HWND hWnd, UINT message, UINT wParam, LONG lParam )
{
	static HWND hWndTreeView;   // Handle of the tree window
	NM_TREEVIEW * tv;

	switch ( message )  {

		case WM_CREATE:
			// Create the tree view window and initialize its
			// image list
			hWndTreeView = CreateTreeView( hWnd );
			if ( hWndTreeView == NULL )
				MessageBox( NULL, "Tree View not created!", NULL, MB_OK );
			// Add items to the tree window
			if ( ! AddTreeViewItems( hWndTreeView ) )
				MessageBox( NULL, "Items not added", NULL, MB_OK );
			// Use user profile to select initial set of services
			DefaultTreeSelections( hWndTreeView );
			break;          

        case WM_SIZE:
			// Move or resize the service-selection treeview.
            MoveWindow( hWndTreeView, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
            break;

		case WM_NOTIFY:
			// Process clicks on items, for selecting/deselecting items
			if ( wParam != ID_TREEVIEW )
				return 0;
	    	tv = (NM_TREEVIEW *) lParam;
			if ( tv->hdr.code == NM_CLICK )  {
				// Its a click 
		        TV_HITTESTINFO	tvhti;
		        HTREEITEM		htiItemClicked;
				HTREEITEM		hItemRange;

				// Ensure the click was on something interesting
		        tvhti.pt = ClickPoint( hWndTreeView );
		        htiItemClicked = TreeView_HitTest( hWndTreeView, &tvhti );
				if ( htiItemClicked == NULL  ||  !(tvhti.flags & TVHT_ONITEM) )
					break;

				// If the item previously selected is not this item and the shift 
				// key is pressed, select all items in between.
				hItemRange = TreeView_GetSelection( hWndTreeView );
				if ( hItemRange && hItemRange != htiItemClicked && (GetAsyncKeyState(VK_SHIFT) & 0x8000))  {
					RECT	rcSelected;
					RECT	rcClicked;

					// check the selected item
					/*ToggleTreeItemState( hWndTreeView, hItemRange );*/

					// Get the rectangle of the selected and clicked items - we 
					// need these to determine if the clicked item is above or 
					// below the selected item
					TreeView_GetItemRect( hWndTreeView, hItemRange, &rcSelected, FALSE );
					TreeView_GetItemRect( hWndTreeView, htiItemClicked, &rcClicked, FALSE );

					//check the rest of the items in between
					if ( rcClicked.top > rcSelected.top )  {
						while ( (hItemRange = TreeView_GetNextVisible(hWndTreeView, hItemRange)) != htiItemClicked )
							SetTreeItemState( hWndTreeView, hItemRange, HOOK_PART, FALSE );
					} else {
						while ( (hItemRange = TreeView_GetPrevVisible(hWndTreeView, hItemRange)) != htiItemClicked )
							SetTreeItemState( hWndTreeView, hItemRange, HOOK_PART, FALSE );
					}
				}

				// Switch state of clicked item
				SetTreeItemState( hWndTreeView, htiItemClicked, HOOK_PART, FALSE );

				// mark as changed
				ProfileChanged = TRUE;
			}
			break;

		case WM_COMMAND:
			switch ( LOWORD( wParam ))  {
				case IDOK:
					// Hook all currently selected services
					HookServices( hWndTreeView, hSelectRoot );
					break;

				case IDM_SAVE: 
					// Save a list of the currently selected services to a file
					WriteDefaultTreeSelections( hWndTreeView, hSelectRoot );
					ProfileChanged = FALSE;
					break;

				case IDM_EXIT:
					// All done, we can exit
					SendMessage( hWnd, WM_CLOSE, 0, 0 );
					break;
					
				default:
					return DefWindowProc( hWnd, message, wParam, lParam );
			}
			break;

		case WM_CLOSE:
			// Never really close down. Just hide so we don't lose our state.
			ShowWindow( hWnd, SW_HIDE );
			return 0;
						
		case WM_DESTROY:
			// Now we really have to shut down
			return 0;

		default:
			return DefWindowProc(hWnd, message, wParam, lParam );
	}
	return 0;
}


/****************************************************************************
* 
*    FUNCTION: CreateTreeView(HWND)
*
*    PURPOSE:  Creates the service-selection tree view window and initializes it
*
****************************************************************************/
HWND CreateTreeView( HWND hWndParent )                                     
{
	HWND 				hwndTree;		// handle to tree view window
	RECT 				rc;			// rectangle for setting size of window

	// Ensure that the common control DLL is loaded.
	InitCommonControls();

	// Get the size and position of the parent window.
	GetClientRect( hWndParent, &rc );

	// Create and fill image list for selection tree view
	hSelectImageList = ImageList_Create( 16, 16, ILC_MASK, 5, 1 );
	ImageList_AddIcon( hSelectImageList, LoadIcon( hInst, "UNSELECTED"   ));
	ImageList_AddIcon( hSelectImageList, LoadIcon( hInst, "SELECTED"     ));
 	ImageList_AddIcon( hSelectImageList, LoadIcon( hInst, "SOMESELECTED" ));

	// Create the tree view window.
	hwndTree = CreateWindowEx( 0L, WC_TREEVIEW,	"",
		WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT,
		0, 0, rc.right - rc.left, rc.bottom - rc.top - 15,
		hWndParent,	(HMENU)ID_TREEVIEW, hInst, NULL );

	if ( hwndTree ) {
		// Add image list
   		TreeView_SetImageList( hwndTree, hSelectImageList, 0 );
		// Set backdrop for images
     	ImageList_SetBkColor( hSelectImageList, GetSysColor( COLOR_WINDOW ));
	}

	return hwndTree;
}

/****************************************************************************
* 
*    FUNCTION: AddTreeViewItems(HWND)
*
*    PURPOSE: Fills in the services tree view list. 
*
****************************************************************************/

BOOL AddTreeViewItems( HWND hwndTree )
{
 	SECURITY_ATTRIBUTES	security;
	HANDLE				fp;				// file handle
	char				Line[ 100 ];	// line read from file
	int					len = 0;		// current length of line read from file
	DWORD				n;
	HTREEITEM			hPrevDevice; 	// Last device added
	HTREEITEM			hPrevService;	// Last service added

	// Open the services.dat file	
	memset( &security, 0, sizeof security );
	security.nLength = sizeof security;
	fp = CreateFile( "services.dat", GENERIC_READ, FILE_SHARE_READ, 
					 &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( fp == INVALID_HANDLE_VALUE )  {
		char msgbuf[ 200 ];
		wsprintf( msgbuf, "Can't open \"services.dat\": Error %d", GetLastError() );
		MessageBox( NULL, msgbuf, NULL, MB_OK );
		return FALSE;
	}

	// First add the root item
	hSelectRoot = AddOneTreeViewItem( hwndTree, TVI_ROOT, TVI_LAST, "All Virtual Devices", 0 );

	// Initialize previous values
	hPrevDevice	 = hSelectRoot;
	hPrevService = TVI_FIRST;

	// Iterate over lines of file reading and adding items
	while ( ReadFile( fp, Line+len, sizeof Line - len, &n, NULL ) )  {
		DWORD	Ordinal = 0;	// Ordinal of service being read
		char *	Name;			// Name of service being read
		char *	p = Line;		// Pointer for parsing line

		// Adjust new line length
		len += n;

		// Scan for ordinal
		while ( len > 0  &&  ISWHITE(*p) )
			++p, --len;
		if ( len == 0 )
			break;
		for (;;) {
			DWORD ch = (DWORD)CharUpper( (LPTSTR)*p++ );
			if ( --len == 0 )
				break;
			if ( ch >= '0' && ch <= '9' )		Ordinal = Ordinal * 16 + ch - '0';
			else if ( ch >= 'A' && ch <= 'F' )	Ordinal = Ordinal * 16 + ch - 'A' + 10;
			else break;
		}
		if ( len == 0  ||  Ordinal == 0  ||  !ISWHITE(p[-1]) )  {
			MessageBox( NULL, "Badly formatted ordinal", NULL, MB_OK );
			return FALSE;
		}

		// Scan for service name
		while ( len > 0  &&  ISWHITE(*p) )
			++p, --len;
		Name = p;
		while ( len > 0  &&  !ISWHITE(*p) )
			++p, --len;
		*p++ = '\0'; --len;

		// Add service to tree view
		if ( Ordinal == (DWORD)-1 )  {
			// Add a device.
			hPrevDevice = AddOneTreeViewItem( hwndTree, hSelectRoot, TVI_SORT, Name, 0 );
			hPrevService = TVI_FIRST;
		} else {
			// Add a service
			struct service * s = NewService( Ordinal, Name );
			wsprintf( msgbuf, "0x%08X  %s", Ordinal, Name );
			hPrevService = AddOneTreeViewItem( hwndTree, hPrevDevice, hPrevService, msgbuf, (DWORD)s );
			s->hTree = hPrevService;
		}

		// Adjust contents of line for next pass
		MoveMemory( Line, p, len );
	}
	CloseHandle( fp );

	return TRUE;
}


/***************************************************************************
* 
*    FUNCTION: AddOneItem( HTREEITEM, LPSTR, HTREEITEM, int )
*
*    PURPOSE: Inserts a tree view item in service tree in the specified place. 
*
****************************************************************************/

// This function fills out the TV_ITEM and TV_INSERTSTRUCT structures
// and adds the item to the tree view control.
HTREEITEM AddOneTreeViewItem( HWND hwndTree, HTREEITEM hParent, HTREEITEM hInsAfter,
							LPSTR szText, DWORD lParam )
{				  
	HTREEITEM		hItem;
	TV_ITEM			tvI;
	TV_INSERTSTRUCT	tvIns;

	// The pszText, iImage, and iSelectedImage are filled out.
	tvI.mask			= TVIF_TEXT | TVIF_PARAM;
	tvI.pszText			= szText;
	tvI.cchTextMax		= lstrlen( szText );
	tvI.lParam			= lParam;

	tvIns.item			= tvI;
	tvIns.hInsertAfter	= hInsAfter;
	tvIns.hParent		= hParent;
	
	// Insert the item into the tree.
	hItem = (HTREEITEM)SendMessage( hwndTree, TVM_INSERTITEM, 0, (LPARAM)&tvIns);

	return hItem;
}



/***************************************************************************
* 
*	FUNCTION:	DefaultTreeSelections( HTREEITEM )
*
*	PURPOSE:	Read the list of services that we're previously hooked from
*				the profile.dat file, and mark those services as selected in
*				the current service-selection tree.
*
****************************************************************************/

BOOL DefaultTreeSelections( HWND hwndTree )
{
 	SECURITY_ATTRIBUTES	security;
	HANDLE				fp;
	char				Line[ 100 ];
	int					len = 0;
	DWORD				n;
	DWORD				cnt = 0;

	// open the file
	memset( &security, 0, sizeof security );
	security.nLength = sizeof security;
	fp = CreateFile( "profile.dat", GENERIC_READ, FILE_SHARE_READ, 
					 &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( fp == INVALID_HANDLE_VALUE ) 
		return FALSE;

	// Read file to add additional items
	while ( ReadFile( fp, Line+len, sizeof Line - len, &n, NULL ) )  {
		struct service *	Service;
		DWORD				Ordinal = 0;
		char			 *	p = Line;

		// Adjust new line length
		len += n;

		// Scan for ordinal
		while ( len > 0  &&  ISWHITE(*p) )
			++p, --len;
		if ( len == 0 )
			break;
		for (;;) {
			DWORD ch = (DWORD)CharUpper( (LPTSTR)*p++ );
			if ( --len == 0 )
				break;
			if ( ch >= '0' && ch <= '9' )		Ordinal = Ordinal * 16 + ch - '0';
			else if ( ch >= 'A' && ch <= 'F' )	Ordinal = Ordinal * 16 + ch - 'A' + 10;
			else break;
		}
		if ( len == 0  ||  Ordinal == 0  ||  !ISWHITE(p[-1]) )  {
			MessageBox( NULL, "Badly formatted ordinal", NULL, MB_OK );
			return FALSE;
		}

		// register ordinal as being hooked
		if ( Service = LookupService( Ordinal ) )  {
			SetTreeItemState( hwndTree, Service->hTree, HOOK_YES, FALSE );
			++cnt;
		}
		
		// Adjust contents of line for next pass
		MoveMemory( Line, p, len );
	}
	CloseHandle( fp );

	// Determine if we should hook the selections just read in.
	wsprintf( msgbuf, "Hook previously saved selections now?\n(%d services)", cnt );
	if ( MessageBox( NULL, msgbuf, "VxDMon", MB_YESNO ) == IDYES )  {
		// Yes, hook them now.
		HookServices( hwndTree, hSelectRoot );
	}

	return TRUE;
}


/******************************************************************************
*
*	FUNCTION:	WriteHookServices
*
*	PURPOSE:	Write to a file all services in service-selection tree that are 
*				currently selected.
*
******************************************************************************/
BOOL WriteHookServices( HANDLE fp, HWND hTree, HTREEITEM hItem )
{
	HTREEITEM	Child = TreeView_GetChild( hTree, hItem );

	if ( Child )  {
		// This is not a leaf item.  Recurse on all children.
		while ( Child != NULL )  {
			WriteHookServices( fp, hTree, Child );
			Child = TreeView_GetNextSibling( hTree, Child );
		}
	} else {
		// Leaf item.  Write out status
		TV_ITEM				tvi;
		struct service	*	pService;

		// Determine current hook state
		tvi.hItem = hItem;
		tvi.mask = TVIF_IMAGE | TVIF_PARAM;
		TreeView_GetItem( hTree, &tvi );
		pService = (struct service *) tvi.lParam;
		if ( tvi.iImage == HOOK_YES )  {
			char	Line[ 100 ];
			DWORD	cnt = 0;
			wsprintf( Line, "%08X\r\n", pService->Ordinal );
			WriteFile( fp, Line, strlen(Line), &cnt, NULL );
		}
	}

	return TRUE;
}


/******************************************************************************
*
*	FUNCTION:	WriteDefaultTreeSelections
*
*	PURPOSE:	Traverse the service-selection tree and write to the 
*				profile.dat file all services that are currently selected.
*
******************************************************************************/
void WriteDefaultTreeSelections( HWND hwndTree, HTREEITEM hItem )
{
	SECURITY_ATTRIBUTES	security;
	HANDLE				fp;

	// Open file
	memset( &security, 0, sizeof security );
	security.nLength = sizeof security;
	fp = CreateFile( "profile.dat", GENERIC_WRITE, FILE_SHARE_WRITE, 
					 &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( fp == INVALID_HANDLE_VALUE )  {
		wsprintf( msgbuf, "Can't open \"profile.dat\": Error %d", GetLastError() );
		MessageBox( NULL, msgbuf, NULL, MB_OK );
		return;
	}
	// Recurse writing items
	WriteHookServices( fp, hwndTree, hItem );
	// close file
	CloseHandle( fp );
	return;
}

/******************************************************************************
*
*	FUNCTION:	ClickPoint
*
*	PURPOSE:	Return the point, in window-space coordinates, where the
*				cursor was clicked.
*
******************************************************************************/
POINT ClickPoint( HWND hWnd )
{
	POINT	point;
	DWORD	dwpos = GetMessagePos();
	point.x = LOWORD( dwpos );
	point.y = HIWORD( dwpos );
	MapWindowPoints( HWND_DESKTOP, hWnd, &point, 1 );
	return point;
}


/******************************************************************************
*
*	FUNCTION:	CreateCallTree
*
*	PURPOSE:	Open a descendant/ancestor call tree window.
*				Uses global variables to determine what type of tree is desired.
******************************************************************************/
void CreateCallTree( HWND hWnd, struct vxdmon_stats * item, BOOL calldown )
{
	HWND		hCallTree;

	// Set global variables indicating contents of new window
	CallTreeStat = item;
	CallTreeDown = calldown;

	// Set name of window
	if ( CallTreeDown )
		wsprintf( msgbuf, "Services called by %s", 
				LookupService(((struct vxdmon_stats *) CallTreeStat)->Ordinal)->Name );
	else
		wsprintf( msgbuf, "Callers of %s", 
				LookupService(((struct vxdmon_stats *) CallTreeStat)->Ordinal)->Name ); 

	// Create a call-tree window for the service
	hCallTree = CreateWindow( calldown ? "VxDMonCallDownClass" : "VxDMonCallUpClass",
						msgbuf,
						WS_OVERLAPPEDWINDOW,
						CW_USEDEFAULT, CW_USEDEFAULT, 300, 300,
						NULL, NULL, hInst, NULL );
	if ( !hCallTree )  {
		wsprintf( msgbuf, "Call tree not created: Error %d", GetLastError() );
		MessageBox( hWnd, msgbuf, NULL, MB_OK );
	}
	// Display the window
	ShowWindow( hCallTree, SW_SHOWNORMAL );
	UpdateWindow( hCallTree );
}


/******************************************************************************
*
*	FUNCTION:	WriteCallData
*
*	PURPOSE:	Write the call-tree information to a file
*
******************************************************************************/
void WriteCallData( HANDLE fp, HWND hTree, HTREEITEM hItem, int depth )
{
	HTREEITEM	Child;
	char		Line[ 100 ];
	DWORD		cnt = 0;
	TV_ITEM		tvi;
	int			i;
	char		servicename[64];

	// Get the item's name
	tvi.hItem 	= hItem;
	tvi.mask 	= TVIF_TEXT | TVIF_PARAM;
	tvi.pszText	= servicename;
	tvi.cchTextMax	= sizeof servicename;
	TreeView_GetItem( hTree, &tvi );

	// don't print root since its redundant
	if ( depth >= 0 ) {
		// Print a number of tabs equal to depth
		for ( i = 0; i < depth; i++ )  {
			wsprintf( Line, "\t");
			WriteFile( fp, Line, strlen(Line), &cnt, NULL );
		}
		// Print the name of the service
		wsprintf( Line,"%s\r\n", servicename );
		WriteFile( fp, Line, strlen(Line), &cnt, NULL );
	}

	// If any children exist then recurse on them
	for ( Child = TreeView_GetChild( hTree, hItem );
		  Child != NULL;
		  Child = TreeView_GetNextSibling( hTree, Child ) )
	{
		WriteCallData( fp, hTree, Child, depth+1 );
	}

	return;
}


/******************************************************************************
*
*	FUNCTION:	SaveCallData
*
*	PURPOSE:	Write a call tree to an ASCII file.
*
******************************************************************************/
void SaveCallData( HWND hWnd, char * CallFileName, HWND hWndTreeView )
{
	SECURITY_ATTRIBUTES	security;
	HANDLE				fp;
	char				Line[128];
	int					cnt;

	// Open the file
	memset( &security, 0, sizeof security );
	security.nLength = sizeof security;
	fp = CreateFile( CallFileName, GENERIC_WRITE, FILE_SHARE_WRITE, 
					 &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( fp == INVALID_HANDLE_VALUE )  {
		wsprintf( msgbuf, "Can't open \"%s\": Error %d", CallFileName, GetLastError() );
		MessageBox( NULL, msgbuf, NULL, MB_OK );
		return;
	}

	// Fetch text from title bar of window
	GetWindowText( hWnd, msgbuf, sizeof msgbuf );
	// Write title bar to file
	wsprintf( Line, "%s:\r\n\n", msgbuf );
	WriteFile( fp, Line, strlen(Line), &cnt, NULL );

	// Write call tree data to file
	WriteCallData( fp, hWndTreeView, TreeView_GetChild(hWndTreeView, 0 ), -1 );
	
	// Close file and exit
  	CloseHandle( fp );
	return;
}


/****************************************************************************
*
*    FUNCTION: CallWndProc( HWND, unsigned, WORD, LONG )
*
*    PURPOSE:  Processes messages for call-tree windows
*
****************************************************************************/

LONG APIENTRY CallWndProc( HWND hWnd, UINT message, UINT wParam, LONG lParam )
{
	HWND			hWndTreeView;   // Handle of the tree window
	RECT 			rc;				// rectangle for setting size of window
	NM_TREEVIEW *	tv;

	// Get value of tree window saved during WM_CREATE
	hWndTreeView = (HWND) GetWindowLong( hWnd, 0 );

	switch ( message )  {

		case WM_CREATE:
			// Get the size and position of the parent window.
			GetClientRect( hWnd, &rc );

			// Create the tree view window.
			hWndTreeView = CreateWindowEx( 0L, WC_TREEVIEW,	"",
				WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT,
				0, 0, rc.right - rc.left, rc.bottom - rc.top - 15,
				hWnd, (HMENU)ID_CALLVIEW, hInst, NULL );
			if ( hWndTreeView == NULL )
				MessageBox( NULL, "Call Tree View not created!", NULL, MB_OK );

			// Save the treeview handle for later invocations
			SetWindowLong( hWnd, 0, (LONG)hWndTreeView );

			// Set image list for tree
	   		TreeView_SetImageList( hWndTreeView, hCallImageList, 0 );

			// post hourglass icon
			SetCapture(hWnd);
			hSaveCursor = SetCursor(hHourGlass);

			// Add all callers/callees to tree
			AddCallTreeItems( hWndTreeView, TVI_ROOT, CallTreeStat, 0 );

			// notify user that operation is done
			SetCursor(hSaveCursor);
			ReleaseCapture();

			break;          

        case WM_SIZE:
			// Move or resize window
            MoveWindow( hWndTreeView, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
            break;

		case WM_NOTIFY:
			// Detect double clicks on items
			if ( wParam != ID_CALLVIEW )
				return 0;
	    	tv = (NM_TREEVIEW *) lParam;
			if ( tv->hdr.code == NM_DBLCLK  ||  tv->hdr.code == NM_RDBLCLK )  {
				// Have a double click
		        TV_HITTESTINFO	tvhti;
		        HTREEITEM		htiItemClicked;

				// Ensure the click was on something interesting
		        tvhti.pt = ClickPoint( hWndTreeView );
		        htiItemClicked = TreeView_HitTest( hWndTreeView, &tvhti );
				if ( htiItemClicked  &&  tvhti.flags & TVHT_ONITEM )  {
					// Create call graph for item clicked on
					TV_ITEM		Item;

					// Determine which service was clicked
					Item.mask		= TVIF_PARAM;
					Item.hItem		= htiItemClicked;
					if ( ! TreeView_GetItem( hWndTreeView, &Item ) )
						return 0;

					// Create the window
					CreateCallTree( hWnd,
									(struct vxdmon_stats *) Item.lParam,
									tv->hdr.code == NM_DBLCLK );
				}
			}
			break;

		case WM_COMMAND:
			switch ( LOWORD( wParam ) )  {
				case IDM_ANCESTORS:
				case IDM_DESCENDANTS:  {
					// Create call graph for currently selected item
					TV_ITEM		Item;
					HTREEITEM	hItem = TreeView_GetSelection( hWndTreeView );
					if ( ! hItem )
						return 0;

					// Get info about currently select item
					Item.mask	= TVIF_PARAM;
					Item.hItem	= hItem;
					if ( ! TreeView_GetItem( hWndTreeView, &Item ) )
						return 0;

					// Create the window
					CreateCallTree( hWnd,
									(struct vxdmon_stats *) Item.lParam,
									LOWORD(wParam) == IDM_DESCENDANTS );
					return 0;
				}

				case IDM_CALLSAVE:
					// Save call graph to file
 					if( GetFileName( hWnd, CallFileName, "Call Graph Files (*.CLL)\0*.CLL\0", 
 								"CLL" )) 
						SaveCallData( hWnd, CallFileName, hWndTreeView );
					return 0;

				case IDM_EXIT:
					// Close call graph
					SendMessage( hWnd, WM_CLOSE, 0, 0 );
					return 0;

				default:
					return DefWindowProc( hWnd, message, wParam, lParam );
			}
			break;

		default:
			return DefWindowProc( hWnd, message, wParam, lParam );
	}
	return 0;
}

/******************************************************************************
*
*	FUNCTION:	AddCallTreeItems
*
*	PURPOSE:	Build a call tree based upon contents of statitics buffer.
*				We rely on brute force searching of all the data, rather
*				than doing anything intelligent.
*
******************************************************************************/
BOOL AddCallTreeItems( HWND hTree, HTREEITEM Parent, struct vxdmon_stats * stats, int depth )
{
	DWORD		j;
	HTREEITEM	me;
	char	*	Name = LookupService(stats->Ordinal)->Name;
	TV_ITEM		tvi;

	// Add the given, top-level, item
	me = AddOneTreeViewItem( hTree, Parent, TVI_SORT, Name, (DWORD)stats );

	// Set image for direction of call
	tvi.hItem			= me;
	tvi.mask			= TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	tvi.iImage			= depth == 0 ? CALL_NONE : CallTreeDown ? CALL_DOWN : CALL_UP;
	tvi.iSelectedImage	= tvi.iImage;
	TreeView_SetItem( hTree, &tvi );
	
	// Check if we appear as our own ancestor
	while ( Parent != NULL  &&  Parent != TVI_ROOT )  {
		TV_ITEM	Item;
		Item.mask	= TVIF_PARAM;
		Item.hItem	= Parent;
		TreeView_GetItem( hTree, &Item ); 
		if ( Item.lParam == (int)stats )
			// We're part of a cycle.  Don't recurse on children
			return TRUE;
		Parent = TreeView_GetParent( hTree, Parent ); 
	}

	if ( CallTreeDown )  {
		// Add everything item calls
		for ( j = 0; j < numStats; ++j )  {
			DWORD c;
			for ( c = 0; c < CALLER_CNT; ++c )  {
				if ( Stats[j].Caller[c] == stats->Ordinal )  {
					// Make sure item is not already in tree
					HTREEITEM Sibling;
					for ( Sibling = TreeView_GetChild( hTree, me );
						  Sibling != NULL;
						  Sibling = TreeView_GetNextSibling( hTree, Sibling ) )
					{		   
						TV_ITEM		tvi;

						// Get sibling info
						tvi.hItem = Sibling;
						tvi.mask = TVIF_PARAM;
						TreeView_GetItem( hTree, &tvi );
						if ( tvi.lParam == (int) &Stats[j] )
							goto duplicate_callee;
					}

				 	// Add item to tree
					AddCallTreeItems( hTree, me, &Stats[j], depth+1 );
				}
			}
			duplicate_callee:;
		}
	} else {
		// Add everything calling item
		for ( j = 0; j < CALLER_CNT; ++j )  {
			DWORD child = stats->Caller[j];
			if ( child )  {
				DWORD c;

				// Ensure we haven't already added child
				for ( c = 0; c < j; ++c )
					if ( stats->Caller[c] == child )
						goto duplicate_caller;

				// Locate child
				for ( c = 0; c < numStats; ++c )
					if ( Stats[c].Ordinal == child )
						break;
				if ( c >= numStats )
					continue;

				// Recurse on child
				AddCallTreeItems( hTree, me, &Stats[c], depth+1 );
			}
			duplicate_caller:;
		}
	}

	// Initially have tree fully expanded (except first level)
	if ( depth != 1 )
		TreeView_Expand( hTree, me, TVE_EXPAND ); 

	return TRUE;
}


/***************************************************************************
* 
*    FUNCTION:	HookServices
*
*    PURPOSE:	Hook/unhook all services marked in selection tree view  
*
****************************************************************************/

BOOL HookServices( HWND hTree, HTREEITEM hItem )
{
	HTREEITEM		Child 			= TreeView_GetChild( hTree, hItem );
	static DWORD  	NotLoadedOrd	= (DWORD) -1;
    DWORD           nb;

	if ( Child )  {
		// This is not a leaf item.  Recurse on all children.
		while ( Child != NULL )  {
			HookServices( hTree, Child );
			Child = TreeView_GetNextSibling( hTree, Child );
		}
	} else {
		// Leaf item.  Tell VxD to hook/unhook it.
		TV_ITEM				tvi;
		struct service	*	pService;

		// Determine current hook state
		tvi.hItem = hItem;
		tvi.mask = TVIF_IMAGE | TVIF_PARAM;
		TreeView_GetItem( hTree, &tvi );
		pService = (struct service *) tvi.lParam;
		if ( pService->Hooked != (tvi.iImage == HOOK_YES) )  {
			// do we already know that vxd is not loaded?
			if( NotLoadedOrd == (pService->Ordinal >> 16 )) {
				// Reset state of item
				SetTreeItemState( hTree, hItem,
							pService->Hooked ? HOOK_YES : HOOK_NO, FALSE );
			} else {
				// try to hook the service
				if ( DeviceIoControl( vxd_handle,
									pService->Hooked
										? VXDMON_unhookservice
										: VXDMON_hookservice,
									&pService->Ordinal, sizeof(DWORD),
									NULL, 0, &nb, NULL ) )
				{
					// Successfully hooked/unhooked service.  Mark as such.
					NotLoadedOrd 	 = (DWORD) -1;
					pService->Hooked = !pService->Hooked;
				} else {
					// Hook/unhook failed.  Find out why.
					DWORD ErrorCode = GetLastError();
					char vxdname[ 12 ];

					// get VxD name for error message
					tvi.hItem		= TreeView_GetParent( hTree, hItem );
					tvi.mask		= TVIF_TEXT | TVIF_PARAM;
					tvi.pszText		= vxdname;
					tvi.cchTextMax	= sizeof vxdname;
					TreeView_GetItem( hTree, &tvi );

					// error because VxD not loaded?
					if ( ErrorCode == VXDMON_ERROR_NOSUCHVXD ) {
						wsprintf( msgbuf, "\nVxD %s is not loaded\n\n", vxdname );
						NotLoadedOrd = pService->Ordinal >> 16;		
					} else { 
						// unknown error
						wsprintf( msgbuf, "Couldn't %s service\n\n%s:%s\n#0x%08X",
								 pService->Hooked ? "unhook" : "hook",
								 vxdname, pService->Name, pService->Ordinal );
					}
					MessageBox( NULL, msgbuf, NULL, MB_ICONEXCLAMATION );

					// Reset state of item to what it was before being selected/deselected
					SetTreeItemState( hTree, hItem,
								pService->Hooked ? HOOK_YES : HOOK_NO, FALSE );
				}
			}
		}
	}

	return TRUE;
}

/****************************************************************************
*
*	FUNCTION:	About
*
*	PURPOSE:	Processes messages for "About" dialog box
*
****************************************************************************/

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
	}
	return FALSE;   
}
