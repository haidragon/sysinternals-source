//----------------------------------------------------------------------
//
// NewSID
//
// Copyright (c) 1997-2002 Mark Russinovich and Bryce Cogswell
//
// Wizard GUI support routines.
//
//----------------------------------------------------------------------
#include <windows.h>
#include <tchar.h>
#include "resource.h"
#include "wizard.h"
#include "sid.h"

HWND CWizardWindow::m_hdlgCurrent = NULL;

CPropPage::CPropPage( const TCHAR * HeaderTitle, const TCHAR * HeaderSubTitle, const TCHAR * Template, DIALOGPROC DialogProc )
{
	m_DialogProc		= DialogProc;
	m_Template			= Template;
	m_Type				= CPropPage::INTERNAL;
	m_HeaderTitle		= HeaderTitle;
	m_HeaderSubTitle	= HeaderSubTitle;
	m_Next				= (const TCHAR *)1;	// next
	m_Back				= NULL;				// previous in history
	m_hWnd				= NULL;
	m_Forward			= true;
}

//----------------------------------------------------------------------
//  
//  DialogProc
//  
//  The default dialog procedure for pages.  The user's page procedure
//  effectively subclasses this.
//
//----------------------------------------------------------------------
BOOL CALLBACK CPropPage::DialogProc( HWND hDlg, UINT message, UINT wParam, LONG lParam )
{
	CPropPage * page = (CPropPage *) GetWindowLong( hDlg, GWL_USERDATA );

	// If first call then set window data
	if ( message == WM_INITDIALOG )  {
		SetWindowLong( hDlg, GWL_USERDATA, lParam );
		page = (CPropPage *) lParam;
		page->m_hWnd = hDlg;
	}

	// call property page's event handler first
	if ( page  &&  page->m_DialogProc ) {

		SetWindowLong( hDlg, DWL_MSGRESULT, TRUE );
		if ( page->m_DialogProc( page, message, wParam, lParam ) )  {
			// result is in DWL_MSGRESULT
			// tell wizard that page handled message
			return (BOOL) GetWindowLong( hDlg, DWL_MSGRESULT );
		}
	}

	// call our own event handler
	switch ( message ) {
			
		case WM_CTLCOLORSTATIC:
			if ( page->m_Type == ENTRY || page->m_Type == FINISH )  {
				// White background for welcome text
				return (LONG)GetStockObject(WHITE_BRUSH);
			}
			break;

		case WM_COMMAND:
			switch (wParam) {

				case IDOK:
					break;

				case IDCANCEL:
					break;

				case IDC_BACK:
					break;
		}
		break;
	}

	return false;
}




CWizardWindow::CWizardWindow( HINSTANCE hInstance, const TCHAR * Title )
{
	m_hInstance		= hInstance;

	// initialize list of pages to be empty
	m_Pages			= NULL;
	m_PageCnt		= 0;
	m_CurrentPage	= -1;
	m_StartPage		= 0;
	m_Title			= Title;

	// initialize fonts
	LOGFONT		logfont;

	m_hFontBold = (HFONT)GetStockObject( DEFAULT_GUI_FONT );
	GetObject( m_hFontBold, sizeof logfont, &logfont);
	logfont.lfWeight = FW_BOLD;
	m_hFontBold = CreateFontIndirect( &logfont );
	logfont.lfHeight = -17;
	logfont.lfWeight = FW_NORMAL;
	_tcscpy( logfont.lfFaceName, _T("verdana"));
	m_hFontBigBold = CreateFontIndirect( &logfont );

	// Register wizard class
	WNDCLASSEX	wndclass = { 0 };
	wndclass.cbSize			= sizeof wndclass;
	wndclass.style          = CS_HREDRAW | CS_VREDRAW;	// causes flicker...
	wndclass.lpfnWndProc	= DialogProc;
	wndclass.cbWndExtra		= DLGWINDOWEXTRA;
	wndclass.hInstance		= m_hInstance;
	wndclass.hIcon          = LoadIcon( m_hInstance, TEXT("APPICON") );
	wndclass.hIconSm		= LoadIcon( m_hInstance, TEXT("APPICON") );
	wndclass.hCursor		= LoadCursor( NULL, IDC_ARROW );
	wndclass.hbrBackground	= GetSysColorBrush(COLOR_BTNFACE);
	wndclass.lpszClassName	= TEXT("WizardClass");
	wndclass.lpszMenuName   = NULL;
	RegisterClassEx( &wndclass );
}


