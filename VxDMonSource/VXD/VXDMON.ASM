;****************************************************************************
;                                                                           *
; VXDMHLP								    *
;								  	    *
; Copyright (c) 1996 Bryce Cogswell and Mark Russinovich		    *
; All rights reserved                                                       *
;									    *
;****************************************************************************
;									    *
; VXDMHLP: Monitors entry and exit of VxDs.				    *
;                                                                           *
;****************************************************************************

;===========================================================================
	page	,132
	title	VXDMHLP - VXDMHLP VxD
	name	VXDMHLP.vxd
;===========================================================================
;
;   Module:
;	Contains everything
;
;===========================================================================
;
;   Functional Description: - 
;
;
;			
;============================================================================

;============================================================================
;				I N C L U D E S
;============================================================================
.386p
	.xlist
	include	vmm.inc
	include vwin32.inc
	include debug.inc
	; for message box
	include shell.inc
	; for testing
	include vcache.inc
.list

	include VXDMON.inc

;============================================================================
;	 	   		MACROS
;============================================================================

;============================================================================
; 			  P U B L I C   D A T A
;============================================================================

VXD_LOCKED_DATA_SEG

; flags for stats operations
UPDATESTATS	EQU		1
ZEROSTATS	EQU		2
UPDATEZEROSTATS	EQU		3


myRDTSC	MACRO
	db	0Fh, 31h	; rdtsc
	nop			; pad to 4 bytes long
	nop			; pad to 4 bytes long
	ENDM

; -------------------------------------------------------------------------
; Jump table for commands initiated by Devmon Windows program
; -------------------------------------------------------------------------
Service_Table	label	dword
	dd	offset32	ioctl_closehandle
	dd	offset32	ioctl_getversion
	dd	offset32	ioctl_getstats
	dd	offset32	ioctl_getzerostats
	dd	offset32	ioctl_zerostats
	dd	offset32	ioctl_hookservice
	dd	offset32	ioctl_unhookservice
	dd	offset32	ioctl_getoverhead
Service_Table_Size	EQU	($ - Service_Table) / 4


; -------------------------------------------------------------------------
; This points to the most recently called service.
; -------------------------------------------------------------------------
; max call stack depth
MRUMaxStack		EQU	32
; max amount by which we expect stack to grow during VxD calls
MRUMaxLocalStack	EQU	512

; size of this structure must be power of 2
MRUService	STRUC
MRU_Ordinal	DD	?
MRU_SP		DD	?
MRUService	ENDS

; call stack
MRUStack	MRUService	 MRUMaxStack dup (<?>)
; current call stack pointer
MRUStackPtr	DD	0

; indicates if error on exit
FixRetErr	DD	0

; -------------------------------------------------------------------------
; Use this to track error conditions
; -------------------------------------------------------------------------
IoctlError	DD	0



; -------------------------------------------------------------------------
; This template defines the structure we allocate for each hooked service.
; It is customized for a particular service upon creation.
;
; We provide labels for all relocatable addresses so that we can adjust
; them when the template is instantiated.
; -------------------------------------------------------------------------

HookTemplate:
	ServiceStats	<0,0,0,0,0,0,0>
HookTemplatePrevHooker	dd	?
HookTemplatePrevPtr	EQU offset32 $ + 4 - offset32 HookTemplate
BeginProc HookTemplateProc, Hook_Proc HookTemplatePrevHooker
	call	MonEnter
HookTemplateMonEnter	EQU offset32 $ - offset32 HookTemplate - 4
	jmp	[HookTemplatePrevHooker]
HookTemplatePrevHookerJmp EQU offset32 $ - offset32 HookTemplate - 4
EndProc HookTemplateProc
	align	4
HookTemplateEnd:

HookTemplateLen	EQU	(offset32 HookTemplateEnd - offset32 HookTemplate)
MaxHooks	EQU	2000
MaxPages	EQU	((HookTemplateLen * MaxHooks + 4095) / 4096)
HookTable	dd	0
HookFree	dd	0
HookUsed	dd	0


