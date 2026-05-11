/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

    pathsmol.c

Abstract:

    GetShortPathName{A,W} and the BaseSearchLongName_U helper that
    walks a path looking for the first component that violates DOS 8.3.

    Carved out of the deleted BASE/CLIENT/VDM.C during the VDM purge —
    these two are documented Win32 APIs with no actual VDM dependency
    (they query FileAlternateNameInformation), so they survive while
    the rest of vdm.c is gone.

Revision History:

    MicroNT: extracted from VDM.C circa 2026.

--*/

#include "basedll.h"

LPWSTR  BaseSearchLongName_U(LPCWSTR lpPathName);

DWORD
APIENTRY
GetShortPathNameA(
    IN  LPCSTR  lpszLongPath,
    IN  LPSTR   lpShortPath,
    IN  DWORD   cchBuffer
    )
{
    UNICODE_STRING  UString, UStringRet;
    ANSI_STRING     AString;
    NTSTATUS	    Status;
    LPWSTR          lpShortPathW;
    DWORD           ReturnValue=0;

    if (lpszLongPath == NULL) {
	SetLastError(ERROR_INVALID_PARAMETER);
	return 0;
	}
    try {
        RtlInitAnsiString(&AString, lpszLongPath);
	Status = Basep8BitStringToUnicodeString(&UString,
					       &AString,
					       TRUE
					       );
	if (!NT_SUCCESS(Status)){
            BaseSetLastNTError(Status);
            goto gspTryExit;
	    }
        if (ARGUMENT_PRESENT(lpShortPath) && cchBuffer > 0) {
            lpShortPathW = RtlAllocateHeap(RtlProcessHeap(), 0,
                                        cchBuffer * sizeof(WCHAR)
					);
            if (lpShortPathW == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                goto gspTryExit;
		}
	    }
	else {
            lpShortPathW = NULL;
            cchBuffer = 0;
	    }
	ReturnValue = GetShortPathNameW(UString.Buffer,
                                        lpShortPathW,
                                        cchBuffer
					);
        if (ReturnValue != 0 && ReturnValue <= cchBuffer) {
            if (ARGUMENT_PRESENT(lpShortPath)) {
                AString.Buffer = lpShortPath;
                AString.MaximumLength = (USHORT) cchBuffer;
                UString.MaximumLength = (USHORT)(cchBuffer * sizeof(WCHAR));
                UStringRet.Buffer = lpShortPathW;
		UStringRet.Length = (USHORT)(ReturnValue * sizeof(WCHAR));
		Status = BasepUnicodeStringTo8BitString(&AString,
							&UStringRet,
							FALSE
							);
		if (!NT_SUCCESS(Status)) {
		    BaseSetLastNTError(Status);
                    ReturnValue=0;
                    goto gspTryExit;
		    }
		}
	    }
gspTryExit:;
        }

    finally {
	    RtlFreeUnicodeString(&UString);
            RtlFreeHeap(RtlProcessHeap(), 0, lpShortPathW);
        }

    return ReturnValue;
}

/****
GetShortPathName

Description:
    This function converts the given path name to its short form if
     needed. The conversion  may not be necessary and in that case,
     this function simply copies down the given name to the return buffer.
    The caller can have the return buffer set equal to the given path name
     address.

Parameters:
    lpszLongPath -  Points to a NULL terminated string.
    lpszShortPath - Buffer address to return the short name.
    cchBuffer - Buffer size in char of lpszShortPath.

Return Value
    If the GetShortPathName function succeeds, the return value is the length,
    in characters, of the string copied to lpszShortPath,
    not including the terminating
    null character.

    If the lpszShortPath is too small, the return value is
    the size of the buffer, in
    characters, required to hold the path.

    If the function fails, the return value is zero. To get
    extended error information, use
    the GetLastError function.

Remarks:
    The "short name" can be longer than its "long name". lpszLongPath doesn't
    have to be a fully qualified path name or a long path name.

****/

