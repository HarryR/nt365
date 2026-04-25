/*
 * dma.c - HAL DMA layer for MicroNT.
 *
 * MicroNT is committed to PCI bus-master DMA only:
 *
 *  - No ISA / EISA / MCA bus support  - QEMU pc / q35 don't expose them.
 *  - No 8237 DMA controller  - no devices use legacy channel DMA.
 *  - No bounce-buffer pool / map registers  - PCI bus master can drive
 *    its own DMA cycles to any 32-bit physical address; the kernel is
 *    32-bit non-PAE, so all device-visible memory is reachable directly.
 *  - All BARs forced into the low 32-bit MMIO window by
 *    HalpRelocateHighPciBars (see ixpcibus.c).
 *
 * Drivers that ask for an ISA DMA channel get NULL from HalGetAdapter
 * - they fail cleanly at init time rather than corrupt memory.
 *
 * The ADAPTER_OBJECT layout is private to the HAL (forward-declared in
 * io.h, opaque to drivers); the full definition lives below.
 */

#define _NTSYSTEM_
#include "halp.h"

/* Spinlock - same fastcall-via-macro pattern as ixpcibus.c. */
KIRQL FASTCALL KfAcquireSpinLock(PKSPIN_LOCK SpinLock);
VOID  FASTCALL KfReleaseSpinLock(PKSPIN_LOCK SpinLock, KIRQL OldIrql);
#define KeAcquireSpinLock(a, b) *(b) = KfAcquireSpinLock(a)
#define KeReleaseSpinLock(a, b) KfReleaseSpinLock(a, b)

/* IO_TYPE_ADAPTER lives in io.h; not redefining here. */

/* ------------------------------------------------------------------ *
 * ADAPTER_OBJECT - private HAL layout.
 *
 * One per device that calls HalGetAdapter; cached in HalpAdapterList
 * so repeated calls return the same pointer. Devices don't unplug in
 * our environment so there's no removal path - adapters live for the
 * lifetime of the system.
 * ------------------------------------------------------------------ */
typedef struct _ADAPTER_OBJECT {
    /* Type/Size let the kernel / Io subsystem identify the object
       even though they treat it as opaque. IO_TYPE_ADAPTER from io.h. */
    CSHORT          Type;
    CSHORT          Size;

    LIST_ENTRY      Link;       /* HalpAdapterList chain */

    /* Bus identity - used only to dedupe adapter creation. NT 3.5's
       DEVICE_DESCRIPTION doesn't carry a slot number, just the bus,
       so dedup is per (InterfaceType, BusNumber). In practice every
       miniport calls HalGetAdapter exactly once anyway. */
    INTERFACE_TYPE  InterfaceType;
    ULONG           BusNumber;

    /* Capability flags - mirror what the device descriptor asked for
       so any future code that peeks at the adapter behaves correctly. */
    BOOLEAN         MasterDevice;
    BOOLEAN         ScatterGather;
    BOOLEAN         Dma32BitAddresses;
} ADAPTER_OBJECT, *PADAPTER_OBJECT;

static LIST_ENTRY  HalpAdapterList;
static KSPIN_LOCK  HalpAdapterListLock;
static BOOLEAN     HalpAdapterListInited = FALSE;

static VOID
HalpEnsureAdapterList(VOID)
{
    if (!HalpAdapterListInited) {
        InitializeListHead(&HalpAdapterList);
        KeInitializeSpinLock(&HalpAdapterListLock);
        HalpAdapterListInited = TRUE;
    }
}