; -------------------------------------------------------------------------
; This template defines the structure we allocate to capture when a service
; returns.  It is customized for a particular service at the time the
; service is invoked.
; -------------------------------------------------------------------------
RetTemplate:
	call	MonExit
RetTemplateHooker EQU	(offset32 $ - offset32 RetTemplate)
	dd	0	; pointer to hooker structure
RetTemplateOrigAddr  EQU  (offset32 $ - offset32 RetTemplate)
	dd	0	; original return address
RetTemplateSP	EQU	(offset32 $ - offset32 RetTemplate)
	dd	0	; pointer to return address on stack
RetTemplateTime EQU	(offset32 $ - offset32 RetTemplate)
	dd	0
	dd	0
RetTemplateEnd:

MaxReturn	EQU	400
RetTemplateLen	EQU	(offset32 RetTemplateEnd - offset32 RetTemplate)
RetTableSize	EQU	(MaxReturn * RetTemplateLen)

ReturnTable	db	(MaxReturn * RetTemplateLen) dup (?)
ReturnFree	dd	offset32 ReturnTable



VXD_LOCKED_DATA_ENDS


;============================================================================
;	           D E V I C E   D E C L A R A T I O N
;============================================================================

VXD_LOCKED_CODE_SEG

DECLARE_VIRTUAL_DEVICE VXDMHLP,	\
	VXDMHLP_MAJOR_VERSION, 	\
	VXDMHLP_MINOR_VERSION,	\
	VXDMHLP_Control, ,	\
	UNDEFINED_INIT_ORDER


;============================================================================
;			    M A I N   C O D E
;============================================================================


; -------------------------------------------------------------------------
; Called each time a service is invoked.
; -------------------------------------------------------------------------
BeginProc MonEnter
	pushfd

	; disable interrupts so our timing computations aren't corrupted
	cli

	push	esi
	push	edi
	push	eax
	push	edx

	; get pointer to hook structure
	mov	esi, [esp+20]		; get return address
	sub	esi, HookTemplateMonEnter + 4

	; increment entry count
	inc	[esi].SS_Enter

	; allocate a return structure to replace original return with
	mov	eax, [ReturnFree]	; get location of return function
	mov	edi, [eax].RetTemplateHooker	; get location of next on list
	mov	[ReturnFree], edi	; update pointer to first on list

	; change return address to point to us, fetch original return address
	mov	edi, eax		; copy pointer to return structure
	xchg	[esp+24], eax		; store new return address, fetch old

	; save original return address
	mov	[edi].RetTemplateOrigAddr, eax

	; store service hooker address
	mov	[edi].RetTemplateHooker, esi

	; save address of return address on stack
	lea	eax, [esp+24]
	mov	[edi].RetTemplateSP, eax

	; do stuff for recording callers/callees
	call	MRUEnter

	; update time spent in function (do as late as possible)
rdts1:	myRDTSC					; edx:eax = rdtsc
	mov	[edi].RetTemplateTime, eax	; store time low
	mov	[edi].RetTemplateTime+4, edx	; store time high

	pop	edx
	pop	eax
	pop	edi
	pop	esi
	popfd
	ret
EndProc	MonEnter



; -------------------------------------------------------------------------
; Called each time a service returns from invocation.
; -------------------------------------------------------------------------
BeginProc MonExit
	pushfd
	push	esi
	push	edi
	push	eax
	push	edx

	; disable interrupts so our timing computations aren't corrupted
	cli

	; get pointer to original structure
	mov	edi, [esp+20]			; return address
	sub	edi, RetTemplateHooker		; point to front of return template
	mov	esi, [edi].RetTemplateHooker	; get hooker structure pointer

	; update time spent in function (do as early as possible)
