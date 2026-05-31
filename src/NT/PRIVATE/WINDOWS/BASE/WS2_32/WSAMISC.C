/*++

Copyright (c) 1992 Microsoft Corporation

Module Name:

    WsaMisc.c

Abstract:

    This module contains support for the following WinSock APIs;

        WSACancelAsyncRequest()
        WSACencelBlockingCall()
        WSACleanup()
        WSAGetLastError()
        WSAIoctl()
        WSAIsBlocking()
        WSASetBlockingHook()
        WSAUnhookBlockingHook()
        WSASetLastError()
        WSAStartup()

Author:

    David Treadwell (davidtr)    15-May-1992

Revision History:

--*/

#include "winsockp.h"


int PASCAL
WSACleanup (
    VOID
    )

/*++

Routine Description:

    An application is required to perform a (successful) WSAStartup()
    call before it can use Windows Sockets services.  When it has
    completed the use of Windows Sockets, the application may call
    WSACleanup() to deregister itself from a Windows Sockets
    implementation.

Arguments:

    None.

Return Value:

    The return value is 0 if the operation was successful.  Otherwise
    the value SOCKET_ERROR is returned, and a specific error number may
    be retrieved by calling WSAGetLastError().

--*/

{
    PSOCKET_INFORMATION socket;
    LINGER lingerInfo;
    PLIST_ENTRY listEntry;

    WS_ENTER( "WSACleanup", NULL, NULL, NULL, NULL );

    if ( !SockEnterApi( TRUE, TRUE, FALSE ) ) {
        WS_EXIT( "WSACleanup", SOCKET_ERROR, TRUE );
        return SOCKET_ERROR;
    }

    //
    // Don't synchronize DLL termination--just blow away all the
    // sockets.  This is necessary to prevent deadlocks in abnormal
    // termination of the process.
    //

    SockAcquireGlobalLockExclusive( );

    //
    // Decrement the reference count of calls to WSAStartup().
    //

    SockWsaStartupCount--;

    //
    // If the count of calls to WSAStartup() is not 0, we shouldn't do
    // cleanup yet.  Just return.
    //

    if ( SockWsaStartupCount > 0 ) {
        SockReleaseGlobalLock( );
        IF_DEBUG(MISC) {
            WS_PRINT(( "Leaving WSACleanup().\n" ));
        }
        WS_EXIT( "WSACleanup", NO_ERROR, FALSE );
        return NO_ERROR;
    }

    //
    // Indicate that the DLL is no longer initialized.  This will
    // result in all open sockets being abortively disconnected.
    //

    SockTerminating = TRUE;;

    //
    // Close each open socket.  We loop looking for open sockets until
    // all sockets are either off the list of in the closing state.
    //

    for ( listEntry = SocketListHead.Flink;
          listEntry != &SocketListHead; ) {

        SOCKET socketHandle;

        socket = CONTAINING_RECORD(
                     listEntry,
                     SOCKET_INFORMATION,
                     SocketListEntry
                     );

        //
        // If this socket is about to close, go on to the next socket.
        //

        if ( socket->State == SocketStateClosing ) {
            listEntry = listEntry->Flink;
            continue;
        }

        //
        // Pull the handle into a local in case another thread closes
        // this socket just as we are trying to close it.
        //

        socketHandle = socket->Handle;

        //
        // Release the global lock so that we don't cause a deadlock
        // from out-of-order lock acquisitions.
        //

        SockReleaseGlobalLock( );

        //
        // Set each socket to linger for 0 seconds.  This will cause
        // the connection to reset, if appropriate, when we close the
        // socket.
        //

        lingerInfo.l_onoff = 1;
        lingerInfo.l_linger = 0;
        setsockopt(
            socketHandle,
            SOL_SOCKET,
            SO_LINGER,
            (char *)&lingerInfo,
            sizeof(lingerInfo)
            );

        //
        // Perform the actual close of the socket.
        //

        closesocket( socketHandle );

        SockAcquireGlobalLockExclusive( );

        //
        // Restart the search from the beginning of the list.  We cannot
        // use listEntry->Flink because the socket that is pointed to by
        // listEntry may have been freed.
        //

        listEntry = SocketListHead.Flink;
    }

    SockReleaseGlobalLock( );

    //
    // Free cached information about helper DLLs.
    //
    // !!! we need some way to synchronize this with all sockets closing--
    //     refcnts on helper DLL info structs?


    IF_DEBUG(MISC) {
        WS_PRINT(( "Leaving WSACleanup().\n" ));
    }

    WS_EXIT( "WSACleanup", NO_ERROR, FALSE );
    return NO_ERROR;

} // WSACleanup


