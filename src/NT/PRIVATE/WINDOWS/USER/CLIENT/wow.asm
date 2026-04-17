	TITLE	Z:\home\user\Projects\HarryR\MicroNT\src\NT\PRIVATE\WINDOWS\USER\CLIENT\wow.c
	.386P
include listing.inc
if @Version gt 510
.model FLAT
else
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
_DATA	SEGMENT DWORD USE32 PUBLIC 'DATA'
_DATA	ENDS
CONST	SEGMENT DWORD USE32 PUBLIC 'CONST'
CONST	ENDS
_BSS	SEGMENT DWORD USE32 PUBLIC 'BSS'
_BSS	ENDS
_TLS	SEGMENT DWORD USE32 PUBLIC 'TLS'
_TLS	ENDS
;	COMDAT _NtCurrentTeb@0
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _SetCallServerFlag@0
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _SetLastErrorEx@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _WOWRtlCopyMemory@12
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _WOWlstrlenA@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _PtiCurrent@0
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _HMValidateHandle@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _HMValidateHandleNoRip@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetPrevPwnd@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __GetKeyState@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _LookupMenuItem@16
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _ClientValidateHandle@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetClassNameA@12
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __GetDesktopWindow@0
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetDesktopWindow@0
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetDlgItem@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetKeyboardState@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetKeyState@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetMenu@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __GetMenuItemCount@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetMenuItemCount@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __GetMenuItemID@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetMenuItemID@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __GetMenuState@12
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetMenuState@12
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __GetWindow@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _IsWindow@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetWindow@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __GetParent@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetParent@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __GetSubMenu@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetSubMenu@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetSysColor@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetSystemMetrics@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __GetTopWindow@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetTopWindow@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __IsChild@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _IsChild@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __IsIconic@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _IsIconic@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __IsWindowEnabled@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _IsWindowEnabled@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __IsWindowVisible@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _IsWindowVisible@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __IsZoomed@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _IsZoomed@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __ClientToScreen@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _ClientToScreen@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __GetClientRect@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetClientRect@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetCursorPos@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __GetWindowRect@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _GetWindowRect@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __ScreenToClient@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _ScreenToClient@8
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _EnableMenuItem@12
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _PctiCurrent@0
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT __PhkNext@4
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _CallNextHookEx@16
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
;	COMDAT _WOW16DefHookProc@16
_TEXT	SEGMENT PARA USE32 PUBLIC 'CODE'
_TEXT	ENDS
FLAT	GROUP _DATA, CONST, _BSS
	ASSUME	CS: FLAT, DS: FLAT, SS: FLAT
endif
PUBLIC	_HMValidateHandle@8
EXTRN	_wow16gpsi:DWORD
;	COMDAT _HMValidateHandle@8
_TEXT	SEGMENT
; File Z:\home\user\Projects\HarryR\MicroNT\src\NT\PRIVATE\WINDOWS\USER\CLIENT\wow.c
_h$ = 8
_bType$ = 12
_HMValidateHandle@8 PROC NEAR				; COMDAT
; Line 227
	push	ebp
	mov	ebp, esp
	mov	eax, DWORD PTR _wow16gpsi
	mov	edx, DWORD PTR _h$[ebp]
; Line 236
	mov	ecx, edx
	and	ecx, 65535				; 0000ffffH
	cmp	DWORD PTR [eax+8], ecx
	jbe	SHORT $L16330
	mov	eax, DWORD PTR [eax+4]
	imul	ecx, 12					; 0000000cH
	add	eax, ecx
	shr	edx, 16					; 00000010H
	cmp	WORD PTR [eax+10], dx
	je	SHORT $L15917
	test	dx, dx
	je	SHORT $L15917
	cmp	dx, 65535				; 0000ffffH
	jne	SHORT $L16330
$L15917:
	mov	cl, BYTE PTR _bType$[ebp]
	cmp	BYTE PTR [eax+8], cl
	jne	SHORT $L15918
	mov	eax, DWORD PTR [eax]
	jmp	SHORT $L15910
$L15918:
	cmp	cl, 255					; 000000ffH
	jne	SHORT $L15915
	mov	eax, DWORD PTR [eax]
	jmp	SHORT $L15910
$L16330:
	mov	cl, BYTE PTR _bType$[ebp]
; Line 238
$L15915:
; Line 274
	xor	eax, eax
; Line 275
$L15910:
	pop	ebp
	ret	8
_HMValidateHandle@8 ENDP
_TEXT	ENDS
PUBLIC	_HMValidateHandleNoRip@8
;	COMDAT _HMValidateHandleNoRip@8
_TEXT	SEGMENT
_h$ = 8
_bType$ = 12
_HMValidateHandleNoRip@8 PROC NEAR			; COMDAT
; Line 281
	push	ebp
	mov	ebp, esp
	mov	eax, DWORD PTR _wow16gpsi
	mov	edx, DWORD PTR _h$[ebp]
; Line 288
	mov	ecx, edx
	and	ecx, 65535				; 0000ffffH
	cmp	DWORD PTR [eax+8], ecx
	jbe	SHORT $L15937
	mov	eax, DWORD PTR [eax+4]
	imul	ecx, 12					; 0000000cH
	add	eax, ecx
	shr	edx, 16					; 00000010H
	cmp	WORD PTR [eax+10], dx
	je	SHORT $L15939
	test	dx, dx
	je	SHORT $L15939
	cmp	dx, 65535				; 0000ffffH
	jne	SHORT $L15937
$L15939:
	mov	cl, BYTE PTR _bType$[ebp]
	cmp	BYTE PTR [eax+8], cl
	jne	SHORT $L15940
	mov	eax, DWORD PTR [eax]
	jmp	SHORT $L15933
$L15940:
	cmp	cl, 255					; 000000ffH
	jne	SHORT $L15937
	mov	eax, DWORD PTR [eax]
	jmp	SHORT $L15933
; Line 289
$L15937:
	xor	eax, eax
; Line 290
$L15933:
	pop	ebp
	ret	8
_HMValidateHandleNoRip@8 ENDP
_TEXT	ENDS
PUBLIC	_GetPrevPwnd@8
;	COMDAT _GetPrevPwnd@8
_TEXT	SEGMENT
_pwndList$ = 8
_pwndFind$ = 12
_GetPrevPwnd@8 PROC NEAR				; COMDAT
; Line 304
	push	ebp
	mov	ebp, esp
; Line 307
	mov	eax, DWORD PTR _pwndList$[ebp]
	test	eax, eax
	jne	SHORT $L15947
; Line 308
	xor	eax, eax
	jmp	SHORT $L15944
; Line 310
$L15947:
	mov	eax, DWORD PTR [eax+24]
	test	eax, eax
	jne	SHORT $L15948
; Line 311
	xor	eax, eax
	jmp	SHORT $L15944
; Line 313
$L15948:
	mov	ecx, DWORD PTR [eax+28]
; Line 314
	xor	edx, edx
; Line 316
	mov	eax, DWORD PTR _pwndFind$[ebp]
	cmp	ecx, eax
	je	SHORT $L15951
$L15950:
	test	ecx, ecx
	je	SHORT $L15951
; Line 317
	mov	edx, ecx
; Line 318
	mov	ecx, DWORD PTR [ecx+20]
; Line 319
	cmp	ecx, eax
	jne	SHORT $L15950
$L15951:
; Line 321
	sub	ecx, eax
	cmp	ecx, 1
	sbb	eax, eax
	and	eax, edx
; Line 322
$L15944:
	pop	ebp
	ret	8
_GetPrevPwnd@8 ENDP
_TEXT	ENDS
PUBLIC	__GetKeyState@4
;	COMDAT __GetKeyState@4
_TEXT	SEGMENT
_vk$ = 8
__GetKeyState@4 PROC NEAR				; COMDAT
; Line 339
	push	ebp
	mov	ebp, esp
	push	ebx
	push	esi