rdts2:	myRDTSC					; edx:eax = rdtsc
	sub	eax, [edi].RetTemplateTime
	sbb	edx, [edi].RetTemplateTime+4
	add	[esi].SS_TimeLo, eax		; add time low
	adc	[esi].SS_TimeHi, edx		; add time high

	; replace our return address with original
	mov	edx, [edi].RetTemplateOrigAddr
	mov	[esp+20], edx			; replace return address

	; increment exit count
	inc	[esi].SS_Exit

	; make return address structure available to someone else
	xor	edx, edx
	mov	[edi].RetTemplateSP, edx	; mark as unused
	mov	edx, [ReturnFree]		; get base list pointer
	mov	[edi].RetTemplateHooker, edx	; set link pointer to base

	; now finalize freeing the structure
	mov	[ReturnFree], edi		; set base list pointer to ours

	; do stuff for recording callers/callees
	call	MRUExit

	pop	edx
	pop	eax
	pop	edi
	pop	esi
	popfd
	ret
EndProc	MonExit



BeginProc	MRUEnter
	; if stack is currently empty, only put us on it
	mov	eax, MRUStackPtr
	or	eax, eax
	jz	mru_enter_reset

	; ensure we're using same stack as MRU
	; check if esp is larger than last
	add	eax, offset32 MRUStack - size MRUService
	mov	edx, [eax].MRU_SP
	sub	edx, esp
	je	mru_enter_reset
	; check if esp is much smaller than last
	cmp	edx, MRUMaxLocalStack
	jg	mru_enter_reset

	; indicate that we were called by MRU service
	mov	edx, [esi].SS_CallerPtr		; get position to save caller
	push	[eax].MRU_Ordinal		; get last called service
	pop	[esi+edx*4].SS_Caller		; save caller
	inc	edx
	and	edx, CALLER_CNT-1
	mov	[esi].SS_CallerPtr, edx		; update caller pointer

	; update MRU call stack
	mov	edx, [esi].SS_Ordinal
	mov	[eax+size MRUService].MRU_Ordinal, edx
	mov	[eax+size MRUService].MRU_SP, esp

	; update stack pointer location
	sub	eax, offset32 MRUStack - 2 * size MRUService
	and	eax, (MRUMaxStack-1) * size MRUService
	mov	MRUStackPtr, eax

	; all done
	ret

mru_enter_reset:
	; reset mru stack to be empty
	mov	eax, size MRUService
	mov	MRUStackPtr, eax
	; make us the only thing on the stack
	mov	edx, [esi].SS_Ordinal
	mov	MRUStack.MRU_Ordinal, edx
	mov	MRUStack.MRU_SP, esp
	; all done
	ret
EndProc	MRUEnter


BeginProc	MRUExit
	; get pointer to top item on call stack
	mov	eax, MRUStackPtr		; eax = last stack entry
	or	eax, eax
	jz	mru_exit_empty			; stack is empty
	; ensure that it is us
	sub	eax, size MRUService		; back down to previous item
	mov	edx, [MRUStack+eax].MRU_Ordinal
	cmp	[esi].SS_Ordinal, edx
	jne	mru_exit_reset			; who knows how we got here?
	; update stack pointer location
	mov	MRUStackPtr, eax
	; all done
	ret
mru_exit_reset:
	xor	eax, eax
	mov	MRUStackPtr, eax
mru_exit_empty:
	; all done
	ret
EndProc	MRUExit



; -------------------------------------------------------------------------
; Initialize the linked list of hooks for hooked services.
; These are created using the HookTemplate.
; -------------------------------------------------------------------------
BeginProc InitHookTable
	; Allocate space for hook functions and statistics.
	; We initialize enough memory to hook tons of services, but
	; lock only the portion we need, allowing most to be paged out.
	VMMcall	_PageAllocate, <MaxPages, PG_SYS, 0, 0, 0, 0, 0, PAGEZEROINIT>
	mov	[HookTable], eax		; save table pointer
	mov	edi, eax
	mov	[HookFree], edi			; set pointer to first
	lea	esi, HookTemplate
	mov	ecx, MaxHooks
	sub	eax, esi
	cld
init_hook_loop:
	mov	edx, ecx
	; Copy template
	mov	ecx, HookTemplateLen
	rep movsb
	sub	esi, HookTemplateLen
	; Fix up relocatable items
	mov	[edi-HookTemplateLen].SS_Next, edi	; set pointer to next
	sub	[edi-HookTemplateLen].HookTemplateMonEnter, eax
	add	[edi-HookTemplateLen].HookTemplatePrevHookerJmp, eax
	add	[edi-HookTemplateLen].HookTemplatePrevPtr, eax

	add	eax, HookTemplateLen
	mov	ecx, edx
	loop	init_hook_loop
	xor	ecx, ecx
	mov	[edi-HookTemplateLen].SS_Next, ecx	; set pointer to next
	ret