int PASCAL
WSAGetLastError(
    VOID
    )

/*++

Routine Description:

    This function returns the last network error that occurred.  When a
    particular Windows Sockets API function indicates that an error has
    occurred, this function should be called to retrieve the appropriate
    error code.

Arguments:

    None.

Return Value:

    The return value indicates the error code for the last Windows
    Sockets API routine performed by this thread.

--*/

{

    return GetLastError( );

} // WSAGetLastError


void PASCAL
WSASetLastError(
    IN int Error
    )

/*++

Routine Description:

    This function allows an application to set the error code to be
    returned by a subsequent WSAGetLastError() call for the current
    thread.  Note that any subsequent Windows Sockets routine called by
    the application will override the error code as set by this routine.

Arguments:

    iError - Specifies the error code to be returned by a subsequent
        WSAGetLastError() call.

Return Value:

    None.

--*/

{

    SetLastError( Error );

} // WSASetLastError


int PASCAL
WSAStartup (
    WORD wVersionRequired,
    LPWSADATA lpWsaData
    )

/*++

Routine Description:

    This function MUST be the first Windows Sockets function called by
    an application.  It allows an application to specify the version of
    Windows Sockets API required and to retrieve details of the specific
    Windows Sockets implementation.  The application may only issue
    further Windows Sockets API functions after a successful
    WSAStartup() invocation.

    In order to support future Windows Sockets implementations and
    applications which may have functionality differences from Windows
    Sockets 1.0, a negotiation takes place in WSAStartup().  An
    application passes to WSAStartup() the highest Windows Sockets
    version that it can take advantage of.  If this version is lower
    than the lowest version supported by the Windows Sockets DLL, the
    DLL cannot support the application and WSAStartup() returns
    WSAVERNOTSUPPORTED.  Otherwise, the DLL will attempt to register the
    application as a client: if this fails, WSAStartup() fails and
    returns WSASYSNOTREADY.  If the DLL can support the application and
    the registration process succeeds, .the function stores the highest
    version of Windows Sockets supported by the DLL in the wHighVersion
    element of the WSAData structure and returns 0.  If wHighVersion is
    lower than the lowest version supported by the application, the
    application either fails its initialization or attempts to find
    another Windows Sockets DLL on the system.

    This negotiation allows both a Windows Sockets DLL and a Windows
    Sockets application to support a range of Windows Sockets versions.
    An application can successfully utilize a DLL if there is any
    overlap in the versions.  The following chart gives examples of how
    WSAStartup() works in conjunction with different application and DLL
    versions:

    App       DLL       wVersionRequired  wHighVersion  Result
    versions  Versions

    1.0       1.0       1.0               1.0           use
                                                        1.0

    1.0 2.0   1.0       2.0               1.0           use
                                                        1.0

    1.0       1.0 2.0   1.0               2.0           use

    1.0       2.0 3.0   1.0               (failure)     fail

    2.0 3.0   1.0       3.0               1.0           fail

    1.0 2.0   1.0 2.0   3.0               3.0           use

    Once an application has made a successful WSAStartup() call, it may
    proceed to make other Windows Sockets API calls as needed.  When it
    has finished using the services of the Windows Sockets DLL, the
    application should call WSACleanup().

    Details of the actual Windows Sockets implementation are described
    in the WSAData structure defined as follows:

    struct WSAData {
         WORD wVersion;
         WORD wHighVersion;
         char szDescription[WSADESCRIPTION_LEN+1];
         char szSystemStatus[WSASYSSTATUS_LEN+1];
         int  iMaxSockets;
         int  iMaxUdpDg;
         char FAR *     lpVendorInfo
    };

    The members of this structure are:

    Element        Usage

    wVersion - The version of the Windows Sockets DLL, encoded as for
        wVersionRequired.

    wHighVersion - The highest version of the Windows Sockets
        specification that this DLL can support (also encoded as above).
        Normally this will be the same as wVersion.

    szDescription - A null-terminated ASCII string into which the
        Windows Sockets DLL copies a description of the Windows Sockets
        implementation, including vendor identification.  The text (up
        to 256 characters in length) may contain any characters, but
        vendors are cautioned against including control and formatting
        characters: the most likely use that an application will put
        this to is to display it (possibly truncated) in a status
        message.

    szSystemStatus - A null-terminated ASCII string into which the
        Windows Sockets DLL copies relevant status or configuration
        information.  The Windows Sockets DLL should use this field only
        if the information might be useful to the user or support staff:
        it should not be considered as an extension of the szDescription
        field.

    iMaxSockets - The maximum number of sockets which a single process
        can potentially open.  A Windows Sockets implementation may
        provide a global pool of sockets for allocation to any process;
        alternatively it may allocate per-process resources for sockets.
        The number may well reflect the way in which the Windows Sockets
        DLL or the networking software was configured.  Application
        writers may use this number as a crude indication of whether the
        Windows Sockets implementation is usable by the application.
        For example, an X Windows server might check iMaxSockets when
        first started: if it is less than 8, the application would
        display an error message instructing the user to reconfigure the
        networking software.  (This is a situation in which the
        szSystemStatus text might be used.) Obviously there is no
        guarantee that a particular application can actually allocate
        iMaxSockets sockets, since there may be other Windows Sockets
        applications in use.

    iMaxUdpDg - The size in bytes of the largest UDP datagram that can
        be sent or received by a Windows Sockets application.  If the
        implementation imposes no limit, iMaxUdpDg is zero.  In many
        implementations of Berkeley sockets, there is an implicit limit
        of 8192 bytes on UDP datagrams (which are fragmented if
        necessary).  A Windows Sockets implementation may impose a limit
        based, for instance, on the allocation of fragment reassembly
        buffers.  The minimum value of iMaxUdpDg for a compliant Windows
        Sockets implementation is 512.  Note that regardless of the
        value of iMaxUdpDg, it is inadvisable to attempt to send a
        broadcast datagram which is larger than the Maximum Transmission
        Unit (MTU) for the network.  (The Windows Sockets API does not
        provide a mechanism to discover the MTU, but it must be no less
        than 512 bytes.)

    lpVendorInfo - A far pointer to a vendor-specific data structure.
        The definition of this structure (if supplied) is beyond the
        scope of this specification.

Arguments:

    None.

Return Value:

    WSAStartup() returns zero if successful.  Otherwise it returns one
    of the error codes listed below.  Note that the normal mechanism
    whereby the application calls WSAGetLastError() to determine the
    error code cannot be used, since the Windows Sockets DLL may not
    have established the client data area where the "last error"
    information is stored.

--*/

