/*++


Copyright (c) 1989  Microsoft Corporation

Module Name:

    ixhwsup.c

Abstract:

    This module contains the IoXxx routines for the NT I/O system that
    are hardware dependent.  Were these routines not hardware dependent,
    they would reside in the iosubs.c module.

Author:

    Darryl E. Havens (darrylh) 11-Apr-1990

Environment:

    Kernel mode

Revision History:


--*/

#include "halp.h"

#define MAXHANDLERTYPE (MaximumInterfaceType+1)


PBUSHANDLER    HalpBusTable[MAXHANDLERTYPE];
INTERFACE_TYPE HalpConfigTable[MaximumBusDataType];




ULONG HalpNoBusData (
    IN PVOID BusHandler,
    IN PVOID RootHandler,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    );

NTSTATUS
HalpNoAdjustResourceList (
    IN PVOID BusHandler,
    IN PVOID RootHandler,
    IN OUT PIO_RESOURCE_REQUIREMENTS_LIST   *pResourceList
    );

NTSTATUS
HalpNoAssignSlotResources (
    IN PVOID BusHandler,
    IN PVOID RootHandler,
    IN PUNICODE_STRING          RegistryPath,
    IN PUNICODE_STRING          DriverClassName       OPTIONAL,
    IN PDRIVER_OBJECT           DriverObject,
    IN PDEVICE_OBJECT           DeviceObject          OPTIONAL,
    IN ULONG                    SlotNumber,
    IN OUT PCM_RESOURCE_LIST   *AllocatedResources
    );


ULONG
HalpGetSystemInterruptVector (
    IN PVOID BusHandler,
    IN PVOID RootHandler,
    IN ULONG BusInterruptLevel,
    IN ULONG BusInterruptVector,
    OUT PKIRQL Irql,
    OUT PKAFFINITY Affinity
    );

BOOLEAN
HalpTranslateSystemBusAddress (
    IN PVOID BusHandler,
    IN PVOID RootHandler,
    IN PHYSICAL_ADDRESS BusAddress,
    IN OUT PULONG AddressSpace,
    OUT PPHYSICAL_ADDRESS TranslatedAddress
    );


VOID
HalpInitBusHandlers (
    VOID
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT,HalpInitBusHandlers)
#pragma alloc_text(INIT,HalpAllocateBusHandler)
#pragma alloc_text(PAGE,HalAdjustResourceList)
#pragma alloc_text(PAGE,HalAssignSlotResources)
#pragma alloc_text(PAGE,HalGetInterruptVector)
#pragma alloc_text(PAGE,HalpNoAssignSlotResources)
#endif


VOID
HalpInitBusHandlers (
    VOID
    )
{
    PBUSHANDLER     Bus;

    if (KeGetCurrentPrcb()->Number) {
        // only need to do this once
        return ;
    }

    //
    // Build internal-bus 0, or system level bus
    //

    Bus = HalpAllocateBusHandler (Internal, -1, 0, -1, -1, 0);
    Bus->GetBusData = HalpNoBusData;
    Bus->SetBusData = HalpNoBusData;
    Bus->GetInterruptVector  = HalpGetSystemInterruptVector;
    Bus->TranslateBusAddress = HalpTranslateSystemBusAddress;
    Bus->AdjustResourceList  = HalpNoAdjustResourceList;
    Bus->AssignSlotResources = HalpNoAssignSlotResources;

    //
    // Register ISA bus 0 as a child of Internal with identity translation.
    // No ISA-specific data or enumeration — just enough so drivers that
    // specify AdapterInterfaceType=Isa can translate bus addresses.
    //

    Bus = HalpAllocateBusHandler (Isa, -1, 0, Internal, 0, 0);
    Bus->GetBusData = HalpNoBusData;
    Bus->AdjustResourceList = HalpNoAdjustResourceList;
    DbgPrint("HAL: ISA bus handler: Translate=%p Parent=%p\n",
             Bus->TranslateBusAddress, Bus->ParentHandler);

    //
    // PCI buses are discovered by HalpInitializePciBus() called
    // from HalReportResourceUsage during Phase 1.
    //
}


