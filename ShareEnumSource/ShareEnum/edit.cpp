//////////////////////////////////////////////////////////////
// Copyright (C) 2002-2003 Bryce Cogswell 
// www.sysinternals.com
// cogswell@winternals.com
//
// You may modify and use this code for personal use only.
// Redistribution in any form is expressly prohibited
// without permission by the author.
//////////////////////////////////////////////////////////////
#define _WIN32_WINNT 0x404
#include <windows.h>
#include <aclui.h>
#include <aclapi.h>
#include <Lmshare.h>
#include <Lm.h>
#include <tchar.h>
#include "resource.h"
#include "shareenum.h"

//#pragma comment( lib, "aclui.lib" )
#pragma comment( lib, "comctl32.lib" )
#pragma comment( lib, "advapi32.lib" )
#pragma comment( lib, "user32.lib" )

// specific access rights
// ----------------------
// SC_MANAGER_ALL_ACCESS 
// WINSTA_ENUMDESKTOPS
// DESKTOP_READOBJECTS
// SERVICE_QUERY_CONFIG, SERVICE_ALL_ACCESS
// KEY_QUERY_VALUE, KEY_READ
// TOKEN_ASSIGN_PRIMARY
// PROCESS_TERMINATE         
// THREAD_TERMINATE               
// JOB_OBJECT_ASSIGN_PROCESS           
// MUTANT_QUERY_STATE  
// TIMER_QUERY_STATE   
// SECTION_QUERY       
// FILE_READ_DATA            
// CLUSAPI_READ_ACCESS


//#define SERVER_ACCESS_ADMINISTER    
//#define SERVER_ACCESS_ENUMERATE     
//#define SERVER_READ         
//#define SERVER_WRITE         
//#define SERVER_EXECUTE       

#define SHARE_FULLCONTROL	0x001F01FF
#define SHARE_CHANGE		0x001301BF
#define SHARE_READ			0x001200A9

SI_ACCESS g_ModifySharesAccess[] = {
	// these are a much easier-to-swallow listing of basic rights for desktops
	{ &GUID_NULL, SHARE_FULLCONTROL,	MAKEINTRESOURCE(IDS_FULL_CONTROL),	SI_ACCESS_GENERAL  },	// Full Control
	{ &GUID_NULL, SHARE_CHANGE,			MAKEINTRESOURCE(IDS_CHANGE),		SI_ACCESS_GENERAL  },	// Change
	{ &GUID_NULL, SHARE_READ,			MAKEINTRESOURCE(IDS_READ),			SI_ACCESS_GENERAL  },	// Read
#if 0
	// advanced (detailed) rights
	{ &GUID_NULL, 0x00000001,	MAKEINTRESOURCE( 1), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00000002,	MAKEINTRESOURCE( 2), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00000004,	MAKEINTRESOURCE( 3), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00000008,	MAKEINTRESOURCE( 4), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00000010,	MAKEINTRESOURCE( 5), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00000020,	MAKEINTRESOURCE( 6), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00000040,	MAKEINTRESOURCE( 7), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00000080,	MAKEINTRESOURCE( 8), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00000100,	MAKEINTRESOURCE( 9), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00000200,	MAKEINTRESOURCE(10), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00000400,	MAKEINTRESOURCE(11), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00000800,	MAKEINTRESOURCE(12), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00001000,	MAKEINTRESOURCE(13), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00002000,	MAKEINTRESOURCE(14), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00004000,	MAKEINTRESOURCE(15), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00008000,	MAKEINTRESOURCE(16), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00010000,	MAKEINTRESOURCE(17), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00020000,	MAKEINTRESOURCE(18), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00040000,	MAKEINTRESOURCE(19), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00080000,	MAKEINTRESOURCE(20), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00100000,	MAKEINTRESOURCE(21), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00200000,	MAKEINTRESOURCE(22), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00400000,	MAKEINTRESOURCE(23), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x00800000,	MAKEINTRESOURCE(24), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x01000000,	MAKEINTRESOURCE(25), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x02000000,	MAKEINTRESOURCE(26), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x04000000,	MAKEINTRESOURCE(27), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x08000000,	MAKEINTRESOURCE(28), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x10000000,	MAKEINTRESOURCE(29), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x20000000,	MAKEINTRESOURCE(30), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x40000000,	MAKEINTRESOURCE(31), SI_ACCESS_SPECIFIC },
	{ &GUID_NULL, 0x80000000,	MAKEINTRESOURCE(32), SI_ACCESS_SPECIFIC },
#endif
};