; Line 343
	mov	ecx, DWORD PTR _vk$[ebp]
	cmp	ecx, 256				; 00000100H
	jl	SHORT $L15956
; Line 345
	xor	ax, ax
	jmp	SHORT $L15953
; Line 348
$L15956:
	mov	eax, DWORD PTR fs:64
; Line 349
	test	eax, eax
	jne	SHORT $L15957
; Line 350
	xor	ax, ax
	jmp	SHORT $L15953
; Line 359
$L15957:
	xor	edx, edx
	mov	eax, DWORD PTR [eax+28]
	mov	esi, ecx
	sar	esi, 2
	movzx	esi, BYTE PTR [eax+esi+132]
	and	cl, 3
	lea	ebx, DWORD PTR [ecx*2]
; Line 364
	mov	eax, 1
	lea	ecx, DWORD PTR [ebx+1]
	shl	eax, cl
	test	eax, esi
	je	SHORT $L15958
; Line 365
	mov	edx, 1
; Line 370
$L15958:
	mov	eax, 1
	mov	cl, bl
	shl	eax, cl
	test	eax, esi
	je	SHORT $L15959
; Line 371
	or	dh, 128					; 00000080H
; Line 373
$L15959:
	mov	ax, dx
; Line 374
$L15953:
	pop	esi
	pop	ebx
	pop	ebp
	ret	4
__GetKeyState@4 ENDP
_TEXT	ENDS
PUBLIC	_LookupMenuItem@16
;	COMDAT _LookupMenuItem@16
_TEXT	SEGMENT
_pMenu$ = 8
_wCmd$ = 12
_dwFlags$ = 16
_ppMenuItemIsOn$ = 20
_i$ = -4
_LookupMenuItem@16 PROC NEAR				; COMDAT
; Line 391
	push	ebp
	mov	ebp, esp
	sub	esp, 4
	xor	eax, eax
	push	ebx
	push	esi
	push	edi
; Line 396
	cmp	DWORD PTR _pMenu$[ebp], eax
	je	SHORT $L15969
	mov	esi, DWORD PTR _wCmd$[ebp]
	cmp	esi, -1
	je	SHORT $L15969
; Line 404
	test	BYTE PTR _dwFlags$[ebp+1], 4
; Line 405
	mov	ecx, DWORD PTR _pMenu$[ebp]
	je	SHORT $L15970
	cmp	DWORD PTR [ecx+40], esi
	jbe	SHORT $L15964
; Line 406
	mov	eax, DWORD PTR [ecx+64]
	imul	esi, 52					; 00000034H
	add	eax, esi
; Line 407
	mov	edi, DWORD PTR _ppMenuItemIsOn$[ebp]
	test	edi, edi
	je	SHORT $L15964
; Line 408
	mov	DWORD PTR [edi], ecx
; Line 410
	jmp	SHORT $L15964
$L15970:
	mov	ecx, DWORD PTR [ecx+40]
	mov	DWORD PTR _i$[ebp], ecx
; Line 418
	mov	ecx, DWORD PTR _pMenu$[ebp]
	mov	edx, DWORD PTR _i$[ebp]
	mov	ecx, DWORD PTR [ecx+64]
	imul	edx, 52					; 00000034H
	lea	ebx, DWORD PTR [ecx+edx-52]
	mov	edi, DWORD PTR _ppMenuItemIsOn$[ebp]
$L15974:
	mov	ecx, DWORD PTR _i$[ebp]
	dec	DWORD PTR _i$[ebp]
	test	ecx, ecx
	je	SHORT $L15964
; Line 423
	test	BYTE PTR [ebx], 16			; 00000010H
	je	SHORT $L15977
; Line 425
	push	edi
	push	DWORD PTR _dwFlags$[ebp]
	push	esi
	push	DWORD PTR [ebx+4]
	call	_LookupMenuItem@16
; Line 426
	jmp	SHORT $L15975
$L15977:
; Line 431
	cmp	DWORD PTR [ebx+4], esi
	jne	SHORT $L15975
; Line 436
	mov	eax, ebx
; Line 437
	test	edi, edi
	je	SHORT $L15975
; Line 438
	mov	ecx, DWORD PTR _pMenu$[ebp]
	mov	DWORD PTR [edi], ecx
; Line 418
$L15975:
	sub	ebx, 52					; 00000034H
	test	eax, eax
	je	SHORT $L15974
; Line 444
	jmp	SHORT $L15964
; Line 396
$L15969:
; Line 398
	xor	eax, eax
; Line 445
$L15964:
	pop	edi
	pop	esi
	pop	ebx
	mov	esp, ebp
	pop	ebp
	ret	16					; 00000010H
_LookupMenuItem@16 ENDP
_TEXT	ENDS
PUBLIC	_ClientValidateHandle@8
;	COMDAT _ClientValidateHandle@8
_TEXT	SEGMENT
_handle$ = 8
_btype$ = 12
_ClientValidateHandle@8 PROC NEAR			; COMDAT
; Line 460
	push	ebp
	mov	ebp, esp
; Line 469
	push	DWORD PTR _btype$[ebp]
	push	DWORD PTR _handle$[ebp]
	call	_HMValidateHandle@8
; Line 470
	pop	ebp
	ret	8
_ClientValidateHandle@8 ENDP
_TEXT	ENDS
PUBLIC	_GetClassNameA@12
;	COMDAT _GetClassNameA@12
_TEXT	SEGMENT
_hwnd$ = 8
_lpClassName$ = 12
_nMaxCount$ = 16
_GetClassNameA@12 PROC NEAR				; COMDAT
; Line 478
	push	ebp
	mov	ebp, esp
	push	esi
	push	edi
; Line 483
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 485
	test	eax, eax
	jne	SHORT $L15991
; Line 486
	xor	eax, eax
	jmp	SHORT $L15987
; Line 488
$L15991:
	mov	edx, DWORD PTR _nMaxCount$[ebp]
	test	edx, edx
	je	SHORT $L15992
; Line 489
	mov	eax, DWORD PTR [eax+84]
	mov	esi, DWORD PTR [eax+100]
; Line 490
	push	ds
	pop	es
	mov	edi, esi
	mov	ecx, -1
	sub	eax, eax
	repnz	scasb
	not	ecx
	dec	ecx
; Line 491
	dec	edx
	cmp	edx, ecx
	jl	SHORT $L16341
	mov	edx, ecx
$L16341:
; Line 492
	push	ds
	pop	es
	mov	eax, DWORD PTR _lpClassName$[ebp]
	mov	edi, eax
	mov	ecx, edx
	shr	ecx, 2
	rep	movsd
	mov	ecx, edx
	and	ecx, 3
	rep	movsb
; Line 493
	mov	BYTE PTR [eax+edx], 0
; Line 496
$L15992:
	mov	eax, edx
; Line 497
$L15987:
	pop	edi
	pop	esi
	pop	ebp
	ret	12					; 0000000cH
_GetClassNameA@12 ENDP
_TEXT	ENDS
PUBLIC	__GetDesktopWindow@0
;	COMDAT __GetDesktopWindow@0
_TEXT	SEGMENT
__GetDesktopWindow@0 PROC NEAR				; COMDAT
; Line 510
	mov	eax, DWORD PTR fs:64
; Line 513
	test	eax, eax
	jne	SHORT $L15996
; Line 514
	xor	eax, eax
	jmp	SHORT $L15993
; Line 516
$L15996:
	mov	eax, DWORD PTR [eax+72]
; Line 517
	test	eax, eax
	jne	SHORT $L16342
	xor	eax, eax
	jmp	SHORT $L15993
$L16342:
	mov	eax, DWORD PTR [eax+40]
; Line 518
$L15993:
	ret	0
__GetDesktopWindow@0 ENDP
_TEXT	ENDS
PUBLIC	_GetDesktopWindow@0
;	COMDAT _GetDesktopWindow@0
_TEXT	SEGMENT
_GetDesktopWindow@0 PROC NEAR				; COMDAT
; Line 526
	call	__GetDesktopWindow@0