DWORD
APIENTRY
GetShortPathNameW(
    IN  LPCWSTR lpszLongPath,
    IN  LPWSTR  lpszShortPath,
    IN  DWORD   cchBuffer
    )
{

    RTL_PATH_TYPE   RtlPathType;
    LPWSTR	    p, p1, p2, pLast, pDst;
    DWORD	    wchTotal, Length;
    WCHAR	    wch, BufferForFileNameInfo[4 + 14];
    IO_STATUS_BLOCK IoStatusBlock;
    OBJECT_ATTRIBUTES	Obja;
    NTSTATUS	    Status;
    PFILE_NAME_INFORMATION pFileNameInfo;
    HANDLE	    Handle;
    PWCHAR          pLocalBuffer;
    UINT            uReturnVal;


    UNICODE_STRING  UStringNtName;

    UStringNtName.Buffer = NULL;
    pLocalBuffer = NULL;

    if (!ARGUMENT_PRESENT(lpszLongPath)) {
	SetLastError(ERROR_INVALID_PARAMETER);
	return 0;
	}
    try {
	// decide the path type, we want find out the position of
	// the first character of the first name
        RtlPathType = RtlDetermineDosPathNameType_U(lpszLongPath);
	switch (RtlPathType) {
	    // form: "\\server_name\share_name\rest_of_the_path"
	    case	RtlPathTypeUncAbsolute:
                if ((p = wcschr(lpszLongPath + 2, (WCHAR)'\\')) != NULL &&
		    (p = wcschr(p + 1, (WCHAR) '\\')) != NULL)
		    p++;
		else
		    p = NULL;
		break;

	    // form: "\\.\rest_of_the_path"
	    case	RtlPathTypeLocalDevice:
                p = (LPWSTR)lpszLongPath + 4;
		break;

	    // form: "\\."
	    case	RtlPathTypeRootLocalDevice:
		p = NULL;
		break;

	    // form: "D:\rest_of_the_path"
	    case	RtlPathTypeDriveAbsolute:
                p = (LPWSTR)lpszLongPath + 3;
		break;

	    // form: "D:rest_of_the_path"
	    case	RtlPathTypeDriveRelative:
                p = (LPWSTR)lpszLongPath+2;
		// handle .\ and ..\ cases
		while (*p != UNICODE_NULL && *p == (WCHAR) '.') {
		    if (p[1] == (WCHAR) '\\')
			p += 2;
		    else if(p[1] == (WCHAR)'.' && p[2] == (WCHAR) '\\')
			    p += 3;
			 else
			    break;
		    }
		break;

	    // form: "\rest_of_the_path"
	    case	RtlPathTypeRooted:
                p = (LPWSTR)lpszLongPath + 1;
		break;

	    // form: "rest_of_the_path"
	    case	RtlPathTypeRelative:
                p = (LPWSTR) lpszLongPath;
		while (*p != UNICODE_NULL && *p == (WCHAR) '.') {
		    if (p[1] == (WCHAR) '\\')
			p += 2;
		    else if(p[1] == (WCHAR)'.' && p[2] == (WCHAR) '\\')
			    p += 3;
			 else
			    break;
		    }
		break;

	    default:
		p = NULL;
		break;
	    }


	if (p == NULL ||
	    *(p1 = BaseSearchLongName_U(p)) == UNICODE_NULL) {

	    // nothing to convert, copy down the source string
	    // to the buffer if necessary

	    if (p == NULL)
                Length = wcslen(lpszLongPath) + 1;
	    else
                Length = (DWORD)(p1 - lpszLongPath + 1);
            if (cchBuffer >= Length) {
                if (ARGUMENT_PRESENT(lpszShortPath) && lpszShortPath != lpszLongPath) {
                    RtlMoveMemory(lpszShortPath, lpszLongPath, Length * sizeof(WCHAR));
                    }
                uReturnVal = Length  - 1;
                goto gsnTryExit;
		}
            else {
                uReturnVal = Length;
                goto gsnTryExit;
                }
	    }

	// Make a local buffer so that we won't overlap the
	// source pathname in case the short name is longer than the
	// long name.
        if (cchBuffer > 0 && ARGUMENT_PRESENT(lpszShortPath)) {
	    pLocalBuffer = RtlAllocateHeap(RtlProcessHeap(), 0,
                                           cchBuffer * sizeof(WCHAR));
            if (pLocalBuffer == NULL){
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                uReturnVal = 0;
                goto gsnTryExit;
	    }
        }

	pDst = pLocalBuffer;
        pLast = (LPWSTR)lpszLongPath;
	p2 = p1;
	//get the first component
	while(*p2 != UNICODE_NULL && *p2 != (WCHAR) '\\')
	    p2++;
	wch = *p2;
	*p2  = UNICODE_NULL;
	// convert to nt path name
        Status = RtlDosPathNameToNtPathName_U(lpszLongPath,
					      &UStringNtName,
					      NULL,
					      NULL
					      );

	*p2 = wch;
	if (!NT_SUCCESS(Status)) {
	    BaseSetLastNTError(Status);
            uReturnVal = 0;
            goto gsnTryExit;
	    }


	// we need this because we will assume the buffer is big enough
	//
	if (UStringNtName.MaximumLength <
	    (DOS_MAX_PATH_LENGTH + 1) * sizeof(WCHAR))
	    {
	    LPSTR	p;

	    p = RtlAllocateHeap(RtlProcessHeap(), 0,
				(DOS_MAX_PATH_LENGTH + 1) * sizeof(WCHAR)
				);
	    if (p == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                uReturnVal = 0;
                goto gsnTryExit;
		}
	    RtlMoveMemory(p, UStringNtName.Buffer, UStringNtName.Length);
	    RtlFreeHeap(RtlProcessHeap(), 0, UStringNtName.Buffer);
	    UStringNtName.Buffer =(PWSTR)p;
	    UStringNtName.MaximumLength = (DOS_MAX_PATH_LENGTH + 1)
					  * sizeof(WCHAR);
	    }

	pFileNameInfo = (PFILE_NAME_INFORMATION)BufferForFileNameInfo;
	wchTotal = 0;
	while (TRUE) {
	    // p1 -> first character of the long name
	    // p2 -> last character of the long name (\ or NULL)
	    // copy the short name in the source first
	    Length = (DWORD)(p1 - pLast);
	    if (Length > 0) {
		wchTotal += Length;
                if (cchBuffer > wchTotal && ARGUMENT_PRESENT(lpszShortPath)) {
		    RtlMoveMemory(pDst, pLast, Length * sizeof(WCHAR));
		    pDst += Length;
		    }
		}
	    InitializeObjectAttributes(&Obja,
				       &UStringNtName,
				       OBJ_CASE_INSENSITIVE,
				       NULL,
				       NULL
				       );
	    Status = NtOpenFile(&Handle,
				FILE_GENERIC_READ,
				&Obja,
				&IoStatusBlock,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				FILE_SYNCHRONOUS_IO_NONALERT
				);
	    if (!NT_SUCCESS(Status)) {
		BaseSetLastNTError(Status);
                uReturnVal = 0;
                goto gsnTryExit;
		}

	    Status = NtQueryInformationFile(Handle,
					    &IoStatusBlock,
					    pFileNameInfo,
					    sizeof(BufferForFileNameInfo),
					    FileAlternateNameInformation
					    );
	    NtClose(Handle);
	    if (!NT_SUCCESS(Status)) {
		BaseSetLastNTError(Status);
                uReturnVal = 0;
                goto gsnTryExit;
		}
	    // the returned length is in bytes!!
	    wchTotal += pFileNameInfo->FileNameLength / sizeof(WCHAR);
            if (cchBuffer > wchTotal && ARGUMENT_PRESENT(lpszShortPath)) {
		RtlMoveMemory(pDst,
			      pFileNameInfo->FileName,
			      pFileNameInfo->FileNameLength
			      );
		pDst += pFileNameInfo->FileNameLength / sizeof(WCHAR);
		}
	    // if nothing left, we are done
	    if (*p2 == UNICODE_NULL)
		break;
	    else {
		pLast = p2;
		p1 = p2 + 1;
		}
	    // get next name
	    if (*(p1 = BaseSearchLongName_U(p1)) == UNICODE_NULL) {
		Length = (DWORD)(p1 - pLast);
		wchTotal += Length;
                if (cchBuffer > wchTotal && ARGUMENT_PRESENT(lpszShortPath)) {
		    RtlMoveMemory(pDst, pLast, Length * sizeof(WCHAR));
		    pDst += Length;
		    break;
		    }
		}
	    else {
		p2 = p1;
		while (*p2 != UNICODE_NULL && *p2 != (WCHAR)'\\')
		    p2++;
		// update the nt path name

		Length = (DWORD) p2 - (DWORD) pLast;

		RtlMoveMemory((BYTE *)UStringNtName.Buffer + UStringNtName.Length,
			      pLast,
			      Length
			      );
		UStringNtName.Length += (USHORT)Length;
		UStringNtName.Buffer[UStringNtName.Length / sizeof(WCHAR)] =
				    UNICODE_NULL;
		}
	    }
        if (cchBuffer > wchTotal && ARGUMENT_PRESENT(lpszShortPath)) {
            RtlMoveMemory(lpszShortPath, pLocalBuffer, wchTotal * sizeof(WCHAR));
            lpszShortPath[wchTotal] = UNICODE_NULL;
            uReturnVal = wchTotal;
	    }
	else
            uReturnVal =  wchTotal + 1;
gsnTryExit:;
	}
    finally {
	 if (UStringNtName.Buffer != NULL)
	    RtlFreeHeap(RtlProcessHeap(), 0, UStringNtName.Buffer);
	 if (pLocalBuffer != NULL)
	    RtlFreeHeap(RtlProcessHeap(), 0, pLocalBuffer);
        }

    return uReturnVal;
}