// Here's my crufted-up mapping for desktop generic rights
GENERIC_MAPPING g_ModifySharesGenericMapping = {
	SHARE_READ,			// GENERIC_READ
	SHARE_CHANGE,		// GENERIC_WRITE
	SHARE_READ,			// GENERIC_EXECUTE
	SHARE_FULLCONTROL,	// GENERIC_ALL
};


HINSTANCE	g_hInst;

PSECURITY_DESCRIPTOR UpdateSD( PSECURITY_DESCRIPTOR OldSD, PSECURITY_DESCRIPTOR NewSD, SECURITY_INFORMATION ri )
{
	BOOL    present;
	BOOL    defaulted;
	PACL	pacl;
	PSID	psid;

	PSECURITY_DESCRIPTOR	sd = LocalAlloc( LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH );
	InitializeSecurityDescriptor( sd, SECURITY_DESCRIPTOR_REVISION );

	if ( OldSD == NULL )
		OldSD = sd;	// use empty descriptor

	//
	// Get SACL
	//
	if ( ! GetSecurityDescriptorSacl( ri & SACL_SECURITY_INFORMATION ? NewSD : OldSD, &present, &pacl, &defaulted ) )
		return NULL;
	SetSecurityDescriptorSacl( sd, present, pacl, defaulted );

	//     
	// Get DACL     
	//      
	if ( ! GetSecurityDescriptorDacl( ri & DACL_SECURITY_INFORMATION ? NewSD : OldSD, &present, &pacl, &defaulted ) )         
		return NULL;      
	SetSecurityDescriptorDacl( sd, present, pacl, defaulted );

	//     
	// Get Owner     
	//
	if ( ! GetSecurityDescriptorOwner( ri & OWNER_SECURITY_INFORMATION ? NewSD : OldSD, &psid, &defaulted ) )         
		return NULL;      
	SetSecurityDescriptorOwner( sd, psid, defaulted );

	//     
	// Get Group     
	//      
	if ( ! GetSecurityDescriptorGroup( ri & GROUP_SECURITY_INFORMATION ? NewSD : OldSD, &psid, &defaulted ) )         
		return NULL;      
	SetSecurityDescriptorGroup( sd, psid, defaulted );

	return sd;
}  


#if 0
struct SidTable : IDataObject
{
	long		m_cRefs;
	int			m_Cnt;
	int			m_Pos;
	HGLOBAL		m_hGlobal;

	SidTable( int cnt )
	{
		m_cRefs		= 0;
		m_Cnt		= cnt;
		m_Pos		= 0;
		m_hGlobal	= GlobalAlloc( GMEM_SHARE, sizeof SID_INFO_LIST + (cnt - ANYSIZE_ARRAY)*sizeof(SID_INFO) + cnt * 2*MAX_PATH * sizeof(TCHAR) );
	}

	~SidTable()
	{
	}

	bool Add( PSID psid, const TCHAR * Name, SID_NAME_USE Use )
	{
		if ( m_Pos >= m_Cnt )
			return false;

		SID_INFO_LIST	*	sids = (SID_INFO_LIST *) GlobalLock( m_hGlobal );
		SID_INFO		*	si   = &sids->aSidInfo[ m_Pos++ ];

		sids->cItems		= m_Pos;
		si->pSid			= psid;
		
		si->pwzCommonName	= (TCHAR *)&sids->aSidInfo[ m_Cnt ] + (m_Pos-1)*2*MAX_PATH;
		_tcscpy( si->pwzCommonName, Name );
		
		si->pwzUPN			= NULL;

		switch ( Use )  {
			case SidTypeUser:	
				si->pwzClass	= _T("User");	
				break;
			case SidTypeGroup:	
			case SidTypeAlias:	
				si->pwzClass	= _T("Group");	
				break;
			case SidTypeComputer:
				si->pwzClass	= _T("Computer");	
				break;
			default:
				si->pwzClass	= NULL;
				break;
		}

		GlobalUnlock( m_hGlobal );
		return true;
	}

