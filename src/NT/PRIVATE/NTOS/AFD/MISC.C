/*++

Copyright (c) 1989  Microsoft Corporation

Module Name:

    misc.c

Abstract:

    This module contains the miscellaneous AFD routines.

Author:

    David Treadwell (davidtr)    13-Nov-1992

Revision History:

--*/

#include "afdp.h"
#define FAR
#define TL_INSTANCE 0
#include <ipexport.h>
#include <tdiinfo.h>
#include <tcpinfo.h>
#include <ntddtcp.h>

VOID
AfdDoWork (
    IN PVOID Context
    );

NTSTATUS
AfdRestartDeviceControl (
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text( PAGE, AfdQueryHandles )
#pragma alloc_text( PAGE, AfdGetInformation )
#pragma alloc_text( PAGE, AfdSetInformation )
#pragma alloc_text( PAGE, AfdGetContext )
#pragma alloc_text( PAGE, AfdGetContextLength )
#pragma alloc_text( PAGE, AfdSetContext )
#pragma alloc_text( PAGE, AfdIssueDeviceControl )
#pragma alloc_text( PAGE, AfdSetEventHandler )
#pragma alloc_text( PAGE, AfdInsertNewEndpointInList )
#pragma alloc_text( PAGE, AfdRemoveEndpointFromList )
#pragma alloc_text( PAGEAFD, AfdCompleteIrpList )
#pragma alloc_text( PAGEAFD, AfdErrorEventHandler )
//#pragma alloc_text( PAGEAFD, AfdRestartDeviceControl ) // can't ever be paged!
#pragma alloc_text( PAGEAFD, AfdDoWork )
#pragma alloc_text( PAGEAFD, AfdQueueWorkItem )
#if DBG
#pragma alloc_text( PAGEAFD, AfdIoCallDriverDebug )
#else
#pragma alloc_text( PAGEAFD, AfdIoCallDriverFree )
#endif
#endif


VOID
AfdCompleteIrpList (
    IN PLIST_ENTRY IrpListHead,
    IN PKSPIN_LOCK SpinLock,
    IN NTSTATUS Status
    )

/*++

Routine Description:

    Completes a list of IRPs with the specified status.

Arguments:

    IrpListHead - the head of the list of IRPs to complete.

    SpinLock - a lock which protects the list of IRPs.

    Status - the status to use for completing the IRPs.

Return Value:

    None.

--*/

{
    PLIST_ENTRY listEntry;
    PIRP irp;
    KIRQL oldIrql;
    KIRQL cancelIrql;

    IoAcquireCancelSpinLock( &cancelIrql );
    KeAcquireSpinLock( SpinLock, &oldIrql );

    while ( !IsListEmpty( IrpListHead ) ) {

        //
        // Remove the first IRP from the list, get a pointer to
        // the IRP and reset the cancel routine in the IRP.  The
        // IRP is no longer cancellable.
        //

        listEntry = RemoveHeadList( IrpListHead );
        irp = CONTAINING_RECORD( listEntry, IRP, Tail.Overlay.ListEntry );
        IoSetCancelRoutine( irp, NULL );

        //
        // We must release the locks in order to actually
        // complete the IRP.  It is OK to release these locks
        // because we don't maintain any absolute pointer into
        // the list; the loop termination condition is just
        // whether the list is completely empty.
        //

        KeReleaseSpinLock( SpinLock, oldIrql );
        IoReleaseCancelSpinLock( cancelIrql );

        //
        // Complete the IRP.
        //

        irp->IoStatus.Status = Status;
        irp->IoStatus.Information = 0;

        IoCompleteRequest( irp, AfdPriorityBoost );

        //
        // Reacquire the locks and continue completing IRPs.
        //

        IoAcquireCancelSpinLock( &cancelIrql );
        KeAcquireSpinLock( SpinLock, &oldIrql );
    }

    KeReleaseSpinLock( SpinLock, oldIrql );
    IoReleaseCancelSpinLock( cancelIrql );

    return;

} // AfdCompleteIrpList


NTSTATUS
AfdErrorEventHandler (
    IN PVOID TdiEventContext,
    IN NTSTATUS Status
    )
{

    IF_DEBUG(CONNECT) {
        KdPrint(( "AfdErrorEventHandler called for endpoint %lx\n",
                      TdiEventContext ));

    }

    return STATUS_SUCCESS;

} // AfdErrorEventHandler


VOID
AfdInsertNewEndpointInList (
    IN PAFD_ENDPOINT Endpoint
    )

/*++

Routine Description:

    Inserts a new endpoint in the global list of AFD endpoints.  If this
    is the first endpoint, then this routine does various allocations to
    prepare AFD for usage.

Arguments:

    Endpoint - the endpoint being added.

Return Value:

    None.

--*/

{
    //
    // Acquire a lock which prevents other threads from performing this
    // operation.
    //

    ExAcquireResourceExclusive( &AfdResource, TRUE );

    ExInterlockedIncrementLong(
        &AfdEndpointsOpened,
        &AfdInterlock
        );

    //
    // If the list of endpoints is empty, do some allocations.
    //

    if ( IsListEmpty( &AfdEndpointListHead ) ) {

        //
        // Allocate data buffers to perform transport bufferring.
        // There's nothing we can do if this fails--it just means that
        // things will be slower.
        //

        (VOID)AfdAllocateInitialBuffers( );

        //
        // Lock down the AFD section that cannot be pagable if any
        // sockets are open.
        //

        ASSERT( AfdDiscardableCodeHandle == NULL );

        AfdDiscardableCodeHandle = MmLockPagableImageSection( AfdGetBuffer );
        ASSERT( AfdDiscardableCodeHandle != NULL );
    }

    //
    // Add the endpoint to the list.
    //

    ExInterlockedInsertHeadList(
        &AfdEndpointListHead,
        &Endpoint->GlobalEndpointListEntry,
        &AfdSpinLock
        );

    //
    // Release the lock and return.
    //

    ExReleaseResource( &AfdResource );

    return;

} // AfdInsertNewEndpointInList


VOID
AfdRemoveEndpointFromList (
    IN PAFD_ENDPOINT Endpoint
    )

/*++

Routine Description:

    Removes a new endpoint from the global list of AFD endpoints.  If
    this is the last endpoint in the list, then this routine does
    various deallocations to save resource utilization.

Arguments:

    Endpoint - the endpoint being removed.

Return Value:

    None.

--*/

{
    //
    // Acquire a lock which prevents other threads from performing this
    // operation.
    //

    ExAcquireResourceExclusive( &AfdResource, TRUE );

    ExInterlockedIncrementLong(
        &AfdEndpointsClosed,
        &AfdInterlock
        );

    //
    // Add the endpoint to the list.
    //

    AfdInterlockedRemoveEntryList(
        &Endpoint->GlobalEndpointListEntry,
        &AfdSpinLock
        );

    //
    // If the list of endpoints is now empty, do some deallocations.
    //

    if ( IsListEmpty( &AfdEndpointListHead ) ) {

        //
        // Deallocate data buffers used to perform transport bufferring.
        //

        (VOID)AfdDeallocateInitialBuffers( );

        //
        // Unlock the AFD section that can be pagable when no sockets
        // are open.
        //

        ASSERT( AfdDiscardableCodeHandle != NULL );

        MmUnlockPagableImageSection( AfdDiscardableCodeHandle );

        AfdDiscardableCodeHandle = NULL;
    }

    //
    // Release the lock and return.
    //

    ExReleaseResource( &AfdResource );

    return;

} // AfdInsertNewEndpointInList


VOID
AfdInterlockedRemoveEntryList (
    IN PLIST_ENTRY ListEntry,
    IN PKSPIN_LOCK SpinLock
    )
{
    KIRQL oldIrql;

    //
    // Our own routine since EX doesn't have a version of this....
    //

    KeAcquireSpinLock( &AfdSpinLock, &oldIrql );
    RemoveEntryList( ListEntry );
    KeReleaseSpinLock( &AfdSpinLock, oldIrql );

} // AfdInterlockedRemoveEntryList


NTSTATUS
AfdQueryHandles (
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp
    )

/*++

Routine Description:

    Returns information about the TDI handles corresponding to an AFD
    endpoint.  NULL is returned for either the connection handle or the
    address handle (or both) if the endpoint does not have that particular
    object.

Arguments:

    Irp - Pointer to I/O request packet.

    IrpSp - pointer to the IO stack location to use for this request.

Return Value:

    NTSTATUS -- Indicates whether the request was successfully queued.

--*/

{
    PAFD_ENDPOINT endpoint;
    PAFD_HANDLE_INFO handleInfo;
    ULONG getHandleInfo;
    NTSTATUS status;

    PAGED_CODE( );

    //
    // Set up local pointers.
    //

    endpoint = IrpSp->FileObject->FsContext;
    ASSERT( IS_AFD_ENDPOINT_TYPE( endpoint ) );
    handleInfo = Irp->AssociatedIrp.SystemBuffer;

    //
    // Make sure that the input and output buffers are large enough.
    //

    if ( IrpSp->Parameters.DeviceIoControl.InputBufferLength <
             sizeof(getHandleInfo) ||
         IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
             sizeof(*handleInfo) ) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    //
    // Determine which handles we need to get.
    //

    getHandleInfo = *(PULONG)Irp->AssociatedIrp.SystemBuffer;

    //
    // If no handle information or invalid handle information was
    // requested, fail.
    //

    if ( (getHandleInfo &
             ~(AFD_QUERY_ADDRESS_HANDLE | AFD_QUERY_CONNECTION_HANDLE)) != 0 ||
         getHandleInfo == 0 ) {
        return STATUS_INVALID_PARAMETER;
    }

    //
    // Initialize the output buffer.
    //

    handleInfo->TdiAddressHandle = NULL;
    handleInfo->TdiConnectionHandle = NULL;

    //
    // If the caller requested a TDI address handle and we have an
    // address handle for this endpoint, dupe the address handle to the
    // user process.
    //

    if ( (getHandleInfo & AFD_QUERY_ADDRESS_HANDLE) != 0 &&
             endpoint->AddressHandle != NULL ) {

        ASSERT( endpoint->AddressFileObject != NULL );

        status = ObOpenObjectByPointer(
                     endpoint->AddressFileObject,
                     OBJ_CASE_INSENSITIVE,
                     NULL,
                     GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                     *IoFileObjectType,
                     KernelMode,
                     &handleInfo->TdiAddressHandle
                     );
        if ( !NT_SUCCESS(status) ) {
            return status;
        }
    }

    //
    // If the caller requested a TDI connection handle and we have a
    // connection handle for this endpoint, dupe the connection handle
    // to the user process.
    //

    if ( (getHandleInfo & AFD_QUERY_CONNECTION_HANDLE) != 0 &&
             endpoint->Type == AfdBlockTypeVcConnecting &&
             endpoint->Common.VcConnecting.Connection != NULL &&
             endpoint->Common.VcConnecting.Connection->Handle != NULL ) {

        ASSERT( endpoint->Common.VcConnecting.Connection->Type == AfdBlockTypeConnection );
        ASSERT( endpoint->Common.VcConnecting.Connection->FileObject != NULL );

        status = ObOpenObjectByPointer(
                     endpoint->Common.VcConnecting.Connection->FileObject,
                     OBJ_CASE_INSENSITIVE,
                     NULL,
                     GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                     *IoFileObjectType,
                     KernelMode,
                     &handleInfo->TdiConnectionHandle
                     );
        if ( !NT_SUCCESS(status) ) {
            if ( handleInfo->TdiAddressHandle != NULL ) {
                ZwClose( handleInfo->TdiAddressHandle );
            }
            return status;
        }
    }

    Irp->IoStatus.Information = sizeof(*handleInfo);

    return STATUS_SUCCESS;

} // AfdQueryHandles


NTSTATUS
AfdGetInformation (
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp
    )

/*++

Routine Description:

    Gets information in the endpoint.

Arguments:

    Irp - Pointer to I/O request packet.

    IrpSp - pointer to the IO stack location to use for this request.

Return Value:

    NTSTATUS -- Indicates whether the request was successfully queued.

--*/

{
    PAFD_ENDPOINT endpoint;
    PAFD_INFORMATION afdInfo;
    PVOID additionalInfo;
    ULONG additionalInfoLength;
    TDI_REQUEST_KERNEL_QUERY_INFORMATION kernelQueryInfo;
    TDI_CONNECTION_INFORMATION connectionInfo;
    NTSTATUS status;

    PAGED_CODE( );

    //
    // Set up local pointers.
    //

    endpoint = IrpSp->FileObject->FsContext;
    ASSERT( IS_AFD_ENDPOINT_TYPE( endpoint ) );
    afdInfo = Irp->AssociatedIrp.SystemBuffer;

    //
    // Make sure that the input and output buffers are large enough.
    //

    if ( IrpSp->Parameters.DeviceIoControl.InputBufferLength <
             sizeof(*afdInfo)  ||
         IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
             sizeof(*afdInfo) ) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    //
    // Figure out the additional information, if any.
    //

    additionalInfo = afdInfo + 1;
    additionalInfoLength =
        IrpSp->Parameters.DeviceIoControl.InputBufferLength - sizeof(*afdInfo);

    //
    // Set up appropriate information in the endpoint.
    //

    switch ( afdInfo->InformationType ) {

    case AFD_MAX_PATH_SEND_SIZE:

        //
        // Set up a query to the TDI provider to obtain the largest
        // datagram that can be sent to a particular address.
        //

        kernelQueryInfo.QueryType = TDI_QUERY_MAX_DATAGRAM_INFO;
        kernelQueryInfo.RequestConnectionInformation = &connectionInfo;

        connectionInfo.UserDataLength = 0;
        connectionInfo.UserData = NULL;
        connectionInfo.OptionsLength = 0;
        connectionInfo.Options = NULL;
        connectionInfo.RemoteAddressLength = additionalInfoLength;
        connectionInfo.RemoteAddress = additionalInfo;

        //
        // Ask the TDI provider for the information.
        //

        status = AfdIssueDeviceControl(
                     endpoint->AddressHandle,
                     &kernelQueryInfo,
                     sizeof(kernelQueryInfo),
                     &afdInfo->Information.Ulong,
                     sizeof(afdInfo->Information.Ulong),
                     TDI_QUERY_INFORMATION
                     );

        //
        // If the request succeeds, use this information.  Otherwise,
        // fall through and use the transport's global information.
        // This is done because not all transports support this
        // particular TDI request, and for those which do not the
        // global information is a reasonable approximation.
        //

        if ( NT_SUCCESS(status) ) {
            break;
        }

    case AFD_MAX_SEND_SIZE:

        //
        // Return the MaxSendSize or MaxDatagramSendSize from the
        // TDI_PROVIDER_INFO based on whether or not this is a datagram
        // endpoint.
        //

        if ( endpoint->EndpointType == AfdEndpointTypeDatagram ) {
            afdInfo->Information.Ulong =
                endpoint->TransportInfo->ProviderInfo.MaxDatagramSize;
        } else {
            afdInfo->Information.Ulong =
                endpoint->TransportInfo->ProviderInfo.MaxSendSize;
        }

        break;

    case AFD_SENDS_PENDING:

        //
        // If this is an endpoint on a bufferring transport, no sends
        // are pending in AFD.  If it is on a nonbufferring transport,
        // return the count of sends pended in AFD.
        //

        if ( endpoint->Type != AfdBlockTypeVcConnecting ) {
            afdInfo->Information.Ulong = 0;
        } else {
            afdInfo->Information.Ulong =
                endpoint->Common.VcConnecting.Connection->VcBufferredSendCount;
        }

        break;

    case AFD_RECEIVE_WINDOW_SIZE:

        //
        // Return the default receive window.
        //

        afdInfo->Information.Ulong = AfdReceiveWindowSize;
        break;

    case AFD_SEND_WINDOW_SIZE:

        //
        // Return the default send window.
        //

        afdInfo->Information.Ulong = AfdSendWindowSize;
        break;

    default:

        return STATUS_INVALID_PARAMETER;
    }

    Irp->IoStatus.Information = sizeof(*afdInfo);
    Irp->IoStatus.Status = STATUS_SUCCESS;

    return STATUS_SUCCESS;

} // AfdGetInformation


NTSTATUS
AfdSetInformation (
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp
    )

/*++

Routine Description:

    Sets information in the endpoint.

Arguments:

    Irp - Pointer to I/O request packet.

    IrpSp - pointer to the IO stack location to use for this request.

Return Value:

    NTSTATUS -- Indicates whether the request was successfully queued.

--*/

{
    PAFD_ENDPOINT endpoint;
    PAFD_CONNECTION connection;
    PAFD_INFORMATION afdInfo;
    NTSTATUS status;

    PAGED_CODE( );

    //
    // Set up local pointers.
    //

    endpoint = IrpSp->FileObject->FsContext;
    ASSERT( IS_AFD_ENDPOINT_TYPE( endpoint ) );
    afdInfo = Irp->AssociatedIrp.SystemBuffer;

    //
    // Make sure that the input buffer is large enough.
    //

    if ( IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*afdInfo) ) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    //
    // Set up appropriate information in the endpoint.
    //

    switch ( afdInfo->InformationType ) {

    case AFD_NONBLOCKING_MODE:

        //
        // Set the blocking mode of the endpoint.  If TRUE, send and receive
        // calls on the endpoint will fail if they cannot be completed
        // immediately.
        //

        endpoint->NonBlocking = afdInfo->Information.Boolean;
        break;

    case AFD_RECEIVE_WINDOW_SIZE:
    case AFD_SEND_WINDOW_SIZE: {

        LONG newBytes;
        PCLONG maxBytes;
        PCSHORT maxCount;
#ifdef AFDDBG_QUOTA
        PVOID chargeBlock;
        PSZ chargeType;
#endif

        //
        // First determine where the appropriate limits are stored in the
        // connection or endpoint.  We do this so that we can use common
        // code to charge quota and set the new counters.
        //

        if ( endpoint->Type == AfdBlockTypeVcConnecting ) {

            connection = endpoint->Common.VcConnecting.Connection;

            if ( afdInfo->InformationType == AFD_SEND_WINDOW_SIZE ) {
                maxBytes = &connection->MaxBufferredSendBytes;
                maxCount = &connection->MaxBufferredSendCount;
            } else {
                maxBytes = &connection->MaxBufferredReceiveBytes;
                maxCount = &connection->MaxBufferredReceiveCount;
            }

#ifdef AFDDBG_QUOTA
            chargeBlock = connection;
            chargeType = "SetInfo vcnb";
#endif

        } else if ( endpoint->Type == AfdBlockTypeDatagram ) {

            if ( afdInfo->InformationType == AFD_SEND_WINDOW_SIZE ) {
                maxBytes = &endpoint->Common.Datagram.MaxBufferredSendBytes;
                maxCount = &endpoint->Common.Datagram.MaxBufferredSendCount;
            } else {
                maxBytes = &endpoint->Common.Datagram.MaxBufferredReceiveBytes;
                maxCount = &endpoint->Common.Datagram.MaxBufferredReceiveCount;
            }

#ifdef AFDDBG_QUOTA
            chargeBlock = endpoint;
            chargeType = "SetInfo dgrm";
#endif

        } else {

            return STATUS_INVALID_PARAMETER;
        }

        //
        // Charge or return quota to the process making this request.
        //

        newBytes = afdInfo->Information.Ulong - (ULONG)(*maxBytes);

        if ( newBytes > 0 ) {

            try {

                PsChargePoolQuota(
                    endpoint->OwningProcess,
                    NonPagedPool,
                    newBytes
                    );

            } except ( EXCEPTION_EXECUTE_HANDLER ) {
#if DBG
               DbgPrint( "AfdSetInformation: PsChargePoolQuota failed.\n" );
#endif
               return STATUS_QUOTA_EXCEEDED;
            }

            AfdRecordQuotaHistory(
                endpoint->OwningProcess,
                newBytes,
                chargeType,
                chargeBlock
                );

        } else {

            PsReturnPoolQuota(
                endpoint->OwningProcess,
                NonPagedPool,
                -1 * newBytes
                );
            AfdRecordQuotaHistory(
                endpoint->OwningProcess,
                newBytes,
                chargeType,
                chargeBlock
                );
        }

        //
        // Set up the new information in the AFD internal structure.
        //

        *maxBytes = (CLONG)afdInfo->Information.Ulong;
        *maxCount = (CSHORT)(afdInfo->Information.Ulong / AfdBufferMultiplier);

        break;
    }

    default:

        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;

} // AfdSetInformation



NTSTATUS
AfdGetContext (
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp
    )
{
    PAFD_ENDPOINT endpoint;

    PAGED_CODE( );

    //
    // Set up local pointers.
    //

    endpoint = IrpSp->FileObject->FsContext;
    ASSERT( IS_AFD_ENDPOINT_TYPE( endpoint ) );

    //
    // Make sure that the output buffer is large enough to hold all the
    // context information for this socket.
    //

    if ( IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
             endpoint->ContextLength ) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    //
    // If there is no context, return nothing.
    //

    if ( endpoint->Context == NULL ) {
        Irp->IoStatus.Information = 0;
        return STATUS_SUCCESS;
    }

    //
    // Return the context information we have stored for this endpoint.
    //

    RtlCopyMemory(
        Irp->AssociatedIrp.SystemBuffer,
        endpoint->Context,
        endpoint->ContextLength
        );

    Irp->IoStatus.Information = endpoint->ContextLength;

    return STATUS_SUCCESS;

} // AfdGetContext


NTSTATUS
AfdGetContextLength (
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp
    )
{
    PAFD_ENDPOINT endpoint;

    PAGED_CODE( );

    //
    // Set up local pointers.
    //

    endpoint = IrpSp->FileObject->FsContext;
    ASSERT( IS_AFD_ENDPOINT_TYPE( endpoint ) );

    //
    // Make sure that the output buffer is large enough to hold the
    // context buffer length.
    //

    if ( IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
             sizeof(endpoint->ContextLength) ) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    //
    // Return the length of the context information we have stored for
    // this endpoint.
    //

    *(PULONG)Irp->AssociatedIrp.SystemBuffer = endpoint->ContextLength;

    Irp->IoStatus.Information = sizeof(endpoint->ContextLength);

    return STATUS_SUCCESS;

} // AfdGetContextLength


NTSTATUS
AfdSetContext (
    IN PIRP Irp,
    IN PIO_STACK_LOCATION IrpSp
    )
{
    PAFD_ENDPOINT endpoint;
    ULONG newContextLength;

    PAGED_CODE( );

    //
    // Set up local pointers.
    //

    endpoint = IrpSp->FileObject->FsContext;
    ASSERT( IS_AFD_ENDPOINT_TYPE( endpoint ) );
    newContextLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;

    //
    // If there is no context buffer on the endpoint, or if the context
    // buffer is too small, allocate a new context buffer from paged pool.
    //

    if ( endpoint->Context == NULL ||
             endpoint->ContextLength < newContextLength ) {

        PVOID newContext;

        //
        // Allocate a new context buffer.
        //

        newContext = ExAllocatePoolWithQuota( PagedPool, newContextLength );
        if ( newContext == NULL ) {
            return STATUS_NO_MEMORY;
        }

        //
        // Free the old context buffer, if there was one.
        //

        if ( endpoint->Context != NULL ) {
            ExFreePool( endpoint->Context );
        }

        endpoint->Context = newContext;
    }

    //
    // Store the passed-in context buffer.
    //

    endpoint->ContextLength = newContextLength;

    RtlCopyMemory(
        endpoint->Context,
        Irp->AssociatedIrp.SystemBuffer,
        newContextLength
        );

    Irp->IoStatus.Information = 0;

    return STATUS_SUCCESS;

} // AfdSetContext


NTSTATUS
AfdSetEventHandler (
    IN HANDLE FileHandle,
    IN ULONG EventType,
    IN PVOID EventHandler,
    IN PVOID EventContext
    )

/*++

Routine Description:

    Sets up a TDI indication handler on a connection or address object
    (depending on the file handle).  This is done synchronously, which
    shouldn't usually be an issue since TDI providers can usually complete
    indication handler setups immediately.

Arguments:

    FileHandle - a handle to an open connection or address object.

    EventType - the event for which the indication handler should be
        called.

    EventHandler - the routine to call when tghe specified event occurs.

    EventContext - context which is passed to the indication routine.

Return Value:

    NTSTATUS -- Indicates the status of the request.

--*/

{
    TDI_REQUEST_KERNEL_SET_EVENT parameters;

    PAGED_CODE( );

    parameters.EventType = EventType;
    parameters.EventHandler = EventHandler;
    parameters.EventContext = EventContext;

    return AfdIssueDeviceControl(
               FileHandle,
               &parameters,
               sizeof(parameters),
               NULL,
               0,
               TDI_SET_EVENT_HANDLER
               );

} // AfdSetEventHandler


NTSTATUS
AfdIssueDeviceControl (
    IN HANDLE FileHandle,
    IN PVOID IrpParameters,
    IN ULONG IrpParametersLength,
    IN PVOID MdlBuffer,
    IN ULONG MdlBufferLength,
    IN UCHAR MinorFunction
    )

/*++

Routine Description:

    Issues a device control returst to a TDI provider and waits for the
    request to complete.

Arguments:

    FileHandle - a TDI handle.

    IrpParameters - information to write to the parameters section of the
        stack location of the IRP.

    IrpParametersLength - length of the parameter information.  Cannot be
        greater than 16.

    MdlBuffer - if non-NULL, a buffer of nonpaged pool to be mapped
        into an MDL and placed in the MdlAddress field of the IRP.

    MdlBufferLength - the size of the buffer pointed to by MdlBuffer.

    MinorFunction - the minor function code for the request.

Return Value:

    NTSTATUS -- Indicates the status of the request.

--*/

{
    NTSTATUS status;
    PFILE_OBJECT fileObject;
    PIRP irp;
    PIO_STACK_LOCATION irpSp;
    KEVENT event;
    IO_STATUS_BLOCK ioStatusBlock;
    PDEVICE_OBJECT deviceObject;
    PMDL mdl;

    PAGED_CODE( );

    //
    // Initialize the kernel event that will signal I/O completion.
    //

    KeInitializeEvent( &event, SynchronizationEvent, FALSE );

    //
    // Get the file object corresponding to the directory's handle.
    // Referencing the file object every time is necessary because the
    // IO completion routine dereferneces it.
    //

    status = ObReferenceObjectByHandle(
                 FileHandle,
                 0L,                         // DesiredAccess
                 NULL,
                 KernelMode,
                 (PVOID *)&fileObject,
                 NULL
                 );
    if ( !NT_SUCCESS(status) ) {
        return status;
    }

    //
    // Set the file object event to a non-signaled state.
    //

    (VOID) KeResetEvent( &fileObject->Event );

    //
    // Attempt to allocate and initialize the I/O Request Packet (IRP)
    // for this operation.
    //

    deviceObject = IoGetRelatedDeviceObject ( fileObject );

    irp = IoAllocateIrp( (deviceObject)->StackSize, TRUE );
    if ( irp == NULL ) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Fill in the service independent parameters in the IRP.
    //

    irp->Flags = (LONG)IRP_SYNCHRONOUS_API;
    irp->RequestorMode = KernelMode;
    irp->PendingReturned = FALSE;

    irp->UserIosb = &ioStatusBlock;
    irp->UserEvent = &event;

    irp->Overlay.AsynchronousParameters.UserApcRoutine = NULL;

    irp->AssociatedIrp.SystemBuffer = NULL;
    irp->UserBuffer = NULL;

    irp->Tail.Overlay.Thread = PsGetCurrentThread();
    irp->Tail.Overlay.OriginalFileObject = fileObject;
    irp->Tail.Overlay.AuxiliaryBuffer = NULL;

    DEBUG ioStatusBlock.Status = STATUS_UNSUCCESSFUL;
    DEBUG ioStatusBlock.Information = (ULONG)-1;

    //
    // If an MDL buffer was specified, get an MDL, map the buffer,
    // and place the MDL pointer in the IRP.
    //

    if ( MdlBuffer != NULL ) {

        mdl = IoAllocateMdl(
                  MdlBuffer,
                  MdlBufferLength,
                  FALSE,
                  FALSE,
                  irp
                  );
        if ( mdl == NULL ) {
            IoFreeIrp( irp );
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        MmBuildMdlForNonPagedPool( mdl );

    } else {

        irp->MdlAddress = NULL;
    }

    //
    // Put the file object pointer in the stack location.
    //

    irpSp = IoGetNextIrpStackLocation( irp );
    irpSp->FileObject = fileObject;
    irpSp->DeviceObject = deviceObject;

    //
    // Fill in the service-dependent parameters for the request.
    //

    ASSERT( IrpParametersLength <= sizeof(irpSp->Parameters) );
    RtlCopyMemory( &irpSp->Parameters, IrpParameters, IrpParametersLength );

    irpSp->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    irpSp->MinorFunction = MinorFunction;

    //
    // Set up a completion routine which we'll use to free the MDL
    // allocated previously.
    //

    IoSetCompletionRoutine( irp, AfdRestartDeviceControl, NULL, TRUE, TRUE, TRUE );

    //
    // Queue the IRP to the thread and pass it to the driver.
    //

    IoEnqueueIrp( irp );

    status = IoCallDriver( deviceObject, irp );

    //
    // If necessary, wait for the I/O to complete.
    //

    if ( status == STATUS_PENDING ) {
        KeWaitForSingleObject( (PVOID)&event, UserRequest, KernelMode,  FALSE, NULL );
    }

    //
    // If the request was successfully queued, get the final I/O status.
    //

    if ( NT_SUCCESS(status) ) {
        status = ioStatusBlock.Status;
    }

    return status;

} // AfdIssueDeviceControl


NTSTATUS
AfdRestartDeviceControl (
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context
    )
{
    //
    // N.B.  This routine can never be demand paged because it can be
    // called before any endpoints have been placed on the global
    // list--see AfdAllocateEndpoint() and it's call to
    // AfdGetTransportInfo().
    //

    //
    // If there was an MDL in the IRP, free it and reset the pointer to
    // NULL.  The IO system can't handle a nonpaged pool MDL being freed
    // in an IRP, which is why we do it here.
    //

    if ( Irp->MdlAddress != NULL ) {
        IoFreeMdl( Irp->MdlAddress );
        Irp->MdlAddress = NULL;
    }

    return STATUS_SUCCESS;

} // AfdRestartDeviceControl


VOID
AfdQueueWorkItem (
    IN PWORKER_THREAD_ROUTINE AfdWorkerRoutine,
    IN PVOID Context
    )
{
    PAFD_WORK_ITEM afdWorkItem;
    PWORK_QUEUE_ITEM workQueueItem;
    KIRQL oldIrql;

    afdWorkItem = AFD_ALLOCATE_POOL(
                      NonPagedPoolMustSucceed,
                      sizeof(*afdWorkItem)
                      );

    afdWorkItem->AfdWorkerRoutine = AfdWorkerRoutine;
    afdWorkItem->Context = Context;

    //
    // If AFD's queue of work items is empty, add this item to the queue 
    // and fire off an executive worker thread to start servicing the 
    // list.  
    //

    KeAcquireSpinLock( &AfdSpinLock, &oldIrql );

    if ( IsListEmpty( &AfdWorkQueueListHead ) ) {

        InsertTailList( &AfdWorkQueueListHead, &afdWorkItem->WorkItemListEntry );
    
        workQueueItem = AFD_ALLOCATE_POOL(
                            NonPagedPoolMustSucceed,
                            sizeof(*workQueueItem)
                            );

        ExInitializeWorkItem( workQueueItem, AfdDoWork, workQueueItem );
        ExQueueWorkItem( workQueueItem, DelayedWorkQueue );
    
    } else {

        InsertTailList( &AfdWorkQueueListHead, &afdWorkItem->WorkItemListEntry );
    }

    KeReleaseSpinLock( &AfdSpinLock, oldIrql );

    return;

} // AfdQueueWorkItem


VOID
AfdDoWork (
    IN PVOID Context
    )
{
    PAFD_WORK_ITEM afdWorkItem;
    KIRQL oldIrql;
    PLIST_ENTRY listEntry;

    //
    // Empty the queue of AFD work items.
    //

    KeAcquireSpinLock( &AfdSpinLock, &oldIrql );

    while ( !IsListEmpty( &AfdWorkQueueListHead ) ) {

        //
        // Take the first item from the queue and find the address
        // of the AFD work item structure.
        //

        listEntry = RemoveHeadList( &AfdWorkQueueListHead );
        afdWorkItem = CONTAINING_RECORD(
                          listEntry,
                          AFD_WORK_ITEM,
                          WorkItemListEntry
                          );

        KeReleaseSpinLock( &AfdSpinLock, oldIrql );

        //
        // Call the AFD worker routine.
        //
    
        afdWorkItem->AfdWorkerRoutine( afdWorkItem->Context );

        //
        // Free the pool allocated for the AFD work item, reacquire
        // the lock, and continue emptying the AFD work queue.
        //

        AFD_FREE_POOL( afdWorkItem );
    
        KeAcquireSpinLock( &AfdSpinLock, &oldIrql );
    }

    KeReleaseSpinLock( &AfdSpinLock, oldIrql );

    //
    // The AFD work queue is empty.  Free the EX work item structure we 
    // allocated in AfdQueueWorkItem().  
    //

    AFD_FREE_POOL( Context );

    return;

} // AfdDoWork

#if DBG

typedef struct _AFD_OUTSTANDING_IRP {
    LIST_ENTRY OutstandingIrpListEntry;
    PIRP OutstandingIrp;
    PCHAR FileName;
    ULONG LineNumber;
} AFD_OUTSTANDING_IRP, *PAFD_OUTSTANDING_IRP;


NTSTATUS
AfdIoCallDriverDebug (
    IN PAFD_ENDPOINT Endpoint,
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PCHAR FileName,
    IN ULONG LineNumber
    )
{
    PAFD_OUTSTANDING_IRP outstandingIrp;
    KIRQL oldIrql;

    //
    // Get an outstanding IRP structure to hold the IRP.
    //

    outstandingIrp = AFD_ALLOCATE_POOL( NonPagedPool, sizeof(AFD_OUTSTANDING_IRP) );
    if ( outstandingIrp == NULL ) {
        Irp->IoStatus.Status = STATUS_NO_MEMORY;
        IoSetNextIrpStackLocation( Irp );
        IoCompleteRequest( Irp, AfdPriorityBoost );
        return STATUS_NO_MEMORY;
    }

    //
    // Initialize the structure and place it on the endpoint's list of
    // outstanding IRPs.
    //

    outstandingIrp->OutstandingIrp = Irp;
    outstandingIrp->FileName = FileName;
    outstandingIrp->LineNumber = LineNumber;

    KeAcquireSpinLock( &AfdSpinLock, &oldIrql );
    InsertTailList(
        &Endpoint->OutstandingIrpListHead,
        &outstandingIrp->OutstandingIrpListEntry
        );
    Endpoint->OutstandingIrpCount++;
    KeReleaseSpinLock( &AfdSpinLock, oldIrql );

    //
    // Pass the IRP to the TDI provider.
    //

    return IoCallDriver( DeviceObject, Irp );

} // AfdIoCallDriverDebug


VOID
AfdCompleteOutstandingIrpDebug (
    IN PAFD_ENDPOINT Endpoint,
    IN PIRP Irp
    )
{
    PAFD_OUTSTANDING_IRP outstandingIrp;
    KIRQL oldIrql;
    PLIST_ENTRY listEntry;

    //
    // First find the IRP on the endpoint's list of outstanding IRPs.
    //

    KeAcquireSpinLock( &AfdSpinLock, &oldIrql );

    for ( listEntry = Endpoint->OutstandingIrpListHead.Flink;
          listEntry != &Endpoint->OutstandingIrpListHead;
          listEntry = listEntry->Flink ) {

        outstandingIrp = CONTAINING_RECORD(
                             listEntry,
                             AFD_OUTSTANDING_IRP,
                             OutstandingIrpListEntry
                             );
        if ( outstandingIrp->OutstandingIrp == Irp ) {
            RemoveEntryList( listEntry );
            ASSERT( Endpoint->OutstandingIrpCount != 0 );
            Endpoint->OutstandingIrpCount--;
            KeReleaseSpinLock( &AfdSpinLock, oldIrql );
            AFD_FREE_POOL( outstandingIrp );
            return;
        }
    }

    //
    // The corresponding outstanding IRP structure was not found.  This
    // should never happen unless an allocate for an outstanding IRP
    // structure failed above.
    //

    KdPrint(( "AfdCompleteOutstandingIrp: Irp %lx not found on endpoint %lx\n",
                  Irp, Endpoint ));

    ASSERT( Endpoint->OutstandingIrpCount != 0 );

    Endpoint->OutstandingIrpCount--;

    KeReleaseSpinLock( &AfdSpinLock, oldIrql );

    return;

} // AfdCompleteOutstandingIrpDebug

#else


NTSTATUS
AfdIoCallDriverFree (
    IN PAFD_ENDPOINT Endpoint,
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )
{
    //
    // Increment the count of IRPs outstanding on the endpoint.  This
    // allows the cleanup code to abort the VC if there is outstanding
    // IO when a cleanup occurs.
    //

    ExInterlockedIncrementLong(
        &Endpoint->OutstandingIrpCount,
        &AfdInterlock
        );

    //
    // Pass the IRP to the TDI provider.
    //

    return IoCallDriver( DeviceObject, Irp );

} // AfdIoCallDriverFree


VOID
AfdCompleteOutstandingIrpFree (
    IN PAFD_ENDPOINT Endpoint,
    IN PIRP Irp
    )
{
    //
    // Decrement the count of IRPs on the endpoint.
    //

    ExInterlockedDecrementLong(
        &Endpoint->OutstandingIrpCount,
        &AfdInterlock
        );

    return;

} // AfdCompleteOutstandingIrpFree

#endif


#if DBG

#undef ExAllocatePool
#undef ExFreePool

#define AFD_POOL_TAG ' dfA'

LIST_ENTRY AfdPoolListHead;
ULONG AfdTotalAllocations = 0;
ULONG AfdTotalFrees = 0;
ULONG AfdTotalBytesAllocated = 0;
KSPIN_LOCK AfdDebugSpinLock;

typedef struct _AFD_POOL_HEADER {
    LIST_ENTRY GlobalPoolListEntry;
    PCHAR FileName;
    ULONG LineNumber;
    ULONG Size;
    ULONG Unused;   // make structure size multiple of 8 (for alignment)
} AFD_POOL_HEADER, *PAFD_POOL_HEADER;

VOID
AfdInitializeDebugData (
    VOID
    )
{
    InitializeListHead( &AfdPoolListHead );

    KeInitializeSpinLock( &AfdDebugSpinLock );

    return;

} // AfdInitializeDebugData


PVOID
AfdAllocatePool (
    IN POOL_TYPE PoolType,
    IN ULONG NumberOfBytes,
    IN PCHAR FileName,
    IN ULONG LineNumber,
    IN BOOLEAN WithQuota
    )
{
    PAFD_POOL_HEADER header;
    KIRQL oldIrql;

    ASSERT( PoolType == NonPagedPool || PoolType == NonPagedPoolMustSucceed );

    if ( WithQuota ) {
        header = ExAllocatePoolWithQuotaTag(
                     PoolType,
                     NumberOfBytes + sizeof(*header),
                     AFD_POOL_TAG
                     );
    } else {
        header = ExAllocatePoolWithTag(
                     PoolType,
                     NumberOfBytes + sizeof(*header),
                     AFD_POOL_TAG
                     );
    }

    if ( header == NULL ) {
        return NULL;
    }

    header->FileName = FileName;
    header->LineNumber = LineNumber;
    header->Size = NumberOfBytes;

    KeAcquireSpinLock( &AfdDebugSpinLock, &oldIrql );

    InsertTailList( &AfdPoolListHead, &header->GlobalPoolListEntry );
    AfdTotalAllocations++;
    AfdTotalBytesAllocated += header->Size;

    KeReleaseSpinLock( &AfdDebugSpinLock, oldIrql );

    return (PVOID)(header + 1);

} // AfdAllocatePool


VOID
AfdFreePool (
    IN PVOID Pointer
    )
{
    KIRQL oldIrql;
    PAFD_POOL_HEADER header = (PAFD_POOL_HEADER)Pointer - 1;

    KeAcquireSpinLock( &AfdDebugSpinLock, &oldIrql );

    RemoveEntryList( &header->GlobalPoolListEntry );
    AfdTotalFrees++;
    AfdTotalBytesAllocated -= header->Size;

    header->GlobalPoolListEntry.Flink = (PLIST_ENTRY)0xFFFFFFFF;
    header->GlobalPoolListEntry.Blink = (PLIST_ENTRY)0xFFFFFFFF;

    KeReleaseSpinLock( &AfdDebugSpinLock, oldIrql );

    ExFreePool( (PVOID)header );

} // AfdFreePool

#ifdef AFDDBG_QUOTA
typedef struct {
    union {
        ULONG Bytes;
        struct {
            UCHAR Reserved[3];
            UCHAR Sign;
        } ;
    } ;
    UCHAR Location[12];
    PVOID Block;
    PVOID Process;
    PVOID Reserved2[2];
} QUOTA_HISTORY, *PQUOTA_HISTORY;
#define QUOTA_HISTORY_LENGTH 512
QUOTA_HISTORY AfdQuotaHistory[QUOTA_HISTORY_LENGTH];
ULONG AfdQuotaHistoryIndex = 0;

VOID
AfdRecordQuotaHistory(
    IN PEPROCESS Process,
    IN LONG Bytes,
    IN PSZ Type,
    IN PVOID Block
    )
{
    KIRQL oldIrql;
    ULONG index;
    PQUOTA_HISTORY history;

    KeAcquireSpinLock( &AfdDebugSpinLock, &oldIrql );
    index = AfdQuotaHistoryIndex++;
    KeReleaseSpinLock( &AfdDebugSpinLock, oldIrql );

    index &= QUOTA_HISTORY_LENGTH - 1;
    history = &AfdQuotaHistory[index];

    history->Bytes = Bytes;
    history->Sign = Bytes < 0 ? '-' : '+';
    RtlCopyMemory( history->Location, Type, 12 );
    history->Block = Block;
    history->Process = Process;

    return;

} // AfdRecordQuotaHistory
#endif

#endif
