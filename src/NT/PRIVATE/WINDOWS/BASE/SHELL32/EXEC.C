/****************************** Module Header ******************************\
* Module Name: exec.c
*
* Copyright (c) 1991  Microsoft Corporation
*
* MicroNT-owned ShellExecute / FindExecutable.
*
* The NT 3.5 shell32 ShellExecute (SHELL/LIBRARY/exec.c) buried its launch
* core under ~550 lines of legacy compatibility: window-level DDE
* conversations (CreateWindow + WM_DDE_INITIATE + GetProp/RegisterWindow-
* Message), the shell hook, WOW / DOS-VDM detection, and per-drive
* environment juggling -- all of which assume a csrss USER server and a
* 16-bit subsystem that MicroNT does not have.  This is the headless launch
* core only:
*
*   - resolve a document's handler from the registry association
*     (HKEY_CLASSES_ROOT: .ext -> progid -> progid\shell\<verb>\command),
*     substituting %1 / %* in the command template; and
*   - launch directly with CreateProcess.
*
* No DDE, no WOW, no shell hook, no message-box error UI.  Errors come back
* as the classic ShellExecute SE_ERR_* codes (return value <= 32).
*
\***************************************************************************/

#include <windows.h>
#include <string.h>
#include "shell.h"
#include "privshl.h"

#ifndef SE_ERR_ACCESSDENIED
#define SE_ERR_ACCESSDENIED 5
#endif

//
// A successful ShellExecute historically returned the launched instance
// handle; on Win32 any value > 32 means success.
//
#define SE_OK ((HINSTANCE)33)

static const WCHAR c_szOpen[]      = L"open";
static const WCHAR c_szShellPath[] = L"\\shell\\";
static const WCHAR c_szCommand[]   = L"\\command";

/***************************************************************************\
* MapLaunchError
*
* Translate a CreateProcess failure into the classic ShellExecute return.
\***************************************************************************/
static HINSTANCE MapLaunchError(void)
{
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND:    return (HINSTANCE)SE_ERR_FNF;
    case ERROR_PATH_NOT_FOUND:    return (HINSTANCE)SE_ERR_PNF;
    case ERROR_ACCESS_DENIED:     return (HINSTANCE)SE_ERR_ACCESSDENIED;
    case ERROR_SHARING_VIOLATION: return (HINSTANCE)SE_ERR_SHARE;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:       return (HINSTANCE)SE_ERR_OOM;
    default:                      return (HINSTANCE)SE_ERR_FNF;
    }
}

/***************************************************************************\
* RegReadDefaultW
*
* Read a key's default (unnamed) REG_SZ value into buf.  Returns
* ERROR_SUCCESS on success.
\***************************************************************************/
static LONG RegReadDefaultW(HKEY hRoot, LPCWSTR lpSubKey, LPWSTR buf, DWORD cchBuf)
{
    HKEY  hKey;
    LONG  err;
    DWORD type;
    DWORD cb;

    err = RegOpenKeyExW(hRoot, lpSubKey, 0, KEY_QUERY_VALUE, &hKey);
    if (err != ERROR_SUCCESS)
        return err;

    type = REG_NONE;
    cb   = (cchBuf - 1) * sizeof(WCHAR);
    err  = RegQueryValueExW(hKey, NULL, NULL, &type, (LPBYTE)buf, &cb);
    RegCloseKey(hKey);

    if (err != ERROR_SUCCESS)
        return err;

    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return ERROR_FILE_NOT_FOUND;

    //
    // RegQueryValueExW does not guarantee NUL termination if the stored data
    // was not terminated; force it.
    //
    buf[cb / sizeof(WCHAR)] = L'\0';
    return ERROR_SUCCESS;
}

