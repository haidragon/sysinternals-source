//----------------------------------------------------------------------------
//
// AdRestore
// by Mark Russinovich
// 2003 Sysinternals - www.sysinternals.com
//
// Enumerates and optionally restores deleted AD objects. Please send
// feature requests if you find this utility useful.
//
//----------------------------------------------------------------------------
#define INC_OLE2
#define UNICODE 1
#define _WIN32_DCOM

#include <windows.h>
#include <winuser.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <winldap.h>
#include <ntldap.h>
#include <ntdsapi.h>
#include <activeds.h>
#include <assert.h>

#define ARRAYSIZE(_x)		(sizeof(_x)/sizeof(_x[0]))


//----------------------------------------------------------------------------
//
//  RestoreDeletedObject()
//
//  Restores a deleted object. 
//
//  pwszDeletedDN - Contains the fully-qualified distinguished name of the 
//  deleted object.
//
//  pwszDestContainerDN - Contains the fully-qualified distinguished name of 
//  the folder that the delted object should be moved to.
//
//  Returns S_OK if successful or an HRESULT or LDAP error code otherwise.
//
//----------------------------------------------------------------------------
HRESULT RestoreDeletedObject(LPCWSTR pwszDeletedDN, LPCWSTR pwszDestContainerDN)
{
    if((NULL == pwszDeletedDN) || (NULL == pwszDestContainerDN))
    {
        return E_POINTER;
    }
    
    HRESULT hr = E_FAIL;

    // Build the new distinguished name.
    LPWSTR pwszNewDN = new WCHAR[lstrlenW(pwszDeletedDN) + lstrlenW(pwszDestContainerDN) + 1];
    if(pwszNewDN)
    {
        lstrcpyW(pwszNewDN, pwszDeletedDN);

        // Search for the first 0x0A character. This is the delimiter in the deleted object name.
        LPWSTR pwszChar;
        for(pwszChar = pwszNewDN; *pwszChar; pwszChar = CharNextW(pwszChar))
        {
            if(('\\' == *pwszChar) && ('0' == *(pwszChar + 1)) && ('A' == *(pwszChar + 2)))
            {
                break;
            }
            
        }

        if(0 != *pwszChar)
        {
            // Truncate the name string at the delimiter.
            *pwszChar = 0;

            // Add the last known parent DN to complete the DN.
            lstrcatW(pwszNewDN, L",");
            lstrcatW(pwszNewDN, pwszDestContainerDN);

            PLDAP	ld;

            // Initialize LDAP.
            ld = ldap_init(NULL, LDAP_PORT);
            if(NULL != ld) 
            {
                ULONG ulRC;
                ULONG version = LDAP_VERSION3;

                // Set the LDAP version.
                ulRC = ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, (void*)&version);
                if(LDAP_SUCCESS == ulRC)
                {
                    // Establish a connection with the server.
                    ulRC = ldap_connect(ld, NULL);
                    if(LDAP_SUCCESS == ulRC)
                    {                    
                        // Bind to the LDAP server.
                        ulRC = ldap_bind_s(ld, NULL, NULL, LDAP_AUTH_NEGOTIATE);
                        if(LDAP_SUCCESS == ulRC)
                        {
                            // Setup the new values.
                            LPWSTR rgNewVals[] = {pwszNewDN, NULL};

                            /*
                            Remove the isDeleted attribute. This cannot be set 
                            to FALSE or the restore operation will not work.
                            */
                            LDAPModW modIsDeleted = { LDAP_MOD_DELETE, L"isDeleted", NULL };

                            /*
                            Set the new DN, in effect, moving the deleted 
                            object to where it resided before the deletion.
                            */
                            LDAPModW modDN = { LDAP_MOD_REPLACE, L"distinguishedName", rgNewVals };
                            
                            // Initialize the LDAPMod structure.
                            PLDAPModW ldapMods[] = 
                            {
                                &modIsDeleted,
                                &modDN,
                                NULL
                            };

                            /*
                            Use the LDAP_SERVER_SHOW_DELETED_OID control to 
                            modify deleted objects.
                            */
                            LDAPControlW showDeletedControl;
                            showDeletedControl.ldctl_oid = LDAP_SERVER_SHOW_DELETED_OID_W;
                            showDeletedControl.ldctl_value.bv_len = 0;
                            showDeletedControl.ldctl_value.bv_val = NULL;
                            showDeletedControl.ldctl_iscritical = TRUE;

                            // Initialzie the LDAPControl structure
                            PLDAPControlW ldapControls[] = { &showDeletedControl, NULL };

                            /*
                            Modify the specified attributes. This must performed 
                            in one step, which is why the LDAP APIs must be used 
                            to restore a deleted object.
                            */
                            ulRC = ldap_modify_ext_sW(ld, (PWCHAR)pwszDeletedDN, ldapMods, ldapControls, NULL);
                            if(LDAP_SUCCESS == ulRC)
                            {
                                hr = S_OK;
                            }
                            else if(LDAP_ALREADY_EXISTS == ulRC)
                            {
                                /*
                                An object already exists with the specified name 
                                in the specified target container. At this point, 
                                a new name must be selected.
                                */
                            }
                        }
                    }
                }

                if(LDAP_SUCCESS != ulRC)
                {
                    hr = ulRC;
                    
                    OutputDebugString(ldap_err2string(ulRC));
                }

                // Release the LDAP session.
                ldap_unbind(ld);
            }
        }
        else
        {
            /*
            If the end of the string is reached before the delimiter is found, just 
            end and fail.
            */
            hr = E_INVALIDARG;
        }
    
        delete pwszNewDN;
    }
    else
    {
        hr = E_OUTOFMEMORY;
    }

    return hr;
}