	STDMETHODIMP QueryInterface( REFIID iid, void** ppv )
	{
		if ( iid == IID_IDataObject || IID_IUnknown == iid )  {
			*ppv = this;
		} else {
			*ppv = NULL;
			return E_NOINTERFACE;
		}

		reinterpret_cast<IUnknown*>( *ppv )->AddRef();
		return S_OK;
	}
	STDMETHODIMP_(ULONG) AddRef()
	{
		return ++m_cRefs;
	}
	STDMETHODIMP_(ULONG) Release()
	{
		ULONG n = --m_cRefs;
		if ( n == 0 )
			delete this;
		return n;
	}

    HRESULT STDMETHODCALLTYPE GetData( FORMATETC * pformatetcIn, STGMEDIUM * pmedium )
	{
		if ( pformatetcIn->cfFormat != RegisterClipboardFormat( CFSTR_ACLUI_SID_INFO_LIST ) )
			return E_INVALIDARG;
		
		if ( pformatetcIn->tymed != TYMED_HGLOBAL )
			return E_INVALIDARG;

		pmedium->tymed		= TYMED_HGLOBAL;
		pmedium->hGlobal	= m_hGlobal;
		return S_OK;
	}
    HRESULT STDMETHODCALLTYPE GetDataHere( FORMATETC *pformatetc, STGMEDIUM *pmedium)
	{
		return E_NOTIMPL;
	}
    HRESULT STDMETHODCALLTYPE QueryGetData( FORMATETC *pformatetc)
	{
		return E_NOTIMPL;
	}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc( FORMATETC *pformatectIn, FORMATETC *pformatetcOut )
	{
		return E_NOTIMPL;
	}
	HRESULT STDMETHODCALLTYPE SetData( FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease )
	{
		return E_NOTIMPL;
	}
	HRESULT STDMETHODCALLTYPE EnumFormatEtc( DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc )
	{
		return E_NOTIMPL;
	}
	HRESULT STDMETHODCALLTYPE DAdvise( FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection )
	{
		return E_NOTIMPL;
	}
    HRESULT STDMETHODCALLTYPE DUnadvise( DWORD dwConnection )    
	{
		return E_NOTIMPL;
	}
    HRESULT STDMETHODCALLTYPE EnumDAdvise( IEnumSTATDATA **ppenumAdvise )
	{
		return E_NOTIMPL;
	}
};


struct CObjectSecurityInfoBase2 : ISecurityInformation2
{
	long		m_cRefs;

	CObjectSecurityInfoBase2()
	  : m_cRefs(0)
	{
	}
	virtual ~CObjectSecurityInfoBase2() 
	{
	}

	STDMETHODIMP QueryInterface( REFIID iid, void** ppv )
	{
		if ( iid == IID_ISecurityInformation2  ||  IID_IUnknown == iid )  {
			*ppv = this;
		} else {
			*ppv = NULL;
			return E_NOINTERFACE;
		}

		reinterpret_cast<IUnknown*>( *ppv )->AddRef();
		return S_OK;
	}
	STDMETHODIMP_(ULONG) AddRef()
	{
		return ++m_cRefs;
	}
	STDMETHODIMP_(ULONG) Release()
	{
		ULONG n = --m_cRefs;
		if ( 0 == n )
			delete this;
		return n;
	}

	STDMETHODIMP_(BOOL) IsDaclCanonical( PACL pDacl )
	{
		return true;
	}

