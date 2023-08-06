//////////////////////////////////////////////////////////////
// Copyright (C) 2002-2003 Bryce Cogswell 
// www.sysinternals.com
// cogswell@winternals.com
//
// You may modify and use this code for personal use only.
// Redistribution in any form is expressly prohibited
// without permission by the author.
//////////////////////////////////////////////////////////////
#define _WIN32_WINNT 0x0400		// WM_MOUSEWHEEL support
#include <windows.h>
#include <stdio.h>
#include <commctrl.h>
#include <tchar.h>
#include <ctype.h>
#include <lm.h>
#include <Aclapi.h>
#include <comdef.h>		// bstr_t support

#include "shareenum.h"
#include "resource.h"
#include "resizer.h"
#include "listview.h"


bool IsDomainAdmin( const TCHAR * domain );
int Properties( HINSTANCE hInst, HWND hParent, const TCHAR * shareName );
bool PingAddress( const _bstr_t & destaddr, int dwTimeout );

const TCHAR ALL_DOMAINS[]	= _T(" <All domains>");
const TCHAR IP_RANGE[]		= _T(" <IP address range>");

struct {
	HINSTANCE	hInst;
	HWND		Abort;		// 'abort in progress' window
	CShare	*	ShareList;
	TCHAR		LocalComputerName[ MAX_PATH ];
	long		ThreadCount;
	long		MaxThreads;
	HWND		hMainDlg;
} g;


#define THREAD_STACK_SIZE	0	// (256*1024)



static struct LISTVIEW_COLUMN Columns[] = 
{
	{ TEXT("Share Path"),	240,	DATATYPE_TEXT},
	{ TEXT("Local Path"),	100,	DATATYPE_TEXT},
	{ TEXT("Domain"),		100,	DATATYPE_TEXT},
	{ TEXT("Type"),			80,		DATATYPE_TEXT},
	{ TEXT("Everyone"),		80,		DATATYPE_TEXT},
	{ TEXT("Other Read"),	120,	DATATYPE_TEXT},
	{ TEXT("Other Write"),	120,	DATATYPE_TEXT},
	{ TEXT("Deny"),			80,		DATATYPE_TEXT},
	{ NULL,					0,		(DATATYPE)-1}
};



PSID EveryoneSid()
{
	static PSID	sid = NULL;
	
	if ( sid == NULL )  {
		SID_IDENTIFIER_AUTHORITY SIDAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
		AllocateAndInitializeSid( &SIDAuthWorld, 1,
									 SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0,
									 &sid );
	}
	return sid;
}

PSID LocalAdminSid()
{
	static PSID sid = NULL;

	if ( sid == NULL )  {
		SID_IDENTIFIER_AUTHORITY ntauth = SECURITY_NT_AUTHORITY;
		AllocateAndInitializeSid( &ntauth, 2,
									SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, 
									&sid );
	}
	return sid;
}

PSID SystemSid()
{
	static PSID	sid = NULL;

	if ( sid == NULL )  {
		SID_IDENTIFIER_AUTHORITY SIDAuthWorld = SECURITY_NT_AUTHORITY;
		AllocateAndInitializeSid( &SIDAuthWorld, 1,
									 SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0,
									 &sid );
	}
	return sid;
}



TCHAR * GetTextualSid( PSID pSid )
{
	PSID_IDENTIFIER_AUTHORITY psia;
	DWORD dwSubAuthorities;
	DWORD dwCounter;
	DWORD dwSidSize;

	// Validate the binary SID.
	if ( ! IsValidSid( pSid ) )
		return NULL;

	// Get the identifier authority value from the SID.
	psia = GetSidIdentifierAuthority( pSid );

	// Get the number of subauthorities in the SID.
	dwSubAuthorities = *GetSidSubAuthorityCount(pSid);

	// Compute the buffer length.
	// S-SID_REVISION- + IdentifierAuthority- + subauthorities- + NULL
	dwSidSize = (15 + 12 + (12 * dwSubAuthorities) + 1) * sizeof(TCHAR);

	// Check input buffer length.
	// If too small, indicate the proper size and set last error.
	TCHAR	*	TextualSid = new TCHAR[ dwSidSize ];

	// Add 'S' prefix and revision number to the string.
	dwSidSize = wsprintf( TextualSid, TEXT("S-%lu-"), SID_REVISION );

	// Add SID identifier authority to the string.
	if ( (psia->Value[0] != 0) || (psia->Value[1] != 0) )  {
		dwSidSize += wsprintf( TextualSid + dwSidSize,
								TEXT("0x%02hx%02hx%02hx%02hx%02hx%02hx"),
								(USHORT)psia->Value[0],
								(USHORT)psia->Value[1],
								(USHORT)psia->Value[2],
								(USHORT)psia->Value[3],
								(USHORT)psia->Value[4],
								(USHORT)psia->Value[5]);
	} else {
		dwSidSize += wsprintf( TextualSid + dwSidSize,
								TEXT("%lu"),
								(ULONG)(psia->Value[5]      )   +
								(ULONG)(psia->Value[4] <<  8)   +
								(ULONG)(psia->Value[3] << 16)   +
								(ULONG)(psia->Value[2] << 24)   );
	}

	// Add SID subauthorities to the string.
	//
	for ( dwCounter = 0; dwCounter < dwSubAuthorities; dwCounter++ )  {
		dwSidSize += wsprintf( TextualSid + dwSidSize, TEXT("-%lu"), *GetSidSubAuthority(pSid, dwCounter) );
	}

	return TextualSid;
}



int AddListviewRow( HWND hListview, const TCHAR * path, const TCHAR * domain, const TCHAR * localpath, const TCHAR * type, 
				   const TCHAR * Everyone, const TCHAR * OtherRead, const TCHAR * OtherWrite, 
				   const TCHAR * deny )
{
	CShare * share = new CShare( path, localpath, domain, type, Everyone, OtherRead, OtherWrite, deny, ',' );
	share->InsertInList( &g.ShareList );

	// Add to listview
	LVITEM	item;
	item.mask = LVIF_TEXT | LVIF_IMAGE;
	item.iItem = 0x7FFFFFFF;
	item.iSubItem = 0;
	item.iImage = 0;
	item.pszText = (TCHAR *) path;
	item.iItem = ListView_InsertItem( hListview, &item );

	// set param to index, so we can look ourself up during sorting
	item.mask = LVIF_PARAM;
	item.lParam = item.iItem;
	ListView_SetItem( hListview, &item );
	item.mask = LVIF_TEXT;

	item.iSubItem += 1;
	item.pszText = (TCHAR *) localpath;
	ListView_SetItem( hListview, &item );

	item.iSubItem += 1;
	item.pszText = (TCHAR *) domain;
	ListView_SetItem( hListview, &item );

	item.iSubItem += 1;
	item.pszText = (TCHAR *) type;
	ListView_SetItem( hListview, &item );

	item.iSubItem += 1;
	item.pszText = (TCHAR *) Everyone;
	ListView_SetItem( hListview, &item );

	item.iSubItem += 1;
	item.pszText = (TCHAR *) OtherRead;
	ListView_SetItem( hListview, &item );

	item.iSubItem += 1;
	item.pszText = (TCHAR *) OtherWrite;
	ListView_SetItem( hListview, &item );

	item.iSubItem += 1;
	item.pszText = (TCHAR *) deny;
	ListView_SetItem( hListview, &item );

	return item.iItem;
}