//----------------------------------------------------------------------------
//
//  GetDeletedObjectsContainer()
//
//  Binds to the Deleted Object container.
//
//----------------------------------------------------------------------------
HRESULT GetDeletedObjectsContainer(IADsContainer **ppContainer)
{
    if(NULL == ppContainer)
    {
        return E_INVALIDARG;
    }

    HRESULT hr;
    IADs *pRoot;

    *ppContainer = NULL;

    // Bind to the rootDSE object.
    hr = ADsOpenObject(L"LDAP://rootDSE",
                    NULL,
                    NULL,
                    ADS_SECURE_AUTHENTICATION,
                    IID_IADs,
                    (LPVOID*)&pRoot);
    if(SUCCEEDED(hr))
    {
        VARIANT var;
        
        VariantInit(&var);

        // Get the current domain DN.
        hr = pRoot->Get(L"defaultNamingContext", &var);
        if(SUCCEEDED(hr))
        {
            // Build the binding string.
            LPWSTR pwszFormat = L"LDAP://<WKGUID=%s,%s>";
            LPWSTR pwszPath;

            pwszPath = new WCHAR[wcslen(pwszFormat) + wcslen(GUID_DELETED_OBJECTS_CONTAINER_W) + wcslen(var.bstrVal)];
            if(NULL != pwszPath)
            {
                swprintf(pwszPath, pwszFormat, GUID_DELETED_OBJECTS_CONTAINER_W, var.bstrVal);

                // Bind to the object.
                hr = ADsOpenObject(pwszPath,
                                NULL,
                                NULL,
                                ADS_FAST_BIND | ADS_SECURE_AUTHENTICATION,
                                IID_IADsContainer,
                                (LPVOID*)ppContainer);

                delete pwszPath;
            }
            else
            {
                hr = E_OUTOFMEMORY;
            }

            VariantClear(&var);        
        }

        pRoot->Release(); 
    }

    return hr;
}