/* ------------------------------------------------------------------ *
 * HalGetAdapter.
 *
 * For PCI bus master: allocate or return cached ADAPTER_OBJECT for
 * (BusNumber, SlotNumber). NumberOfMapRegisters reports the maximum
 * scatter-gather list entries the driver may request - 0xFF is plenty
 * for current consumers (NVMe MDTS caps transfers around a few MiB,
 * each at most 256 pages = 256 PRP entries; SCSI miniports default
 * to 0xFF or NumberOfPhysicalBreaks).
 *
 * Reject ISA / EISA / MCA - drivers that need legacy channel DMA must
 * not silently get a bus-master adapter. Returning NULL fails
 * ScsiPortGetUncachedExtension cleanly.
 *
 * NT 3.5's DEVICE_DESCRIPTION has no Dma64BitAddresses field; that
 * landed in NT4. We unconditionally set Dma32BitAddresses=TRUE in
 * the adapter, matching what a 32-bit non-PAE kernel can address.
 * ------------------------------------------------------------------ */
PADAPTER_OBJECT
HalGetAdapter(
    IN PDEVICE_DESCRIPTION DeviceDescription,
    IN OUT PULONG NumberOfMapRegisters
    )
{
    PADAPTER_OBJECT adapter;
    PLIST_ENTRY     entry;
    KIRQL           oldIrql;

    HalpEnsureAdapterList();

    /* Reject what we don't support. */
    if (DeviceDescription->InterfaceType != PCIBus) {
        DbgPrint("HAL: HalGetAdapter rejected non-PCI interface=%d\n",
                 DeviceDescription->InterfaceType);
        return NULL;
    }
    if (!DeviceDescription->Master) {
        DbgPrint("HAL: HalGetAdapter rejected non-bus-master device "
                 "(bus=%d) - MicroNT supports bus-master DMA only\n",
                 DeviceDescription->BusNumber);
        return NULL;
    }

    /* Look for a cached adapter for this (interface, bus). */
    KeAcquireSpinLock(&HalpAdapterListLock, &oldIrql);
    for (entry = HalpAdapterList.Flink;
         entry != &HalpAdapterList;
         entry = entry->Flink) {
        adapter = CONTAINING_RECORD(entry, ADAPTER_OBJECT, Link);
        if (adapter->BusNumber == DeviceDescription->BusNumber &&
            adapter->InterfaceType == DeviceDescription->InterfaceType) {
            KeReleaseSpinLock(&HalpAdapterListLock, oldIrql);
            *NumberOfMapRegisters = 0xFF;
            return adapter;
        }
    }
    KeReleaseSpinLock(&HalpAdapterListLock, oldIrql);

    /* No match - allocate. */
    adapter = (PADAPTER_OBJECT)ExAllocatePool(NonPagedPool,
                                              sizeof(ADAPTER_OBJECT));
    if (!adapter) {
        DbgPrint("HAL: HalGetAdapter ExAllocatePool failed\n");
        return NULL;
    }
    RtlZeroMemory(adapter, sizeof(ADAPTER_OBJECT));
    adapter->Type              = IO_TYPE_ADAPTER;
    adapter->Size              = sizeof(ADAPTER_OBJECT);
    adapter->InterfaceType     = DeviceDescription->InterfaceType;
    adapter->BusNumber         = DeviceDescription->BusNumber;
    adapter->MasterDevice      = TRUE;
    adapter->ScatterGather     = DeviceDescription->ScatterGather;
    adapter->Dma32BitAddresses = TRUE;   /* 32-bit non-PAE kernel */

    KeAcquireSpinLock(&HalpAdapterListLock, &oldIrql);
    InsertTailList(&HalpAdapterList, &adapter->Link);
    KeReleaseSpinLock(&HalpAdapterListLock, oldIrql);

    *NumberOfMapRegisters = 0xFF;
    DbgPrint("HAL: HalGetAdapter created PCI bus=%d adapter=%p (S/G=%d)\n",
             adapter->BusNumber, adapter, adapter->ScatterGather);
    return adapter;
}

/* ------------------------------------------------------------------ *
 * HalAllocateCommonBuffer / HalFreeCommonBuffer.
 *
 * PCI on x86 is fully cache-coherent in both directions (snooped on
 * the CPU side, DMA observes CPU writes, no software flush needed).
 * MmAllocateContiguousMemory gives us non-paged, contiguous physical
 * memory; we cap the upper paddr at 4 GiB - 1 to match our 32-bit
 * Dma32BitAddresses claim.
 * ------------------------------------------------------------------ */