PBUSHANDLER HalpAllocateBusHandler (
    IN INTERFACE_TYPE   InterfaceType,
    IN BUS_DATA_TYPE    BusDataType,
    IN ULONG            BusNumber,
    IN BUS_DATA_TYPE    ParentBusDataType,
    IN ULONG            ParentBusNumber,
    IN ULONG            BusSpecificData
    )
{
    PBUSHANDLER         Bus, *pBus;
    IN PBUSHANDLER      ParentHandler;


    for (pBus = &HalpBusTable[InterfaceType]; *pBus; pBus = &Bus->Next) {
        Bus = *pBus;
        if (Bus->BusNumber == BusNumber) {
            //
            // Caller is changing bus handler, free the old one
            //

            *pBus = Bus->Next;
            ExFreePool (Bus);
            break;
        }
    }

    ParentHandler = HalpHandlerForBus (ParentBusDataType, ParentBusNumber);

    Bus = (PBUSHANDLER) ExAllocatePool (NonPagedPool,
                sizeof (BUSHANDLER) + BusSpecificData);

    RtlZeroMemory (Bus, sizeof (BUSHANDLER) + BusSpecificData);
    Bus->BusNumber = BusNumber;
    Bus->InterfaceType = InterfaceType;
    Bus->ConfigurationType = BusDataType;
    Bus->ParentHandler = ParentHandler;

    if (ParentHandler) {
        //
        // Inherit handlers from parent
        //

        Bus->GetBusData             = ParentHandler->GetBusData;
        Bus->SetBusData             = ParentHandler->SetBusData;
        Bus->AdjustResourceList     = ParentHandler->AdjustResourceList;
        Bus->AssignSlotResources    = ParentHandler->AssignSlotResources;
        Bus->TranslateBusAddress    = ParentHandler->TranslateBusAddress;

        if (InterfaceType != Internal) {
            Bus->GetInterruptVector = ParentHandler->GetInterruptVector;
        }
    }

    //
    // If this bus has a configuration space, then set the
    // association
    //

    if (BusDataType != -1) {
        HalpConfigTable[BusDataType] = InterfaceType;
    }

    if (BusSpecificData) {
        Bus->BusData = Bus + 1;
    }

    //
    // Link in the bus structure
    //

    Bus->Next = HalpBusTable[InterfaceType];
    HalpBusTable[InterfaceType] = Bus;
    return Bus;
}


PBUSHANDLER HalpHandlerForBus (
    IN INTERFACE_TYPE InterfaceType,
    IN ULONG          BusNumber
    )
{
    PBUSHANDLER     Bus;

    if (InterfaceType < Internal  ||  InterfaceType >= MAXHANDLERTYPE) {
        return NULL;
    }

    Bus = HalpBusTable[InterfaceType];
    while (Bus) {
        if (Bus->BusNumber == BusNumber  &&  Bus->GetInterruptVector) {
            return Bus;
        }

        Bus = Bus->Next;
    }

    return NULL;
}



PBUSHANDLER HalpHandlerForConfigSpace (
    IN BUS_DATA_TYPE  BusDataType,
    IN ULONG          BusNumber
    )
{
    PBUSHANDLER     Bus;
    if (!HalpConfigTable[BusDataType]) {
        return NULL;
    }

    Bus = HalpBusTable[HalpConfigTable[BusDataType]];
    while (Bus) {
        if (Bus->BusNumber == BusNumber) {
            return Bus;
        }

        Bus = Bus->Next;
    }

    return NULL;
}


ULONG
HalGetBusData(
    IN BUS_DATA_TYPE  BusDataType,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Length
    )
{
    return HalGetBusDataByOffset (BusDataType,BusNumber,SlotNumber,Buffer,0,Length);
}

ULONG
HalGetBusDataByOffset (
    IN BUS_DATA_TYPE  BusDataType,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    )
/*++

Routine Description:

    Dispatcher for GetBusData

--*/
{
    PBUSHANDLER Handler;

    Handler = HalpHandlerForConfigSpace (BusDataType, BusNumber);
    if (!Handler) {
        return 0;
    }

    return Handler->GetBusData (Handler, Handler, SlotNumber, Buffer, Offset, Length);
}

ULONG
HalSetBusData(
    IN BUS_DATA_TYPE  BusDataType,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Length
    )
{
    return HalSetBusDataByOffset (BusDataType,BusNumber,SlotNumber,Buffer,0,Length);
}

ULONG
HalSetBusDataByOffset(
    IN BUS_DATA_TYPE  BusDataType,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    )
/*++

Routine Description:

    Dispatcher for SetBusData

--*/
{
    PBUSHANDLER Handler;

    Handler = HalpHandlerForConfigSpace (BusDataType, BusNumber);
    if (!Handler) {
        return 0;
    }

    return Handler->SetBusData (Handler, Handler, SlotNumber, Buffer, Offset, Length);
}