int CWizardWindow::AddPage( CPropPage * page )
{
	m_Pages = (CPropPage **)realloc( m_Pages, (m_PageCnt+1)*sizeof m_Pages[0] );
	m_Pages[ m_PageCnt ] = page;
	page->m_WizardWindow = this;
	return m_PageCnt++;
}



//----------------------------------------------------------------------
//  
//  GotoPage
//  
//  Switch to a specified wizard page
//
//----------------------------------------------------------------------
bool CWizardWindow::GotoPage( int nPage, bool forward )
{
	RECT					rc;
	POINT					ul, lr;
	bool					isInterior = false;

	// Make sure page is in range
	if ( nPage < 0  ||  nPage >= m_PageCnt )
		return false;

	// Get pointers to current page and new page
	CPropPage * oldPage = CurrentPage();
	CPropPage * newPage = GetPage( nPage );

	// Update shared buttons
	if ( newPage->m_Type == CPropPage::ENTRY )  {
		// Entry page
		EnableBack( false );
		EnableNext( true );
		EnableCancel( true );
	} else if ( newPage->m_Type == CPropPage::FINISH )  {
		// Exit page
		EnableFinish( true );
		EnableCancel( false );
		EnableBack( true );
	} else {
		// Interior page
		isInterior = true;
		EnableNext( true );
		EnableCancel( true );
		EnableBack( true );
	}

	// Set forward/backward status on pages
	newPage->m_Forward = forward;
	if ( oldPage )
		oldPage->m_Forward = forward;

	// If new window hasn't been created, create it now
	if ( newPage->GetHwnd() == NULL )  {

		// Get window position
		if ( isInterior )
			GetWindowRect( GetDlgItem( m_hWnd, IDC_PAGEFRAME ), &rc );
		else
			GetWindowRect( GetDlgItem( m_hWnd, IDC_EXTPAGEFRAME ), &rc );			
		ul.y = rc.top;
		ul.x = rc.left;
		lr.y = rc.bottom;
		lr.x = rc.right;
		MapWindowPoints( HWND_DESKTOP, m_hWnd, &ul, 1 );
		MapWindowPoints( HWND_DESKTOP, m_hWnd, &lr, 1 );

		// create window
		newPage->m_hWnd = CreateDialogParam( m_hInstance, newPage->m_Template, 
											m_hWnd, CPropPage::DialogProc, (LPARAM)newPage ); 

		// Move window to correct location
		MoveWindow( newPage->m_hWnd, ul.x, ul.y, lr.x - ul.x, lr.y - ul.y, TRUE );
	}


	// Set window text
	SetDlgItemText( m_hWnd, IDC_TITLE,    newPage->m_HeaderTitle );
	SetDlgItemText( m_hWnd, IDC_EXTTITLE, newPage->m_HeaderTitle );
	SetDlgItemText( m_hWnd, IDC_SUBTITLE, newPage->m_HeaderSubTitle );

	// Show/Hide graphical elements
	ShowWindow( GetDlgItem( m_hWnd, IDC_UPPERLINE ), isInterior ? SW_SHOW : SW_HIDE );
	ShowWindow( GetDlgItem( m_hWnd, IDC_SUBTITLE  ), isInterior ? SW_SHOW : SW_HIDE );
	ShowWindow( GetDlgItem( m_hWnd, IDC_BANNER    ), isInterior ? SW_SHOW : SW_HIDE );
	ShowWindow( GetDlgItem( m_hWnd, IDC_TITLE     ), isInterior ? SW_SHOW : SW_HIDE );
	ShowWindow( GetDlgItem( m_hWnd, IDC_WATERMARK ), isInterior ? SW_HIDE : SW_SHOW );
	ShowWindow( GetDlgItem( m_hWnd, IDC_EXTTITLE  ), isInterior ? SW_HIDE : SW_SHOW );


	// Place white background appropriately
	if ( isInterior )  {
		// Header
		GetWindowRect( GetDlgItem( m_hWnd, IDC_UPPERLINE ), &rc );
		lr.x = rc.right;
		lr.y = rc.bottom - 2;
		MapWindowPoints( HWND_DESKTOP, m_hWnd, &lr, 1 );
		ul.x = 0;
		ul.y = 0;
	} else {
		// Right size
		GetWindowRect( GetDlgItem( m_hWnd, IDC_EXTPAGEFRAME ), &rc );
		ul.x = rc.left;
		ul.y = rc.top;
		lr.x = rc.right;
		lr.y = rc.bottom + 4;
		MapWindowPoints( HWND_DESKTOP, m_hWnd, &ul, 1 );
		MapWindowPoints( HWND_DESKTOP, m_hWnd, &lr, 1 );
		ul.y = 0;
	}
	SetWindowPos( GetDlgItem(m_hWnd,IDC_WHITEBACKGROUND), HWND_TOP, ul.x, ul.y, lr.x, lr.y, 0 );


	// Make new window current. This ensures that if the new window calls us recursively we act relative to it.
	if ( forward )
		newPage->m_Previous = m_CurrentPage;
	m_CurrentPage = nPage;

	// Hide old window
	if ( oldPage != NULL  &&  oldPage->GetHwnd() )
		ShowWindow( oldPage->GetHwnd(), SW_HIDE );

	// Dispay new window
	ShowWindow( newPage->GetHwnd(), SW_SHOWNORMAL );

	// Set focus to NEXT button, or first control of property page
	if ( IsWindowEnabled( GetDlgItem( m_hWnd, IDOK ) ) )
		SetFocus( GetDlgItem( m_hWnd, IDOK ) );
	else
		SetFocus( GetNextDlgTabItem( m_hWnd, GetDlgItem( m_hWnd, IDCANCEL ), false ) );

	return true;
}