/**
    This function search a long name(invalid dos name) in the given string.
    The following characters are not valid in file name domain:
    * + , : ; < = > ? [ ] |
    Input: lpPathName
    Output: LPWSTR point to the first character of the first long name
	    if it can find any long name, it points to the terminate NULL
	    character
**/
LPWSTR	BaseSearchLongName_U(
    LPCWSTR lpPathName
    )

{
    LPWSTR  pFirst, pLast, pDot;
    BOOL    fLongNameFound;
    WCHAR   wch;

    if (*lpPathName == UNICODE_NULL)
	return (LPWSTR)lpPathName;

    pFirst = pLast = (LPWSTR)lpPathName;
    fLongNameFound = FALSE;
    pDot = NULL;
    while (TRUE) {

	wch = *pLast;
	if (wch == (WCHAR) '\\' || wch == UNICODE_NULL) {
	    // if base name is longer than 8(no matter if
	    // there is a dot) or the extention name is
	    // longer than 3, the name is an invalid dos file name
	    if ((!pDot && (DWORD)(pLast - pFirst) > 8) ||
		(pDot && ((DWORD)(pLast - pDot) > 3 + 1 ||
			  (DWORD)(pLast - pFirst) > 8 + 3 + 1||
			  (DWORD)(pLast - pFirst) == 0))
	       ) {

		fLongNameFound = TRUE;
		break;
		}
	    if (wch == UNICODE_NULL)
		break;
	    // start from the next component
	    pFirst = ++pLast;
	    pDot = NULL;
	    continue;
	    }
	if (wch == (WCHAR) '.') {
	    // if two or more '.' or the base name is longer than
	    // 8 characters or no base name at all, it is an illegal dos file name
            if (pDot != NULL || ((DWORD)(pLast - pFirst)) > 8 ||
                (pLast == pFirst && *(pLast + 1) != (WCHAR) '\\')){
		fLongNameFound = TRUE;
		break;
		}
	    pDot = pLast++;
	    continue;
	    }

	if (wch <= (WCHAR) ' '||
	    wch == (WCHAR) '*'||
	    wch == (WCHAR) '+'||
	    wch == (WCHAR) ','||
	    wch == (WCHAR) ':'||
	    wch == (WCHAR) ';'||
	    wch == (WCHAR) '<'||
	    wch == (WCHAR) '='||
	    wch == (WCHAR) '>'||
	    wch == (WCHAR) '?'||
	    wch == (WCHAR) '['||
	    wch == (WCHAR) ']'||
	    wch == (WCHAR) '|') {
	    fLongNameFound = TRUE;
	    break;
	    }
	pLast++;
	}

    return (fLongNameFound ? pFirst : pLast);
}