; Line 527
	test	eax, eax
	jne	SHORT $L16345
	xor	eax, eax
	jmp	SHORT $L15997
$L16345:
	mov	eax, DWORD PTR [eax]
; Line 528
$L15997:
	ret	0
_GetDesktopWindow@0 ENDP
_TEXT	ENDS
PUBLIC	_GetDlgItem@8
;	COMDAT _GetDlgItem@8
_TEXT	SEGMENT
_id$ = 12
_hwnd$ = 8
_GetDlgItem@8 PROC NEAR					; COMDAT
; Line 534
	push	ebp
	mov	ebp, esp
; Line 538
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 539
	test	eax, eax
	jne	SHORT $L16004
; Line 540
	xor	eax, eax
	jmp	SHORT $L16348
; Line 542
$L16004:
	mov	ecx, DWORD PTR [eax+28]
; Line 543
	test	ecx, ecx
	je	SHORT $L16352
	mov	eax, DWORD PTR _id$[ebp]
$L16006:
	cmp	DWORD PTR [ecx+112], eax
	je	SHORT $L16007
; Line 544
	mov	ecx, DWORD PTR [ecx+20]
	test	ecx, ecx
	jne	SHORT $L16006
$L16007:
; Line 546
	test	ecx, ecx
	jne	SHORT $L16347
$L16352:
	xor	eax, eax
	jmp	SHORT $L16348
$L16347:
	mov	eax, DWORD PTR [ecx]
$L16348:
; Line 552
	pop	ebp
	ret	8
_GetDlgItem@8 ENDP
_TEXT	ENDS
PUBLIC	_GetKeyboardState@4
;	COMDAT _GetKeyboardState@4
_TEXT	SEGMENT
_pb$ = 8
_pq$ = -4
_GetKeyboardState@4 PROC NEAR				; COMDAT
; Line 568
	push	ebp
	mov	ebp, esp
	sub	esp, 4
	push	ebx
	push	esi
	push	edi
; Line 573
	mov	eax, DWORD PTR fs:64
; Line 574
	test	eax, eax
	jne	SHORT $L16014
; Line 575
	xor	eax, eax
	jmp	SHORT $L16010
; Line 577
$L16014:
	mov	eax, DWORD PTR [eax+28]
	mov	DWORD PTR _pq$[ebp], eax
; Line 579
	xor	esi, esi
	mov	edx, DWORD PTR _pb$[ebp]
$L16015:
; Line 580
	mov	BYTE PTR [edx], 0
	mov	ecx, esi
	sar	ecx, 2
	mov	eax, DWORD PTR _pq$[ebp]
	lea	eax, DWORD PTR [ecx+eax+132]
	mov	ecx, esi
	and	cl, 3
	add	cl, cl
; Line 581
	movzx	edi, BYTE PTR [eax]
	mov	ebx, 1
	shl	ebx, cl
	test	edi, ebx
	je	SHORT $L16018
; Line 582
	mov	BYTE PTR [edx], 128			; 00000080H
; Line 584
$L16018:
	movzx	eax, BYTE PTR [eax]
	mov	edi, 1
	inc	cl
	shl	edi, cl
	test	eax, edi
	je	SHORT $L16016
; Line 585
	or	BYTE PTR [edx], 1
; Line 579
$L16016:
	inc	esi
	inc	edx
	cmp	esi, 256				; 00000100H
	jl	SHORT $L16015
; Line 588
	mov	eax, 1
; Line 589
$L16010:
	pop	edi
	pop	esi
	pop	ebx
	mov	esp, ebp
	pop	ebp
	ret	4
_GetKeyboardState@4 ENDP
_TEXT	ENDS
PUBLIC	_GetKeyState@4
EXTRN	_wow16CsrFlag:DWORD
;	COMDAT _GetKeyState@4
_TEXT	SEGMENT
_nVirtKey$ = 8
_GetKeyState@4 PROC NEAR				; COMDAT
; Line 594
	push	ebp
	mov	ebp, esp
; Line 595
	mov	eax, DWORD PTR fs:64
; Line 597
	test	eax, eax
	jne	SHORT $L16023
; Line 598
	xor	ax, ax
	jmp	SHORT $L16021
; Line 603
$L16023:
	mov	eax, DWORD PTR [eax+28]
	test	BYTE PTR [eax+248], 1
	je	SHORT $L16024
; Line 604
	mov	eax, DWORD PTR _wow16CsrFlag
	mov	BYTE PTR [eax], 1
	xor	ax, ax
	jmp	SHORT $L16021
$L16024:
; Line 606
	push	DWORD PTR _nVirtKey$[ebp]
	call	__GetKeyState@4
; Line 607
$L16021:
	pop	ebp
	ret	4
_GetKeyState@4 ENDP
_TEXT	ENDS
PUBLIC	_GetMenu@4
;	COMDAT _GetMenu@4
_TEXT	SEGMENT
_hwnd$ = 8
_GetMenu@4 PROC NEAR					; COMDAT
; Line 612
	push	ebp
	mov	ebp, esp
; Line 615
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 617
	test	eax, eax
	jne	SHORT $L16028
; Line 618
	xor	eax, eax
	jmp	SHORT $L16029
; Line 624
$L16028:
	mov	cl, BYTE PTR [eax+163]
	and	cl, 192					; 000000c0H
	cmp	cl, 64					; 00000040H
	mov	eax, DWORD PTR [eax+112]
	je	SHORT $L16029
; Line 625
	test	eax, eax
	jne	SHORT $L16358
	xor	eax, eax
	jmp	SHORT $L16029
$L16358:
	mov	eax, DWORD PTR [eax]
; Line 626
$L16029:
	pop	ebp
	ret	4
_GetMenu@4 ENDP
_TEXT	ENDS
PUBLIC	__GetMenuItemCount@4
;	COMDAT __GetMenuItemCount@4
_TEXT	SEGMENT
_pMenu$ = 8
__GetMenuItemCount@4 PROC NEAR				; COMDAT
; Line 643
	push	ebp
	mov	ebp, esp
; Line 644
	mov	eax, DWORD PTR _pMenu$[ebp]
	test	eax, eax
	je	SHORT $L16033
; Line 645
	mov	eax, DWORD PTR [eax+40]
	jmp	SHORT $L16032
; Line 647
$L16033:
; Line 648
	mov	eax, -1
; Line 649
$L16032:
	pop	ebp
	ret	4
__GetMenuItemCount@4 ENDP
_TEXT	ENDS
PUBLIC	_GetMenuItemCount@4
;	COMDAT _GetMenuItemCount@4
_TEXT	SEGMENT
_hMenu$ = 8
_GetMenuItemCount@4 PROC NEAR				; COMDAT
; Line 653
	push	ebp
	mov	ebp, esp
; Line 656
	push	2
	push	DWORD PTR _hMenu$[ebp]
	call	_ClientValidateHandle@8
; Line 658
	test	eax, eax
	jne	SHORT $L16037
; Line 659
	mov	eax, -1
	jmp	SHORT $L16035
; Line 661
$L16037:
	push	eax
	call	__GetMenuItemCount@4
; Line 662
$L16035:
	pop	ebp
	ret	4
_GetMenuItemCount@4 ENDP
_TEXT	ENDS
PUBLIC	__GetMenuItemID@8
;	COMDAT __GetMenuItemID@8
_TEXT	SEGMENT
_pMenu$ = 8
_nPos$ = 12
__GetMenuItemID@8 PROC NEAR				; COMDAT
; Line 676
	push	ebp
	mov	ebp, esp
; Line 678
	mov	eax, -1
; Line 684
	mov	ecx, DWORD PTR _pMenu$[ebp]
	mov	edx, DWORD PTR _nPos$[ebp]
	cmp	DWORD PTR [ecx+40], edx
	jle	SHORT $L16043
	test	edx, edx
	jl	SHORT $L16043
