//////////////////////////////////////////////////////////////
// Copyright (C) 2002-2003 Bryce Cogswell 
// www.sysinternals.com
// cogswell@winternals.com
//
// You may modify and use this code for personal use only.
// Redistribution in any form is expressly prohibited
// without permission by the author.
//////////////////////////////////////////////////////////////
#pragma once
#include <windowsx.h>
#include <tchar.h>

enum ANCHOR {
	ANCHOR_DEFAULT	= 0,
	ANCHOR_LEFT		= 1,
	ANCHOR_RIGHT	= 2,
	ANCHOR_BOTH		= 3,
	ANCHOR_TOP		= ANCHOR_LEFT,
	ANCHOR_BOTTOM	= ANCHOR_RIGHT,
};

#define DEFER 1


class CResizer {
	HWND	m_hDlg;			// Handle to dialog window
	POINT	m_origSize;		// Size of dialog when first created (in template)
	POINT	m_oldSize;		// Most recent size of dialog
	POINT	m_newSize;		// Size as a result of resizing
	HDWP	m_hdwp;			// Deferred window position handle
	long	m_horzPercent;	// Horizontal breakpoint percentage
	long	m_vertPercent;	// Horizontal breakpoint percentage
	bool	m_Enable;
	struct OVERRIDE {
		HWND	hChild;
		ANCHOR	horzAnchor;
		ANCHOR	vertAnchor;
	}	*	m_OverrideList;	// Overrides for default anchor points
	int		m_OverrideCnt;


	OVERRIDE * FindOverride( HWND hChild, bool create )
	{
		if ( hChild == NULL )
			return NULL;
		for ( int i = 0; i < m_OverrideCnt; ++i )
			if ( m_OverrideList[i].hChild == hChild )
				return m_OverrideList + i;
		if ( ! create )
			return NULL;
		// create item
		m_OverrideList = (OVERRIDE *)realloc( m_OverrideList, ++m_OverrideCnt*sizeof m_OverrideList[0] );
		OVERRIDE * override = m_OverrideList + m_OverrideCnt - 1;
		override->hChild	 = hChild;
		override->horzAnchor = ANCHOR_DEFAULT;
		override->vertAnchor = ANCHOR_DEFAULT;
		return override;
	}
	
	static void inline UpdatePos( long & pos, long oldWidth, long newWidth )
	{
		// anchor to right
		pos = newWidth - (oldWidth - pos);
	}

