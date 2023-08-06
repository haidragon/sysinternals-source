//============================================================================
//
// Process Explorer
// Copyright (C) 1999-2005 Mark Russinovich
// Sysinternals - www.sysinternals.com
//
// Registry.h
//
//============================================================================

typedef enum {
	SETTING_TYPE_DWORD,
	SETTING_TYPE_BOOLEAN,
	SETTING_TYPE_DOUBLE,
	SETTING_TYPE_WORD,
	SETTING_TYPE_STRING,
	SETTING_TYPE_DWORD_ARRAY,
	SETTING_TYPE_WORD_ARRAY,
	SETTING_TYPE_BINARY
} REG_SETTING_TYPE;

typedef struct {
	PTCHAR	Valuename;
	REG_SETTING_TYPE	Type;
    DWORD	Size; // Optional
	PVOID   Setting;
	double	DefaultSetting;
} REG_SETTING, *PREG_SETTING;


class CRegistry {

private:
	PTCHAR	m_Keyname;
	HKEY	hKey;

public:
	CRegistry( PTCHAR Keyname ) 
	{
		m_Keyname = _tcsdup( Keyname );
	}

	~CRegistry()
	{
		free( m_Keyname );
	}

	void ReadRegSettings( PREG_SETTING Settings )
	{
		PREG_SETTING	curSetting;

		hKey = NULL;
		RegOpenKeyEx(HKEY_CURRENT_USER, 
				m_Keyname, 0, KEY_READ, &hKey );
		curSetting = Settings;
		while( curSetting->Valuename ) {

			switch( curSetting->Type ) {
			case SETTING_TYPE_DWORD:
				ReadValue( curSetting->Valuename, (PDWORD) curSetting->Setting,
					(DWORD) curSetting->DefaultSetting );
				break;
			case SETTING_TYPE_BOOLEAN:
				ReadValue( curSetting->Valuename, (PBOOLEAN) curSetting->Setting,
					(BOOLEAN) curSetting->DefaultSetting );
				break;
			case SETTING_TYPE_DOUBLE:
				ReadValue( curSetting->Valuename, (double *) curSetting->Setting,
					curSetting->DefaultSetting );
				break;
			case SETTING_TYPE_WORD:
				ReadValue( curSetting->Valuename, (short *) curSetting->Setting,
					(WORD) curSetting->DefaultSetting );
				break;
			case SETTING_TYPE_STRING:
				ReadValue( curSetting->Valuename, (PCHAR) curSetting->Setting,
					curSetting->Size, (PCHAR) (DWORD_PTR) curSetting->DefaultSetting );
				break;
			case SETTING_TYPE_DWORD_ARRAY:
				ReadValueArray( curSetting->Valuename, curSetting->Size/sizeof DWORD,
					(PDWORD) curSetting->Setting );
				break;
			case SETTING_TYPE_WORD_ARRAY:
				ReadValueArray( curSetting->Valuename, curSetting->Size/sizeof(short),
					(PWORD) curSetting->Setting );
				break;
			case SETTING_TYPE_BINARY:
				ReadValueBinary( curSetting->Valuename, (PBYTE) curSetting->Setting,
					curSetting->Size );
				break;
			}
			curSetting++;
		}
		if( hKey ) {

			RegCloseKey( hKey );
		}
	}
	void WriteRegSettings( PREG_SETTING Settings )
	{
		PREG_SETTING	curSetting;

		if( !RegCreateKeyEx(HKEY_CURRENT_USER, 
				m_Keyname, NULL, NULL, 0, KEY_WRITE, NULL, &hKey, NULL )) {
					
			curSetting = Settings;
			while( curSetting->Valuename ) {

				switch( curSetting->Type ) {
				case SETTING_TYPE_DWORD:
					WriteValue( curSetting->Valuename, *(PDWORD) curSetting->Setting );
					break;
				case SETTING_TYPE_BOOLEAN:
					WriteValue( curSetting->Valuename, *(PBOOLEAN) curSetting->Setting );
					break;
				case SETTING_TYPE_DOUBLE:
					WriteValue( curSetting->Valuename, *(double *) curSetting->Setting );
					break;
				case SETTING_TYPE_WORD:
					WriteValue( curSetting->Valuename, *(short *) curSetting->Setting );
					break;
				case SETTING_TYPE_STRING:
					WriteValue( curSetting->Valuename, (PCHAR) curSetting->Setting );
					break;
				case SETTING_TYPE_DWORD_ARRAY:
					WriteValueArray( curSetting->Valuename, curSetting->Size/sizeof DWORD,
						(PDWORD) curSetting->Setting );
					break;
				case SETTING_TYPE_WORD_ARRAY:
					WriteValueArray( curSetting->Valuename, curSetting->Size/sizeof(short),
						(PWORD) curSetting->Setting );
					break;
				case SETTING_TYPE_BINARY:
					WriteValueBinary( curSetting->Valuename, (PBYTE) curSetting->Setting,
						curSetting->Size );
					break;
				}
				curSetting++;
			}
			RegCloseKey( hKey );
		}
	}

private:
	// Reads
	void ReadValue( PTCHAR Valuename, PDWORD Value, DWORD Default = 0 )
	{
		DWORD	length = sizeof(DWORD);
		if( RegQueryValueEx( hKey, Valuename, NULL, NULL, (PBYTE) Value,
			&length )) {
	
			*Value = Default;
		}
	}
	void ReadValue( PTCHAR Valuename, PBOOLEAN Value, BOOLEAN Default = FALSE )
	{
		DWORD	length = sizeof(DWORD);
		DWORD	val = (DWORD) *Value;
		if( RegQueryValueEx( hKey, Valuename, NULL, NULL, (PBYTE) &val,
			&length )) {

			*Value = Default;

		} else {

			*Value = (BOOLEAN) val;
		}
	}
	void ReadValue( PTCHAR Valuename, short *Value, short Default = 0 )
	{
		DWORD	length = sizeof(DWORD);
		DWORD	val = (DWORD) *Value;
		if( RegQueryValueEx( hKey, Valuename, NULL, NULL, (PBYTE) &val,
			&length )) {
	
			*Value = Default;
		
		} else {

			*Value = (short) val;
		}
	}
	void ReadValue( PTCHAR Valuename, double *Value, double Default = 0.0 )
	{
		DWORD	length = sizeof(double);
		if( RegQueryValueEx( hKey, Valuename, NULL, NULL, (PBYTE) Value,
			&length )) {
	
			*Value = Default;

		} 
	}
	void ReadValue( PTCHAR Valuename, PCHAR Value, DWORD Length, PCHAR Default )
	{
		if( RegQueryValueEx( hKey, Valuename, NULL, NULL, (PBYTE) Value,
			&Length ) && Default ) {
	
			strcpy( Value, Default );
		}
	}
	void ReadValueBinary( PTCHAR Valuename, PBYTE Value, DWORD Length )
	{
		RegQueryValueEx( hKey, Valuename, NULL, NULL, Value,
			&Length );
	}
	void ReadValueArray( PTCHAR Valuename, DWORD Number, PDWORD Entries )
	{
		HKEY	hSubKey;
		TCHAR	subVal[16];
		DWORD	length;

		if( !RegOpenKeyEx(hKey, 
				Valuename, 0, KEY_READ, &hSubKey )) {	

			for( DWORD i = 0; i < Number; i++ ) {
			
				length = sizeof(DWORD);
				_stprintf( subVal, _T("%d"), i );
				RegQueryValueEx( hSubKey, subVal, NULL, NULL, (PBYTE) &Entries[i], &length);
			}
			RegCloseKey( hSubKey );
		}
	}	
	void ReadValueArray( PTCHAR Valuename, DWORD Number, PWORD Entries )
	{
		HKEY	hSubKey;
		TCHAR	subVal[16];
		DWORD	length;
		DWORD	val;

		if( !RegOpenKeyEx(hKey, 
				Valuename, 0, KEY_READ, &hSubKey )) {	

			for( DWORD i = 0; i < Number; i++ ) {
			
				length = sizeof(DWORD);
				_stprintf( subVal, _T("%d"), i );
				if( !RegQueryValueEx( hSubKey, subVal, NULL, NULL, (PBYTE) &val, &length)) {
				
					Entries[i] = (WORD) val;

				} 
			}
			RegCloseKey( hSubKey );
		}
	}	