int CWizardWindow::LookupPage( const TCHAR * name )
{
	for ( int i = 0; i < m_PageCnt; ++i )
		if ( _tcsicmp( name, m_Pages[i]->m_Template ) == 0 )
			return i;
	return -1;
}



void CWizardWindow::EnableNext( bool enable )
{
	SetDlgItemText( m_hWnd, IDOK, TEXT("&Next >") );
	EnableWindow( GetDlgItem( m_hWnd, IDOK ), enable );
}

void CWizardWindow::EnableBack( bool enable )
{
	EnableWindow( GetDlgItem( m_hWnd, IDC_BACK ), enable );
}

void CWizardWindow::EnableCancel( bool enable )
{
	EnableWindow( GetDlgItem( m_hWnd, IDCANCEL ), enable );
}

void CWizardWindow::EnableFinish( bool enable )
{
	SetDlgItemText( m_hWnd, IDOK, TEXT("&Finish") );
	EnableWindow( GetDlgItem( m_hWnd, IDOK ), enable );
}


bool CWizardWindow::GoNext() 
{
	bool			ok;
	CPropPage	*	page = CurrentPage();
	if ( abs( (long)page->m_Next ) < 1024 )
		ok = GotoPage( m_CurrentPage + (long)page->m_Next, true );
	else
		ok = GotoPage( page->m_Next, true );
	return ok;
}


bool CWizardWindow::GoBack()
{
	bool			ok;
	CPropPage	*	page = CurrentPage();
	if ( page->m_Back == NULL )
		ok = GotoPage( page->m_Previous, false );
	else if ( abs( (long)page->m_Back ) < 1024 )
		ok = GotoPage( m_CurrentPage + (long)page->m_Back, false );
	else
		ok = GotoPage( page->m_Back, false );
	return ok;
}

HWND CWizardWindow::GetCurrentDlg()
{
    return m_hdlgCurrent;
}

bool CWizardWindow::AskToExit()
{
    return ::MessageBox( 
		m_hWnd, 
        TEXT("Are you sure you want to exit?"), 
        APPNAME, 
        MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 ) == IDYES;
}

