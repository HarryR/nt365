/*++

Copyright (c) 1990-1993  Microsoft Corporation

Module Name:

    WinSpolp.h

Abstract:

    Header file for Print APIs

Revision History:

--*/
#ifndef _WINSPOLP_
#define _WINSPOLP_
#ifdef __cplusplus
extern "C" {
#endif
#if(WINVER < 0x0400)
#include <prsht.h>
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define PRINTER_CONTROL_AVAILABLE        4
#define PRINTER_CONTROL_UNAVAILABLE      5
#define PRINTER_CONTROL_SET_STATUS       6
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define PRINTER_STATUS_UNAVAILABLE       0x00800000
#define PRINTER_STATUS_POWER_SAVE        0x01000000
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define PRINTER_ATTRIBUTE_WORK_OFFLINE   0x00000400
#endif /* WINVER < 0x0400 */
#define PRINTER_ATTRIBUTE_UPDATEWININI      0x80000000
#if(WINVER < 0x0400)
#define JOB_CONTROL_DELETE             5
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define JOB_STATUS_USER_INTERVENTION PRINTER_STATUS_USER_INTERVENTION   // 0x00010000
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
typedef struct _DRIVER_INFO_3% {
    DWORD   cVersion;
    LPTSTR%   pName;                    // QMS 810
    LPTSTR%   pEnvironment;             // Win32 x86
    LPTSTR%   pDriverPath;              // c:\drivers\pscript.dll
    LPTSTR%   pDataFile;                // c:\drivers\QMS810.PPD
    LPTSTR%   pConfigFile;              // c:\drivers\PSCRPTUI.DLL
    LPTSTR%   pHelpFile;                // c:\drivers\PSCRPTUI.HLP
    LPTSTR%   pDependentFiles;          // PSCRIPT.DLL\0QMS810.PPD\0PSCRIPTUI.DLL\0PSCRIPTUI.HLP\0PSTEST.TXT\0\0
    LPTSTR%   pMonitorName;             // "PJL monitor"
    LPTSTR%   pDefaultDataType;         // "EMF"
} DRIVER_INFO_3%, *PDRIVER_INFO_3%, *LPDRIVER_INFO_3%;
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
typedef struct _DOC_INFO_2% {
    LPTSTR%   pDocName;
    LPTSTR%   pOutputFile;
    LPTSTR%   pDatatype;
    DWORD   dwMode;
    DWORD   JobId;
} DOC_INFO_2%, *PDOC_INFO_2%, *LPDOC_INFO_2%;

#define DI_CHANNEL              1    // start direct read/write channel,

//Internal for printprocessor interface
#define DI_CHANNEL_WRITE        2    // Direct write only - background read thread ok

#define DI_READ_SPOOL_JOB       3

#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
typedef struct _PORT_INFO_2% {
    LPTSTR%   pPortName;
    LPTSTR%   pMonitorName;
    LPTSTR%   pDescription;
} PORT_INFO_2%, *PPORT_INFO_2%, *LPPORT_INFO_2%;
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
BOOL
WINAPI
EnumPrinterPropertySheets(
    HANDLE  hPrinter,
    HWND    hWnd,
    LPFNADDPROPSHEETPAGE    lpfnAdd,
    LPARAM  lParam
);
#endif /* WINVER < 0x0400 */
#ifdef __cplusplus
}
#endif
#endif // _WINSPOLP_
