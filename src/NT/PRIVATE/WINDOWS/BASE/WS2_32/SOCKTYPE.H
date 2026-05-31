/*++

Copyright (c) 1992 Microsoft Corporation

Module Name:

    SockType.h

Abstract:

    Contains type definitions for the WinSock DLL.

Author:

    David Treadwell (davidtr)    25-Feb-1992

Revision History:

--*/

#ifndef _SOCKTYPE_
#define _SOCKTYPE_

// !!! this belongs someplace else!!!

struct sockaddr_nb {
    short           snb_family;
    unsigned short  snb_type;
    char            snb_address[16];
};

//
// Winsock 2 surface needed by WSAIoctl().  The DLL is built on the Winsock
// 1.1 <winsock.h>, which predates these, so we declare the minimum here
// rather than pulling in a full <winsock2.h>/<mstcpip.h>.  IOC_IN/IOC_OUT,
// SOCKET and WSAEOPNOTSUPP already come from <winsock.h>.
//

#define IOC_VENDOR  0x18000000
#define IOC_WS2     0x08000000

#define _WSAIO(x,y)    (IOC_VOID|(x)|(y))
#define _WSAIOR(x,y)   (IOC_OUT|(x)|(y))
#define _WSAIOW(x,y)   (IOC_IN|(x)|(y))
#define _WSAIORW(x,y)  (IOC_INOUT|(x)|(y))

//
// The only control codes WSAIoctl() handles (see WSAMISC.c).  SIO_RCVALL and
// the extension-function/overlapped codes are deliberately rejected there.
//

#define SIO_RCVALL                          _WSAIOW(IOC_VENDOR,1)
#define SIO_KEEPALIVE_VALS                  _WSAIOW(IOC_VENDOR,4)
#define SIO_GET_EXTENSION_FUNCTION_POINTER  _WSAIORW(IOC_WS2,6)

//
// Argument structure for SIO_KEEPALIVE_VALS.  onoff enables/disables
// keepalive on the connection; keepalivetime/keepaliveinterval are accepted
// but governed by the stack's global timers (this TCP is NT4-era and has no
// per-socket keepalive timers -- see WSAMISC.c).
//

struct tcp_keepalive {
    u_long  onoff;
    u_long  keepalivetime;
    u_long  keepaliveinterval;
};

//
// WSAIoctl() is synchronous-only here, so the overlapped argument types are
// just the Win32 OVERLAPPED forms; the overlapped path itself is rejected.
//

typedef OVERLAPPED   WSAOVERLAPPED;
typedef OVERLAPPED * LPWSAOVERLAPPED;

typedef
void
(CALLBACK * LPWSAOVERLAPPED_COMPLETION_ROUTINE) (
    IN DWORD dwError,
    IN DWORD cbTransferred,
    IN LPWSAOVERLAPPED lpOverlapped,
    IN DWORD dwFlags
    );

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
    );

//
// SOCKET_STATE defines the various states a socket may have.  Note that
// not all states are valid for all types of sockets.
//

typedef enum _SOCKET_STATE {
    SocketStateOpen,
    SocketStateBound,
    SocketStateBoundSpecific,           // Datagram only
    SocketStateListening,               // VC server only
    SocketStateConnected,               // VC only
    SocketStateClosing
} SOCKET_STATE, *PSOCKET_STATE;

//
// SOCKET_INFORMATION stores information for each socket opened by a
// process.  These structures are stored in the generic table
// SocketTable.  Different fields of this structure are filled in during
// different APIs.
//

typedef struct _SOCKET_INFORMATION {

    SOCKET_STATE State;
    ULONG ReferenceCount;
    LIST_ENTRY SocketListEntry;
    SOCKET Handle;
    DWORD SocketSerialNumber;
    INT AddressFamily;
    INT SocketType;
    INT Protocol;
    PSOCKADDR LocalAddress;
    INT LocalAddressLength;
    PSOCKADDR RemoteAddress;
    INT RemoteAddressLength;

    HANDLE TdiAddressHandle;
    HANDLE TdiConnectionHandle;

    //
    // Info stored for WSAAsyncSelect().
    //

    DWORD AsyncSelectSerialNumber;
    HWND AsyncSelecthWnd;
    UINT AsyncSelectwMsg;
    LONG AsyncSelectlEvent;
    LONG DisabledAsyncSelectEvents;

    //
    // Socket options controlled by getsockopt(), setsockopt().
    //

    struct linger LingerInfo;
    ULONG SendTimeout;
    ULONG ReceiveTimeout;
    ULONG ReceiveBufferSize;
    ULONG SendBufferSize;
    BOOLEAN Broadcast;
    BOOLEAN Debug;
    BOOLEAN ReuseAddresses;
    BOOLEAN NonBlocking;
    BOOLEAN DontUseWildcard;
    BOOLEAN KeepAlive;

    BOOLEAN ConnectInProgress;

    //
    // Shutdown state.
    //

    BOOLEAN ReceiveShutdown;
    BOOLEAN SendShutdown;

    BOOLEAN ConnectOutstanding;

    //
    // A resource to synchronize access to the socket.
    //

    RTL_RESOURCE Lock;

} SOCKET_INFORMATION, *PSOCKET_INFORMATION;

//
// A typedef for blocking hook functions.
//