{
    WS_ENTER( "WSAStartup", (PVOID)wVersionRequired, lpWsaData, NULL, NULL );

    if ( !SockEnterApi( FALSE, TRUE, FALSE ) ) {
        WS_EXIT( "WSAStartup", GetLastError( ), TRUE );
        return GetLastError( );
    }

    //
    // We don't support WinSock versions below 1.0.  The low byte 
    // contains the major revision number, the high byte contains the 
    // minor revision number.  Note that is WSAStartup() has already
    // been called we don't do this versions negotiation.
    //

    SockAcquireGlobalLockExclusive( );

    if ( SockWsaStartupCount == 0 &&
         ( LOBYTE(wVersionRequired) < 0x01 ||
               ( LOBYTE(wVersionRequired) == 0x01 &&
                 HIBYTE(wVersionRequired) < 0x01 ) ) ) {
        SockReleaseGlobalLock( );
        SetLastError( WSAVERNOTSUPPORTED );
        WS_EXIT( "WSAStartup", WSAVERNOTSUPPORTED, TRUE );
        return WSAVERNOTSUPPORTED;
    }

    //
    // If WSAStartup() has already been called, then the caller must
    // pass in the same version number as we're using, which must
    // be version 1.1.
    //

    if ( SockWsaStartupCount != 0 &&
             wVersionRequired != 0x0101 ) {
        SockReleaseGlobalLock( );
        SetLastError( WSAVERNOTSUPPORTED );
        WS_EXIT( "WSAStartup", WSAVERNOTSUPPORTED, TRUE );
        return WSAVERNOTSUPPORTED;
    }

    //
    // Remember that the app has called WSAStartup.
    //

    SockWsaStartupCount++;
    SockReleaseGlobalLock( );

    //
    // Fill in the WSAData structure.
    //

    lpWsaData->wVersion = 0x0101;
    lpWsaData->wHighVersion = 0x0101;
    strcpy( lpWsaData->szDescription, "Microsoft Windows Sockets Version 1.1." );
    strcpy( lpWsaData->szSystemStatus, "Running." );
    lpWsaData->iMaxSockets = 0x7FFF;
    lpWsaData->iMaxUdpDg = 65535-8;

    SockTerminating = FALSE;

    WS_EXIT( "WSAStartup", NO_ERROR, FALSE );
    return NO_ERROR;

} // WSAStartup


