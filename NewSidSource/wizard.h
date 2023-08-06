#ifndef _WIZARD_H_INCLUDED_
#define _WIZARD_H_INCLUDED_

class CWizardWindow;
class CPropPage;


#define WM_APP_WIZARD_NEXT				(WM_APP+0)
#define WM_APP_WIZARD_BACK				(WM_APP+1)


class CWizardWindow
{
	HWND				m_hWnd;
	HINSTANCE			m_hInstance;

	HFONT				m_hFontBold;
	HFONT				m_hFontBigBold;
	const TCHAR		*	m_Title;

	int					m_StartPage;
	CPropPage		**	m_Pages;
	int					m_PageCnt;
	int					m_CurrentPage;

	static LONG CALLBACK DialogProc( HWND hDlg, UINT message, UINT wParam, LONG lParam );
	bool			GoNext();	// go to next page
	bool			GoBack();	// go to previous page

    bool			AskToExit();

public:
	CWizardWindow( HINSTANCE hInstance, const TCHAR * Title );

	int				AddPage( CPropPage * page );
	HWND			Run();
	bool			GotoPage( int n, bool forward );
	int				LookupPage( const TCHAR * name );

	HWND			GetHwnd()					{ return m_hWnd; }
	CPropPage	*	GetPage( int n )			{ return n >= 0  &&  n < m_PageCnt ? m_Pages[ n ] : NULL; }
	CPropPage	*	CurrentPage()				{ return GetPage( m_CurrentPage ); }
	bool			GotoPage( const TCHAR * name, bool forward )	{ return GotoPage( LookupPage( name ), forward ); }
	void			SetStartPage( int n )		{ m_StartPage = n; }
	void			EnableNext( bool );
	void			EnableBack( bool );
	void			EnableCancel( bool );
	void			EnableFinish( bool );

public:
    static HWND     GetCurrentDlg();
    
protected:
    static HWND     m_hdlgCurrent;
};




class CPropPage 
{
friend CWizardWindow;
public:
	enum PAGE_TYPE {
		ENTRY,
		INTERNAL,
		FINISH
	};
	typedef bool (* DIALOGPROC)( CPropPage * page, UINT message, UINT wParam, LONG lParam );

private:

	CWizardWindow	*	m_WizardWindow;			// Wizard this page belongs to

	enum PAGE_TYPE 		m_Type;					// Type: Entry, Internal or Finish
	const TCHAR		*	m_Next;					// Page to advance to when Next button pressed
	const TCHAR		*	m_Back;					// Page to back-up to when Back button pressed

	const TCHAR		*	m_HeaderTitle;			// text in header
	const TCHAR		*	m_HeaderSubTitle;		// secondary text in header
	const TCHAR		*	m_Template;			
	bool			(*	m_DialogProc)( CPropPage * page, UINT message, UINT wParam, LONG lParam );
	HWND				m_hWnd;
	int					m_Previous;				// how we got to this page
	bool				m_Forward;				// we are advancing to this page (not backing up to it)

	static BOOL CALLBACK DialogProc( HWND hDlg, UINT message, UINT wParam, LONG lParam );

public:
	CPropPage( const TCHAR * HeaderTitle, const TCHAR * HeaderSubTitle, const TCHAR * Template, DIALOGPROC DialogProc );

	HWND				GetDlgItem( int n )				{ return ::GetDlgItem( m_hWnd, n ); }
	void				SetNext( const TCHAR * name )	{ m_Next = name; }
	void				SetBack( const TCHAR * name )	{ m_Back = name; }

	CWizardWindow	*	GetWizard() const				{ return m_WizardWindow; };
	void				SetType( PAGE_TYPE type )		{ m_Type = type; }

	bool				GotoPage( const TCHAR * name, bool forward )	{ return m_WizardWindow->GotoPage( name, forward ); }
	bool				Forward()						{ return m_Forward; }
//	bool				AdvancePage( int n, bool forward )	{ return m_WizardWindow->GotoPage( n, forward ); }

	void				EnableNext( bool en )			{ m_WizardWindow->EnableNext( en );		}
	void				EnableBack( bool en )			{ m_WizardWindow->EnableBack( en );		}
	void				EnableCancel( bool en )			{ m_WizardWindow->EnableCancel( en );	}
	void				EnableFinish( bool en )			{ m_WizardWindow->EnableFinish( en );	}

	void				GoNext()						{ PostMessage( m_WizardWindow->GetHwnd(), WM_APP_WIZARD_NEXT, 0, 0 ); }
	void				GoBack()						{ PostMessage( m_WizardWindow->GetHwnd(), WM_APP_WIZARD_BACK, 0, 0 ); }

	// allow this class to be used as a window handle
	operator			HWND() const					{ return m_hWnd; }
	HWND				GetHwnd()						{ return m_hWnd; }
};

#endif

