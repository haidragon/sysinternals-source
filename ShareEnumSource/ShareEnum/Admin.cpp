//////////////////////////////////////////////////////////////
// Copyright (C) 2002-2003 Bryce Cogswell 
// www.sysinternals.com
// cogswell@winternals.com
//
// You may modify and use this code for personal use only.
// Redistribution in any form is expressly prohibited
// without permission by the author.
//////////////////////////////////////////////////////////////
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <lm.h>
#include <tchar.h>



PSID DomainAdminSid( const TCHAR * domain )
{
	// Get domain SID
	BYTE			DomainSid[ 1024 ]		= { 0 };
	DWORD			DomainSidLen			= sizeof DomainSid;
	TCHAR			DomainName[ MAX_PATH ]	= _T("");
	DWORD			DomainNameLen			= MAX_PATH;
	SID_NAME_USE	Use;
	if ( ! LookupAccountName( NULL, domain, DomainSid, &DomainSidLen, DomainName, &DomainNameLen, &Use ) )
		return NULL;

	// validate domain sid
	if ( Use != SidTypeDomain )
		return NULL;
	if ( !IsValidSid( DomainSid ) )
		return NULL;

	// Get size of sid
	DWORD	SubAuthorityCount	= *GetSidSubAuthorityCount( DomainSid );

	// NT 4.0 adds an odd RID to the end: get rid of it
	if ( *GetSidSubAuthority( DomainSid, SubAuthorityCount - 1 ) == 0xFFFFFFFF )
		--SubAuthorityCount;

	// allocate storage for new Sid. domain Sid + Rid
	PSID pSid = (PSID) new BYTE[ GetSidLengthRequired( (UCHAR)(SubAuthorityCount + 1)) ];

	InitializeSid( pSid, GetSidIdentifierAuthority( DomainSid ), (UCHAR)(SubAuthorityCount+1) );

	// copy existing subauthorities from account domain Sid into new Sid
	for ( DWORD i = 0; i < SubAuthorityCount; i++ )  {
		*GetSidSubAuthority( pSid, i ) = *GetSidSubAuthority( DomainSid, i );
	}

	// append Rid to new Sid
	*GetSidSubAuthority( pSid, SubAuthorityCount ) = DOMAIN_GROUP_RID_ADMINS;

	if ( !IsValidSid( pSid ) )
		return NULL;

	return pSid;
}

bool MemberOfGroup( PSID Sid )
{
	HANDLE	hToken;
	if ( ! OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &hToken ) )
		return false;

	DWORD cb = 0;
	GetTokenInformation( hToken, TokenGroups, 0, 0, &cb );
	TOKEN_GROUPS * ptg = (TOKEN_GROUPS *) new BYTE[ cb ];
	GetTokenInformation( hToken, TokenGroups, ptg, cb, &cb );
	CloseHandle( hToken );

	bool bIsAdmin = false;
	for ( DWORD i = 0; i < ptg->GroupCount; ++i )  {
		if ( !IsValidSid( ptg->Groups[i].Sid ) )
			continue;
		if ( EqualSid( ptg->Groups[i].Sid, Sid ) )  {
			bIsAdmin = true;
			break;
		}
	}

	delete []ptg;

	return bIsAdmin;
}

bool IsDomainAdmin( const TCHAR * domain = NULL )
{
	PSID	sid = DomainAdminSid( domain );
	if ( sid == NULL )
		return false;
	return MemberOfGroup( sid );
}
