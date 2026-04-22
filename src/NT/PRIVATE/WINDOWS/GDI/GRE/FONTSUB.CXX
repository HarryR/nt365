/******************************Module*Header*******************************\
* Module Name: fontsub.cxx
*
* Support for the [FontSubstitutes] section of WIN.INI (new functionality
* from Windows 3.1).
*
* Created: 27-Jan-1992 10:31:31
* Author: Gilman Wong [gilmanw]
*
* Copyright (c) 1990 Microsoft Corporation
*
\**************************************************************************/

// In Windows 3.1, there is a [FontSubstitutes] section that allows
// face names in LOGFONTs to match other facenames.
//
// For example,
//
//  [FontSubstitutes]
//      Helv=MS Sans Serif
//
// means that a LOGFONT with a lfFacename of "Helv" will match a physical
// font with a facename of either "Helv" or "MS Sans Serif".  That is,
// "Helv" has an alternate match (or substitute match) of "MS Sans Serif".
//
// In Win 3.1, the standard "Helv" and "Tms Rmn" faces have been replaced
// with "MS Sans Serif" and "MS Serif", respectively.  This substitution
// capability provides Win 3.1 with Win 3.0 compatibility for apps that
// use the old name convention.

#include "precomp.hxx"
#ifndef PRECOMPILED_GRE

#include <wcstr.h>

#include "engine.hxx"

#include "dcobj.hxx"
#include "pdevobj.hxx"
#include "fontsub.hxx"
#include "fontinc.hxx"

extern "C" {
#include "winsrv.h"
};
#endif

// This is a global reference to the font substitution table.  If the table
// is not initialized properly, then this is NULL and should not be
// dereferenced.

static PFONTSUB gpfsTable;
static COUNT    gcfsTable;


LONG cCapString(WCHAR *pwcDst,WCHAR *pwcSrc,INT cMax);


/******************************Public*Routine******************************\
* vInitFontSubTable
*
* Initializes the font substitutes table from data in the [FontSubstitutes]
* section of the WIN.INI file.  No error return code is provided since, if
* this is not successful, then the table simply will not exist and the
* global pointer to the table will remain NULL.
*
* History:
*  28-Jan-1992 -by- Gilman Wong [gilmanw]
* Wrote it.
\**************************************************************************/