	STDMETHODIMP LookupSids( ULONG cSids, PSID *rgpSids, LPDATAOBJECT *pSidNames )
	{	
		SidTable	* sids = new SidTable( cSids );
		sids->AddRef();

		for ( ULONG i = 0; i < cSids; ++i )  {
			TCHAR			Name[ MAX_PATH ];
			TCHAR			Domain[ MAX_PATH ];
			SID_NAME_USE	Use;
			DWORD			cbName = MAX_PATH;
			DWORD			cbDomain = MAX_PATH;

			BOOL ok = LookupAccountSid( g_ComputerName+2, rgpSids[i], Name, &cbName, Domain, &cbDomain, &Use );
			if ( ok )  {
				TCHAR fullname[ 2*MAX_PATH ];
				_stprintf( fullname, _T("%s (%s\\%s)"), Name, Domain, Name );

				sids->Add( rgpSids[i], fullname, Use );
			}
		}

		*pSidNames = sids;

		return S_OK;
	}
};
#endif


// This base class factors out common implementation between
// the window station and desktop implementations of ISecurityInformation.
// For instance, the Set/GetSecurity calls can be implemented once,
// since they both just call Set/GetSecurityInfo passing a handle
// (of either a winsta or a desktop).
struct CObjectSecurityInfoBase : ISecurityInformation
{
	long					m_cRefs;
	DWORD					m_grfExtraFlags;
	const wchar_t* const	m_pszObjectName;
	const wchar_t* const	m_pszPageTitle;
	wchar_t					m_Computer[ MAX_PATH ];
	wchar_t				*	m_Name;

	CObjectSecurityInfoBase(	DWORD grfExtraFlags,
								const wchar_t* pszObjectName,
								const wchar_t* pszPageTitle = 0 )
	  : m_cRefs(0),
		m_grfExtraFlags( grfExtraFlags ),
		m_pszObjectName( pszObjectName ),
		m_pszPageTitle( pszPageTitle )
	{
		_tcscpy( m_Computer, m_pszObjectName );
		m_Name = _tcschr( m_Computer+2, '\\' );
		if ( m_Name )  {
			*m_Name++ = 0;
		}
	}
	
	virtual ~CObjectSecurityInfoBase()
	{
	}

	STDMETHODIMP QueryInterface( REFIID iid, void** ppv )
	{
		if ( iid == IID_ISecurityInformation  ||  IID_IUnknown == iid )  {
			*ppv = this;
		} else if ( iid == IID_ISecurityInformation2 )  {
#if 0
			CObjectSecurityInfoBase2	* I2 = new CObjectSecurityInfoBase2;
			*ppv = I2;
#else
			*ppv = NULL;
#endif
		} else  {
			*ppv = NULL;
		}

		if ( *ppv == NULL )
			return E_NOINTERFACE;

		reinterpret_cast<IUnknown*>( *ppv )->AddRef();
		return S_OK;
	}
	STDMETHODIMP_(ULONG) AddRef()
	{
		return ++m_cRefs;
	}
	STDMETHODIMP_(ULONG) Release()
	{
		ULONG n = --m_cRefs;
		if ( 0 == n )
			delete this;
		return n;
	}

	STDMETHODIMP GetObjectInformation( SI_OBJECT_INFO * poi )
	{
		static bool			LookedForDC = false;
		static TCHAR	*	domainController = NULL;
	
		if ( ! LookedForDC )  {
			WKSTA_USER_INFO_1	*	user = NULL;
			NetWkstaUserGetInfo( NULL, 1, (BYTE **)&user );
			NetGetDCName( NULL, user->wkui1_logon_domain, (BYTE **)&domainController );
			NetApiBufferFree( user );
			LookedForDC = true;
		}

		// We want to edit the DACL (PERMS), the OWNER,
		// and we want the Advanced button
		poi->dwFlags		= SI_EDIT_OWNER | SI_EDIT_PERMS | m_grfExtraFlags;

		// this determines the module used to discover stringtable entries
		poi->hInstance		= g_hInst;
		poi->pszServerName	= domainController ? domainController : m_Computer+2;
		poi->pszObjectName	= const_cast<wchar_t*>( m_pszObjectName );
		poi->pszPageTitle	= const_cast<wchar_t*>( m_pszPageTitle );

		if ( m_pszPageTitle )
			poi->dwFlags |= SI_PAGE_TITLE;

		return S_OK;
	}