EndProc InitHookTable


; -------------------------------------------------------------------------
; Initialize the linked list of return points for invoked functions.
; These are created using the RetTemplate.
; -------------------------------------------------------------------------
BeginProc InitReturnTable
	lea	edi, ReturnTable
	mov	[ReturnFree], edi		; set pointer to first
	lea	esi, RetTemplate
	mov	ecx, MaxReturn
	mov	eax, offset32 ReturnTable - offset32 RetTemplate
	cld
init_return_loop:
	mov	edx, ecx
	mov	ecx, RetTemplateLen
	rep movsb
	sub	esi, RetTemplateLen
	mov	[edi-RetTemplateLen+RetTemplateHooker], edi	; set pointer to next
	sub	[edi-RetTemplateLen+RetTemplateHooker-4], eax	; adjust call address
	add	eax, RetTemplateLen
	mov	ecx, edx
	loop	init_return_loop
	xor	ecx, ecx
	mov	[edi-RetTemplateLen+RetTemplateHooker], ecx	; set pointer to next
	ret
EndProc InitReturnTable



;============================================================================
;
; VXDMHLP_Control - Device control procedure for the VxD. Dispatches all
;                  Windows VxD messages.
;
; Exit:	If carry clear then
;	    Successful
;	else
;	    Control call failed
;
; Destroys: EAX, EBX, ECX, EDX, ESI, EDI, Flags
;
;============================================================================

public VXDMHLP_Control
VXDMHLP_Control PROC NEAR

	Control_Dispatch SYS_DYNAMIC_DEVICE_INIT,	VXDMHLP_Device_Init
	Control_Dispatch SYS_DYNAMIC_DEVICE_EXIT,	VXDMHLP_Device_Exit
	Control_Dispatch W32_DEVICEIOCONTROL,		VXDMHLP_ioctl
	clc
	ret

VXDMHLP_Control ENDP


;============================================================================
;
; VXDMHLP_ioctl - Respond to IOcontrol messages sent by Win32 program.
;
; Entry: esi -> DIOC block
; 
; Exit:
;
;============================================================================

Public VXDMHLP_ioctl
BeginProc VXDMHLP_ioctl

	mov	IoctlError, VXDMHLP_ERROR_NOSUCHSERVICE
	mov	ecx,[esi].dwIoControlCode	; get ioctl code
	inc	ecx				; base is -1
	cmp	ecx, Service_Table_Size		; out of bounds ?
	jae	ioctl_failure			; y: bad code, exit
	jmp	Service_Table[4*ecx]		; index into table

; -------------------------------------------------------------------------
; -------------------------------------------------------------------------
ioctl_closehandle:
	; Nothing to do for this
	jmp	ioctl_success		; exit successfully

; -------------------------------------------------------------------------
; -------------------------------------------------------------------------
ioctl_getversion:
	; Nothing to do for this
	jmp	ioctl_success		; exit successfully

; -------------------------------------------------------------------------
; Get the statistics we've collected for all hooked services
; -------------------------------------------------------------------------

ioctl_zerostats:
	mov	ebx, ZEROSTATS			; zero the stats, no update
	mov	edx, [HookUsed]
	cld
ioctl_dozero:
	; check for end of list
	cmp	edx, 0
	je	ioctl_success

	; zero volatile statistics
	cli
	push	esi
	lea	esi, [edx]			; get stats pointer
	xor	eax, eax
	mov	[esi].SS_Enter,  eax
	mov	[esi].SS_Exit,   eax
	mov	[esi].SS_TimeLo, eax
	mov	[esi].SS_TimeHi, eax
	sti
	pop	esi
	; move to next service
	mov	edx, [edx].SS_Next
	jmp	ioctl_dozero

; get stats with no zero