/***************************************************************************\
* IsExecutableExtW
*
* TRUE for the image extensions we launch directly via CreateProcess without
* consulting a registry association.
\***************************************************************************/
static BOOL IsExecutableExtW(LPCWSTR lpExt)
{
    return wcsicmp(lpExt, L".exe") == 0 ||
           wcsicmp(lpExt, L".com") == 0 ||
           wcsicmp(lpExt, L".bat") == 0 ||
           wcsicmp(lpExt, L".cmd") == 0;
}

/***************************************************************************\
* AppendQuoted
*
* Append src to *ppDst, wrapping it in double quotes if it contains a space
* and is not already quoted.  Advances *ppDst past what was written.
\***************************************************************************/
static void AppendQuoted(LPWSTR *ppDst, LPCWSTR src)
{
    LPWSTR  d = *ppDst;
    BOOL    quote;

    quote = (src[0] != WCHAR_QUOTE) && (StrChrW((LPWSTR)src, WCHAR_SPACE) != NULL);

    if (quote)
        *d++ = WCHAR_QUOTE;
    wcscpy(d, src);
    d += wcslen(src);
    if (quote)
        *d++ = WCHAR_QUOTE;
    *d = L'\0';

    *ppDst = d;
}

/***************************************************************************\
* BuildAssocCommandW
*
* Resolve lpFile's "<verb>" command template from the registry association,
* then expand %1 (the file) and %* (the parameters) into lpCmdOut.  Returns
* an SE_ERR_* code on failure, 0 on success.
\***************************************************************************/
static int BuildAssocCommandW(
    LPCWSTR lpVerb,
    LPCWSTR lpFile,
    LPCWSTR lpExt,
    LPCWSTR lpParams,
    LPWSTR  lpCmdOut)
{
    WCHAR   szProgId[CBCOMMAND];
    WCHAR   szKey[CBCOMMAND];
    WCHAR   szTemplate[CBCOMMAND];
    LPWSTR  s;
    LPWSTR  d;
    BOOL    bFileDone   = FALSE;
    BOOL    bParamsDone = FALSE;

    //
    // .ext -> progid
    //
    if (RegReadDefaultW(HKEY_CLASSES_ROOT, lpExt, szProgId, CBCOMMAND) != ERROR_SUCCESS)
        return SE_ERR_NOASSOC;

    //
    // progid\shell\<verb>\command -> template
    //
    if ((wcslen(szProgId) + wcslen(c_szShellPath) + wcslen(lpVerb) +
         wcslen(c_szCommand) + 1) > CBCOMMAND)
        return SE_ERR_NOASSOC;

    wcscpy(szKey, szProgId);
    wcscat(szKey, c_szShellPath);
    wcscat(szKey, lpVerb);
    wcscat(szKey, c_szCommand);

    if (RegReadDefaultW(HKEY_CLASSES_ROOT, szKey, szTemplate, CBCOMMAND) != ERROR_SUCCESS)
        return SE_ERR_NOASSOC;

    //
    // Expand the template.  %1 -> file (raw; the template owns any quoting),
    // %* -> parameters, %% -> %, other %x dropped.
    //
    s = szTemplate;
    d = lpCmdOut;
    while (*s) {
        if (*s == L'%') {
            switch (s[1]) {
            case L'1':
                wcscpy(d, lpFile);
                d += wcslen(lpFile);
                bFileDone = TRUE;
                s += 2;
                continue;
            case L'*':
                if (lpParams) {
                    wcscpy(d, lpParams);
                    d += wcslen(lpParams);
                }
                bParamsDone = TRUE;
                s += 2;
                continue;
            case L'%':
                *d++ = L'%';
                s += 2;
                continue;
            default:
                if (s[1] >= L'2' && s[1] <= L'9') {
                    s += 2;       // unsupported positional arg, drop
                    continue;
                }
                break;            // lone '%', copy literally
            }
        }
        *d++ = *s++;
    }
    *d = L'\0';

    if (!bFileDone) {
        *d++ = WCHAR_SPACE;
        AppendQuoted(&d, lpFile);
    }
    if (lpParams && *lpParams && !bParamsDone) {
        *d++ = WCHAR_SPACE;
        wcscpy(d, lpParams);
    }

    return 0;
}