	STDMETHODIMP GetSecurity( SECURITY_INFORMATION ri, void** ppsd, BOOL bDefault )
	{
		// map directly onto the winsta/desktop security descriptor
		SHARE_INFO_502 * Info502 = NULL;
		DWORD status = NetShareGetInfo( m_Computer, (wchar_t *)m_Name, 502, (BYTE **)&Info502 );
		if ( status == 0 )  {
			PSECURITY_DESCRIPTOR	sd = Info502->shi502_security_descriptor;
			IsValidSecurityDescriptor( sd );
			*ppsd = sd;
		}
		
		return status ? HRESULT_FROM_WIN32(status) : S_OK;
	}

	STDMETHODIMP SetSecurity( SECURITY_INFORMATION ri, void* psd )
	{
		// Get original sd
		BYTE * sdOrig;
		SHARE_INFO_502 * Info502 = NULL;
		DWORD status = NetShareGetInfo( m_Computer, m_Name, 502, (BYTE **)&Info502 );
		if ( status == 0 )  {
			SetLastError( 0 );
			IsValidSecurityDescriptor( Info502->shi502_security_descriptor );
			GetLastError();
			sdOrig = (BYTE *) Info502->shi502_security_descriptor;
		} else {
			return HRESULT_FROM_WIN32(status);
		}

		IsValidSecurityDescriptor( psd );
		IsValidSecurityDescriptor( sdOrig );

		// Combine with new SD components
		PSECURITY_DESCRIPTOR	sdAbs = UpdateSD( sdOrig, psd, ri );
		if ( ! IsValidSecurityDescriptor( sdAbs ) )
			return HRESULT_FROM_WIN32(GetLastError());

		// convert to relative format
		DWORD nb = 0;
		MakeSelfRelativeSD( sdAbs, NULL, &nb );
		PSECURITY_DESCRIPTOR	sdRel = (BYTE *) LocalAlloc( LMEM_FIXED, nb );
		if ( ! MakeSelfRelativeSD( sdAbs, sdRel, &nb ) )
			return HRESULT_FROM_WIN32(GetLastError());
		IsValidSecurityDescriptor( sdRel );
		GetSecurityDescriptorLength( sdRel );

		// Set updated sd
		Info502->shi502_security_descriptor = sdRel;
		status = NetShareSetInfo( m_Computer, m_Name, 502, (BYTE *)Info502, NULL );

		LocalFree( sdAbs );
		LocalFree( sdRel );

		return status ? HRESULT_FROM_WIN32(status) : S_OK;
	}

	STDMETHODIMP PropertySheetPageCallback( HWND hwnd, UINT msg, SI_PAGE_TYPE pt )
	{
		// this is effectively a pass-through from the PropertySheet callback,
		// which we don't care about for this sample
		return S_OK;
	}
};



struct CModifySharesSecurityInfo : CObjectSecurityInfoBase
{
	CModifySharesSecurityInfo( const wchar_t* pszObjectTitle, const wchar_t* pszPageTitle = 0 )
	  : CObjectSecurityInfoBase( SI_CONTAINER | SI_NO_ACL_PROTECT, pszObjectTitle, pszPageTitle )
	{
	}

	~CModifySharesSecurityInfo()
	{
	}

	STDMETHODIMP GetAccessRights(	const GUID*,
									DWORD dwFlags,
									SI_ACCESS** ppAccess,
									ULONG* pcAccesses,
									ULONG* piDefaultAccess )
	{
		// here's where we hand back the desktop permissions->strings mapping
		*ppAccess		= const_cast<SI_ACCESS*>( g_ModifySharesAccess );
		*pcAccesses		= sizeof g_ModifySharesAccess / sizeof *g_ModifySharesAccess;
		*piDefaultAccess = 0;
		return S_OK;
	}