int GetAccountName( TCHAR * buf, const TCHAR * host, PSID sid )
{
	TCHAR			domainName[ MAX_PATH ];
	TCHAR			userName[ MAX_PATH ];
	DWORD			nameLength = MAX_PATH - 1;
	SID_NAME_USE	snu;

#if 1
	if ( EqualSid( sid, EveryoneSid() ) )
		// hardwire name so we work internationally
		return _stprintf( buf, _T("Everyone") );
#endif

	// lookup account on local system
	if ( LookupAccountSid( NULL, sid, userName, &nameLength, domainName, &nameLength, &snu ) )  {
		if ( domainName[0] )
			return _stprintf( buf, _T("%s\\%s"), domainName, userName );
		else
			return _stprintf( buf, _T("%s"), userName );
	}

	// look up account on remote system
	if ( LookupAccountSid( host, sid, userName, &nameLength, domainName, &nameLength, &snu ) )  {
		if ( domainName[0] )
			return _stprintf( buf, _T("%s\\%s"), domainName, userName );
		else
			return _stprintf( buf, _T("%s"), userName );
	}

	TCHAR * text = GetTextualSid( sid );
	int nb = _stprintf( buf, _T("%s"), text );
	delete []text;
	return nb;
}


void GetMask( ACCESS_MASK mask, bool * Read, bool * Write )
{
#define READ_MASK	(FILE_READ_ATTRIBUTES|FILE_READ_DATA|FILE_READ_EA)
#define	WRITE_MASK	(FILE_APPEND_DATA|FILE_WRITE_ATTRIBUTES|FILE_WRITE_DATA|FILE_WRITE_EA)

	*Read	= (mask & READ_MASK ) != 0;
	*Write	= (mask & WRITE_MASK) != 0;
}

int GetMask( TCHAR * buf, ACCESS_MASK mask )
{
#define READ_MASK	(FILE_READ_ATTRIBUTES|FILE_READ_DATA|FILE_READ_EA)
#define	WRITE_MASK	(FILE_APPEND_DATA|FILE_WRITE_ATTRIBUTES|FILE_WRITE_DATA|FILE_WRITE_EA)

	bool Read	= (mask & READ_MASK ) != 0;
	bool Write	= (mask & WRITE_MASK) != 0;

	if ( Read && Write )
		return _stprintf( buf, _T("Read/Write (%X)"), mask );
	if ( Read )
		return _stprintf( buf, _T("Read (%X)"), mask );
	if ( Write )
		return _stprintf( buf, _T("Write (%X)"), mask );
	return _stprintf( buf, _T("Special (%X)"), mask );
}


bool ACLtext( PACL Acl, const TCHAR * host, TCHAR * EveryoneRead, TCHAR * EveryoneWrite, TCHAR * OtherRead, TCHAR * OtherWrite, TCHAR * deny )
{
	ACL_SIZE_INFORMATION     aclSizeInfo;
	ACL_REVISION_INFORMATION aclRevInfo;

	struct USER_PERM {
		PSID			sid;
		ACCESS_MASK		mask[ ACCESS_MAX_MS_ACE_TYPE ];
	};

	EveryoneRead[0]		= 0;
	EveryoneWrite[0]	= 0;
	OtherRead[0]		= 0;
	OtherWrite[0]		= 0;
	deny[0]				= 0;

	if ( ! GetAclInformation( Acl, &aclSizeInfo, sizeof(ACL_SIZE_INFORMATION), AclSizeInformation ) )  {
		_stprintf( EveryoneRead, TEXT("Could not get AclSizeInformation") );
		return false;
	}

	if ( ! GetAclInformation( Acl, &aclRevInfo, sizeof(ACL_REVISION_INFORMATION), AclRevisionInformation ) )  {
		_stprintf( EveryoneRead, TEXT("Could not get AclRevisionInformation"));
		return false;
	}

	USER_PERM *	UserList	= new USER_PERM[ aclSizeInfo.AceCount ];
	DWORD		UserCnt		= 0;

	for ( ULONG i = 0; i < aclSizeInfo.AceCount; i++ )  {
		LPVOID ace;
		if ( ! GetAce( Acl, i, &ace ) )
			break;

		ACE_HEADER *	aceHeader = (ACE_HEADER *) ace;
		ACCESS_MASK		aceMask;
		PSID			aceSid;

		if ( aceHeader->AceType == ACCESS_ALLOWED_ACE_TYPE )  {

			ACCESS_ALLOWED_ACE	*	paaace = (ACCESS_ALLOWED_ACE *) ace;
			aceMask	= paaace->Mask;
			aceSid	= &paaace->SidStart;

		} else if ( aceHeader->AceType == ACCESS_DENIED_ACE_TYPE )  {

			ACCESS_DENIED_ACE	*	padace = (ACCESS_DENIED_ACE *) ace;
			aceMask	= padace->Mask;
			aceSid	= &padace->SidStart;

		} else {

			// Unrecognized
			continue;
		}

		if ( ! IsValidSid( aceSid ) )
			// not a valid sid
			continue;
	
		for ( DWORD u = 0; u < UserCnt; ++u )
			if ( EqualSid( UserList[u].sid, aceSid ) )
				break;
		if ( u >= UserCnt )  {
			UserList[u].sid	= aceSid;
			memset( UserList[u].mask, 0, sizeof UserList[u].mask );
			++UserCnt;
		}

		// add mask to list of masks
		UserList[u].mask[ aceHeader->AceType ]	|= aceMask;
	}

	for ( i = 0; i < UserCnt; ++i )  {
		USER_PERM	*	user = &UserList[i];
		TCHAR		**	read;
		TCHAR		**	write;

		if ( EqualSid( user->sid, EveryoneSid() ) )  {
			read = &EveryoneRead;
			write = &EveryoneWrite;
		} else {
			read = &OtherRead;
			write = &OtherWrite;
		}
	
		if ( user->mask[ACCESS_ALLOWED_ACE_TYPE] )  {

			bool bRead, bWrite;
			GetMask( user->mask[ACCESS_ALLOWED_ACE_TYPE], &bRead, &bWrite );

			if ( bRead )  {
				*read += GetAccountName( *read, host, user->sid );
				*read += _stprintf( *read, _T(", ") );
			}
			if ( bWrite )  {
				*write += GetAccountName( *write, host, user->sid );
				*write += _stprintf( *write, _T(", ") );
			}
		}

		if ( user->mask[ACCESS_DENIED_ACE_TYPE] )  {
			deny += GetAccountName( deny, host, user->sid );
			deny += _stprintf( deny, _T(":") );
			deny += GetMask( deny, user->mask[ACCESS_DENIED_ACE_TYPE] );
			deny += _stprintf( deny, _T(" ") );
		}
	}
	delete []UserList;

	return true;
}