typedef
BOOL
(*PBLOCKING_HOOK) (
    VOID
    );

//
// Structures, etc. to support per-thread variable storage in this DLL.
//

#define MAXALIASES      35
#define MAXADDRS        35

#define HOSTDB_SIZE     (_MAX_PATH + 7)   // 7 == strlen("\\hosts") + 1

typedef struct _WINSOCK_TLS_DATA {
    char *GETHOST_h_addr_ptrs[MAXADDRS + 1];
    struct hostent GETHOST_host;
    char *GETHOST_host_aliases[MAXALIASES];
    char GETHOST_hostbuf[BUFSIZ+1];
    struct in_addr GETHOST_host_addr;
    char GETHOST_HOSTDB[HOSTDB_SIZE];
    FILE *GETHOST_hostf;
    char GETHOST_hostaddr[MAXADDRS];
    char *GETHOST_host_addrs[2];
    int GETHOST_stayopen;
    char GETPROTO_PROTODB[PROTODB_SIZE];
    FILE *GETPROTO_protof;
    char GETPROTO_line[BUFSIZ+1];
    struct protoent GETPROTO_proto;
    char *GETPROTO_proto_aliases[MAXALIASES];
    int GETPROTO_stayopen;
    char GETSERV_SERVDB[SERVDB_SIZE];
    FILE *GETSERV_servf;
    char GETSERV_line[BUFSIZ+1];
    struct servent GETSERV_serv;
    char *GETSERV_serv_aliases[MAXALIASES];
    int GETSERV_stayopen;
    struct state R_INIT__res;
    char INTOA_Buffer[18];
    SOCKET DnrSocketHandle;
    BOOLEAN IsBlocking;
    BOOLEAN IoCancelled;
    BOOLEAN ProcessingGetXByY;
    BOOLEAN GetXByYCancelled;
    BOOLEAN EnableWinsNameResolution;
    BOOLEAN DisableWinsNameResolution;
    SOCKET SocketHandle;
    PBLOCKING_HOOK BlockingHook;
    HANDLE EventHandle;
    ULONG CreateOptions;
#if DBG
    ULONG IndentLevel;
#endif
} WINSOCK_TLS_DATA, *PWINSOCK_TLS_DATA;

#define ACCESS_THREAD_DATA(a,file) \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))-> \
                ## file ## _ ## a )

#define _res ACCESS_THREAD_DATA( _res, R_INIT )

#define SockThreadIsBlocking \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->IsBlocking )

#define SockThreadIoCancelled \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->IoCancelled )

#define SockThreadProcessingGetXByY \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->ProcessingGetXByY )

#define SockThreadGetXByYCancelled \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->GetXByYCancelled )

#define SockThreadSocketHandle \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->SocketHandle )

#define SockThreadBlockingHook \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->BlockingHook )

#define SockThreadEvent \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->EventHandle )

#define SockDnrSocket \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->DnrSocketHandle )

#define SockEnableWinsNameResolution \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->EnableWinsNameResolution )

#define SockDisableWinsNameResolution \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->DisableWinsNameResolution )

#define SockCreateOptions \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->CreateOptions )

#if DBG
#define SockIndentLevel \
            ( ((PWINSOCK_TLS_DATA)TlsGetValue( SockTlsSlot ))->IndentLevel )
#endif

typedef struct _WINSOCK_CONTEXT_BLOCK {

    LIST_ENTRY AsyncThreadQueueListEntry;
    DWORD TaskHandle;
    DWORD OpCode;

    union {

        struct {
            HWND hWnd;
            unsigned int wMsg;
            PCHAR Filter;
            int Length;
            int Type;
            PCHAR Buffer;
            int BufferLength;
        } AsyncGetHost;

        struct {
            HWND hWnd;
            unsigned int wMsg;
            PCHAR Filter;
            PCHAR Buffer;
            int BufferLength;
        } AsyncGetProto;

        struct {
            HWND hWnd;
            unsigned int wMsg;
            PCHAR Filter;
            PCHAR Protocol;
            PCHAR Buffer;
            int BufferLength;
        } AsyncGetServ;

        struct {
            SOCKET SocketHandle;
            DWORD SocketSerialNumber;
            DWORD AsyncSelectSerialNumber;
        } AsyncSelect;

    } Overlay;

} WINSOCK_CONTEXT_BLOCK, *PWINSOCK_CONTEXT_BLOCK;

//
// Opcodes for processing by the winsock asynchronous processing
// thread.
//

#define WS_OPCODE_GET_HOST_BY_ADDR    0x01
#define WS_OPCODE_GET_HOST_BY_NAME    0x02
#define WS_OPCODE_GET_PROTO_BY_NUMBER 0x03
#define WS_OPCODE_GET_PROTO_BY_NAME   0x04
#define WS_OPCODE_GET_SERV_BY_PORT    0x05
#define WS_OPCODE_GET_SERV_BY_NAME    0x06
#define WS_OPCODE_ASYNC_SELECT        0x07

#define WS_OPCODE_TERMINATE           0x10

//
// Prototype for a post message routine.  This must have the same prototype
// as PostMessage().
//

typedef
BOOL
(*PWINSOCK_POST_ROUTINE) (
    HWND hWnd,
    UINT Msg,
    WPARAM wParam,
    LPARAM lParam
    );

#endif // ndef _SOCKTYPE_