PVOID
HalAllocateCommonBuffer(
    IN PADAPTER_OBJECT AdapterObject,
    IN ULONG Length,
    OUT PPHYSICAL_ADDRESS LogicalAddress,
    IN BOOLEAN CacheEnabled
    )
{
    PHYSICAL_ADDRESS HighestAcceptable;
    PVOID            va;

    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(CacheEnabled);  /* x86 PCI is coherent either way */

    HighestAcceptable.HighPart = 0;
    HighestAcceptable.LowPart  = 0xFFFFFFFFul;
    va = MmAllocateContiguousMemory(Length, HighestAcceptable);
    if (!va) {
        LogicalAddress->QuadPart = 0;
        return NULL;
    }

    *LogicalAddress = MmGetPhysicalAddress(va);
    return va;
}

VOID
HalFreeCommonBuffer(
    IN PADAPTER_OBJECT AdapterObject,
    IN ULONG Length,
    IN PHYSICAL_ADDRESS LogicalAddress,
    IN PVOID VirtualAddress,
    IN BOOLEAN CacheEnabled
    )
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(CacheEnabled);
    UNREFERENCED_PARAMETER(Length);

    if (VirtualAddress) {
        MmFreeContiguousMemory(VirtualAddress);
    }
}

BOOLEAN
HalFlushCommonBuffer(
    IN PADAPTER_OBJECT AdapterObject,
    IN ULONG Length,
    IN PHYSICAL_ADDRESS LogicalAddress,
    IN PVOID VirtualAddress
    )
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(VirtualAddress);

    /* x86 PCI is cache-coherent at the bus level; nothing to flush. */
    return TRUE;
}

/* ------------------------------------------------------------------ *
 * HalAllocateAdapterChannel.
 *
 * The classic NT contract is "wait for map registers / channel
 * availability, then call ExecutionRoutine when ready". For pure PCI
 * bus master with no map register pool there is nothing to wait for -
 * the routine is invoked synchronously with MapRegisterBase=NULL.
 *
 * Documented to run at DISPATCH_LEVEL; the caller is expected to
 * already be there. ExecutionRoutine returns IO_ALLOCATION_ACTION,
 * which we ignore: KeepObject is what makes sense here (per-device
 * adapters that live forever) and DeallocateObject would normally
 * remove the adapter from a wait queue and signal the next waiter.
 * We have no queue - the adapter persists across calls regardless.
 * ------------------------------------------------------------------ */
NTSTATUS
HalAllocateAdapterChannel(
    IN PADAPTER_OBJECT AdapterObject,
    IN PWAIT_CONTEXT_BLOCK Wcb,
    IN ULONG NumberOfMapRegisters,
    IN PDRIVER_CONTROL ExecutionRoutine
    )
{
    IO_ALLOCATION_ACTION action;

    UNREFERENCED_PARAMETER(NumberOfMapRegisters);

    if (!AdapterObject || !ExecutionRoutine) {
        return STATUS_INVALID_PARAMETER;
    }

    Wcb->NumberOfMapRegisters = 0;
    Wcb->DeviceRoutine        = ExecutionRoutine;

    action = ExecutionRoutine((PDEVICE_OBJECT)Wcb->DeviceObject,
                              (PIRP)Wcb->CurrentIrp,
                              NULL,                /* MapRegisterBase  */
                              Wcb->DeviceContext);

    UNREFERENCED_PARAMETER(action);
    return STATUS_SUCCESS;
}

VOID
IoFreeAdapterChannel(
    IN PADAPTER_OBJECT AdapterObject
    )
{
    /* Per-device adapters with no map registers / no wait queue:
       nothing to release. The adapter stays alive for further calls. */
    UNREFERENCED_PARAMETER(AdapterObject);
}

VOID
IoFreeMapRegisters(
    IN PADAPTER_OBJECT AdapterObject,
    IN PVOID MapRegisterBase,
    IN ULONG NumberOfMapRegisters
    )
{
    /* Bus master uses no map registers. */
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
}