ioctl_getstats:
	mov	ebx, UPDATESTATS		; update with no clear
	jmp	ioctl_scanstats

; get stats and reset

ioctl_getzerostats:
	mov	ebx, UPDATEZEROSTATS		; update and zero stats

ioctl_scanstats:
	; zero current output size
	xor	eax, eax
	mov	ecx, [esi].lpcbBytesReturned
	mov	[ecx], eax
	; get pointer to source and destination buffers
	mov	edx, [HookUsed]
	mov	edi, [esi].lpvOutBuffer
	; iterate over structures in use, copying
	cld
ioctl_docopy:
	; check for end of list
	cmp	edx, 0
	je	ioctl_success

	; update size of output and exit if buffer full
	mov	ecx, [esi].lpcbBytesReturned	; get output size pointer
	mov	eax, [ecx]			; get current output size
	add	eax, size ServiceStats		; update total size
	cmp	eax, [esi].cbOutBuffer		; less than max output size ?
	jg	ioctl_success
	mov	[ecx], eax			; update output size
	; copy stats to output buffer
	push	esi
	mov	ecx, size ServiceStats / 4
	lea	esi, [edx]			; get stats pointer
	cli
	rep movsd

	test	ebx, ZEROSTATS
	je	nozero

	; zero volatile statistics
	xor	eax, eax
	mov	[esi - size ServiceStats].SS_Enter,  eax
	mov	[esi - size ServiceStats].SS_Exit,   eax
	mov	[esi - size ServiceStats].SS_TimeLo, eax
	mov	[esi - size ServiceStats].SS_TimeHi, eax
nozero:
	sti
	pop	esi
	; move to next service
	mov	edx, [edx].SS_Next
	jmp	ioctl_docopy

; -------------------------------------------------------------------------
; Hook a new service
; -------------------------------------------------------------------------
ioctl_hookservice:
	; get ordinal of interest
	mov	eax, [esi].lpvInBuffer
	mov	eax, [eax]
internal_hookservice:
	; ensure that the vxd is loaded
	mov	IoctlError, VXDMHLP_ERROR_NOSUCHVXD
	mov	edx, eax		; save ordinal
	shr	eax, 16			; get device id
	jz	ioctl_failure		; if zero, we lose
	VMMCall	Get_DDB			; check for DDB
	or	ecx, ecx
	jz	ioctl_failure		; if result zero, we lose
	mov	eax, edx		; restore ordinal
	; get a hook structure
	mov	IoctlError, VXDMHLP_ERROR_OUTOFMEMORY
	mov	edi, [HookFree]
	cmp	edi, 0
	je	ioctl_failure	; no structures available
	; Ensure the page containing the structure is locked in memory.
	; We rely on the fact that a page can be locked multiple times.
	mov	IoctlError, VXDMHLP_ERROR_PAGELOCK
	push	eax
	mov	eax, edi
	shr	eax, 12
	VMMcall _LinPageLock, <eax, 1, 0>
	or	eax, eax	; nonzero if locked, zero if error
	pop	eax
	jz	ioctl_failure
	; fill in service-specific info in structure
	mov	[edi].SS_Ordinal, eax
	xor	edx, edx
	mov	[edi].SS_Enter,  edx
	mov	[edi].SS_Exit,   edx
	mov	[edi].SS_TimeLo, edx
	mov	[edi].SS_TimeHi, edx
	; hook the service
	mov	IoctlError, VXDMHLP_ERROR_HOOK
	push	esi
	lea	esi, [edi +(offset32 HookTemplateProc - offset32 HookTemplate)]
	VMMCall	Hook_Device_Service
	pop	esi
	jc	ioctl_failure
	; update pointer to next available structure
	mov	edx, [edi].SS_Next
	mov	[HookFree], edx
	; add to list of hooked services
	mov	eax, [HookUsed]
	mov	[edi].SS_Next, eax
	mov	[HookUsed], edi
	jmp	ioctl_success

; -------------------------------------------------------------------------
; Unhook a service
; -------------------------------------------------------------------------
ioctl_unhookservice:
	; get ordinal of interest
	mov	eax, [esi].lpvInBuffer
	mov	eax, [eax]