int PASCAL
WSAIoctl (
    SOCKET s,
    DWORD dwIoControlCode,
    LPVOID lpvInBuffer,
    DWORD cbInBuffer,
    LPVOID lpvOutBuffer,
    DWORD cbOutBuffer,
    LPDWORD lpcbBytesReturned,
    LPWSAOVERLAPPED lpOverlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    )

/*++

Routine Description:

    Performs a control operation on a socket.  This implementation supports
    only the synchronous control codes a Winsock 1.1-era BSD application needs;
    it is not the full Winsock 2 ioctl surface.

    Specifically:

        SIO_KEEPALIVE_VALS - enable/disable TCP keepalive on a connected VC
            socket.  The tcp_keepalive 'onoff' field is honored for real (it is
            pushed through AFD to the TCP transport).  The 'keepalivetime' and
            'keepaliveinterval' fields are accepted but NOT applied per-socket:
            this TCP is NT4-era and uses global keepalive timers
            (KeepAliveTime/KeepAliveInterval).  Per-socket timers
            (TCP_SOCKET_KEEPALIVE_VALS) are a Windows 2000 feature and are not
            implemented here.

    Everything else -- SIO_RCVALL (promiscuous capture; out of scope on a
    single-NIC, non-routing host), SIO_GET_EXTENSION_FUNCTION_POINTER
    (AcceptEx/ConnectEx), and any overlapped request -- is rejected with
    WSAEOPNOTSUPP so that this one entry point cannot pull in the overlapped /
    IOCP / mswsock surface.

Arguments:

    s - A descriptor identifying a socket.

    dwIoControlCode - The control code of the operation to perform.

    lpvInBuffer - A pointer to the input buffer.

    cbInBuffer - The size, in bytes, of the input buffer.

    lpvOutBuffer - A pointer to the output buffer (unused here).

    cbOutBuffer - The size, in bytes, of the output buffer (unused here).

    lpcbBytesReturned - A pointer to the actual number of bytes of output.

    lpOverlapped - A pointer to a WSAOVERLAPPED structure.  Must be NULL --
        overlapped operation is not supported.

    lpCompletionRoutine - A completion routine.  Must be NULL.

Return Value:

    Upon successful completion, WSAIoctl() returns 0.  Otherwise a value of
    SOCKET_ERROR is returned, and a specific error code may be retrieved by
    calling WSAGetLastError().

--*/