	static BOOL CALLBACK ResizeCallback( HWND hChild, LPARAM lParam )
	{
		bool		ResizeOkay	= true;
		CResizer *	This		= (CResizer *) lParam;

#if DEFER
		// Ensure child is a direct descendent of parent (mandatory for DeferWindowPos)
		if ( GetParent( hChild ) != This->m_hDlg )
			return TRUE;
#endif

		// Get child window location
		RECT	rc;
		GetWindowRect( hChild, &rc );
		ScreenToClient( This->m_hDlg, (POINT *)&rc.left );
		ScreenToClient( This->m_hDlg, (POINT *)&rc.right );

		// Select a "center" for dividing screen into quadrants
		int horzBreak = This->m_oldSize.x * This->m_horzPercent / 100;
		int vertBreak = This->m_oldSize.y * This->m_vertPercent / 100;

		// Compute default anchor points
		int horzAnchor = ANCHOR_DEFAULT, vertAnchor = ANCHOR_DEFAULT;
		if ( rc.left	<  horzBreak )			horzAnchor |= ANCHOR_LEFT;
		if ( rc.right	>= horzBreak )			horzAnchor |= ANCHOR_RIGHT;
		if ( rc.top		<  vertBreak )			vertAnchor |= ANCHOR_TOP;
		if ( rc.bottom	>= vertBreak )			vertAnchor |= ANCHOR_BOTTOM;

		// Apply window class override
#if _DEBUG
		TCHAR Text[ MAX_PATH ] = TEXT("");
		GetWindowText( hChild, Text, MAX_PATH );
#endif
		TCHAR Class[ MAX_PATH ] = TEXT("");
		GetClassName( hChild, Class, MAX_PATH );
		if ( _tcsicmp( Class, TEXT("Button") ) == 0 )  {
			DWORD Style = GetWindowLong( hChild, GWL_STYLE );
			Style &= 0xF;
			if ( Style == BS_PUSHBUTTON  ||  Style == BS_DEFPUSHBUTTON  ||  Style == BS_OWNERDRAW  ||  Style == BS_USERBUTTON )
				ResizeOkay = false;
		}
		if ( ! ResizeOkay )  {
			// Not allowed to resize this control, so prevent the anchors from being BOTH
			if ( horzAnchor == ANCHOR_BOTH )
				horzAnchor = horzBreak - rc.left >= rc.right - horzBreak ? ANCHOR_LEFT : ANCHOR_RIGHT;
			if ( vertAnchor == ANCHOR_BOTH )
				vertAnchor = vertBreak - rc.top >= rc.bottom - vertBreak ? ANCHOR_TOP : ANCHOR_BOTTOM;
		}

		// Apply user override
		OVERRIDE * override = This->FindOverride( hChild, false );
		if ( override )  {
			if ( override->horzAnchor != ANCHOR_DEFAULT )
				horzAnchor = override->horzAnchor;
			if ( override->vertAnchor != ANCHOR_DEFAULT )
				vertAnchor = override->vertAnchor;
		}

		// adjust size based on anchor points
		if ( horzAnchor == ANCHOR_RIGHT )	UpdatePos( rc.left,		This->m_oldSize.x, This->m_newSize.x );
		if ( horzAnchor &  ANCHOR_RIGHT )	UpdatePos( rc.right,	This->m_oldSize.x, This->m_newSize.x );
		if ( vertAnchor == ANCHOR_BOTTOM )	UpdatePos( rc.top,		This->m_oldSize.y, This->m_newSize.y );
		if ( vertAnchor &  ANCHOR_BOTTOM )	UpdatePos( rc.bottom,	This->m_oldSize.y, This->m_newSize.y );

		// Update position relative to parent, which may be different than dialog
#if DEFER
		This->m_hdwp = DeferWindowPos( This->m_hdwp, hChild, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER );
#else
		MapWindowPoints( This->m_hDlg, GetParent(hChild), (POINT *)&rc, 2 );
		MoveWindow( hChild, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, true );
#endif
		return TRUE;
	}

	void ResizeDialog()
	{
		if ( m_oldSize.y != m_newSize.y  ||  m_oldSize.x != m_newSize.x )  {
			// move all windows
#if DEFER
			m_hdwp = BeginDeferWindowPos( 20 );
#endif
			EnumChildWindows( m_hDlg, ResizeCallback, (LPARAM)this );
#if DEFER
			EndDeferWindowPos( m_hdwp );
#endif
			m_oldSize = m_newSize;
		}
	}


public:

	CResizer( long horzPercent = 50, long vertPercent = 67 )
	{
		m_horzPercent = horzPercent;
		m_vertPercent = vertPercent;
		m_OverrideList = NULL;
		m_OverrideCnt  = 0;
		m_Enable = true;
	}

	void Reset()
	{
		// only called if resizer is being reused
		free( m_OverrideList );
		m_OverrideList = NULL;
		m_OverrideCnt  = 0;
	}

	~CResizer()
	{
		Reset();
	}

	// Called by WM_INITDIALOG handler
	void OnInitDialog( HWND hDlg )
	{
		RECT	rc;
		m_hDlg = hDlg;
		GetWindowRect( m_hDlg, &rc );
		m_oldSize.x = rc.right  - rc.left;
		m_oldSize.y = rc.bottom - rc.top;
		m_origSize = m_oldSize;
	}

	// Called by WM_GETMINMAXINFO handler
	void OnGetMinMaxInfo( WPARAM wParam, LPARAM lParam )
	{
		MINMAXINFO * minmax = (MINMAXINFO *) lParam;
		minmax->ptMinTrackSize = m_origSize;
	}

	// Called by WM_SIZE handler
	void OnSize( WPARAM wParam, WPARAM lParam )
	{
		if ( ! m_Enable )
			return;
		if ( wParam == SIZE_MAXIMIZED  ||  wParam == SIZE_RESTORED )  {
			RECT	rc;
			if ( ! GetWindowRect( m_hDlg, &rc ) )
				return;
			m_newSize.x = rc.right  - rc.left;
			m_newSize.y = rc.bottom - rc.top;
			ResizeDialog();
		}
	}