; Line 685
	mov	ecx, DWORD PTR [ecx+64]
	imul	edx, 52					; 00000034H
	add	ecx, edx
; Line 686
	test	BYTE PTR [ecx], 16			; 00000010H
	jne	SHORT $L16043
; Line 687
	mov	eax, DWORD PTR [ecx+4]
; Line 690
$L16043:
; Line 691
	pop	ebp
	ret	8
__GetMenuItemID@8 ENDP
_TEXT	ENDS
PUBLIC	_GetMenuItemID@8
;	COMDAT _GetMenuItemID@8
_TEXT	SEGMENT
_hMenu$ = 8
_nPos$ = 12
_GetMenuItemID@8 PROC NEAR				; COMDAT
; Line 697
	push	ebp
	mov	ebp, esp
; Line 700
	push	2
	push	DWORD PTR _hMenu$[ebp]
	call	_ClientValidateHandle@8
; Line 702
	test	eax, eax
	jne	SHORT $L16049
; Line 703
	mov	eax, -1
	jmp	SHORT $L16047
; Line 705
$L16049:
	push	DWORD PTR _nPos$[ebp]
	push	eax
	call	__GetMenuItemID@8
; Line 706
$L16047:
	pop	ebp
	ret	8
_GetMenuItemID@8 ENDP
_TEXT	ENDS
PUBLIC	__GetMenuState@12
;	COMDAT __GetMenuState@12
_TEXT	SEGMENT
_pMenu$ = 8
_wId$ = 12
_dwFlags$ = 16
__GetMenuState@12 PROC NEAR				; COMDAT
; Line 723
	push	ebp
	mov	ebp, esp
; Line 730
	push	0
	push	DWORD PTR _dwFlags$[ebp]
	push	DWORD PTR _wId$[ebp]
	push	DWORD PTR _pMenu$[ebp]
	call	_LookupMenuItem@16
	test	eax, eax
	jne	SHORT $L16056
; Line 731
	mov	eax, -1
	jmp	SHORT $L16053
; Line 736
$L16056:
; Line 738
	mov	ecx, DWORD PTR [eax]
	and	ecx, 2303				; 000008ffH
; Line 740
	test	cl, 16					; 00000010H
	je	SHORT $L16057
; Line 751
	mov	eax, DWORD PTR [eax+4]
	and	ch, -9					; fffffff7H
	mov	eax, DWORD PTR [eax+40]
	shl	eax, 8
	or	ecx, eax
; Line 754
$L16057:
	mov	eax, ecx
; Line 755
$L16053:
	pop	ebp
	ret	12					; 0000000cH
__GetMenuState@12 ENDP
_TEXT	ENDS
PUBLIC	_GetMenuState@12
;	COMDAT _GetMenuState@12
_TEXT	SEGMENT
_hMenu$ = 8
_uId$ = 12
_uFlags$ = 16
_GetMenuState@12 PROC NEAR				; COMDAT
; Line 762
	push	ebp
	mov	ebp, esp
; Line 765
	push	2
	push	DWORD PTR _hMenu$[ebp]
	call	_ClientValidateHandle@8
; Line 767
	test	eax, eax
	je	SHORT $L16064
	mov	ecx, DWORD PTR _uFlags$[ebp]
	test	ecx, -65536				; ffff0000H
	jne	SHORT $L16064
; Line 771
	push	ecx
	push	DWORD PTR _uId$[ebp]
	push	eax
	call	__GetMenuState@12
	jmp	SHORT $L16061
; Line 767
$L16064:
; Line 768
	mov	eax, -1
; Line 772
$L16061:
	pop	ebp
	ret	12					; 0000000cH
_GetMenuState@12 ENDP
_TEXT	ENDS
PUBLIC	__GetWindow@8
EXTRN	_GetAppCompatFlags@4:NEAR
;	COMDAT __GetWindow@8
_TEXT	SEGMENT
_pwndStart$ = 8
_cmd$ = 12
__GetWindow@8 PROC NEAR					; COMDAT
; Line 788
	push	ebp
	mov	ebp, esp
	push	esi
	push	edi
; Line 793
	mov	edi, DWORD PTR _cmd$[ebp]
	test	edi, edi
	je	SHORT $L16075
	cmp	edi, 1
	je	SHORT $L16083
	cmp	edi, 2
	je	SHORT $L16074
	cmp	edi, 3
	je	SHORT $L16084
	cmp	edi, 4
	je	SHORT $L16085
	cmp	edi, 5
	je	SHORT $L16086
	jmp	SHORT $L16365
; Line 798
$L16075:
; Line 799
	mov	eax, DWORD PTR fs:64
	push	eax
	call	_GetAppCompatFlags@4
; Line 803
	mov	esi, DWORD PTR _pwndStart$[ebp]
	test	al, 8
	mov	eax, DWORD PTR [esi+24]
	mov	eax, DWORD PTR [eax+28]
	je	SHORT $L16076
	test	eax, eax
	je	SHORT $L16076
	mov	ecx, 8
$L16078:
; Line 804
	test	BYTE PTR [eax+156], cl
	je	SHORT $L16076
; Line 803
	mov	eax, DWORD PTR [eax+20]
	test	eax, eax
	jne	SHORT $L16078
; Line 809
	jmp	SHORT $L16076
; Line 816
$L16083:
; Line 817
	push	0
	mov	esi, DWORD PTR _pwndStart$[ebp]
	push	esi
	call	_GetPrevPwnd@8
; Line 818
	jmp	SHORT $L16076
; Line 794
$L16074:
; Line 795
	mov	esi, DWORD PTR _pwndStart$[ebp]
	mov	eax, DWORD PTR [esi+20]
; Line 796
	jmp	SHORT $L16076
; Line 820
$L16084:
; Line 821
	mov	esi, DWORD PTR _pwndStart$[ebp]
	push	esi
	push	esi
	call	_GetPrevPwnd@8
; Line 822
	jmp	SHORT $L16076
; Line 824
$L16085:
; Line 825
	mov	esi, DWORD PTR _pwndStart$[ebp]
	mov	eax, DWORD PTR [esi+32]
; Line 826
	jmp	SHORT $L16076
; Line 828
$L16086:
; Line 829
	mov	esi, DWORD PTR _pwndStart$[ebp]
	mov	eax, DWORD PTR [esi+28]
; Line 835
$L16076:
; Line 841
	movzx	ecx, WORD PTR [esi+122]
	and	ch, -129				; ffffff7fH
	cmp	ecx, 690				; 000002b2H
	jne	SHORT $L16088
; Line 842
	cmp	edi, 5
	je	SHORT $L16088
$L16365:
; Line 847
	xor	eax, eax
; Line 852
$L16088:
; Line 853
	pop	edi
	pop	esi
	pop	ebp
	ret	8
__GetWindow@8 ENDP
_TEXT	ENDS
PUBLIC	_IsWindow@4
;	COMDAT _IsWindow@4
_TEXT	SEGMENT
_hwnd$ = 8
_IsWindow@4 PROC NEAR					; COMDAT
; Line 857
	push	ebp
	mov	ebp, esp
	push	esi
; Line 865
	push	1
	mov	esi, DWORD PTR _hwnd$[ebp]
	push	esi
	call	_HMValidateHandleNoRip@8
	mov	ecx, eax
; Line 870
	test	ecx, ecx
	je	SHORT $L16098
; Line 880
	mov	eax, DWORD PTR fs:64
	mov	edx, DWORD PTR [eax+20]
; Line 882
	cmp	edx, ecx
	ja	SHORT $L16369
	mov	eax, DWORD PTR _wow16gpsi
	mov	eax, DWORD PTR [eax+932]
	add	eax, edx
	cmp	eax, ecx
	jbe	SHORT $L16369
; Line 888
	and	esi, 65535				; 0000ffffH
	mov	eax, DWORD PTR _wow16gpsi
	imul	esi, 12					; 0000000cH
	mov	eax, DWORD PTR [eax+4]
	test	BYTE PTR [esi+eax+9], 1
	je	SHORT $L16098