internal_unhookservice:
	; locate hook structure
	lea	edx, [HookUsed]
	mov	edi, [edx]
	mov	IoctlError, VXDMHLP_ERROR_NOTFOUND
unhooksearch:
	cmp	edi, 0
	je	ioctl_failure
	cmp	[edi].SS_Ordinal, eax
	je	unhookfound
	lea	edx, [edi].SS_Next
	mov	edi, [edx]
	jmp	unhooksearch
unhookfound:
	; unhook service
	mov	IoctlError, VXDMHLP_ERROR_UNHOOK
	push	esi
	lea	esi, [edi +(offset32 HookTemplateProc - offset32 HookTemplate)]
	VMMCall Unhook_Device_Service
	pop	esi
	jc	ioctl_failure
	; remove from list of used hook structures
	mov	eax, [edi].SS_Next
	mov	[edx], eax
	; add to list of free hook structures
	mov	eax, [HookFree]
	mov	[edi].SS_Next, eax
	mov	[HookFree], edi
	jmp	ioctl_success

; -------------------------------------------------------------------------
; Compute monitoring overhead
; -------------------------------------------------------------------------
ioctl_getoverhead:
	push	esi
	; save current time
rdts3:	myRDTSC				; edx:eax = rdtsc
	push	eax
	; call Get_VMM_Version 128 times
	mov	esi, 128
unhooked_time_loop:
	VxDCall	Get_VMM_Version
	dec	esi
	jnz	unhooked_time_loop
	; save current time
rdts4:	myRDTSC				; edx:eax = rdtsc
	push	eax
	; hook Get_VMM_Version.  This should always be possible.
	GetVxDServiceOrdinal eax, Get_VMM_Version
	call	internal_hookservice
	jc	ioctl_failure
	; call it 128 times
	mov	esi, 128
hooked_time_loop:
	VxDCall	Get_VMM_Version
	dec	esi
	jnz	hooked_time_loop
	; get the time we've recorded
	lea	edx, [HookUsed]
	mov	edi, [edx]
ovrsearch:
	cmp	[edi].SS_Ordinal, 10000h
	je	ovrfound
	lea	edx, [edi].SS_Next
	mov	edi, [edx]
	jmp	ovrsearch
ovrfound:	
	pushd	0
	mov	eax, [edi].SS_TimeLo
	push	eax
	; unhook it
	GetVxDServiceOrdinal eax, Get_VMM_Version
	call	internal_unhookservice
	; now compute the time difference, overhead = (t4-t3)-(t2-t1)
	mov	eax, [esp]
	sub	eax, [esp+4]
	sub	eax, [esp+8]
	add	eax, [esp+12]
	add	esp, 4*4
	; eax now contains the overhead for 128 calls
	shr	eax, 7
	; save overhead per call
	pop	esi
	mov	edx, [esi].lpvOutBuffer	
	mov	[edx], eax
	; set size of output buffer
	mov	edx, [esi].lpcbBytesReturned	; get output size pointer
	mov	eax, 4
	mov	[edx], eax
	jmp	ioctl_success

ioctl_success:
	xor	eax, eax			; return zero = success
	clc
	ret

ioctl_failure:
	mov	eax, IoctlError
	stc
	ret

EndProc	VXDMHLP_ioctl


;============================================================================
;
; VXDMHLP_Device_Exit - Cleans up any hooks that are still installed before
;		    exiting.
;
;============================================================================

Public VXDMHLP_Device_Exit
BeginProc VXDMHLP_Device_Exit

	; clear error condition
	mov	FixRetErr, 0

	; iterate until no more services are hooked
unhookall_loop:
	mov	edi, [HookUsed]
	; check for end of list
	cmp	edi, 0
	je	unhookall_done
	; unhook the service
	mov	eax, [edi].SS_Ordinal
	call	internal_unhookservice
	jnc	unhookall_loop
	cmp	IoctlError, VXDMHLP_ERROR_UNHOOK
	jne	unhookdel
	; major error - can't unload
	mov	FixRetErr, 1