//==============================================================
//
// EnumerateDomains
//
// Create a list of computer domains accessible to this system
//
//==============================================================

class CWorker {
public:
	void Start()
	{
		HANDLE	hThread = NULL;
		InterlockedIncrement( &g.ThreadCount );
#if _MT
		if ( g.MaxThreads == 0  ||  g.ThreadCount < g.MaxThreads )  {
			DWORD	id;
			hThread = CreateThread( NULL, THREAD_STACK_SIZE, Work, this, CREATE_SUSPENDED, &id );
		}
#endif
		if ( hThread )  {
			// thread created
			UpdateStatus();
			SetThreadPriority( hThread, THREAD_PRIORITY_LOWEST );
			ResumeThread( hThread );
		} else {
			// thread create failed, so don't use separate thread
			Work();
		}
	}

	virtual ~CWorker()
	{
	}

protected:
	void UpdateStatus()
	{
		TCHAR msg[ MAX_PATH ];
		if ( g.ThreadCount )
			_stprintf( msg, _T("%d request%s pending..."), g.ThreadCount, g.ThreadCount == 1 ? _T("") : _T("s") );
		else
			msg[0] = 0;
		SetWindowText( GetDlgItem( g.hMainDlg, IDC_STATUS ), msg );
	}

	static DWORD WINAPI Work( void * This )
	{
		CWorker * obj = (CWorker *)This;
		obj->Work();
		return 0;
	}

	virtual void Work() = 0;

	void Finish()
	{
		if ( InterlockedDecrement( &g.ThreadCount ) == 0 )  {
			// We're done enumerating
			PostMessage( g.hMainDlg, WM_APP_ENUM_COMPLETE, 0, 0 );
		}
		UpdateStatus();
		delete this;
	}
};



class CEnumerateShares : public CWorker {
	HWND		m_hListview;
	bstr_t		m_ComputerName;
	bstr_t		m_Domain;

public:
	CEnumerateShares( HWND hListview, const TCHAR * ComputerName, const TCHAR * domain ) :
					m_hListview( hListview ), m_ComputerName( ComputerName ), m_Domain( domain )
	{
	}

	~CEnumerateShares()
	{
	}

private:

	void Work()
	{
		// Enumerate shares on the server
		DWORD				resume = 0;
		NET_API_STATUS		res;

		// Convert an IP address to a name
		if ( iswdigit( ((const WCHAR *)m_ComputerName)[2] ) )  {
			DWORD addr = inet_addr( (char *)m_ComputerName + 2 );
			if ( addr != INADDR_NONE )  {
				struct hostent * host = gethostbyaddr( (char *)&addr, 4, AF_INET );
				if ( host )  {
					char dns[ MAX_PATH ];
					strcpy( dns, host->h_name );
					strupr( dns );
					char * ptr = strchr( dns, '.' );
					if ( ptr )  {
						*ptr = 0;
						m_ComputerName	= (_bstr_t)_T("\\\\") + dns;
						m_Domain		= ptr + 1;
					} else {
						m_ComputerName	= (_bstr_t)_T("\\\\") + dns;
						m_Domain		= _T("");
					}
				}
			}
		}

		do {
			DWORD				er = 0;
			DWORD				tr = 0;
			SHARE_INFO_0	*	Info0;

			if ( g.Abort )
				break;

			// Get list of shares
			res = NetShareEnum( m_ComputerName, 0, (LPBYTE *) &Info0, MAX_PREFERRED_LENGTH, &er, &tr, &resume );
			if ( res != ERROR_SUCCESS  &&  res != ERROR_MORE_DATA )  {

				TCHAR	msg[ 1000 ];
				DWORD	nb = FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM, NULL, res, 0, msg, sizeof msg/sizeof msg[0], NULL );
				if ( nb )  {
					while ( nb > 0  &&  isspace( msg[nb-1] ) )
						msg[--nb] = 0;
				} else {
					GetLastError();
					_stprintf( msg, _T("Error %d"), res );
				}
				AddListviewRow( m_hListview, m_ComputerName, m_Domain, msg, _T("???"), _T(""), _T(""), _T(""), _T("") );
				break;
			}
			if ( er >= 10000 )  {
				TCHAR	msg[ 100 ];
				_stprintf( msg, _T("NetShareEnum returned %d shares (bogus)"), er );
				AddListviewRow( m_hListview, m_ComputerName, m_Domain, msg, _T("???"), _T(""), _T(""), _T(""), _T("") );
				break;
			}

			// Loop through the shares;
			for ( DWORD i = 0; i < er; i++ )  {

				if ( g.Abort )
					break;

				if ( _tcscmp( Info0[i].shi0_netname, _T("ADMIN$") ) == 0 )
					continue;
				if ( _tcscmp( Info0[i].shi0_netname, _T("IPC$") ) == 0 )
					continue;

				// Compute share name
				TCHAR shareName[ MAX_PATH ];
				_stprintf( shareName, _T("%s\\%s"), (const TCHAR *)m_ComputerName, Info0[i].shi0_netname );

				// Get detailed info about the share
				SHARE_INFO_502 * Info502 = NULL;
				res = NetShareGetInfo( m_ComputerName, Info0[i].shi0_netname, 502, (BYTE **)&Info502 );
				if ( res != 0 )  {
					TCHAR	msg[ 1000 ];
					DWORD	nb = FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM, NULL, res, 0, msg, sizeof msg/sizeof msg[0], NULL );
					if ( nb )  {
						while ( nb > 0  &&  isspace( msg[nb-1] ) )
							msg[--nb] = 0;
					} else {
						GetLastError();
						_stprintf( msg, _T("Error %d"), res );
					}
					AddListviewRow( m_hListview, shareName, m_Domain, _T(""), msg, _T(""), _T(""), _T(""), _T("") );
//					AddListviewRow( m_hListview, shareName, m_Domain, Info502->shi502_path, type, EveryoneRead, OtherRead, OtherWrite, deny );
					continue;
				}

				// Get type of resource
				const TCHAR * type = _T("Unknown");
				if ( Info502->shi502_type == STYPE_DISKTREE )  {
					// Check if physical media is available
					if ( GetFileAttributes( shareName ) == 0xFFFFFFFF  &&  GetLastError() == ERROR_FILE_NOT_FOUND )
						type = _T("Disk (not found)");
					else
						type = _T("Disk");
				} else if ( Info502->shi502_type == STYPE_PRINTQ )  {
					type = _T("Printer");
	// 					continue;
				} else {
					continue;
				}

#define MISSING_DACL		((PACL) 0xFFFFFFFF)

				BOOL	present, defaulted;
				PACL	pacl = MISSING_DACL;

				// Get DACL that NetShareEnum sees
				if ( Info502->shi502_security_descriptor == NULL )  {
					// if no security descriptor then everyone has access
					pacl = NULL;
				} else if ( IsValidSecurityDescriptor( Info502->shi502_security_descriptor ) )  {
					// get the dacl
					if ( GetSecurityDescriptorDacl( Info502->shi502_security_descriptor, &present, &pacl, &defaulted ) )
						if ( !present )
							pacl = MISSING_DACL;
				} else {
					// can't say anything about security (missing)
				}

				// Get DACL that GetFileSecurity sees
				PSECURITY_DESCRIPTOR	sd2 = NULL;
				DWORD					nb = 0;

				// Get permissions
				TCHAR		EveryoneRead[ MAX_TEXT ];
				TCHAR		EveryoneWrite[ MAX_TEXT ];
				TCHAR		OtherRead[ MAX_TEXT ];
				TCHAR		OtherWrite[ MAX_TEXT ];
				TCHAR		deny[ MAX_TEXT ];

				EveryoneRead[0]		= 0;
				EveryoneWrite[0]	= 0;
				OtherRead[0]		= 0;
				OtherWrite[0]		= 0;
				deny[0]				= 0;


				// Analyze DACL
				if ( pacl == MISSING_DACL )  {
					_tcscpy( EveryoneRead, _T("???") );
				} else if ( pacl == NULL )  {
					// null acl implies all access
					_tcscpy( EveryoneRead, _T("Read/Write") );
				} else {
					// convert acl to text
					ACLtext( pacl, m_ComputerName, EveryoneRead, EveryoneWrite, OtherRead, OtherWrite, deny );

					// Combine EveryoneRead and EveryoneWrite into EveryoneReadWrite
					if ( EveryoneRead[0] == 'E'  && EveryoneWrite[0] == 'E' )
						_tcscpy( EveryoneRead, _T("Read/Write") );
					else if ( EveryoneRead[0] == 'E' )
						_tcscpy( EveryoneRead, _T("Read") );
					else if ( EveryoneWrite[0] == 'E' )
						_tcscpy( EveryoneRead, _T("Write") );

					// Trim trailing commas in lists
					TCHAR * end;
					for ( end = _tcschr( OtherRead, 0 ) - 1; end >= OtherRead && (*end == ' '  ||  *end == ','); --end )
						*end = 0;
					for ( end = _tcschr( OtherWrite, 0 ) - 1; end >= OtherWrite && (*end == ' '  ||  *end == ','); --end )
						*end = 0;
				}

				free( sd2 );

				// Add to list view
				AddListviewRow( m_hListview, shareName, m_Domain, Info502->shi502_path, type, EveryoneRead, OtherRead, OtherWrite, deny );

				NetApiBufferFree( Info502 );
			}

			NetApiBufferFree( Info0 );

		} while ( res == ERROR_MORE_DATA );

		Finish();
	} // Work
}; // EnumerateShares



class CEnumerateIPAddresses : public CWorker {
	HWND	m_hListview;
	DWORD	m_first;
	DWORD	m_last;

public:
	CEnumerateIPAddresses( HWND hListview, DWORD first, DWORD last ) :
						m_hListview( hListview ), m_first( first ), m_last( last )
	{
	}

	~CEnumerateIPAddresses()
	{
	}

private:
	void Work()
	{
		// Enumerate ip addresses in a block
		for ( DWORD addr = m_first; addr <= m_last; ++addr ) {
			if ( g.Abort )
				break;

			TCHAR	machine[ 20 ];
			_stprintf( machine, _T("\\\\%d.%d.%d.%d"), 
						FIRST_IPADDRESS(addr),
						SECOND_IPADDRESS(addr),
						THIRD_IPADDRESS(addr),
						FOURTH_IPADDRESS(addr) );

			// determine if a machine is at this address
			if ( PingAddress( machine+2, 3 ) )  {
				
				// Found a computer, so enumerate its shares
				CEnumerateShares * e = new CEnumerateShares( m_hListview, machine, _T("") );
				e->Start();
			}
		}

		Finish();
	}
};



class CEnumerateComputers : public CWorker {
	HWND			m_hListview;
	const bstr_t	m_Domain;

public:
	CEnumerateComputers( HWND hListview, const TCHAR * domain ) :
						m_hListview( hListview ), m_Domain( domain )
	{
	}

