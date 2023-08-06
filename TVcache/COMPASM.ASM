;======================================================================
; 
; COMPASM.asm	
;
; Copyright (c) 1995 Mark Russinovich and Bryce Cogswell	
;
; This code is free to use and modify in whatever manner you wish.
;
; Highly optimized assembly language compress/decompress
; for use when running vxd.
; 
; NOTE: in the mask byte, a 0 indicates a hit, while a 1 is a mis
; prediction.
;
;======================================================================
.386p
	NO_SEGMENTS=1
	include vmm.inc
	include vsegment.inc
.list

VXD_LOCKED_DATA_SEG

; prediction table 
ApcTable	db	1000h dup(0)

VXD_LOCKED_DATA_ENDS

VXD_LOCKED_CODE_SEG

;----------------------------------------------------------------------
;
; DecompressByte
;
; Macro for decompressing.
;
; Register usage:
;	EBX:	previous two bytes
;	DL:	hit mask
;	ESI:	current source pointer
;	EDI:	current destination pointer
;
;----------------------------------------------------------------------
DecompressByte	MACRO	
		LOCAL	hit, miss 

		shl	dl, 1
		jnc	hit			; was it a hit
		lodsb	
		mov	ApcTable[ebx], al	; no, update pred table
		jmp	miss
hit:		mov	al, ApcTable[ebx]	; get predicted value
miss:		shl	bx, 6			; create new pred value
		xor	bl, al
		and	bh, 0Fh
		mov	[edi], al		; place in output buffer
		inc	edi
		ENDM

	
;----------------------------------------------------------------------
;
; Decompress
;
; Interfaces with C code to provide super-optimized decompression
; algorithm. Based highly on original FIN Dr. Dobb's decompression
; algorithm. 
;
;----------------------------------------------------------------------
Public ADecompress
ADecompress PROC C USES ebx ecx edx esi edi,
	inbuf_orig:	DWORD,
	outbuf_len:	DWORD,
	outbuf_orig:	DWORD,
	init:		BYTE

	; is it a zero-len buffer?

	cld
	xor	eax, eax
	cmp	outbuf_len, 0
	je	dcexit

	; zero prediction table

	cmp	init, 0
	je	dcnoclear
	mov	ecx, 1000h		; 4K bytes
	shr	ecx, 2			; 4 bytes cleared each loop
	mov	edi, offset ApcTable
	xor	eax, eax
	rep	stosd	
	xor	ebx, ebx		; initialize prediction value
	jmp	dcinitparams

dcnoclear:
	xor	ebx, ebx
	mov	edx, inbuf_orig
	mov	bl, [edx-3]		; setup prediction value
	shl 	bx, 6
	xor	bl, [edx-2]
	shl	bx, 6
	xor	bl, [edx-1]
	and	bh, 0Fh

	; set up parameters
	
dcinitparams:
	mov	esi, inbuf_orig
	mov	edi, outbuf_orig
	mov	ecx, outbuf_len
	shr	ecx, 3			; do 8 byte blocks

	; enter main loop

DecompressBlock:
	lodsb				; get mask byte
	mov	dl, al
	DecompressByte	
	DecompressByte	
	DecompressByte
	DecompressByte	
	DecompressByte	
	DecompressByte	
	DecompressByte	
	DecompressByte	
	dec	ecx
	jnz	DecompressBlock

	; return the amount decompressed

	sub	esi, inbuf_orig
	mov	eax, esi
dcexit:
	ret

ADecompress ENDP


;----------------------------------------------------------------------
;
; CompressByte
;
; This is a macro for processing the current byte on compression.
;
; Register usage:
;	EBX: 	previous two bytes
;	DL: 	accumulated hit mask
;	ESI:	current source pointer
;	EDI:	current destination pointer
;
;----------------------------------------------------------------------
CompressByte	MACRO	curbyte:REQ, bitval: REQ
		LOCAL	hit

		cmp	curbyte, ApcTable[ebx]	; is it a correct prediction?
		je	hit			; yep
		or	dl, bitval		; no, so update table
		mov	ApcTable[ebx], curbyte	; update prediction table
		mov	[edi], curbyte		; copy to destination
		inc	edi			; increment dest pointer