$L16369:
; Line 883
	xor	ecx, ecx
; Line 910
$L16098:
	cmp	ecx, 1
	sbb	eax, eax
	inc	eax
; Line 911
	pop	esi
	pop	ebp
	ret	4
_IsWindow@4 ENDP
_TEXT	ENDS
PUBLIC	_GetWindow@8
;	COMDAT _GetWindow@8
_TEXT	SEGMENT
_hwnd$ = 8
_wCmd$ = 12
_GetWindow@8 PROC NEAR					; COMDAT
; Line 917
	push	ebp
	mov	ebp, esp
	push	esi
; Line 926
	mov	esi, DWORD PTR _hwnd$[ebp]
	push	esi
	call	_IsWindow@4
	test	eax, eax
	jne	SHORT $L16109
; Line 927
	xor	eax, eax
	jmp	SHORT $L16107
; Line 928
$L16109:
	and	esi, 65535				; 0000ffffH
	mov	eax, DWORD PTR _wow16gpsi
	imul	esi, 12					; 0000000cH
	mov	eax, DWORD PTR [eax+4]
	mov	ecx, DWORD PTR [esi+eax]
; Line 929
	test	ecx, ecx
	jne	SHORT $L16110
; Line 930
	xor	eax, eax
	jmp	SHORT $L16107
; Line 934
$L16110:
	mov	eax, DWORD PTR fs:64
	mov	eax, DWORD PTR [eax+72]
	cmp	DWORD PTR [eax+40], ecx
	mov	eax, DWORD PTR _wCmd$[ebp]
	jne	SHORT $L16374
	cmp	eax, 5
	je	SHORT $L16374
; Line 935
	xor	eax, eax
	jmp	SHORT $L16107
$L16374:
	push	eax
	push	ecx
	call	__GetWindow@8
; Line 938
	test	eax, eax
	jne	SHORT $L16370
	xor	eax, eax
	jmp	SHORT $L16107
$L16370:
	mov	eax, DWORD PTR [eax]
; Line 939
$L16107:
	pop	esi
	pop	ebp
	ret	8
_GetWindow@8 ENDP
_TEXT	ENDS
PUBLIC	__GetParent@4
;	COMDAT __GetParent@4
_TEXT	SEGMENT
_pwnd$ = 8
__GetParent@4 PROC NEAR					; COMDAT
; Line 954
	push	ebp
	mov	ebp, esp
	mov	eax, DWORD PTR _pwnd$[ebp]
	mov	cl, BYTE PTR [eax+163]
	and	cl, 192					; 000000c0H
; Line 961
	je	SHORT $L16114
; Line 962
	cmp	cl, 64					; 00000040H
	jne	SHORT $L16115
; Line 963
	mov	eax, DWORD PTR [eax+24]
; Line 964
	jmp	SHORT $L16113
$L16115:
; Line 965
	mov	eax, DWORD PTR [eax+32]
; Line 966
	jmp	SHORT $L16113
; Line 973
$L16114:
	xor	eax, eax
; Line 974
$L16113:
	pop	ebp
	ret	4
__GetParent@4 ENDP
_TEXT	ENDS
PUBLIC	_GetParent@4
;	COMDAT _GetParent@4
_TEXT	SEGMENT
_hwnd$ = 8
_GetParent@4 PROC NEAR					; COMDAT
; Line 979
	push	ebp
	mov	ebp, esp
; Line 982
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 983
	test	eax, eax
	jne	SHORT $L16120
; Line 984
	xor	eax, eax
	jmp	SHORT $L16118
; Line 986
$L16120:
	push	eax
	call	__GetParent@4
; Line 987
	test	eax, eax
	jne	SHORT $L16375
	xor	eax, eax
	jmp	SHORT $L16118
$L16375:
	mov	eax, DWORD PTR [eax]
; Line 988
$L16118:
	pop	ebp
	ret	4
_GetParent@4 ENDP
_TEXT	ENDS
PUBLIC	__GetSubMenu@8
;	COMDAT __GetSubMenu@8
_TEXT	SEGMENT
_pMenu$ = 8
_nPos$ = 12
__GetSubMenu@8 PROC NEAR				; COMDAT
; Line 1002
	push	ebp
	mov	ebp, esp
; Line 1004
	xor	eax, eax
; Line 1009
	mov	ecx, DWORD PTR _pMenu$[ebp]
	mov	edx, DWORD PTR _nPos$[ebp]
	cmp	DWORD PTR [ecx+40], edx
	jbe	SHORT $L16126
; Line 1010
	mov	ecx, DWORD PTR [ecx+64]
	imul	edx, 52					; 00000034H
	add	ecx, edx
; Line 1011
	test	BYTE PTR [ecx], 16			; 00000010H
	je	SHORT $L16126
; Line 1012
	mov	eax, DWORD PTR [ecx+4]
; Line 1016
$L16126:
; Line 1017
	pop	ebp
	ret	8
__GetSubMenu@8 ENDP
_TEXT	ENDS
PUBLIC	_GetSubMenu@8
;	COMDAT _GetSubMenu@8
_TEXT	SEGMENT
_hMenu$ = 8
_nPos$ = 12
_GetSubMenu@8 PROC NEAR					; COMDAT
; Line 1023
	push	ebp
	mov	ebp, esp
; Line 1026
	push	2
	push	DWORD PTR _hMenu$[ebp]
	call	_ClientValidateHandle@8
; Line 1028
	test	eax, eax
	jne	SHORT $L16132
; Line 1029
	xor	eax, eax
	jmp	SHORT $L16130
; Line 1031
$L16132:
	push	DWORD PTR _nPos$[ebp]
	push	eax
	call	__GetSubMenu@8
; Line 1032
	test	eax, eax
	jne	SHORT $L16377
	xor	eax, eax
	jmp	SHORT $L16130
$L16377:
	mov	eax, DWORD PTR [eax]
; Line 1033
$L16130:
	pop	ebp
	ret	8
_GetSubMenu@8 ENDP
_TEXT	ENDS
PUBLIC	_GetSysColor@4
;	COMDAT _GetSysColor@4
_TEXT	SEGMENT
_nIndex$ = 8
_GetSysColor@4 PROC NEAR				; COMDAT
; Line 1038
	push	ebp
	mov	ebp, esp
; Line 1060
	mov	eax, DWORD PTR _nIndex$[ebp]
	test	eax, eax
	jl	SHORT $L16136
	cmp	eax, 21					; 00000015H
	jge	SHORT $L16136
; Line 1065
	mov	ecx, DWORD PTR _wow16gpsi
	mov	eax, DWORD PTR [ecx+eax*4+668]
	jmp	SHORT $L16134
; Line 1060
$L16136:
; Line 1062
	xor	eax, eax
; Line 1066
$L16134:
	pop	ebp
	ret	4
_GetSysColor@4 ENDP
_TEXT	ENDS
PUBLIC	_GetSystemMetrics@4
;	COMDAT _GetSystemMetrics@4
_TEXT	SEGMENT
_index$ = 8
_GetSystemMetrics@4 PROC NEAR				; COMDAT
; Line 1071
	push	ebp
	mov	ebp, esp
; Line 1074
	mov	ecx, DWORD PTR _wow16gpsi
	mov	eax, DWORD PTR _index$[ebp]
	mov	eax, DWORD PTR [ecx+eax*4+384]
; Line 1075
	pop	ebp
	ret	4
_GetSystemMetrics@4 ENDP
_TEXT	ENDS
PUBLIC	__GetTopWindow@4
;	COMDAT __GetTopWindow@4
_TEXT	SEGMENT
_pwnd$ = 8
__GetTopWindow@4 PROC NEAR				; COMDAT
; Line 1091
	push	ebp
	mov	ebp, esp
; Line 1092
	mov	eax, DWORD PTR _pwnd$[ebp]
	test	eax, eax
	jne	SHORT $L16381
	call	__GetDesktopWindow@0