	~CEnumerateComputers()
	{
	}

private:
	void Work()
	{
		// Enumerate servers/containers in domain
		NETRESOURCE	resource	= { 0 };
		resource.dwScope		= RESOURCE_GLOBALNET;
		resource.dwType			= RESOURCETYPE_ANY;
		resource.dwDisplayType	= RESOURCEDISPLAYTYPE_DOMAIN;
		resource.dwUsage		= RESOURCEUSAGE_CONTAINER;
		resource.lpLocalName	= NULL;
		resource.lpRemoteName	= m_Domain;

		HANDLE		hEnum = NULL;
		WNetOpenEnum( RESOURCE_GLOBALNET, RESOURCETYPE_DISK, RESOURCEUSAGE_CONTAINER, &resource, &hEnum );

		// Iterate, reading blocks of resources
		for (;;) {
			if ( g.Abort )
				break;

			NETRESOURCE			NetResourceList[ 512 ];
			DWORD				NetResourceMax	= -1;
			DWORD				nb				= sizeof NetResourceList;
			DWORD				dwResult		= WNetEnumResource( hEnum, &NetResourceMax, NetResourceList, &nb );

			if ( dwResult == ERROR_NO_MORE_ITEMS )
				// Done!
				break;

			if ( dwResult != ERROR_MORE_DATA  &&  dwResult != NO_ERROR ) 
				// Error!
				break;

			// Read items
			for ( DWORD i = 0; i < NetResourceMax; i++ )  {
				NETRESOURCE	*	rp = NetResourceList + i;

				if ( g.Abort )
					break;

				if ( rp->dwDisplayType == RESOURCEDISPLAYTYPE_SERVER )  {
					// Found a computer, so enumerate its shares
					CEnumerateShares * e = new CEnumerateShares( m_hListview, rp->lpRemoteName, m_Domain );
					e->Start();
				}
			}
		}

		WNetCloseEnum( hEnum );
		Finish();
	}
};





bool EnumerateDomains( HWND hCombo, NETRESOURCE * resource )
{
	// Enumerate servers/containers in container
	HANDLE		hEnum = NULL;
	WNetOpenEnum( RESOURCE_GLOBALNET, RESOURCETYPE_DISK, RESOURCEUSAGE_CONTAINER, resource, &hEnum );

	// Iterate, reading blocks of resources
	for (;;) {
		NETRESOURCE		NetResourceList[ 100 ];
		DWORD			NetResourceMax			= -1;
		DWORD			nb			= sizeof NetResourceList;
		DWORD			dwResult	= WNetEnumResource( hEnum, &NetResourceMax, NetResourceList, &nb );

		if ( dwResult == ERROR_NO_MORE_ITEMS ) {
			// Done!
			break;
		} else if ( dwResult != NO_ERROR )  {
			// Error!
			break;
		} else {
			// Read items
			for ( DWORD i = 0; i < NetResourceMax; i++ )  {
				NETRESOURCE	*	rp = NetResourceList + i;

				if ( g.Abort )
					break;

				if ( rp->dwDisplayType == RESOURCEDISPLAYTYPE_DOMAIN )  {
					// add to list
					SendMessage( hCombo, CB_ADDSTRING, 0, (LPARAM) rp->lpRemoteName );
					// don't recurse below domain level
					continue;
				}

				if ( (rp->dwUsage & RESOURCEUSAGE_CONTAINER) == RESOURCEUSAGE_CONTAINER )  {
					// Recurse
					EnumerateDomains( hCombo, rp );
				}
			}
		}
	}
	WNetCloseEnum( hEnum );
	return true;
}