	STDMETHODIMP MapGeneric( const GUID*, UCHAR* pAceFlags, ACCESS_MASK* pMask )
	{
		// here's where we hand back the desktop generic permissions mapping
		MapGenericMask( pMask, &g_ModifySharesGenericMapping );
		return S_OK;
	}

	STDMETHODIMP GetInheritTypes( SI_INHERIT_TYPE** ppInheritTypes, ULONG* pcInheritTypes )
	{
		// Desktops are not containers, and thus have no options for inheritable
		// entries in the DACL or SACL. Since we didn't specify SI_CONTAINER in our
		// GetObjectInformation call, this function will never be called.
		return E_NOTIMPL;
	}
};



int Properties( HINSTANCE hInst, HWND hParent, const TCHAR * shareName )
{
	g_hInst = hInst;


	typedef HPROPSHEETPAGE (WINAPI * typeCreateSecurityPage)( LPSECURITYINFO psi );
	typedef BOOL (WINAPI * typeEditSecurity)( HWND hwndOwner, LPSECURITYINFO psi );
	
	typeCreateSecurityPage	pCreateSecurityPage	= (typeCreateSecurityPage) GetProcAddress( LoadLibrary(L"ACLUI.DLL"), "CreateSecurityPage" );
	typeEditSecurity		pEditSecurity		= (typeEditSecurity)	   GetProcAddress( LoadLibrary(L"ACLUI.DLL"), "EditSecurity" );

	if ( pCreateSecurityPage == NULL  ||  pEditSecurity == NULL )  {
		MessageBox( hParent, _T("Sorry, the File Properties dialog is available only on Windows 2000 and later."),
			APPNAME, MB_OK|MB_ICONINFORMATION );
		return 0;
	}

#if 1
	CModifySharesSecurityInfo * si = new CModifySharesSecurityInfo( shareName, L"Share Permissions" );
	si->AddRef();
	(*pEditSecurity)( hParent, si );
	si->Release();
	return 0;
#else
	// Convert our ISecurityInformation implementations into property pages
	HPROPSHEETPAGE rghps[1];
	{
		CModifySharesSecurityInfo * si0 = new CModifySharesSecurityInfo( L"SrvsvcShareFileInfo",	L"Share Security",	L"Share Permissions" );
//		CModifySharesSecurityInfo * si1 = new CModifySharesSecurityInfo( L"SrvsvcShareFileInfo",    L"Creating File Shares",	L"Modify File Shares" );
//		CModifySharesSecurityInfo * si2 = new CModifySharesSecurityInfo( L"SrvsvcSharePrintInfo",   L"Creating Print Shares",	L"Modify Print Shares" );
//		CModifySharesSecurityInfo * si3 = new CModifySharesSecurityInfo( L"SrvsvcShareAdminInfo",   L"Creating Admin",			L"Modify Admin Shares" );
		return EditSecurity( hParent, si0 );
	
		si0->AddRef();
//		si1->AddRef();
//		si2->AddRef();
//		si3->AddRef();

		rghps[0] = CreateSecurityPage( (LPSECURITYINFO) si0 );
//		rghps[1] = CreateSecurityPage( si1 );
//		rghps[2] = CreateSecurityPage( si2 );
//		rghps[3] = CreateSecurityPage( si3 );

		si0->Release();
//		si1->Release();
//		si2->Release();
//		si3->Release();
	}

	// Wrap our two property pages in a modal dialog by calling PropertySheet
	PROPSHEETHEADER psh; ZeroMemory( &psh, sizeof psh );
	psh.dwSize		= sizeof psh;
	psh.hInstance	= g_hInst;
	psh.hwndParent	= hParent;
	psh.pszCaption	= L"Share Properties";
	psh.nPages		= sizeof rghps / sizeof *rghps;
	psh.phpage		= rghps;

	PropertySheet( &psh );

	return 0;
#endif
}
