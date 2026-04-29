/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

    beep.c

Abstract:

    This module contains the Win32 Beep APIs

Author:

    Steve Wood (stevewo)  5-Oct-1991

Revision History:

--*/

#include "basedll.h"
#include <ntddbeep.h>
#include "conapi.h"

/*
 * Forward declaration
 */

VOID NotifySoundSentry(VOID);

HANDLE hBeepDevice = NULL;

NTSTATUS
BaseDllInitializeBeep( VOID )
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING NameString;
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;

    RtlInitUnicodeString( &NameString, DD_BEEP_DEVICE_NAME_U );
    InitializeObjectAttributes( &ObjectAttributes,
                                &NameString,
                                0,
                                NULL,
                                NULL
                              );
    Status = NtCreateFile( &hBeepDevice,
                           FILE_READ_DATA | FILE_WRITE_DATA,
                           &ObjectAttributes,
                           &IoStatus,
                           NULL,
                           0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           FILE_OPEN_IF,
                           0,
                           (PVOID) NULL,
                           0L
                         );

    return( Status );
}




BOOL
APIENTRY
Beep(
    DWORD dwFreq,
    DWORD dwDuration
    )
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    BEEP_SET_PARAMETERS BeepParameters;

    if (hBeepDevice == NULL) {
        Status = BaseDllInitializeBeep();
        if (!NT_SUCCESS( Status )) {
            BaseSetLastNTError( Status );
            return( FALSE );
            }
        }

    //
    // 0,0 is a special case used to turn off a beep.  Otherwise
    // validate the dwFreq parameter to be in range.
    //

    if ((dwFreq != 0 || dwDuration != 0) &&
        (dwFreq < (ULONG)0x25 || dwFreq > (ULONG)0x7FFF)
       ) {
        Status = STATUS_INVALID_PARAMETER;
        }
    else {
        BeepParameters.Frequency = dwFreq;
        BeepParameters.Duration = dwDuration;

        Status = NtDeviceIoControlFile( hBeepDevice,
                                        NULL,
                                        NULL,
                                        NULL,
                                        &IoStatus,
                                        IOCTL_BEEP_SET,
                                        &BeepParameters,
                                        sizeof( BeepParameters ),
                                        NULL,
                                        0
                                      );
        }

    NotifySoundSentry();

    if (!NT_SUCCESS( Status )) {
        BaseSetLastNTError( Status );
        return( FALSE );
        }
    else {
        //
        // Beep device is asynchronous, so sleep for duration
        // to allow this beep to complete.
        //

        if (dwDuration != (DWORD)-1 && (dwFreq != 0 || dwDuration != 0)) {
            SleepEx( dwDuration, TRUE );
            }

        return( TRUE );
        }
}


VOID
NotifySoundSentry(VOID)
{
    //
    // MicroNT: csrss-free.  Original code LPC'd into basesrv with
    // BasepSoundSentryNotification so csrss could flash the screen for
    // accessibility users when a beep would be inaudible (full-screen
    // DOS box).  We have no console subsystem and no DOS box; the beep
    // device itself still works via NtDeviceIoControlFile in Beep().
    // Silent no-op preserves the export — every Beep() caller already
    // ignores the return.
    //
}