unhookdel:
	; It wasn't found on the list.  Just delete it and move on.
	mov	eax, [HookUsed]
	mov	eax, [edi].SS_Next
	mov	[HookUsed], eax
	jmp	unhookall_loop
unhookall_done:

	; Locate all VxD calls that haven't yet returned, and change their
	; stack so they return to their original callers instead of MonExit.
	lea	edi, ReturnTable
	mov	ecx, MaxReturn
fixret_loop:
	; iterate over return structures
	mov	eax, [edi].RetTemplateSP	; fetch stack pointer
	or	eax, eax			; check if in use
	jz	fixret_continue
	; ensure stack is still in use (heuristic approach)
	shr	eax, 12
	push	ecx
	VMMcall	_PageCheckLinRange, <eax, 1, 0>
	pop	ecx
	or	eax, eax
	jz	fixret_continue
	cli
	mov	eax, [edi].RetTemplateSP	; fetch stack pointer

	; make sure it points at a return template
	mov	edx, [eax]			; get return address
	sub	edx, offset32 ReturnTable
	cmp 	edx, RetTableSize		; within return table?
	jb	fixret_okay
	mov	FixRetErr, 1
	jmp	fixret_continue
	
fixret_okay:
	; patch return address
	mov	edx, [edi].RetTemplateOrigAddr	; fetch original return addr
	mov	[eax], edx			; restore original return addr
fixret_continue:
	sti
	add	edi, RetTemplateLen
	loop	fixret_loop

	; Free the memory we were using if no errors
	stc
	cmp 	FixRetErr, 1			; clears carry???
	je	nofree
	VMMcall	_PageFree, <[HookTable], 0>
	clc
nofree:
	ret

EndProc VXDMHLP_Device_Exit

VXD_LOCKED_CODE_ENDS






;============================================================================
;	   D E V I C E   I N I T I A L I Z A T I O N   C O D E
;============================================================================

VXD_ICODE_SEG

;============================================================================
;									
; VXDMHLP_Device_Init - VXDMHLP Initialization 	
;									
;									
; Entry: ebx -> System VM handle (not used)
;        edx -> Reference data from real mode init portion
;
; Exit: If successful then
;           Carry flag is clear
;       else
;           Carry flag is set to indicate an error -- Device not initialized
;
;============================================================================

VXDMHLPCaption	db	"VxD Monitor",0
VXDMHLPMessage	db	"                                 Loading...",0

BeginProc VXDMHLP_Device_Init

if 0
	; Put up message box indicating we're loading
	VMMcall	Get_Cur_VM_Handle
	mov	eax, MB_OK
	mov	ecx, OFFSET32 VXDMHLPMessage
	mov	edi, OFFSET32 VXDMHLPCaption
	VxDcall	SHELL_SYSMODAL_Message
	int	3
endif
	; determine if we have cpuid instruction
	cli				; disable interrupts
	pushfd				; push flags
	pop	eax			; pop flags
	mov	edx, eax		; save original flags
	xor	eax, 200000h		; toggle bit 21
	push	eax			; push toggled flags
	popfd				; load toggled flags
	pushfd				; push toggled flags
	pop	eax			; pop toggled flags
	push	edx			; push orig flags
	popfd				; restore orig flags
	sti				; enable interrupts
	cmp	eax, edx		; did bit 21 change
	je	nonpentium		; can't change means no cpuid instr
	; we have cpuid
	mov	eax, 1			; request family id
	db	0Fh, 0A2h		; CPUID
	cmp	eax, 500h		; test for pentium family
	jae	pentium			; if so, we're OK
	; otherwise blot out the rdtsc instructions


	; have to zero all rdtsc instructions
nonpentium:
	mov	eax, 0C033D233h		; xor eax, eax	; xor edx, edx
	mov	dword ptr rdts1, eax
	mov	dword ptr rdts2, eax
	mov	dword ptr rdts3, eax
	mov	dword ptr rdts4, eax

pentium:
	; Initialize
	call	InitHookTable
	call	InitReturnTable

	; Ready to go
	clc
	ret
EndProc VXDMHLP_Device_Init

VXD_ICODE_ENDS

end