	// Writes
	void WriteValue( PTCHAR Valuename, DWORD Value ) 
	{
		RegSetValueEx( hKey, Valuename, 0, REG_DWORD, (PBYTE) &Value,
				sizeof(DWORD));
	}
	void WriteValue( PTCHAR Valuename, short Value ) 
	{
		DWORD val = (DWORD) Value;
		RegSetValueEx( hKey, Valuename, 0, REG_DWORD, (PBYTE) &val,
				sizeof(DWORD));
	}
	void WriteValue( PTCHAR Valuename, BOOLEAN Value ) 
	{
		DWORD val = (DWORD) Value;
		RegSetValueEx( hKey, Valuename, 0, REG_DWORD, (PBYTE) &val,
				sizeof(DWORD));
	}
	void WriteValue( PTCHAR Valuename, double Value ) 
	{
		RegSetValueEx( hKey, Valuename, 0, REG_BINARY, (PBYTE) &Value,
				sizeof(double));
	}
	void WriteValue( PTCHAR Valuename, PCHAR Value ) 
	{
		RegSetValueEx( hKey, Valuename, 0, REG_SZ, (PBYTE) Value,
				(DWORD) strlen( Value ));
	}
	void WriteValueBinary( PTCHAR Valuename, PBYTE Value, DWORD Length ) 
	{
		RegSetValueEx( hKey, Valuename, 0, REG_BINARY, Value,
				Length );
	}
	void WriteValueArray( PTCHAR Valuename, DWORD Number, PDWORD Entries )
	{
		HKEY	hSubKey;
		TCHAR	subVal[16];

		if( !RegCreateKeyEx(hKey, 
				Valuename, NULL, NULL, 0, KEY_WRITE, NULL, &hSubKey, NULL )) {	

			for( DWORD i = 0; i < Number; i++ ) {
			
				_stprintf( subVal, _T("%d"), i );
				if( Entries[i] ) 
					RegSetValueEx( hSubKey, subVal, 0, REG_DWORD, (PBYTE) &Entries[i], sizeof(DWORD));
				else
					RegDeleteValue( hSubKey, subVal );
			}
			RegCloseKey( hSubKey );
		}
	}
	void WriteValueArray( PTCHAR Valuename, DWORD Number, PWORD Entries )
	{
		HKEY	hSubKey;
		TCHAR	subVal[16];
		DWORD	val;

		if( !RegCreateKeyEx(hKey, 
				Valuename, NULL, NULL, 0, KEY_WRITE, NULL, &hSubKey, NULL )) {	

			for( DWORD i = 0; i < Number; i++ ) {
			
				_stprintf( subVal, _T("%d"), i );
				val = (DWORD) Entries[i];
				if( Entries[i] ) 
					RegSetValueEx( hSubKey, subVal, 0, REG_DWORD, (PBYTE) &val, sizeof(DWORD));
				else
					RegDeleteValue( hSubKey, subVal );
			}
			RegCloseKey( hSubKey );
		}
	}

};