hit:		shl	bx, 6			; create new pred value
		xor	bl, curbyte
		and	bh, 0Fh
		ENDM


;----------------------------------------------------------------------
;
; Compress
;
; Interfaces with C code to provide super-optimized compression
; algorithm. Based highly on original FIN Dr. Dobb's compression
; algorithm. 
;
;----------------------------------------------------------------------
Public ACompress
ACompress PROC C USES ebx ecx edx esi edi,
	inbuf: 		DWORD,
	inbuf_len: 	DWORD,
	outbuf_orig:	DWORD,
	init:		BYTE

	; is it a zero-len buffer?

	cld
	xor	eax, eax
	cmp	inbuf_len, 0
	je	cexit

	; first, zero prediction table

	cmp	init, 0
	jz	cnoclear
	mov	ecx, 1000h		; 4K byte table
	shr	ecx, 2			; 4 bytes cleared each loop
	mov	edi, offset ApcTable
	xor	eax, eax
	rep	stosd
	xor	ebx, ebx		; initialize prediction value
	jmp	cinitparams
	
cnoclear:
	xor	ebx, ebx
	mov	edx, inbuf
	mov	bl, [edx-3]		; set-up prediction value
	shl	bx, 6
	xor	bl, [edx-2]
	shl	bx, 6
	xor	bl, [edx-1]
	and	bh, 0Fh
	
	; set up parameters

cinitparams:
	mov	esi, inbuf
	mov	edi, outbuf_orig
	mov	ecx, inbuf_len
	shr	ecx, 5			; divide by 8 for 8 byte blocks
	push	ebp

	; enter the main loop
	
CompressBlock:

	mov	ebp, edi		; save location of mask byte
	inc	edi			; skip over mask byte
	xor	dl, dl			; zero bit mask
	lodsw
	CompressByte al, 80h
	CompressByte ah, 40h
	lodsw
	CompressByte al, 20h
	CompressByte ah, 10h
	lodsw
	CompressByte al, 08h
	CompressByte ah, 04h
	lodsw
	CompressByte al, 02h
	CompressByte ah, 01h
	mov	[ebp], dl		; save bit mask

	mov	ebp, edi		; save location of mask byte
	inc	edi			; skip over mask byte
	xor	dl, dl			; zero bit mask
	lodsw
	CompressByte al, 80h
	CompressByte ah, 40h
	lodsw
	CompressByte al, 20h
	CompressByte ah, 10h
	lodsw
	CompressByte al, 08h
	CompressByte ah, 04h
	lodsw
	CompressByte al, 02h
	CompressByte ah, 01h
	mov	[ebp], dl		; save bit mask

	mov	ebp, edi		; save location of mask byte
	inc	edi			; skip over mask byte
	xor	dl, dl			; zero bit mask
	lodsw
	CompressByte al, 80h
	CompressByte ah, 40h
	lodsw
	CompressByte al, 20h
	CompressByte ah, 10h
	lodsw
	CompressByte al, 08h
	CompressByte ah, 04h
	lodsw
	CompressByte al, 02h
	CompressByte ah, 01h
	mov	[ebp], dl		; save bit mask

	mov	ebp, edi		; save location of mask byte
	inc	edi			; skip over mask byte
	xor	dl, dl			; zero bit mask
	lodsw
	CompressByte al, 80h
	CompressByte ah, 40h
	lodsw
	CompressByte al, 20h
	CompressByte ah, 10h
	lodsw
	CompressByte al, 08h
	CompressByte ah, 04h
	lodsw
	CompressByte al, 02h
	CompressByte ah, 01h
	mov	[ebp], dl		; save bit mask

	dec	ecx
	jnz	CompressBlock		; go for another round
	
	; calculate size of output 
	
	pop	ebp
	sub	edi, outbuf_orig
	mov	eax, edi		; return compressed length
cexit:
	ret

ACompress ENDP


VXD_LOCKED_CODE_ENDS

end