/***************************************************************************\
* DoShellExecuteW
*
* The headless launch core shared by ShellExecuteW / ShellExecuteA.
\***************************************************************************/
static HINSTANCE DoShellExecuteW(
    LPCWSTR lpOperation,
    LPCWSTR lpFile,
    LPCWSTR lpParameters,
    LPCWSTR lpDirectory,
    INT     nShowCmd)
{
    LPCWSTR             lpVerb;
    LPCWSTR             lpExt;
    LPWSTR              lpCmd;
    DWORD               cchCmd;
    HINSTANCE           hRet;
    STARTUPINFOW        si;
    PROCESS_INFORMATION pi;

    if (!lpFile || !*lpFile)
        return (HINSTANCE)SE_ERR_FNF;

    lpVerb = (lpOperation && *lpOperation) ? lpOperation : c_szOpen;
    lpExt  = wcsrchr(lpFile, WCHAR_DOT);

    //
    // Size a command buffer big enough for the template expansion plus the
    // file and parameters with quotes.
    //
    cchCmd = CBCOMMAND + wcslen(lpFile) +
             (lpParameters ? wcslen(lpParameters) : 0) + 8;

    lpCmd = (LPWSTR)LocalAlloc(LPTR, cchCmd * sizeof(WCHAR));
    if (!lpCmd)
        return (HINSTANCE)SE_ERR_OOM;

    if (lpExt && IsExecutableExtW(lpExt)) {
        //
        // Directly executable: "<file>" <params>, no association needed.
        //
        LPWSTR d = lpCmd;
        AppendQuoted(&d, lpFile);
        if (lpParameters && *lpParameters) {
            *d++ = WCHAR_SPACE;
            wcscpy(d, lpParameters);
        }
    } else {
        int err;

        if (!lpExt) {
            LocalFree(lpCmd);
            return (HINSTANCE)SE_ERR_NOASSOC;
        }

        err = BuildAssocCommandW(lpVerb, lpFile, lpExt, lpParameters, lpCmd);
        if (err != 0) {
            LocalFree(lpCmd);
            return (HINSTANCE)err;
        }
    }

    memset(&si, 0, sizeof(si));
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = (WORD)nShowCmd;

    if (CreateProcessW(NULL, lpCmd, NULL, NULL, FALSE, 0, NULL,
                       (LPWSTR)lpDirectory, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        hRet = SE_OK;
    } else {
        hRet = MapLaunchError();
    }

    LocalFree(lpCmd);
    return hRet;
}

/***************************************************************************\
* DupAToW
*
* Allocate a wide copy of an ANSI string (CP_ACP).  NULL stays NULL.  Free
* with LocalFree.
\***************************************************************************/
static LPWSTR DupAToW(LPCSTR s)
{
    int    cch;
    LPWSTR w;

    if (!s)
        return NULL;

    cch = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    w   = (LPWSTR)LocalAlloc(LPTR, cch * sizeof(WCHAR));
    if (w)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, cch);
    return w;
}

/***************************************************************************\
* ShellExecuteW / ShellExecuteA (API)
\***************************************************************************/
HINSTANCE APIENTRY ShellExecuteW(
    HWND    hwnd,
    LPCWSTR lpOperation,
    LPCWSTR lpFile,
    LPWSTR  lpParameters,
    LPCWSTR lpDirectory,
    INT     nShowCmd)
{
    UNREFERENCED_PARAMETER(hwnd);
    return DoShellExecuteW(lpOperation, lpFile, lpParameters, lpDirectory, nShowCmd);
}