$L16381:
	mov	eax, DWORD PTR [eax+28]
; Line 1095
	pop	ebp
	ret	4
__GetTopWindow@4 ENDP
_TEXT	ENDS
PUBLIC	_GetTopWindow@4
;	COMDAT _GetTopWindow@4
_TEXT	SEGMENT
_hwnd$ = 8
_GetTopWindow@4 PROC NEAR				; COMDAT
; Line 1100
	push	ebp
	mov	ebp, esp
; Line 1106
	mov	eax, DWORD PTR _hwnd$[ebp]
	test	eax, eax
	jne	SHORT $L16144
; Line 1107
	xor	eax, eax
; Line 1108
$L16145:
; Line 1114
	push	eax
	call	__GetTopWindow@4
; Line 1115
	test	eax, eax
	jne	SHORT $L16382
	xor	eax, eax
	jmp	SHORT $L16142
; Line 1108
$L16144:
; Line 1109
	push	1
	push	eax
	call	_ClientValidateHandle@8
; Line 1110
	test	eax, eax
	jne	SHORT $L16145
; Line 1111
	xor	eax, eax
	jmp	SHORT $L16142
; Line 1115
$L16382:
	mov	eax, DWORD PTR [eax]
; Line 1116
$L16142:
	pop	ebp
	ret	4
_GetTopWindow@4 ENDP
_TEXT	ENDS
PUBLIC	__IsChild@8
;	COMDAT __IsChild@8
_TEXT	SEGMENT
_pwndParent$ = 8
_pwnd$ = 12
__IsChild@8 PROC NEAR					; COMDAT
; Line 1131
	push	ebp
	mov	ebp, esp
	mov	eax, DWORD PTR _pwndParent$[ebp]
	mov	ecx, DWORD PTR _pwnd$[ebp]
; Line 1132
$L16151:
	test	ecx, ecx
	je	SHORT $L16387
; Line 1133
	mov	dl, BYTE PTR [ecx+163]
	and	dl, 192					; 000000c0H
	cmp	dl, 64					; 00000040H
	jne	SHORT $L16387
; Line 1136
	mov	ecx, DWORD PTR [ecx+24]
; Line 1137
	cmp	ecx, eax
	jne	SHORT $L16151
; Line 1138
	mov	eax, 1
	jmp	SHORT $L16149
$L16387:
; Line 1141
	xor	eax, eax
; Line 1142
$L16149:
	pop	ebp
	ret	8
__IsChild@8 ENDP
_TEXT	ENDS
PUBLIC	_IsChild@8
;	COMDAT _IsChild@8
_TEXT	SEGMENT
_hwndParent$ = 8
_hwnd$ = 12
_IsChild@8 PROC NEAR					; COMDAT
; Line 1149
	push	ebp
	mov	ebp, esp
	push	esi
; Line 1152
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
	mov	esi, eax
; Line 1153
	test	esi, esi
	jne	SHORT $L16160
; Line 1154
	xor	eax, eax
	jmp	SHORT $L16157
; Line 1156
$L16160:
	push	1
	push	DWORD PTR _hwndParent$[ebp]
	call	_ClientValidateHandle@8
; Line 1157
	test	eax, eax
	jne	SHORT $L16161
; Line 1158
	xor	eax, eax
	jmp	SHORT $L16157
; Line 1160
$L16161:
	push	esi
	push	eax
	call	__IsChild@8
; Line 1161
$L16157:
	pop	esi
	pop	ebp
	ret	8
_IsChild@8 ENDP
_TEXT	ENDS
PUBLIC	__IsIconic@4
;	COMDAT __IsIconic@4
_TEXT	SEGMENT
_pwnd$ = 8
__IsIconic@4 PROC NEAR					; COMDAT
; Line 1174
	push	ebp
	mov	ebp, esp
; Line 1175
	mov	eax, DWORD PTR _pwnd$[ebp]
	pop	ebp
	mov	al, BYTE PTR [eax+163]
	and	al, 32					; 00000020H
	cmp	al, 1
	sbb	eax, eax
	inc	eax
; Line 1176
	ret	4
__IsIconic@4 ENDP
_TEXT	ENDS
PUBLIC	_IsIconic@4
;	COMDAT _IsIconic@4
_TEXT	SEGMENT
_hwnd$ = 8
_IsIconic@4 PROC NEAR					; COMDAT
; Line 1182
	push	ebp
	mov	ebp, esp
; Line 1185
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 1187
	test	eax, eax
	jne	SHORT $L16167
; Line 1188
	xor	eax, eax
	jmp	SHORT $L16165
; Line 1190
$L16167:
	push	eax
	call	__IsIconic@4
; Line 1191
$L16165:
	pop	ebp
	ret	4
_IsIconic@4 ENDP
_TEXT	ENDS
PUBLIC	__IsWindowEnabled@4
;	COMDAT __IsWindowEnabled@4
_TEXT	SEGMENT
_pwnd$ = 8
__IsWindowEnabled@4 PROC NEAR				; COMDAT
; Line 1204
	push	ebp
	mov	ebp, esp
; Line 1205
	mov	eax, DWORD PTR _pwnd$[ebp]
	pop	ebp
	mov	al, BYTE PTR [eax+163]
	and	al, 8
	cmp	al, 1
	sbb	eax, eax
	neg	eax
; Line 1206
	ret	4
__IsWindowEnabled@4 ENDP
_TEXT	ENDS
PUBLIC	_IsWindowEnabled@4
;	COMDAT _IsWindowEnabled@4
_TEXT	SEGMENT
_hwnd$ = 8
_IsWindowEnabled@4 PROC NEAR				; COMDAT
; Line 1212
	push	ebp
	mov	ebp, esp
; Line 1215
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 1217
	test	eax, eax
	jne	SHORT $L16173
; Line 1218
	xor	eax, eax
	jmp	SHORT $L16171
; Line 1220
$L16173:
	push	eax
	call	__IsWindowEnabled@4
; Line 1221
$L16171:
	pop	ebp
	ret	4
_IsWindowEnabled@4 ENDP
_TEXT	ENDS
PUBLIC	__IsWindowVisible@4
;	COMDAT __IsWindowVisible@4
_TEXT	SEGMENT
_pwnd$ = 8
__IsWindowVisible@4 PROC NEAR				; COMDAT
; Line 1238
	push	ebp
	mov	ebp, esp
; Line 1239
	call	__GetDesktopWindow@0
; Line 1246
	mov	ecx, DWORD PTR _pwnd$[ebp]
	test	ecx, ecx
	je	SHORT $L16392
	mov	edx, DWORD PTR _wow16gpsi
	cmp	DWORD PTR [edx+372], ecx
	je	SHORT $L16392
	mov	edx, 16					; 00000010H
; Line 1249
$L16180:
; Line 1250
	test	BYTE PTR [ecx+163], dl
	je	SHORT $L16390
; Line 1252
	cmp	eax, ecx
	je	SHORT $L16392
; Line 1254
	mov	ecx, DWORD PTR [ecx+24]
; Line 1255
	jmp	SHORT $L16180
$L16390:
; Line 1251
	xor	eax, eax
	jmp	SHORT $L16175
$L16392:
; Line 1247
	mov	eax, 1
; Line 1258
$L16175:
	pop	ebp
	ret	4
__IsWindowVisible@4 ENDP
_TEXT	ENDS
PUBLIC	_IsWindowVisible@4
;	COMDAT _IsWindowVisible@4
_TEXT	SEGMENT
_hwnd$ = 8
_IsWindowVisible@4 PROC NEAR				; COMDAT
; Line 1263
	push	ebp
	mov	ebp, esp
; Line 1267
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 1278
	test	eax, eax
	jne	SHORT $L16188
; Line 1279
	xor	eax, eax
; Line 1280
	jmp	SHORT $L16190
$L16188:
; Line 1281
	push	eax
	call	__IsWindowVisible@4