{
    ULONG error;
    PSOCKET_INFORMATION socket;

    WS_ENTER( "WSAIoctl", (PVOID)s, (PVOID)dwIoControlCode, lpvInBuffer, lpOverlapped );

    if ( !SockEnterApi( TRUE, TRUE, FALSE ) ) {
        WS_EXIT( "WSAIoctl", SOCKET_ERROR, TRUE );
        return SOCKET_ERROR;
    }

    //
    // Overlapped operation is not supported.  Reject it before touching the
    // socket so the overlapped/IOCP path can never be reached through here.
    //

    if ( lpOverlapped != NULL || lpCompletionRoutine != NULL ) {
        SetLastError( WSAEOPNOTSUPP );
        WS_EXIT( "WSAIoctl", SOCKET_ERROR, TRUE );
        return SOCKET_ERROR;
    }

    error = NO_ERROR;

    socket = SockFindAndReferenceSocket( s, TRUE );

    if ( socket == NULL ) {
        SetLastError( WSAENOTSOCK );
        WS_EXIT( "WSAIoctl", SOCKET_ERROR, TRUE );
        return SOCKET_ERROR;
    }

    SockAcquireSocketLockExclusive( socket );

    switch ( dwIoControlCode ) {

    case SIO_KEEPALIVE_VALS: {

        struct tcp_keepalive *keepAliveVals;
        BOOLEAN enable;

        //
        // Keepalive only applies to connection-oriented sockets.
        //

        if ( socket->SocketType == SOCK_DGRAM ) {
            error = WSAEINVAL;
            goto exit;
        }

        if ( lpvInBuffer == NULL ||
                 cbInBuffer < sizeof(struct tcp_keepalive) ) {
            error = WSAEFAULT;
            goto exit;
        }

        keepAliveVals = (struct tcp_keepalive *)lpvInBuffer;
        enable = (BOOLEAN)( keepAliveVals->onoff != 0 );

        //
        // Hand the on/off state to AFD, which pushes it to the TCP transport
        // (immediately if the socket is connected, otherwise when it
        // connects).  keepalivetime/keepaliveinterval are intentionally
        // ignored -- see the routine description.
        //

        error = SockSetInformation(
                    socket,
                    AFD_KEEPALIVE,
                    &enable,
                    NULL,
                    NULL
                    );

        if ( error == NO_ERROR ) {
            socket->KeepAlive = enable;
        }

        if ( ARGUMENT_PRESENT( lpcbBytesReturned ) ) {
            *lpcbBytesReturned = 0;
        }

        break;
    }

    default:

        //
        // SIO_RCVALL, SIO_GET_EXTENSION_FUNCTION_POINTER, and every other
        // code are not supported.
        //

        error = WSAEOPNOTSUPP;
        goto exit;
    }

exit:

    IF_DEBUG(SOCKOPT) {
        if ( error != NO_ERROR ) {
            WS_PRINT(( "WSAIoctl on socket %lx, code %lx failed: %ld\n",
                           s, dwIoControlCode, error ));
        } else {
            WS_PRINT(( "WSAIoctl on socket %lx code %lx succeeded\n",
                           s, dwIoControlCode ));
        }
    }

    SockReleaseSocketLock( socket );
    SockDereferenceSocket( socket );

    if ( error != NO_ERROR ) {
        SetLastError( error );
        WS_EXIT( "WSAIoctl", SOCKET_ERROR, TRUE );
        return SOCKET_ERROR;
    }

    WS_EXIT( "WSAIoctl", NO_ERROR, FALSE );
    return NO_ERROR;

} // WSAIoctl


int PASCAL
WSApSetPostRoutine (
    IN PVOID PostRoutine
    )
{

    SockPostRoutine = PostRoutine;

    return NO_ERROR;

} // WSApSetPostRoutine
