/*++ BUILD Version: 0003    // Increment this if a change has global effects

Copyright (c) 1991, Microsoft Corporation

Module Name:

    winnlsp.h

Abstract:

    Procedure declarations, constant definitions, and macros for the
    NLS component.

Author:

    Julie Bennett (julieb) 31-May-1991

--*/
#ifndef _WINNLSP_
#define _WINNLSP_
#ifdef __cplusplus
extern "C" {
#endif
#if(WINVER < 0x0400)
#define LOCALE_FONTSIGNATURE       0x00000058   /* font signature */
#endif /* WINVER < 0x0400 */
#ifdef __cplusplus
}
#endif
#endif   // _WINNLSP_