; Line 1288
$L16190:
; Line 1289
	pop	ebp
	ret	4
_IsWindowVisible@4 ENDP
_TEXT	ENDS
PUBLIC	__IsZoomed@4
;	COMDAT __IsZoomed@4
_TEXT	SEGMENT
_pwnd$ = 8
__IsZoomed@4 PROC NEAR					; COMDAT
; Line 1302
	push	ebp
	mov	ebp, esp
; Line 1303
	xor	eax, eax
	mov	ecx, DWORD PTR _pwnd$[ebp]
	pop	ebp
	mov	al, BYTE PTR [ecx+163]
	and	eax, 1
; Line 1304
	ret	4
__IsZoomed@4 ENDP
_TEXT	ENDS
PUBLIC	_IsZoomed@4
;	COMDAT _IsZoomed@4
_TEXT	SEGMENT
_hwnd$ = 8
_IsZoomed@4 PROC NEAR					; COMDAT
; Line 1310
	push	ebp
	mov	ebp, esp
; Line 1313
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 1315
	test	eax, eax
	jne	SHORT $L16196
; Line 1316
	xor	eax, eax
	jmp	SHORT $L16194
; Line 1318
$L16196:
	push	eax
	call	__IsZoomed@4
; Line 1319
$L16194:
	pop	ebp
	ret	4
_IsZoomed@4 ENDP
_TEXT	ENDS
PUBLIC	__ClientToScreen@8
;	COMDAT __ClientToScreen@8
_TEXT	SEGMENT
_pwnd$ = 8
_ppt$ = 12
__ClientToScreen@8 PROC NEAR				; COMDAT
; Line 1333
	push	ebp
	mov	ebp, esp
; Line 1334
	mov	eax, DWORD PTR _pwnd$[ebp]
	mov	edx, DWORD PTR _ppt$[ebp]
	mov	ecx, DWORD PTR [eax+64]
	add	DWORD PTR [edx], ecx
; Line 1335
	mov	eax, DWORD PTR [eax+68]
	add	DWORD PTR [edx+4], eax
; Line 1337
	mov	eax, 1
; Line 1338
	pop	ebp
	ret	8
__ClientToScreen@8 ENDP
_TEXT	ENDS
PUBLIC	_ClientToScreen@8
;	COMDAT _ClientToScreen@8
_TEXT	SEGMENT
_hwnd$ = 8
_ppoint$ = 12
_ClientToScreen@8 PROC NEAR				; COMDAT
; Line 1344
	push	ebp
	mov	ebp, esp
	push	esi
; Line 1347
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 1349
	test	eax, eax
	jne	SHORT $L16204
; Line 1350
	xor	eax, eax
	jmp	SHORT $L16202
; Line 1353
$L16204:
	mov	esi, DWORD PTR _ppoint$[ebp]
	mov	ecx, DWORD PTR [eax+64]
	movsx	edx, WORD PTR [esi]
	add	ecx, edx
; Line 1355
	cmp	ecx, -32768				; ffff8000H
	jl	SHORT $L16393
	cmp	ecx, 32767				; 00007fffH
	jl	SHORT $L16397
	mov	ecx, 32767				; 00007fffH
$L16397:
	mov	WORD PTR [esi], cx
	jmp	SHORT $L16394
$L16393:
	mov	WORD PTR [esi], -32768			; ffff8000H
$L16394:
	movsx	ecx, WORD PTR [esi+2]
	add	ecx, DWORD PTR [eax+68]
; Line 1356
	cmp	ecx, -32768				; ffff8000H
	jl	SHORT $L16395
	cmp	ecx, 32767				; 00007fffH
	jl	SHORT $L16398
	mov	ecx, 32767				; 00007fffH
$L16398:
	mov	WORD PTR [esi+2], cx
	jmp	SHORT $L16396
$L16395:
	mov	WORD PTR [esi+2], -32768		; ffff8000H
$L16396:
; Line 1357
	mov	eax, 1
; Line 1362
$L16202:
	pop	esi
	pop	ebp
	ret	8
_ClientToScreen@8 ENDP
_TEXT	ENDS
PUBLIC	__GetClientRect@8
;	COMDAT __GetClientRect@8
_TEXT	SEGMENT
_pwnd$ = 8
_prc$ = 12
__GetClientRect@8 PROC NEAR				; COMDAT
; Line 1376
	push	ebp
	mov	ebp, esp
	push	esi
	push	edi
; Line 1377
	mov	esi, DWORD PTR _pwnd$[ebp]
	add	esi, 64					; 00000040H
	mov	edi, DWORD PTR _prc$[ebp]
	movsd
	movsd
	movsd
	mov	eax, 1
	movsd
; Line 1380
	pop	edi
	pop	esi
	pop	ebp
	ret	8
__GetClientRect@8 ENDP
_TEXT	ENDS
PUBLIC	_GetClientRect@8
;	COMDAT _GetClientRect@8
_TEXT	SEGMENT
_hwnd$ = 8
_GetClientRect@8 PROC NEAR				; COMDAT
; Line 1387
	push	ebp
	mov	ebp, esp
; Line 1390
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 1392
	test	eax, eax
	jne	SHORT $L16213
; Line 1393
	xor	eax, eax
	jmp	SHORT $L16211
; Line 1396
$L16213:
; Line 1397
	add	eax, 64					; 00000040H
; Line 1401
$L16211:
	pop	ebp
	ret	8
_GetClientRect@8 ENDP
_TEXT	ENDS
PUBLIC	_GetCursorPos@4
;	COMDAT _GetCursorPos@4
_TEXT	SEGMENT
_lpPoint$ = 8
_GetCursorPos@4 PROC NEAR				; COMDAT
; Line 1406
	push	ebp
	mov	ebp, esp
	mov	eax, DWORD PTR _wow16gpsi
	mov	eax, DWORD PTR [eax+752]
; Line 1420
	cmp	eax, -32768				; ffff8000H
	jl	SHORT $L16399
	cmp	eax, 32767				; 00007fffH
	mov	ecx, DWORD PTR _lpPoint$[ebp]
	jl	SHORT $L16403
	mov	eax, 32767				; 00007fffH
$L16403:
	mov	WORD PTR [ecx], ax
	jmp	SHORT $L16400
$L16399:
	mov	ecx, DWORD PTR _lpPoint$[ebp]
	mov	WORD PTR [ecx], -32768			; ffff8000H
$L16400:
	mov	eax, DWORD PTR _wow16gpsi
	mov	eax, DWORD PTR [eax+756]
; Line 1421
	cmp	eax, -32768				; ffff8000H
	jl	SHORT $L16401
	cmp	eax, 32767				; 00007fffH
	jl	SHORT $L16404
	mov	eax, 32767				; 00007fffH
$L16404:
	mov	WORD PTR [ecx+2], ax
	jmp	SHORT $L16402
$L16401:
	mov	WORD PTR [ecx+2], -32768		; ffff8000H
$L16402:
; Line 1422
	mov	eax, 1
; Line 1423
	pop	ebp
	ret	4
_GetCursorPos@4 ENDP
_TEXT	ENDS
PUBLIC	__GetWindowRect@8
;	COMDAT __GetWindowRect@8
_TEXT	SEGMENT
_pwnd$ = 8
_prc$ = 12
__GetWindowRect@8 PROC NEAR				; COMDAT
; Line 1437
	push	ebp
	mov	ebp, esp
	push	esi
	push	edi
; Line 1438
	mov	esi, DWORD PTR _pwnd$[ebp]
	add	esi, 48					; 00000030H
	mov	edi, DWORD PTR _prc$[ebp]
	movsd
	movsd
	movsd
	mov	eax, 1
	movsd
; Line 1440
	pop	edi
	pop	esi
	pop	ebp
	ret	8
__GetWindowRect@8 ENDP
_TEXT	ENDS
PUBLIC	_GetWindowRect@8
;	COMDAT _GetWindowRect@8
_TEXT	SEGMENT
_hwnd$ = 8
_GetWindowRect@8 PROC NEAR				; COMDAT
; Line 1445
	push	ebp
	mov	ebp, esp