bool ExportFile( HWND hListView, const TCHAR * path )
{
	HANDLE	hFile = CreateFile( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
		return false;

	LV_ITEM		lvi;
	DWORD		nb;

	// Mark file as unicode
#if UNICODE
	WORD uni = 0xFEFF;
	WriteFile( hFile, &uni, sizeof uni, &nb, NULL );
#endif

	// print headers
	for ( lvi.iSubItem = 0; lvi.iSubItem < sizeof(Columns)/sizeof(Columns[0])-1; ++lvi.iSubItem )  {
		WriteFile( hFile, _T("\""), sizeof(TCHAR), &nb, NULL );
		WriteFile( hFile, Columns[lvi.iSubItem].Title, _tcslen(Columns[lvi.iSubItem].Title)*sizeof(TCHAR), &nb, NULL );
		WriteFile( hFile, _T("\"\t"), 2*sizeof(TCHAR), &nb, NULL );
	}
	WriteFile( hFile, _T("\r\n"), 2*sizeof(TCHAR), &nb, NULL );

	// Get text
	int	max = ListView_GetItemCount( hListView );

	for ( lvi.iItem = 0; lvi.iItem < max; ++lvi.iItem )  {

		// Get item
		lvi.mask = TVIF_TEXT;

		for ( lvi.iSubItem = 0; lvi.iSubItem < sizeof(Columns)/sizeof(Columns[0])-1; ++lvi.iSubItem )  {

			TCHAR Item[ MAX_TEXT ];
			lvi.pszText		= Item;
			lvi.cchTextMax	= MAX_TEXT;

			if ( ! ListView_GetItem( hListView, &lvi ) )
				*Item = 0;

			for ( int i = 0; Item[i]; ++i )
				if ( Item[i] == ',' && Item[i+1] == ' ' )  {
					Item[i] = ' ';
					Item[i+1] = 0x0A;
				}

			WriteFile( hFile, _T("\""), sizeof(TCHAR), &nb, NULL );
			WriteFile( hFile, Item, _tcslen(Item)*sizeof(TCHAR), &nb, NULL );
			WriteFile( hFile, _T("\"\t"), 2*sizeof(TCHAR), &nb, NULL );
		}

		WriteFile( hFile, _T("\r\n"), 2*sizeof(TCHAR), &nb, NULL );
	}

	CloseHandle( hFile );

	return true;
}



INT_PTR CALLBACK IPRangeDialog( HWND hDlg, UINT message, UINT wParam, LONG lParam ) 
{
	static DWORD * range;
	switch ( message )  {
		case WM_INITDIALOG:
			range = (DWORD *)lParam;
			SendMessage( GetDlgItem(hDlg,IDC_FIRST), IPM_SETADDRESS, 0, range[0] );
			SendMessage( GetDlgItem(hDlg,IDC_LAST),  IPM_SETADDRESS, 0, range[1] );
			return TRUE;

		case WM_COMMAND:
			switch ( LOWORD(wParam) )  {
				case IDOK:
					SendMessage( GetDlgItem(hDlg,IDC_FIRST), IPM_GETADDRESS, 0, (LPARAM)&range[0] );
					SendMessage( GetDlgItem(hDlg,IDC_LAST),  IPM_GETADDRESS, 0, (LPARAM)&range[1] );
					if ( range[1] < range[0] )  {
						MessageBox( hDlg, APPNAME, _T("Invalid address range"), MB_OK|MB_ICONWARNING );
						break;
					}
					EndDialog( hDlg, 1 );
					break;
				case IDCANCEL:
					EndDialog( hDlg, 0 );
					break;
			}
			break;

		default:
			break;
	}
    return FALSE;

}


INT_PTR CALLBACK AbortDialog( HWND hDlg, UINT message, UINT wParam, LONG lParam ) 
{
	RECT	parentRc, childRc;

	switch ( message )  {
		case WM_INITDIALOG:
			GetWindowRect( GetParent(hDlg), &parentRc );
			GetWindowRect( hDlg, &childRc );
			parentRc.left = ((parentRc.left + parentRc.right) - (childRc.right - childRc.left)) / 2;
			parentRc.top  = ((parentRc.top + parentRc.bottom) - (childRc.bottom - childRc.top)) / 2;
			MoveWindow( hDlg, parentRc.left, parentRc.top, childRc.right - childRc.left, childRc.bottom - childRc.top, TRUE );
			return TRUE;

		case WM_CLOSE:
			EndDialog( hDlg, 0 );
			return TRUE;

		default:
			break;
	}
    return FALSE;

}


INT_PTR CALLBACK AboutDialog( HWND hDlg, UINT message, UINT wParam, LONG lParam ) 
{
	RECT	parentRc, childRc;
	static HWND		hLink;
	static BOOL		underline_link;
	static HFONT	hFontNormal = NULL;
	static HFONT	hFontUnderline = NULL;
	static HCURSOR	hHandCursor = NULL;
	static HCURSOR	hRegularCursor;
	LOGFONT			logfont;

	switch ( message )  {
	case WM_INITDIALOG:
		GetWindowRect( GetParent(hDlg), &parentRc );
		GetWindowRect( hDlg, &childRc );
		parentRc.left += 70;
		parentRc.top  += 60;
		MoveWindow( hDlg, parentRc.left, parentRc.top, childRc.right - childRc.left, childRc.bottom - childRc.top, TRUE );

		underline_link = TRUE;
		hLink = GetDlgItem( hDlg, IDC_LINK );

		// get link fonts
		hFontNormal = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
		GetObject( hFontNormal, sizeof logfont, &logfont); 
		logfont.lfUnderline = TRUE;
		hFontUnderline = CreateFontIndirect( &logfont );

		// get hand
		hHandCursor = LoadCursor( g.hInst, TEXT("HAND") );
		hRegularCursor = LoadCursor( NULL, IDC_ARROW );
		return TRUE;

	case WM_CTLCOLORSTATIC:
		if ( (HWND)lParam == hLink )  {
			HDC	hdc = (HDC)wParam;
			SetBkMode( hdc, TRANSPARENT );
			if ( GetSysColorBrush(26/*COLOR_HOTLIGHT*/) )
				SetTextColor( hdc, GetSysColor(26/*COLOR_HOTLIGHT*/) );
			else
				SetTextColor( hdc, RGB(0,0,255) );
			SelectObject( hdc, underline_link ? hFontUnderline : hFontNormal );
			return (LONG)GetSysColorBrush( COLOR_BTNFACE );
		}
		break;

	case WM_MOUSEMOVE: {
		POINT	pt = { LOWORD(lParam), HIWORD(lParam) };
		HWND	hChild = ChildWindowFromPoint( hDlg, pt );
		if ( underline_link == (hChild == hLink) )  {
			underline_link = !underline_link;
			InvalidateRect( hLink, NULL, FALSE );
		}
		if ( underline_link )
			SetCursor( hRegularCursor );
		else
			SetCursor( hHandCursor );
		break;
	}

	case WM_LBUTTONDOWN: {
		POINT		pt = { LOWORD(lParam), HIWORD(lParam) };
		HWND		hChild = ChildWindowFromPoint( hDlg, pt );
		if ( hChild == hLink )  {
			ShellExecute( hDlg, TEXT("open"), TEXT("http://www.sysinternals.com"), NULL, NULL, SW_SHOWNORMAL );
		} 
		break;
	}

	case WM_COMMAND:
		switch ( wParam ) {
		case IDOK:
		case IDCANCEL:
			EndDialog( hDlg, 0 );
			return TRUE;
		}
		break; 

	case WM_CLOSE:
		EndDialog( hDlg, 0 );
		return TRUE;

	default:
		break;
	}
    return FALSE;

}



//----------------------------------------------------------------------
//  
// MainDialog
//
// Main interface for editing output
//
//----------------------------------------------------------------------
LRESULT CALLBACK MainDialog( HWND hDlg, UINT message, UINT wParam, LONG lParam ) 
{
	static CResizer		resizer;
	int					idx;
	OPENFILENAME		open;
	static TCHAR		exportPath[ MAX_PATH ];
	TCHAR			*	p;
	bool				ok;
	DWORD				style;
	static bool			Starting = true;
	static HCURSOR		hCursor;
	static DWORD		Ticks;

	switch ( message ) {

		case WM_INITDIALOG:
			// set anchor points
			resizer.OnInitDialog( hDlg );
			resizer.SetHorz( GetDlgItem(hDlg,IDC_REFRESH),		ANCHOR_LEFT );
			resizer.SetHorz( GetDlgItem(hDlg,IDC_EXPORT),		ANCHOR_LEFT );
			resizer.SetVert( GetDlgItem(hDlg,IDC_LIST),			ANCHOR_BOTH );
			resizer.SetHorz( GetDlgItem(hDlg,IDC_DOMAIN),		ANCHOR_LEFT );
			resizer.SetHorz( GetDlgItem(hDlg,IDC_DESCRIPTION),	ANCHOR_LEFT );
			resizer.SetHorz( GetDlgItem(hDlg,IDC_STATUS),		ANCHOR_BOTH );
	
			// clear status text
			SetWindowText( GetDlgItem(hDlg,IDC_STATUS),	_T("") );
	
			// Set cursor for window
			hCursor = LoadCursor( NULL, IDC_ARROW );
			SetClassLong( hDlg, GCL_HCURSOR, (LONG)hCursor );
			SetCursor( hCursor );

			// Create listview columns
			InitListViewColumns( GetDlgItem(hDlg,IDC_LIST), Columns, sizeof Columns/sizeof Columns[0] - 1 );

			style = GetWindowLong( GetDlgItem(hDlg,IDC_LIST), GWL_STYLE );
			style |= LVS_SHOWSELALWAYS;
			SetWindowLong( GetDlgItem(hDlg,IDC_LIST), GWL_STYLE, style );
			ListView_SetExtendedListViewStyleEx( GetDlgItem(hDlg,IDC_LIST), LVS_EX_LABELTIP, LVS_EX_LABELTIP );

			// Get a list of domains on network
			SendMessage( GetDlgItem( hDlg, IDC_DOMAIN ), CB_RESETCONTENT, 0, 0 );
			EnumerateDomains( GetDlgItem( hDlg, IDC_DOMAIN ), NULL );
			if ( SendMessage( GetDlgItem( hDlg, IDC_DOMAIN ), CB_GETCOUNT, 0, 0 ) >= 1 )  {
				SendMessage( GetDlgItem( hDlg, IDC_DOMAIN ), CB_ADDSTRING, 0, (LPARAM)ALL_DOMAINS );
			} else {
				MessageBox( hDlg, _T("No domains or workgroups were found on your network"), APPNAME, MB_OK|MB_ICONSTOP );
				EnableWindow( GetDlgItem( hDlg, IDC_REFRESH ), false );
			}
			SendMessage( GetDlgItem( hDlg, IDC_DOMAIN ), CB_ADDSTRING, 0, (LPARAM)IP_RANGE );
			SendMessage( GetDlgItem( hDlg, IDC_DOMAIN ), CB_SETCURSEL, 0, 0 );

			return TRUE;

		
		case WM_SETCURSOR:
			switch ( LOWORD(lParam) )  {
				case HTTOP:
				case HTBOTTOM:
				case HTLEFT:
				case HTRIGHT:
				case HTTOPLEFT:
				case HTTOPRIGHT:
				case HTBOTTOMLEFT:
				case HTBOTTOMRIGHT:
					break;
				default:
					SetCursor( hCursor ); 
					return true;
			}
			break;

		case WM_APP_ENUM_COMPLETE:
			Ticks = GetTickCount() - Ticks;

			EnableWindow( GetDlgItem( hDlg, IDC_REFRESH ), true );
			EnableWindow( GetDlgItem( hDlg, IDCANCEL    ), false );
			EnableWindow( GetDlgItem( hDlg, IDC_EXPORT ), true );
			hCursor = LoadCursor( NULL, IDC_ARROW );
			SetClassLong( hDlg, GCL_HCURSOR, (LONG)hCursor );
			SetCursor( hCursor );
			if ( g.Abort )  {
				SendMessage( g.Abort, WM_CLOSE, 0, 0 ); 
//				MessageBox( hDlg, _T("Scan cancelled by user"), APPNAME, MB_OK|MB_ICONINFORMATION );
			}
			break;

		case WM_NOTIFY:
			if ( LOWORD(wParam) == IDC_LIST )  {
				NMLISTVIEW	* nm = (NMLISTVIEW *) lParam;
				switch( nm->hdr.code )  {
					case LVN_COLUMNCLICK:
						// sort column
						SortListView( nm->hdr.hwndFrom, nm->iSubItem, Columns );
						break;
					case NM_DBLCLK:
						SendMessage( hDlg, WM_COMMAND, IDC_EXPLORE, 0 );
						break;
					case NM_RCLICK:
						// Create pop-up menu
						idx = ListView_GetSelectionMark( GetDlgItem(hDlg,IDC_LIST) );
						if ( idx >= 0 )  {
							POINT pt;
							GetCursorPos( &pt );

							HMENU hMenu = GetSubMenu( LoadMenu( g.hInst, _T("POPUPMENU") ), 0 );
							SetMenuDefaultItem( hMenu, IDC_PROPERTIES, FALSE );
							TrackPopupMenu( hMenu, 0, pt.x, pt.y, 0, hDlg, NULL );
						}
						break;
				}
			}
			break;

		case WM_COMMAND:
			// Normal notifications
			switch ( LOWORD(wParam) ) {

				case IDOK:
					// quit
					SendMessage( hDlg, WM_CLOSE, 0, 0 );
					break;

				case IDCANCEL:
					// is an enumeration in progress?
					if ( ! IsWindowEnabled( GetDlgItem( hDlg, IDC_REFRESH ) ) )  {
						// have we already cancelled it?
						if ( g.Abort == NULL )  {
							g.Abort = CreateDialog( g.hInst, _T("ABORT"), hDlg, AbortDialog );
						}
					}
					break;

				case IDC_EXPLORE:
				case IDC_PROPERTIES:
					// clicked item, so display device properties
					idx = ListView_GetSelectionMark( GetDlgItem(hDlg,IDC_LIST) );
					if ( idx >= 0 )  {
						TCHAR	path[ MAX_PATH ];
						ListView_GetItemText( GetDlgItem(hDlg,IDC_LIST), idx, 0, path, MAX_PATH );
						if ( LOWORD(wParam) == IDC_EXPLORE )  {
							// explore item
							ShellExecute( NULL, _T("explore"), path, NULL, NULL, SW_SHOWNORMAL );
						} else {
							// show item properties
							if ( _tcschr( path+2, '\\' ) == NULL )
								break;
							Properties( g.hInst, hDlg, path );
						}
					}
					break;

				case IDC_ABOUT:
					DialogBox( g.hInst, _T("ABOUT"), hDlg, AboutDialog );
					break;
				
				case IDC_REFRESH:
					if ( IsWindowEnabled( GetDlgItem( hDlg, IDC_REFRESH ) ) )  {
						// Create a list of all available computers
						EnableWindow( GetDlgItem( hDlg, IDC_REFRESH ), false );
						EnableWindow( GetDlgItem( hDlg, IDCANCEL    ), true );
						EnableWindow( GetDlgItem( hDlg, IDC_EXPORT ), false );
						hCursor = LoadCursor( NULL, IDC_APPSTARTING );
						SetClassLong( hDlg, GCL_HCURSOR, (LONG)hCursor );
						SetCursor( hCursor );

						TCHAR domain[ MAX_PATH ];
						GetDlgItemText( hDlg, IDC_DOMAIN, domain, MAX_PATH );

						delete g.ShareList;
						g.ShareList = NULL;
						g.Abort = NULL;
						ListView_DeleteAllItems( GetDlgItem(hDlg,IDC_LIST) );

						if ( _tcsicmp( domain, ALL_DOMAINS ) == 0 )  {
							// enumerate all domains
							bool warn = false;
							InterlockedIncrement( &g.ThreadCount );
							for ( int i = 1; SendMessage( GetDlgItem(hDlg,IDC_DOMAIN), CB_GETLBTEXT, i, (LPARAM)domain ) != CB_ERR; ++i )  {
								CEnumerateComputers * e = new CEnumerateComputers( GetDlgItem(hDlg,IDC_LIST), domain );
								e->Start();
								if ( ! IsDomainAdmin( domain ) )
									warn = true;
							}
							if ( warn )  {
								MessageBox( hDlg, _T("Information may be incomplete because you are not a domain administrator for one or more of the selected domain."), APPNAME, MB_OK|MB_ICONWARNING );
							}
							if ( InterlockedDecrement( &g.ThreadCount ) == 0 )  {
								PostMessage( g.hMainDlg, WM_APP_ENUM_COMPLETE, 0, 0 );
							}
						} else if ( _tcsicmp( domain, IP_RANGE ) == 0 )  {
							// Enumerate IP range
							static DWORD range[2] = { 0 };
							if ( range[0] == 0  &&  range[1] == 0 )  {
								char name[ MAX_PATH ] = "";
								gethostname( name, MAX_PATH );
								struct hostent * host = gethostbyname( name );
								if ( host  &&  host->h_addr_list[0] )  {
									range[0] = ntohl( *(DWORD *)host->h_addr_list[0] );
									range[1] = ntohl( *(DWORD *)host->h_addr_list[0] );
								}
							}
							if ( ! DialogBoxParam( g.hInst, _T("IPRANGE"), hDlg, IPRangeDialog, (LPARAM)range ) )  {
								PostMessage( g.hMainDlg, WM_APP_ENUM_COMPLETE, 0, 0 );
								break;
							}
							CEnumerateIPAddresses * e = new CEnumerateIPAddresses( GetDlgItem(hDlg,IDC_LIST), range[0], range[1] );
							e->Start();
							if ( ! IsDomainAdmin( domain ) )  {
								MessageBox( hDlg, _T("Information may be incomplete because you are not a domain administrator for the selected domain."), APPNAME, MB_OK|MB_ICONWARNING );
							}
						} else {
							// enumerate selected domain
							CEnumerateComputers * e = new CEnumerateComputers( GetDlgItem(hDlg,IDC_LIST), domain );
							e->Start();
							if ( ! IsDomainAdmin( domain ) )  {
								MessageBox( hDlg, _T("Information may be incomplete because you are not a domain administrator for the selected domain."), APPNAME, MB_OK|MB_ICONWARNING );
							}
						}
					}
					break;

				case IDC_EXPORT:
					// get export file name
					memset( &open, 0, sizeof open );
					open.lStructSize = sizeof open;
					open.hwndOwner = hDlg;
					open.lpstrFilter = TEXT("Unicode Text (*.txt)\0*.txt\0");
					open.lpstrFile = exportPath;
					open.nMaxFile = sizeof exportPath / sizeof(TCHAR);
					open.Flags = OFN_HIDEREADONLY;
					open.lpstrTitle = TEXT("Save as");
					if ( ! GetSaveFileName( &open ) )
						break;
					p = _tcschr( exportPath, 0 );
					p -= 4;
					if ( p < exportPath  ||  _tcsicmp( p, TEXT(".txt") ) != 0 )
						_tcscat( exportPath, TEXT(".txt") );
					DeleteFile( exportPath );
					ok = ExportFile( GetDlgItem(hDlg,IDC_LIST), exportPath );
					if ( ! ok )
						MessageBox( hDlg, TEXT("Error exporting settings"), APPNAME, MB_OK|MB_ICONSTOP );
					break;

				case IDC_COMPARE:
					CreateDialogParam( g.hInst, _T("COMPARE"), hDlg, CompareDialog, (LPARAM)g.ShareList );
					break;
			}
			break;

		case WM_GETMINMAXINFO:
			resizer.OnGetMinMaxInfo( wParam, lParam );
			return 0;

		case WM_SIZE:
			resizer.OnSize( wParam, lParam );
			UpdateWindow( hDlg );
			return 0;

		case WM_NCHITTEST:
			return resizer.OnNcHitTest( wParam, lParam );

		case WM_PAINT:
			resizer.OnPaint( wParam, lParam );
			break;

		case WM_CLOSE:
			SendMessage( hDlg, WM_COMMAND, IDCANCEL, 0 );
			SaveListViewColumns( GetDlgItem(hDlg,IDC_LIST));
			PostQuitMessage( 0 );
			break;
	}

	return DefWindowProc( hDlg, message, wParam, lParam );
}


//----------------------------------------------------------------------
//  
// WinMain
//
// Initialize and start application
//
//----------------------------------------------------------------------
int CALLBACK WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
						LPSTR lpCmdLine, int nCmdShow )
{
	int		 argc;
	WCHAR ** argv = CommandLineToArgvW( GetCommandLine(), &argc );

	WSADATA		wsd;
	WSAStartup( MAKEWORD(2,1), &wsd );

	// initialize things
	InitCommonControls();

	g.hInst			= hInstance;
	g.MaxThreads	= 200;

	if ( argc > 1 )
		g.MaxThreads = _wtoi( argv[1] );

#if 0
	Properties( g.hInst, NULL, _T("\\\\BHC-DELL620\\Recovery Manager") );
#endif


	// register window class
	WNDCLASSEX	wndclass = { 0 };
	wndclass.cbSize			= sizeof wndclass;
	wndclass.style          = CS_HREDRAW | CS_VREDRAW;
 	wndclass.lpfnWndProc    = MainDialog;
	wndclass.hInstance      = hInstance;
	wndclass.cbWndExtra		= DLGWINDOWEXTRA;
	wndclass.hIcon          = LoadIcon( hInstance, TEXT("APPICON") );
	wndclass.hIconSm		= LoadIcon( hInstance, TEXT("APPICON") );
	wndclass.hCursor        = LoadCursor( NULL, IDC_ARROW );
	wndclass.hbrBackground  = (HBRUSH) (COLOR_BTNFACE+1);
	wndclass.lpszClassName  = TEXT("ShareEnumClass");
	if ( RegisterClassEx( &wndclass ) == 0 )
		GetLastError();

	g.hMainDlg = CreateDialog( hInstance, TEXT("MAIN"), NULL, (DLGPROC)MainDialog );
	if ( g.hMainDlg == NULL )  {
		GetLastError();
		return 1;
	}

	DWORD LocalComputerNameLen = MAX_PATH;
	GetComputerName( g.LocalComputerName, &LocalComputerNameLen );

	// make top window
	//SetWindowPos( hMainDlg, HWND_TOP, 1, 1, 0, 0, SWP_NOSIZE );

	// Make the window visible 
	ShowWindow( g.hMainDlg, nCmdShow );
	// Paint window
	UpdateWindow( g.hMainDlg ); 

	// Keyboard accelerators
	HACCEL hAccel = LoadAccelerators( hInstance, TEXT("ACCELERATORS") );

	// Message loop
	MSG msg;
	while ( GetMessage( &msg, NULL, 0, 0 )) {

		if ( TranslateAccelerator( g.hMainDlg, hAccel, &msg ) )
			continue;
		if ( IsDialogMessage( g.hMainDlg, &msg ) )
			continue;

		// process message
		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}

	return 0;
}