/* ------------------------------------------------------------------ *
 * IoMapTransfer.
 *
 * For one MDL-described buffer, return the bus physical address of
 * the contiguous run starting at CurrentVa, and adjust *Length down
 * to the run length. Drivers loop until they've described the whole
 * transfer (one S/G entry per IoMapTransfer call).
 *
 * On i386 with no IOMMU, host paddr == bus paddr - identity. We walk
 * the MDL's PFN array (immediately follows the MDL header) to find
 * the longest physically-contiguous run starting at the page that
 * contains CurrentVa.
 * ------------------------------------------------------------------ */
PHYSICAL_ADDRESS
IoMapTransfer(
    IN PADAPTER_OBJECT AdapterObject,
    IN PMDL Mdl,
    IN PVOID MapRegisterBase,
    IN PVOID CurrentVa,
    IN OUT PULONG Length,
    IN BOOLEAN WriteToDevice
    )
{
    PULONG          pfnArray;
    ULONG           offsetFromMdl;
    ULONG           firstPageIdx;
    ULONG           inPageOffset;
    ULONG           remaining;
    ULONG           runBytes;
    ULONG           prevPfn;
    ULONG           i;
    PHYSICAL_ADDRESS paddr;

    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(WriteToDevice);

    /* PFN array follows the MDL header. */
    pfnArray = (PULONG)(Mdl + 1);

    offsetFromMdl = (ULONG)((PUCHAR)CurrentVa -
                            ((PUCHAR)Mdl->StartVa + Mdl->ByteOffset));
    firstPageIdx  = (Mdl->ByteOffset + offsetFromMdl) >> PAGE_SHIFT;
    inPageOffset  = (Mdl->ByteOffset + offsetFromMdl) & (PAGE_SIZE - 1);

    paddr.HighPart = 0;
    paddr.LowPart  = (pfnArray[firstPageIdx] << PAGE_SHIFT) + inPageOffset;

    remaining = *Length;
    runBytes  = PAGE_SIZE - inPageOffset;
    if (runBytes > remaining) runBytes = remaining;

    /* Extend the contiguous run page-by-page. */
    prevPfn = pfnArray[firstPageIdx];
    for (i = 1; runBytes < remaining; i++) {
        ULONG curPfn = pfnArray[firstPageIdx + i];
        ULONG add;
        if (curPfn != prevPfn + 1) break;
        add = PAGE_SIZE;
        if (runBytes + add > remaining) add = remaining - runBytes;
        runBytes += add;
        prevPfn = curPfn;
    }

    *Length = runBytes;
    return paddr;
}

BOOLEAN
IoFlushAdapterBuffers(
    IN PADAPTER_OBJECT AdapterObject,
    IN PMDL Mdl,
    IN PVOID MapRegisterBase,
    IN PVOID CurrentVa,
    IN ULONG Length,
    IN BOOLEAN WriteToDevice
    )
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(CurrentVa);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(WriteToDevice);

    /* No bounce buffers, no map registers - nothing to flush. PCI on
       x86 is cache-coherent. */
    return TRUE;
}

ULONG
HalReadDmaCounter(
    IN PADAPTER_OBJECT AdapterObject
    )
{
    /* The DMA counter is meaningful only for 8237-channel DMA, which
       we don't support. Bus-master devices manage their own transfer
       counters in device registers - this entry point is irrelevant. */
    UNREFERENCED_PARAMETER(AdapterObject);
    return 0;
}

/* ------------------------------------------------------------------ *
 * HalAllocateCrashDumpRegisters.
 *
 * Crashdump support not implemented. The dump driver path requires a
 * separate adapter+map-register reservation done at boot; we'd need
 * to wire it up alongside an actual dump driver (none ship today).
 * ------------------------------------------------------------------ */
PVOID
HalAllocateCrashDumpRegisters(
    IN PADAPTER_OBJECT AdapterObject,
    IN ULONG NumberOfMapRegisters
    )
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
    return NULL;
}