VOID vInitFontSubTable()
{
// MicroNT: read HKLM\Software\Microsoft\Windows NT\CurrentVersion\FontSubstitutes
// directly via the registry API. The original NT 3.5 code routed through
// GetProfileStringW → BASESRV IniFileMapping → win.ini [FontSubstitutes],
// which is Win16-era baggage we don't need: the GDI server process has
// registry access and the data lives in the registry anyway.

    gpfsTable = (PFONTSUB) NULL;
    gcfsTable = 0;

    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES Obja;
    HANDLE hKey;
    NTSTATUS Status;

    RtlInitUnicodeString(&KeyName,
        L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes");
    InitializeObjectAttributes(&Obja, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&hKey, KEY_READ, &Obja);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("GRE: vInitFontSubTable NtOpenKey FAILED status=%08x\n", Status);
        return;
    }

// Count values first, then allocate exact-fit table.

    ULONG cValues;
    {
        KEY_FULL_INFORMATION kfi;
        ULONG cb;
        Status = NtQueryKey(hKey, KeyFullInformation, &kfi, sizeof(kfi), &cb);
        if (!NT_SUCCESS(Status)) {
            DbgPrint("GRE: vInitFontSubTable NtQueryKey FAILED status=%08x\n", Status);
            NtClose(hKey);
            return;
        }
        cValues = kfi.Values;
    }

    if (cValues == 0) {
        NtClose(hKey);
        return;
    }

    if ((gpfsTable = (PFONTSUB) PVALLOCMEM(cValues * sizeof(FONTSUB))) == (PFONTSUB) NULL) {
        DbgPrint("GRE: vInitFontSubTable allocation FAILED\n");
        NtClose(hKey);
        return;
    }

// Enumerate values. ValueName = logical face, data = substitute face.

    BYTE    Buffer[sizeof(KEY_VALUE_FULL_INFORMATION) + (LF_FACESIZE + 1) * sizeof(WCHAR) * 2];
    PKEY_VALUE_FULL_INFORMATION pkvfi = (PKEY_VALUE_FULL_INFORMATION)Buffer;
    PFONTSUB pfs = gpfsTable;

    for (ULONG i = 0; i < cValues; i++) {
        ULONG cb;
        Status = NtEnumerateValueKey(hKey, i, KeyValueFullInformation,
                                     pkvfi, sizeof(Buffer), &cb);
        if (!NT_SUCCESS(Status))
            break;
        if (pkvfi->Type != REG_SZ)
            continue;

        WCHAR faceName[LF_FACESIZE];
        WCHAR altName[LF_FACESIZE];
        ULONG cchName = pkvfi->NameLength / sizeof(WCHAR);
        ULONG cchData = pkvfi->DataLength / sizeof(WCHAR);
        if (cchName == 0 || cchName >= LF_FACESIZE || cchData >= LF_FACESIZE)
            continue;

        RtlCopyMemory(faceName, pkvfi->Name, cchName * sizeof(WCHAR));
        faceName[cchName] = L'\0';
        PWCHAR pData = (PWCHAR)((PBYTE)pkvfi + pkvfi->DataOffset);
        RtlCopyMemory(altName, pData, cchData * sizeof(WCHAR));
        if (cchData > 0 && altName[cchData - 1] == L'\0')
            cchData--;
        altName[cchData] = L'\0';

        cCapString(pfs->awchFace, faceName, LF_FACESIZE);
        cCapString(pfs->awchAltFace, altName, LF_FACESIZE);
        pfs++;
        gcfsTable++;
    }

    NtClose(hKey);

    DbgPrint("GRE: vInitFontSubTable loaded %d entries\n", gcfsTable);
}


/******************************Public*Routine******************************\
* pwszAlternateFacename
*
* Search the font substitutes table for an alternative facename for the
* given facename.
*
* Return:
*   Pointer to alt facename, NULL if not found.
*
* History:
*  28-Jan-1992 -by- Gilman Wong [gilmanw]
* Wrote it.
\**************************************************************************/

// Note: since the subsitute table is typically very small, its not
//       worth writing a more sophisticated search (with its greater
//       overhead).  If system usage changes in the future (or perhaps
//       in an international release) so that this table becomes large,
//       then something like a binary search might be appropriate.

PWCHAR pwszAlternateFacename (
    PWCHAR  pwchFacename
    )
{
    PFONTSUB pfs = gpfsTable;
    PFONTSUB pfsEnd = gpfsTable + gcfsTable;
    WCHAR    awchCapName[LF_FACESIZE];

// Want case insensitive search, so capitalize the name.

    cCapString(awchCapName, pwchFacename, LF_FACESIZE);

// Scan through the font substitution table for the key string.

    PWCHAR pwcA;
    PWCHAR pwcB;

    for (; pfs < pfsEnd; pfs++)
    {
    // Do the following inline for speed:
    //
    //  if (!wcsncmpi(pwchFacename, pfs->awchFace, LF_FACESIZE))
    //      return (pfs->awchAltFace);

        for (pwcA=awchCapName,pwcB=pfs->awchFace; *pwcA==*pwcB; pwcA++,pwcB++)
        {
            if (*pwcA == 0)
                return(pfs->awchAltFace);
        }
    }

// Nothing found, so return NULL.

    return ((PWCHAR) NULL);
}


 #if DBG
VOID DbgPrintFontSubstitutes()
{
    PFONTSUB pfs = gpfsTable;
    PFONTSUB pfsEnd = gpfsTable + gcfsTable;

// Scan through the font substitution table for the key string.

    DbgPrint("[FontSubstitutes]\n");

    for (; pfs < pfsEnd; pfs++)
        DbgPrint("\t%ws=%ws\n", pfs->awchFace, pfs->awchAltFace);
}
#endif
