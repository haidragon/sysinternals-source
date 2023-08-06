#include <windows.h>
#include <stdio.h>

char IsFeaturePresent( DWORD Feature )
{
	return IsProcessorFeaturePresent( Feature ) ? 'Y' : 'N';
}

void main( int argc, char *argv[] )
{
	HKEY		hKey;
	DWORD		error, type, valueLength;
	char		processorType[MAX_PATH];

	printf("\nProcess Feature v1.10\n");
	printf("Copyright (C) 2005 Mark Russinovich\n");
	printf("Sysinternals - www.sysinternals.com\n\n");

	error = RegOpenKeyEx( HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, 
						KEY_READ, &hKey );
	if( error == ERROR_SUCCESS ) {
		
		valueLength = sizeof processorType;
		if( RegQueryValueEx( hKey, "ProcessorNameString", NULL, &type, (PBYTE) processorType, &valueLength ) ==
			ERROR_SUCCESS ) {

			printf("%s\n", processorType );
		}
		RegCloseKey( hKey );
	}

	error = RegOpenKeyEx( HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, 
						KEY_READ, &hKey );
	if( error == ERROR_SUCCESS ) {
		
		valueLength = sizeof processorType;
		if( RegQueryValueEx( hKey, "processor_identifier", NULL, &type, (PBYTE) processorType, &valueLength ) ==
			ERROR_SUCCESS ) {

			printf("%s\n", processorType );
		}
		RegCloseKey( hKey );
	}
	printf("No Execute Protection:              %C\n", IsFeaturePresent( PF_NX_ENABLED ));
	printf("Physical Address Extensions (PAE):  %C\n", IsFeaturePresent( PF_PAE_ENABLED ));
	printf("Floating point emulation:           %C\n", IsFeaturePresent( PF_FLOATING_POINT_EMULATED ));
	printf("Pentium Floating point errata:      %C\n", IsFeaturePresent( PF_FLOATING_POINT_PRECISION_ERRATA));
	printf("RDTSC (Cycle counter):              %C\n", IsFeaturePresent( PF_RDTSC_INSTRUCTION_AVAILABLE ));
	printf("MMX Instruction Set:                %C\n", IsFeaturePresent( PF_MMX_INSTRUCTIONS_AVAILABLE ));
	printf("3D Now Instruction Set:             %C\n", IsFeaturePresent( PF_3DNOW_INSTRUCTIONS_AVAILABLE ));
	printf("SSE Instruction Set:                %C\n", IsFeaturePresent( PF_XMMI_INSTRUCTIONS_AVAILABLE ));
	printf("SSE2 Instruction Set:               %C\n", IsFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE ));
}
