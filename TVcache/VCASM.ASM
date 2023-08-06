;======================================================================
; 
; VCASM.asm	
;
; Copyright (c) 1995 Mark Russinovich and Bryce Cogswell	
;
; This file contains the assembly language interface wrappers for
; tvcache. Note that register save/restores were determined
; many times through actual monitoring of the real vcache's usage.
;
;======================================================================
.386p
	NO_SEGMENTS=1
	include vmm.inc
	include vsegment.inc
.list

VXD_LOCKED_DATA_SEG


VXD_LOCKED_DATA_ENDS

VXD_LOCKED_CODE_SEG

	extern	_CTVCACHE_Get_Version	: near
	extern	_CTVCACHE_Register	: near
	extern	_CTVCACHE_GetSize	: near
	extern	_CTVCACHE_CheckAvail	: near
	extern	_CTVCACHE_FindBlock	: near
	extern	_CTVCACHE_FreeBlock	: near
	extern	_CTVCACHE_MakeMRU	: near
	extern	_CTVCACHE_Hold		: near
	extern	_CTVCACHE_Unhold	: near
	extern	_CTVCACHE_Enum		: near
	extern	_CTVCACHE_TestHold	: near
	extern	_CTVCACHE_GetStats	: near
	extern	_CTVCACHE_Deregister	: near
	extern	_CTVCACHE_SwapBuffers	: near
	extern	_CTVCACHE_RelinquishPage: near
	extern	_CTVCACHE_UseThisPage	: near
	extern	_CTVCACHE_AdjustMinimum	: near



BeginProc _TVCache_Get_Version
	push	ecx
	push	edx
	call	_CTVCACHE_Get_Version
	xor	ebx, ebx
	clc
	pop	edx
	pop	ecx
	ret
EndProc _TVCache_Get_Version


BeginProc _TVCache_Register
	push	ecx
	push	edx
	push	ecx			; nblocks
	push	esi			; buffer discard proc
	call	_CTVCACHE_Register
	add	esp, 8
	pop	edx
	pop	ecx
	ret				; eax = fsd id
EndProc _TVCache_Register


BeginProc _TVCache_GetSize
	push	edi
	push	esi
	push	ebp
	mov	ebp, esp
	sub	esp, 8			; create space for returns
	lea	edx, [ebp-4]
	push	edx			; pointer to minsize
	lea	edx, [ebp-8]
	push	edx			; pointer to maxsize
	movzx	eax, ah
	push	eax			; fsd id
	call	_CTVCACHE_GetSize
	add	esp, 3*4		; eax = cursize
	pop	edx			; maxsize
	pop	ecx			; minsize
	pop	ebp
	pop	esi
	pop	edi
	ret
EndProc _TVCache_GetSize


BeginProc _TVCache_CheckAvail
	push	ecx
	push	ecx
	movzx	eax, ah	
	push	eax
	call	_CTVCACHE_CheckAvail
	add	esp, 8
	pop	ecx
	cmp	eax, ecx
	ret
EndProc _TVCache_CheckAvail


BeginProc _TVCache_FindBlock
	push	edx
	push	ecx
	push	ebp
	mov	ebp, esp
	sub	esp, 12			; create space for returns
	lea	edx, [ebp-4]
	push	edx			; pointer to buffer address
	lea	edx, [ebp-8]
	push	edx			; pointer to cache locked	
	lea	edx, [ebp-12]
	push	edx			; pointer to cache block
	push	edi			; key 2
	push	ebx			; key 1
	movzx	edx, al			; flags
	push	edx
	movzx	edx, ah			; fsd id
	push	edx
	call	_CTVCACHE_FindBlock
	mov	edx, eax
	add	esp, 7*4		; pop off fsdid, opt, key1, key2
	pop	esi			; cache block handle
	pop	ecx			; block is locked (BOOL)
	pop	eax			; buffer address
	mov	esp, ebp
	pop	ebp

	; set up the flags

	cmp	edx, 1
	jnz	notfound
	neg	ecx			; clear zero flag if locked
	clc
	pop	ecx
	pop	edx
	ret
			
notfound:	
	neg	ecx			; clear zero flag if locked
	stc
	pop	ecx
	pop	edx
	ret
EndProc _TVCache_FindBlock


BeginProc _TVCache_FreeBlock
	pusha
	push	esi
	call	_CTVCACHE_FreeBlock
	add	esp, 4
	popa
	ret
EndProc _TVCache_FreeBlock


BeginProc _TVCache_MakeMRU
	pusha
	push	esi
	call	_CTVCACHE_MakeMRU
	add	esp, 4
	popa
	ret
EndProc _TVCache_MakeMRU


BeginProc _TVCache_Hold
	pusha
	pushfd
	push	esi
	call	_CTVCACHE_Hold
	add	esp, 4
	popfd
	inc	eax
	popa
	ret
