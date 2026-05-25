/****************************** Module Header ******************************\
* Module Name: clres.c
*
* Copyright (c) 1985-91, Microsoft Corporation
*
* String-resource loading (LoadStringA / LoadStringW), MicroNT-owned.
*
* The stock USER client routes resource access through a PRESCALLS jump
* table so the same code can run client-side, server-side, or as a WOW
* (16-bit) callback.  MicroNT's user32 only ever loads resources from the
* calling Win32 process, so the table collapses to direct kernel32 calls
* (FindResourceExW / LoadResource / LockResource).  The icon/cursor best-fit
* machinery from the original rtlres.c is dropped -- it needs the display
* info and gdi, which MicroNT's user32 does not carry.
*
* History:
* 04-05-91 ScottLu      Original client/server resource code.
\***************************************************************************/

#include "precomp.h"
#pragma hdrstop

/***************************************************************************\
* LoadStringWorker
*
* Copy string resource wID from hmod into lpBuffer (Unicode), at most
* cchBufferMax WCHARs including the appended NUL.  Returns the count of
* characters copied, not counting the NUL.
*
* String tables hold 16 strings per RT_STRING resource: the resource id is
* (wID >> 4) + 1 and the string's slot within it is wID & 0x0F.  Each string
* in the block is length-prefixed (a leading WCHAR count, then that many
* WCHARs, no terminator).
\***************************************************************************/

static int
LoadStringWorker(
    HINSTANCE hmod,
    UINT wID,
    LPWSTR lpBuffer,
    int cchBufferMax,
    WORD wLangId)
{
    HRSRC   hResInfo;
    HGLOBAL hStringSeg;
    LPWSTR  lpsz;
    int     cch;

    /*
     * Need at least room for the terminating NULL.
     */
    if (!lpBuffer || (cchBufferMax-- == 0))
        return 0;

    cch = 0;

    /*
     * Find the string segment that holds this id.
     */
    hResInfo = FindResourceExW(hmod, RT_STRING,
                   (LPCWSTR)MAKEINTRESOURCEW((((USHORT)wID >> 4) + 1)),
                   wLangId);
    if (hResInfo) {

        hStringSeg = LoadResource(hmod, hResInfo);

        if (hStringSeg && (lpsz = (LPWSTR)LockResource(hStringSeg)) != NULL) {

            /*
             * Skip the strings ahead of ours in this 16-string block.
             */
            wID &= 0x0F;
            while (TRUE) {
                cch = (int)*lpsz++;     // length prefix (in WCHARs)
                if (wID-- == 0)
                    break;
                lpsz += cch;            // step over to the next string
            }

            /*
             * Truncate to the caller's buffer.
             */
            if (cch > cchBufferMax)
                cch = cchBufferMax;

            RtlCopyMemory(lpBuffer, lpsz, cch * sizeof(WCHAR));
        }
    }

    lpBuffer[cch] = L'\0';

    return cch;
}

/***************************************************************************\
* LoadStringW (API)
\***************************************************************************/

int
WINAPI
LoadStringW(
    HINSTANCE hmod,
    UINT wID,
    LPWSTR lpBuffer,
    int cchBufferMax)
{
    return LoadStringWorker(hmod, wID, lpBuffer, cchBufferMax, 0);
}

/***************************************************************************\
* LoadStringA (API)
*
* Load as Unicode, then translate to the caller's ANSI buffer.
\***************************************************************************/

int
WINAPI
LoadStringA(
    HINSTANCE hmod,
    UINT wID,
    LPSTR lpAnsiBuffer,
    int cchBufferMax)
{
    LPWSTR  lpUniBuffer;
    int     cchUnicode;
    int     cbAnsi = 0;

    if (cchBufferMax == 0)
        return 0;

    lpUniBuffer = (LPWSTR)LocalAlloc(LMEM_FIXED, cchBufferMax * sizeof(WCHAR));
    if (!lpUniBuffer) {
        SRIP0(RIP_WARNING, "LoadStringA out of memory");
        return 0;
    }

    cchUnicode = LoadStringWorker(hmod, wID, lpUniBuffer, cchBufferMax, 0);

    if (cchUnicode) {
        /*
         * Translate, including the NUL the worker appended, into the caller's
         * ANSI buffer.
         */
        cbAnsi = WCSToMB(lpUniBuffer, cchUnicode + 1, &lpAnsiBuffer,
                         cchBufferMax, FALSE);
    } else {
        lpAnsiBuffer[0] = '\0';
    }

    LocalFree(lpUniBuffer);

    /*
     * Don't count the terminating NUL.
     */
    if (cbAnsi > 0)
        cbAnsi--;

    return cbAnsi;
}