NTSTATUS
HalAdjustResourceList (
    IN OUT PIO_RESOURCE_REQUIREMENTS_LIST   *pResourceList
    )
/*++

Routine Description:

    Dispatcher for AdjustResourceList

--*/
{
    PBUSHANDLER Handler;

    Handler = HalpHandlerForBus (
                (*pResourceList)->InterfaceType,
                (*pResourceList)->BusNumber
              );
    if (!Handler) {
        return STATUS_SUCCESS;
    }

    return Handler->AdjustResourceList (Handler, Handler, pResourceList);
}

NTSTATUS
HalAssignSlotResources (
    IN PUNICODE_STRING          RegistryPath,
    IN PUNICODE_STRING          DriverClassName       OPTIONAL,
    IN PDRIVER_OBJECT           DriverObject,
    IN PDEVICE_OBJECT           DeviceObject          OPTIONAL,
    IN INTERFACE_TYPE           BusType,
    IN ULONG                    BusNumber,
    IN ULONG                    SlotNumber,
    IN OUT PCM_RESOURCE_LIST   *AllocatedResources
    )
/*++

Routine Description:

    Dispatcher for AssignSlotResources

--*/
{
    PBUSHANDLER Handler;

    Handler = HalpHandlerForBus (BusType, BusNumber);
    if (!Handler) {
        return STATUS_NOT_FOUND;
    }

    return Handler->AssignSlotResources (
                Handler,
                Handler,
                RegistryPath,
                DriverClassName,
                DriverObject,
                DeviceObject,
                SlotNumber,
                AllocatedResources
            );
}


ULONG
HalGetInterruptVector(
    IN INTERFACE_TYPE  InterfaceType,
    IN ULONG BusNumber,
    IN ULONG BusInterruptLevel,
    IN ULONG BusInterruptVector,
    OUT PKIRQL Irql,
    OUT PKAFFINITY Affinity
    )
/*++

Routine Description:

    Dispatcher for GetInterruptVector

--*/
{
    PBUSHANDLER Handler;

    Handler = HalpHandlerForBus (InterfaceType, BusNumber);
    *Irql = 0;
    *Affinity = 0;

    if (!Handler) {
        return 0;
    }

    return Handler->GetInterruptVector (
              Handler, Handler, BusInterruptLevel, BusInterruptVector, Irql, Affinity);
}

BOOLEAN
HalTranslateBusAddress(
    IN INTERFACE_TYPE  InterfaceType,
    IN ULONG BusNumber,
    IN PHYSICAL_ADDRESS BusAddress,
    IN OUT PULONG AddressSpace,
    OUT PPHYSICAL_ADDRESS TranslatedAddress
    )
/*++

Routine Description:

    Dispatcher for TranslateBusAddress

--*/
{
    PBUSHANDLER Handler;

    Handler = HalpHandlerForBus (InterfaceType, BusNumber);
    if (!Handler) {
        return FALSE;
    }

    return Handler->TranslateBusAddress (
              Handler, Handler, BusAddress, AddressSpace, TranslatedAddress);
}


//
// Null handlers
//

ULONG HalpNoBusData (
    IN PVOID BusHandler,
    IN PVOID RootHandler,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    )
/*++

Routine Description:

    Stub handler for buses which do not have a configuration space

--*/
{
    return 0;
}

NTSTATUS
HalpNoAdjustResourceList (
    IN PVOID BusHandler,
    IN PVOID RootHandler,
    IN OUT PIO_RESOURCE_REQUIREMENTS_LIST   *pResourceList
    )
/*++

Routine Description:

    Stub handler for buses which do not have a configuration space

--*/
{
    return STATUS_UNSUCCESSFUL;
}


NTSTATUS
HalpNoAssignSlotResources (
    IN PVOID BusHandler,
    IN PVOID RootHandler,
    IN PUNICODE_STRING          RegistryPath,
    IN PUNICODE_STRING          DriverClassName       OPTIONAL,
    IN PDRIVER_OBJECT           DriverObject,
    IN PDEVICE_OBJECT           DeviceObject          OPTIONAL,
    IN ULONG                    SlotNumber,
    IN OUT PCM_RESOURCE_LIST   *AllocatedResources
    )
/*++

Routine Description:

    Stub handler for buses which do not have a configuration space

--*/
{
    return STATUS_NOT_SUPPORTED;
}


