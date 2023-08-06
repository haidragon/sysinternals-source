;****************************************************************************
;
; VXDMHLP header file
;
;****************************************************************************


	VXDMHLPName	EQU	<'VXDMHLP VXD     '> 	;Must be 16 chars
	VXDMHLPRev	EQU	00H

; REC version equates

VXDMHLP_MAJOR_VERSION	EQU	1
VXDMHLP_MINOR_VERSION	EQU	0


; -------------------------------------------------------------------------
; The statistics we accumulate for each service
; -------------------------------------------------------------------------
CALLER_CNT	EQU	8

ServiceStats	STRUC
SS_Ordinal	DD	?
SS_Next		DD	?
SS_Enter	DD	?
SS_Exit		DD	?
SS_TimeLo	DD	?
SS_TimeHi	DD	?
SS_CallerPtr	DD	?
SS_Caller	DD	CALLER_CNT dup (?)
ServiceStats	ENDS



VXDMHLP_ERROR_NOSUCHSERVICE	EQU	1
VXDMHLP_ERROR_OUTOFMEMORY	EQU	2
VXDMHLP_ERROR_PAGELOCK		EQU	3
VXDMHLP_ERROR_NOTFOUND		EQU	4
VXDMHLP_ERROR_HOOK		EQU	5
VXDMHLP_ERROR_UNHOOK		EQU	6
VXDMHLP_ERROR_NOSUCHVXD		EQU	7