; Line 1448
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 1450
	test	eax, eax
	jne	SHORT $L16223
; Line 1451
	xor	eax, eax
	jmp	SHORT $L16221
; Line 1454
$L16223:
; Line 1455
	add	eax, 48					; 00000030H
; Line 1459
$L16221:
	pop	ebp
	ret	8
_GetWindowRect@8 ENDP
_TEXT	ENDS
PUBLIC	__ScreenToClient@8
;	COMDAT __ScreenToClient@8
_TEXT	SEGMENT
_pwnd$ = 8
_ppt$ = 12
__ScreenToClient@8 PROC NEAR				; COMDAT
; Line 1473
	push	ebp
	mov	ebp, esp
; Line 1474
	mov	eax, DWORD PTR _pwnd$[ebp]
	mov	edx, DWORD PTR _ppt$[ebp]
	mov	ecx, DWORD PTR [eax+64]
	sub	DWORD PTR [edx], ecx
; Line 1475
	mov	eax, DWORD PTR [eax+68]
	sub	DWORD PTR [edx+4], eax
; Line 1477
	mov	eax, 1
; Line 1478
	pop	ebp
	ret	8
__ScreenToClient@8 ENDP
_TEXT	ENDS
PUBLIC	_ScreenToClient@8
;	COMDAT _ScreenToClient@8
_TEXT	SEGMENT
_hwnd$ = 8
_ppoint$ = 12
_ScreenToClient@8 PROC NEAR				; COMDAT
; Line 1483
	push	ebp
	mov	ebp, esp
; Line 1486
	push	1
	push	DWORD PTR _hwnd$[ebp]
	call	_ClientValidateHandle@8
; Line 1488
	test	eax, eax
	jne	SHORT $L16231
; Line 1489
	xor	eax, eax
	jmp	SHORT $L16229
; Line 1492
$L16231:
	mov	edx, DWORD PTR _ppoint$[ebp]
	movsx	ecx, WORD PTR [edx]
	sub	ecx, DWORD PTR [eax+64]
; Line 1494
	cmp	ecx, -32768				; ffff8000H
	jl	SHORT $L16405
	cmp	ecx, 32767				; 00007fffH
	jl	SHORT $L16409
	mov	ecx, 32767				; 00007fffH
$L16409:
	mov	WORD PTR [edx], cx
	jmp	SHORT $L16406
$L16405:
	mov	WORD PTR [edx], -32768			; ffff8000H
$L16406:
	movsx	ecx, WORD PTR [edx+2]
	sub	ecx, DWORD PTR [eax+68]
; Line 1495
	cmp	ecx, -32768				; ffff8000H
	jl	SHORT $L16407
	cmp	ecx, 32767				; 00007fffH
	jl	SHORT $L16410
	mov	ecx, 32767				; 00007fffH
$L16410:
	mov	WORD PTR [edx+2], cx
	jmp	SHORT $L16408
$L16407:
	mov	WORD PTR [edx+2], -32768		; ffff8000H
$L16408:
; Line 1496
	mov	eax, 1
; Line 1501
$L16229:
	pop	ebp
	ret	8
_ScreenToClient@8 ENDP
_TEXT	ENDS
PUBLIC	_EnableMenuItem@12
;	COMDAT _EnableMenuItem@12
_TEXT	SEGMENT
_hMenu$ = 8
_uIDEnableItem$ = 12
_uEnable$ = 16
_EnableMenuItem@12 PROC NEAR				; COMDAT
; Line 1507
	push	ebp
	mov	ebp, esp
	push	esi
; Line 1511
	push	2
	push	DWORD PTR _hMenu$[ebp]
	call	_ClientValidateHandle@8
; Line 1512
	test	eax, eax
	jne	SHORT $L16239
; Line 1513
	mov	eax, -1
	jmp	SHORT $L16236
; Line 1519
$L16239:
	push	0
	mov	esi, DWORD PTR _uEnable$[ebp]
	push	esi
	push	DWORD PTR _uIDEnableItem$[ebp]
	push	eax
	call	_LookupMenuItem@16
	test	eax, eax
	jne	SHORT $L16240
; Line 1520
	mov	eax, -1
	jmp	SHORT $L16236
; Line 1526
$L16240:
	mov	eax, DWORD PTR [eax]
; Line 1527
	xor	esi, eax
	test	esi, 3
	jne	SHORT $L16241
; Line 1528
	and	eax, 3
	jmp	SHORT $L16236
; Line 1532
$L16241:
	mov	eax, DWORD PTR _wow16CsrFlag
	mov	BYTE PTR [eax], 1
	xor	eax, eax
; Line 1536
$L16236:
	pop	esi
	pop	ebp
	ret	12					; 0000000cH
_EnableMenuItem@12 ENDP
_TEXT	ENDS
PUBLIC	_PctiCurrent@0
;	COMDAT _PctiCurrent@0
_TEXT	SEGMENT
_PctiCurrent@0 PROC NEAR				; COMDAT
; Line 1550
	mov	eax, DWORD PTR fs:24
	add	eax, 448				; 000001c0H
; Line 1551
	ret	0
_PctiCurrent@0 ENDP
_TEXT	ENDS
PUBLIC	__PhkNext@4
;	COMDAT __PhkNext@4
_TEXT	SEGMENT
_phk$ = 8
__PhkNext@4 PROC NEAR					; COMDAT
; Line 1566
	push	ebp
	mov	ebp, esp
	mov	ecx, DWORD PTR _phk$[ebp]
	mov	eax, DWORD PTR [ecx+20]
; Line 1572
	test	eax, eax
	jne	SHORT $L16246
; Line 1574
	test	BYTE PTR [ecx+32], 1
	mov	eax, 0
	jne	SHORT $L16246
; Line 1575
	mov	eax, DWORD PTR [ecx+12]
	mov	eax, DWORD PTR [eax+72]
	mov	ecx, DWORD PTR [ecx+24]
	mov	eax, DWORD PTR [eax+ecx*4+52]
; Line 1578
$L16246:
; Line 1579
	pop	ebp
	ret	4
__PhkNext@4 ENDP
_TEXT	ENDS
PUBLIC	_CallNextHookEx@16
;	COMDAT _CallNextHookEx@16
_TEXT	SEGMENT
_CallNextHookEx@16 PROC NEAR				; COMDAT
; Line 1596
	push	esi
; Line 1604
	mov	eax, DWORD PTR fs:64
	mov	esi, eax
; Line 1608
	test	esi, esi
	je	SHORT $L16416
; Line 1611
	call	_PctiCurrent@0
; Line 1617
	push	DWORD PTR [esi+156]
	call	__PhkNext@4
	test	eax, eax
	mov	eax, 0
	je	SHORT $L16252
; Line 1622
	mov	eax, DWORD PTR _wow16CsrFlag
	mov	BYTE PTR [eax], 1
$L16416:
	xor	eax, eax
; Line 1756
$L16252:
	pop	esi
	ret	16					; 00000010H
_CallNextHookEx@16 ENDP
_TEXT	ENDS
PUBLIC	_WOW16DefHookProc@16
;	COMDAT _WOW16DefHookProc@16
_TEXT	SEGMENT
_nCode$ = 8
_wParam$ = 12
_lParam$ = 16
_hhk$ = 20
_WOW16DefHookProc@16 PROC NEAR				; COMDAT
; Line 1764
	push	ebp
	mov	ebp, esp
; Line 1765
	push	DWORD PTR _lParam$[ebp]
	push	DWORD PTR _wParam$[ebp]
	push	DWORD PTR _nCode$[ebp]
	push	DWORD PTR _hhk$[ebp]
	call	_CallNextHookEx@16
; Line 1766
	pop	ebp
	ret	16					; 00000010H
_WOW16DefHookProc@16 ENDP
_TEXT	ENDS
END