//----------------------------------------------------------------------
//  
//  WizardWindowProc
//  
//  
//
//----------------------------------------------------------------------
LONG CALLBACK CWizardWindow::DialogProc( HWND hDlg, UINT message, UINT wParam, LONG lParam )
{
	CWizardWindow* wiz = (CWizardWindow *) GetWindowLong( hDlg, GWL_USERDATA );
	bool ok;
	CPropPage* page;
    
	switch ( message ) {
			
		case WM_INITDIALOG:
			// save pointer to "this" pointer
			SetWindowLong( hDlg, GWL_USERDATA, lParam );
			wiz = (CWizardWindow *) lParam;
			wiz->m_hWnd = hDlg;

			// Set title of wizard
			SetWindowText( hDlg, wiz->m_Title );

			// Create white background for header
			CreateWindow( TEXT("STATIC"), TEXT(""), WS_CHILD|WS_VISIBLE|SS_WHITERECT, 0, 0, 0, 0, hDlg, (HMENU) IDC_WHITEBACKGROUND, wiz->m_hInstance, 0 );

			// Create first page
			wiz->GotoPage( wiz->m_StartPage, true );

			return false;

		case WM_CTLCOLORSTATIC:
			// Make sure title is displayed in bold
			if ( (HWND)lParam == GetDlgItem(hDlg,IDC_TITLE) )  {
				// interior page title
				HDC	hdc = (HDC)wParam;
				SelectObject( hdc, wiz->m_hFontBold );
				return (LONG)GetStockObject(WHITE_BRUSH);
			} else if ( (HWND)lParam == GetDlgItem(hDlg,IDC_EXTTITLE) )  {
				// exterior page title
				HDC	hdc = (HDC)wParam;
				SelectObject( hdc, wiz->m_hFontBigBold );
				return (LONG)GetStockObject(WHITE_BRUSH);
			} else if ( (HWND)lParam == GetDlgItem(hDlg,IDC_SUBTITLE) )  {
				// interior page subtitle
				return (LONG)GetStockObject(WHITE_BRUSH);
			}
			break;

		case WM_APP_WIZARD_NEXT:
			return wiz->GoNext();

		case WM_APP_WIZARD_BACK:
			return wiz->GoBack();

		case WM_COMMAND:
            // forward command to property page for handling
			page = wiz->CurrentPage();
			if ( page->m_DialogProc( page, WM_COMMAND, wParam, lParam ) )
				// handled by property page
				return GetWindowLong( *page, DWL_MSGRESULT );

            // wasn't handled, so perform default action
			switch ( wParam ) {
				case IDOK:
                    if ( page->m_Type == CPropPage::FINISH )
                    {
                        ::PostQuitMessage( 0 );
                        //::PostMessage( hDlg, WM_CLOSE, 0, NULL );
                        return TRUE;
                    }

					ok = wiz->GoNext();
					if ( !ok )
						MessageBox( hDlg, TEXT("Page not found"), APPNAME, MB_OK|MB_ICONSTOP );
					return TRUE;

				case IDC_BACK:
					ok = wiz->GoBack();
					if ( !ok )
						MessageBox( hDlg, TEXT("Page not found"), APPNAME, MB_OK|MB_ICONSTOP );
					return TRUE;

				case IDCANCEL:
					//if ( AskToExit() )  {
						SendMessage( hDlg, WM_CLOSE, 0, 0 );
					//}
					//return TRUE;

			}
			break;
			
		case WM_ACTIVATE:
			// track currently active dialog box so IsDialogMessage can perform translations
            CWizardWindow::m_hdlgCurrent = wParam ? hDlg : NULL;
			break;


		case WM_CLOSE:
            if ( wiz->CurrentPage()->m_Type != CPropPage::FINISH )
            {
                if ( wiz->AskToExit() )
                    PostQuitMessage( 0 );
            }
            else
                PostQuitMessage( 0 );
			
            return TRUE;

		default:
			if ( message < WM_APP  ||  message > 0xFFFF )
				break;
			// fall through
		case WM_PARENTNOTIFY:
			// forward command to property page for handling
			page = wiz->CurrentPage();
			if ( page )
				if ( page->m_DialogProc( page, message, wParam, lParam ) )
					return GetWindowLong( *page, DWL_MSGRESULT );
			break;
	}

	// Not processed: perform default processing
	return DefWindowProc( hDlg, message, wParam, lParam );
}


HWND CWizardWindow::Run()
{
	return CreateDialogParam( m_hInstance, TEXT("WIZARD"), NULL, (DLGPROC)DialogProc, (LPARAM)this );
}