//----------------------------------------------------------------------------
//
//  EnumDeletedObjects()
//
//  Enumerates all of the objects in the Deleted Objects container.
//
//----------------------------------------------------------------------------
HRESULT EnumDeletedObjects( PWCHAR SearchFilter, BOOLEAN Restore, PDWORD ItemsFound )
{
    HRESULT hr;
    IADsContainer *pDeletedObjectsCont = NULL;
    IDirectorySearch *pSearch = NULL;
    // Set the attributes to retrieve.
    LPWSTR rgAttributes[] = {L"cn", L"distinguishedName", L"lastKnownParent"};

	*ItemsFound = 0;
    hr = GetDeletedObjectsContainer(&pDeletedObjectsCont);
    if(FAILED(hr))
    {
        goto cleanup;
    }

    hr = pDeletedObjectsCont->QueryInterface(IID_IDirectorySearch, (LPVOID*)&pSearch);    
    if(FAILED(hr))
    {
        goto cleanup;
    }

    ADS_SEARCH_HANDLE hSearch;

    // Only search for direct children of the container.
    ADS_SEARCHPREF_INFO rgSearchPrefs[3];
    rgSearchPrefs[0].dwSearchPref = ADS_SEARCHPREF_SEARCH_SCOPE;
    rgSearchPrefs[0].vValue.dwType = ADSTYPE_INTEGER;
    rgSearchPrefs[0].vValue.Integer = ADS_SCOPE_ONELEVEL;

    // Search for deleted objects.
    rgSearchPrefs[1].dwSearchPref = ADS_SEARCHPREF_TOMBSTONE;
    rgSearchPrefs[1].vValue.dwType = ADSTYPE_BOOLEAN;
    rgSearchPrefs[1].vValue.Boolean = TRUE;

    // Set the page size.
    rgSearchPrefs[2].dwSearchPref = ADS_SEARCHPREF_PAGESIZE;
    rgSearchPrefs[2].vValue.dwType = ADSTYPE_INTEGER;
    rgSearchPrefs[2].vValue.Integer = 1000;

    // Set the search preference
    hr = pSearch->SetSearchPreference(rgSearchPrefs, ARRAYSIZE(rgSearchPrefs));
    if(FAILED(hr))
    {
        goto cleanup;
    }



    // Execute the search
    hr = pSearch->ExecuteSearch(    SearchFilter,
                                    rgAttributes,
                                    ARRAYSIZE(rgAttributes),
                                    &hSearch);
    if(SUCCEEDED(hr))
    {    
        // Call IDirectorySearch::GetNextRow() to retrieve the next row of data
        while(S_OK == (hr = pSearch->GetNextRow(hSearch)))
        {
            ADS_SEARCH_COLUMN col;
            UINT i;
            
            // Enumerate the retrieved attributes.
            for(i = 0; i < ARRAYSIZE(rgAttributes); i++)
            {
                hr = pSearch->GetColumn(hSearch, rgAttributes[i], &col);
                if(SUCCEEDED(hr))
                {
                    switch(col.dwADsType)
                    {
                        case ADSTYPE_CASE_IGNORE_STRING:
                        case ADSTYPE_DN_STRING:
                        case ADSTYPE_PRINTABLE_STRING:
                        case ADSTYPE_NUMERIC_STRING:
                        case ADSTYPE_OCTET_STRING:
                            wprintf(L"%s: ", rgAttributes[i]); 
                            for(DWORD x = 0; x < col.dwNumValues; x++)
                            {
                                wprintf(col.pADsValues[x].CaseIgnoreString); 
                                if((x + 1) < col.dwNumValues)
                                {
                                    wprintf(L","); 
                                }
                            }
                            wprintf(L"\n");
							
                            break;
                    }
	                pSearch->FreeColumn(&col);
                }
            }
			(*ItemsFound)++;
            wprintf(L"\n");

			if( Restore ) {

				WCHAR answer[MAX_PATH];
				wprintf(L"Do you want to restore this object (y/n)? ");
				fflush( stdout );
				_getws( answer );
				if( towupper( answer[0] ) == 'Y' ) {

					ADS_SEARCH_COLUMN colDn, colPn;

					pSearch->GetColumn(hSearch, rgAttributes[1], &colDn);
					pSearch->GetColumn(hSearch, rgAttributes[2], &colPn);

					hr = RestoreDeletedObject( colDn.pADsValues[0].CaseIgnoreString,
						colPn.pADsValues[0].CaseIgnoreString );
					if( FAILED( hr )) {

						wprintf(L"\nRestore failed: %d\n", hr );

					} else {

						wprintf(L"\nRestore succeeded.\n");
					}
					pSearch->FreeColumn(&colDn);
					pSearch->FreeColumn(&colPn);
				}

				wprintf(L"\n");
			}
		}

        // Close the search handle to clean up.
        pSearch->CloseSearchHandle(hSearch);
    }

cleanup:

    if(pDeletedObjectsCont)
    {
        pDeletedObjectsCont->Release();
    }

    if(pSearch)
    {
        pSearch->Release();
    }

    return hr;
}



int Usage()
{
	printf("Usage: AdRestore [-r] [searchfilter]\n\n");
	printf("   -r       Prompt to restore deleted objects found.\n\n");
	printf("This command enumerates all objects with the string \"comp\" in the name.\n\n");
	printf("     adrestore comp\n");
	return 1;
}


int wmain( int argc, PWCHAR argv[] )
{
	WCHAR	filter[MAX_PATH];
	DWORD	itemsFound, totalItemsFound = 0, filterIndex = -1;
	int		i;
	BOOLEAN	restore = FALSE;

    CoInitialize(0);

	wprintf(L"\nAdRestore v1.1\n");
	wprintf(L"by Mark Russinovich\n");
	wprintf(L"Sysinternals - www.sysinternals.com\n\n");

	for( i = 1; i < argc; i++ ) {

		if( argv[i][0] == L'/' ||
			argv[i][0] == L'-' ) {

			towupper( argv[i][1] );
			switch( towupper( argv[i][1] )) {
			case L'R':
				restore = TRUE;
				break;
			default:
				return Usage();
			}

		} else {

			if( filterIndex != -1 ) return Usage();
			filterIndex = i;
		}
	}
	wprintf(L"\nEnumerating domain deleted objects:\n\n");

	for( i = 0; i < 2; i++ ) {

		if( filterIndex != -1 ) {

			if( i == 0 ) 
				swprintf( filter, L"(cn=*%s*)", argv[filterIndex] );
			else
				swprintf( filter, L"(ou=*%s*)", argv[filterIndex] );

		} else {

			if( i == 0 ) 
				swprintf( filter, L"(cn=*)");
			else
				swprintf( filter, L"(ou=*)");
		}
		EnumDeletedObjects( filter, restore, &itemsFound );
		totalItemsFound += itemsFound;
	}
	wprintf(L"Found %d item%s matching search criteria.\n\n", totalItemsFound,
		totalItemsFound == 1 ? L"" : L"s" );
	return 0;
}