HINSTANCE APIENTRY ShellExecuteA(
    HWND   hwnd,
    LPCSTR lpOperation,
    LPCSTR lpFile,
    LPSTR  lpParameters,
    LPCSTR lpDirectory,
    INT    nShowCmd)
{
    LPWSTR    wOp, wFile, wParams, wDir;
    HINSTANCE hRet;

    UNREFERENCED_PARAMETER(hwnd);

    wOp     = DupAToW(lpOperation);
    wFile   = DupAToW(lpFile);
    wParams = DupAToW(lpParameters);
    wDir    = DupAToW(lpDirectory);

    hRet = DoShellExecuteW(wOp, wFile, wParams, wDir, nShowCmd);

    if (wOp)     LocalFree(wOp);
    if (wFile)   LocalFree(wFile);
    if (wParams) LocalFree(wParams);
    if (wDir)    LocalFree(wDir);

    return hRet;
}

/***************************************************************************\
* FindExecutableW / FindExecutableA (API)
*
* Return the executable that would handle lpFile's "open" verb, in lpResult,
* without launching it.  HINSTANCE > 32 on success.
\***************************************************************************/
HINSTANCE APIENTRY FindExecutableW(
    LPCWSTR lpFile,
    LPCWSTR lpDirectory,
    LPWSTR  lpResult)
{
    WCHAR   szProgId[CBCOMMAND];
    WCHAR   szKey[CBCOMMAND];
    WCHAR   szTemplate[CBCOMMAND];
    LPCWSTR lpExt;
    LPWSTR  s;
    LPWSTR  d;

    UNREFERENCED_PARAMETER(lpDirectory);

    if (lpResult)
        *lpResult = L'\0';

    if (!lpFile || !*lpFile)
        return (HINSTANCE)SE_ERR_FNF;

    lpExt = wcsrchr(lpFile, WCHAR_DOT);
    if (!lpExt)
        return (HINSTANCE)SE_ERR_NOASSOC;

    //
    // A directly-runnable image is its own executable.
    //
    if (IsExecutableExtW(lpExt)) {
        if (GetFullPathNameW(lpFile, CBPATHMAX, lpResult, &s) == 0)
            wcscpy(lpResult, lpFile);
        return SE_OK;
    }

    if (RegReadDefaultW(HKEY_CLASSES_ROOT, lpExt, szProgId, CBCOMMAND) != ERROR_SUCCESS)
        return (HINSTANCE)SE_ERR_NOASSOC;

    wcscpy(szKey, szProgId);
    wcscat(szKey, c_szShellPath);
    wcscat(szKey, c_szOpen);
    wcscat(szKey, c_szCommand);

    if (RegReadDefaultW(HKEY_CLASSES_ROOT, szKey, szTemplate, CBCOMMAND) != ERROR_SUCCESS)
        return (HINSTANCE)SE_ERR_NOASSOC;

    //
    // The executable is the first token of the command template (strip a
    // leading quote, stop at the closing quote or first space, drop any %x).
    //
    s = szTemplate;
    d = lpResult;
    if (*s == WCHAR_QUOTE) {
        s++;
        while (*s && *s != WCHAR_QUOTE)
            *d++ = *s++;
    } else {
        while (*s && *s != WCHAR_SPACE && *s != L'%')
            *d++ = *s++;
    }
    *d = L'\0';

    return SE_OK;
}

HINSTANCE APIENTRY FindExecutableA(
    LPCSTR lpFile,
    LPCSTR lpDirectory,
    LPSTR  lpResult)
{
    LPWSTR    wFile, wDir;
    WCHAR     wResult[CBPATHMAX];
    HINSTANCE hRet;

    wResult[0] = L'\0';
    wFile = DupAToW(lpFile);
    wDir  = DupAToW(lpDirectory);

    hRet = FindExecutableW(wFile, wDir, wResult);

    if (lpResult) {
        WideCharToMultiByte(CP_ACP, 0, wResult, -1, lpResult, CBPATHMAX, NULL, NULL);
    }

    if (wFile) LocalFree(wFile);
    if (wDir)  LocalFree(wDir);

    return hRet;
}