	// Called on WM_NCHITTEST (need for resize grip only)
	UINT OnNcHitTest( WPARAM wParam, LPARAM lParam )
	{
		// check whether cursor is in size grip
		UINT ht = DefWindowProc( m_hDlg, WM_NCHITTEST, wParam, lParam );
		if ( ht == HTCLIENT )  {
	  		if ( ! IsZoomed( m_hDlg ) )  {
				RECT rc;
				GetWindowRect( m_hDlg, &rc );
				rc.left = rc.right  - GetSystemMetrics( SM_CXHSCROLL );
				rc.top  = rc.bottom - GetSystemMetrics( SM_CYVSCROLL );
				POINT pt = { GET_X_LPARAM ( lParam ), GET_Y_LPARAM( lParam ) };
				if ( PtInRect( &rc, pt ) )
					return HTBOTTOMRIGHT;
			}
		}
		return ht;
	}

	// Called on WM_PAINT (need for resize grip only)
	void OnPaint( WPARAM wParam, LPARAM lParam )
	{
		if ( ! IsZoomed( m_hDlg ) )  {
			// Paint size grip
			PAINTSTRUCT		ps;
			HDC				hdc = BeginPaint( m_hDlg, &ps );
			RECT			rc;
			GetClientRect( m_hDlg, &rc );
			rc.left = rc.right  - GetSystemMetrics( SM_CXHSCROLL );
			rc.top  = rc.bottom - GetSystemMetrics( SM_CYVSCROLL );
    		DrawFrameControl( hdc, &rc, DFC_SCROLL, DFCS_SCROLLSIZEGRIP );
			EndPaint( m_hDlg, &ps );
		}
	}


	// If you're lazy just call this on every message
	bool OnMessage( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam )
	{
		switch ( message ) {
			case WM_INITDIALOG:
				OnInitDialog( hDlg );
				return false;

			case WM_GETMINMAXINFO:
				OnGetMinMaxInfo( wParam, lParam );
				return false;

			case WM_SIZE:
				OnSize( wParam, lParam );
				InvalidateRect( hDlg, NULL, true );
				return false;

			case WM_NCHITTEST:
				SetWindowLong( hDlg, DWL_MSGRESULT, OnNcHitTest( wParam, lParam ) );
				return true;

			case WM_PAINT:
				OnPaint( wParam, lParam );
				return false;

			default:
				return false;
		}
	}

	// If you're really lazy just call this on every message
	static bool OnMessageEx( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam )
	{
		CResizer * This = (CResizer *) GetWindowLong( hDlg, GWL_USERDATA );
		
		switch ( message ) {
			case WM_INITDIALOG:
				This = new CResizer();
				SetWindowLong( hDlg, GWL_USERDATA, (LONG)This );
				break;

			case WM_DESTROY:
				delete This;
				SetWindowLong( hDlg, GWL_USERDATA, 0 );
				break;

			case WM_COMMAND:
				switch ( wParam ) {
					case IDOK:
					case IDCANCEL:
						// If dialog is modal we won' receive WM_DESTORY, so assume these close the window
						delete This;
						SetWindowLong( hDlg, GWL_USERDATA, 0 );
						break;
				}
				break; 
		}

		return This ? This->OnMessage( hDlg, message, wParam, lParam ) : false;
	}


	// Called to manually resize the window
	POINT Resize( const POINT * pNewSize )
	{
		if ( pNewSize == NULL )	// if size not specified use original size
			pNewSize = &m_origSize;
		POINT curSize = m_oldSize;
		SetWindowPos( m_hDlg, NULL, 0, 0, pNewSize->x, pNewSize->y, SWP_NOREDRAW|SWP_NOOWNERZORDER|SWP_NOZORDER|SWP_NOMOVE );
		return curSize;
	}

	// Called to explicitly set the anchors for a child window
	void SetHorz( HWND hChild, ANCHOR horzAnchor )
	{
		OVERRIDE * override = FindOverride( hChild, true );
		override->horzAnchor = horzAnchor;
	}
	void SetVert( HWND hChild, ANCHOR vertAnchor )
	{
		OVERRIDE * override = FindOverride( hChild, true );
		override->vertAnchor = vertAnchor;
	}
	void SetBoth( HWND hChild, ANCHOR horzAnchor, ANCHOR vertAnchor )
	{
		OVERRIDE * override = FindOverride( hChild, true );
		override->horzAnchor = horzAnchor;
		override->vertAnchor = vertAnchor;
	}

	void Enable( bool enable )
	{
		m_Enable = enable;
	}
};