EndProc _TVCache_Hold


BeginProc _TVCache_Unhold
	pusha
	pushfd
	push	esi
	call	_CTVCACHE_Unhold
	add	esp, 4
	popfd
	dec	eax
	popa
	ret
EndProc _TVCache_Unhold


BeginProc _TVCache_Enum
	pusha
	push	ebp			; ref 3
	push	ecx			; ref 2
	push	ebx			; ref 1
	push	edx			; callback function
	movzx	eax, ah
	push	eax			; fsd id
	call	_CTVCACHE_Enum
	add	esp, 20
	popa
	ret
EndProc _TVCache_Enum


BeginProc _TVCache_TestHandle
	ret
EndProc _TVCache_TestHandle


BeginProc _TVCache_VerifySums
	ret
EndProc _TVCache_VerifySums


BeginProc _TVCache_RecalcSums
	ret
EndProc _TVCache_RecalcSums


BeginProc _TVCache_TestHold	
	push	ecx
	push	edx
	push	esi
	push	edi
	push	esi
	call	_CTVCACHE_TestHold
	add	esp, 4
	or	eax, eax
	pop	edi
	pop	esi
	pop	edx
	pop	ecx
	ret
EndProc _TVCache_TestHold


BeginProc _TVCache_GetStats
	push	eax
	push	ebp
	push	esi
	mov	ebp, esp
	sub	esp, 16
	lea	eax, [ebp-4]		; discarded blocks
	push	eax
	lea	eax, [ebp-8]		; base address
	push	eax
	lea	eax, [ebp-12]		; hits
	push	eax
	lea	eax, [ebp-16]		; misses
	push	eax
	call	_CTVCACHE_GetStats
	add	esp, 16
	pop	ebx			; misses
	pop	ecx			; hits
	pop	edx			; base
	pop	edi			; discards
	mov	esp, ebp
	pop	esi
	pop	ebp
	pop	eax
	ret
EndProc _TVCache_GetStats


BeginProc _TVCache_Deregister
	pusha
	push	eax
	call	_CTVCACHE_Deregister
	add	esp, 4
	popa
	ret
EndProc _TVCache_Deregister


BeginProc _TVCache_AdjustMinimum
	movzx   eax, ah			; fsd id
	push	eax
	push	ecx			; quota
	call	_CTVCACHE_AdjustMinimum
	add	esp, 8
	cmp	eax, 1
	ret
EndProc _TVCache_AdjustMinimum


BeginProc _TVCache_SwapBuffers
	pusha
	push	edi
	push	esi
	call	_CTVCACHE_SwapBuffers
	add	esp, 8
	cmp	eax, 1
	popa
	ret
EndProc _TVCache_SwapBuffers


BeginProc _TVCache_RelinquishPage
	push	ecx
	push	edx
	call	_CTVCACHE_RelinquishPage
	pop	edx
	pop	ecx
	ret
EndProc _TVCache_RelinquishPage


BeginProc _TVCache_UseThisPage
	pusha
	push	eax
	call	_CTVCACHE_UseThisPage
	add	esp, 4
	popa
	ret
EndProc _TVCache_UseThisPage

;----------------------------------------------------------------------
;
; EnumCallback
;
; This is the only non-API wrapper. It is used to call the specified
; callback during enum functions.
;
;----------------------------------------------------------------------
Public EnumCallback
EnumCallback PROC C USES eax ebx ecx edx esi edi ebp,
	callback: DWORD,
	blockid : DWORD,
	refebx  : DWORD,
	refecx  : DWORD,
	refebp  : DWORD,
	hold    : DWORD

	mov	esi, blockid
	mov	edx, callback
	mov	edi, hold
	mov	eax, refebx
	mov	ebx, [eax]
	mov	eax, refecx
	mov	ecx, [eax]
	mov	eax, refebp
	push	ebp
	mov	ebp, [eax]
	cmp	edi, 1
	
	; make the call

	call	edx
	
	mov	edx, ebp
	pop	ebp
	mov	eax, refebp
	mov	[eax], edx
	mov	eax, refebx
	mov	[eax], ebx
	mov	eax, refecx
	mov	[eax], ecx
	ret
	
EnumCallback ENDP


BeginProc __TVCache_CreateLookupCache
	xor	eax, eax
	ret
EndProc __TVCache_CreateLookupCache

BeginProc  __TVCache_CloseLookupCache
	ret
EndProc  __TVCache_CloseLookupCache

BeginProc  __TVCache_DeleteLookupCache
	ret
EndProc  __TVCache_DeleteLookupCache

BeginProc  __TVCache_Lookup
	ret
EndProc  __TVCache_Lookup

BeginProc  __TVCache_UpdateLookup
	ret
EndProc  __TVCache_UpdateLookup



VXD_LOCKED_CODE_ENDS

end