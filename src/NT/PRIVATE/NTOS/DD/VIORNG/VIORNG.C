/*++

    viorng.c — virtio-rng entropy source. A pure entropy *pump* with NO
    user-facing device: a dedicated system thread periodically asks the
    device for random bytes and folds them straight into the kernel RNG
    pool via RngAddEntropy. It is one of several entropy inputs; userland
    never talks to it directly (that path is NtGenerateSecureRandom).

    PCI device 1AF4:1005 (Red Hat / Qumranet, legacy virtio-rng).
    QEMU exposes this when invoked with:
        -device virtio-rng-pci,disable-modern=on,disable-legacy=off

    One request at a time: the pump thread submits a write-only descriptor,
    waits on DoneEvent (signaled by the completion DPC), then absorbs the
    bytes and sleeps. No \Device\ object, no IRP dispatch.

--*/

#include <ntddk.h>
#include "virtio.h"
#include "vio_pci.h"
#include "vio_ids.h"

/* RNG subsystem entropy intake — exported by ntoskrnl (NTOS/RNG). */
extern VOID RngAddEntropy(IN PVOID Buffer, IN ULONG Length);

/* ------------------------------------------------------------------ *
 * Per-device extension. Pointed to by DEVICE_OBJECT->DeviceExtension
 * (the device object is unnamed — it exists only to anchor this state
 * and the interrupt; it is never opened).
 * ------------------------------------------------------------------ */
typedef struct _VIORNG_DEV {
    VIRTIO_PCI_DEV    Pci;
    PDEVICE_OBJECT    DevObj;
    PKINTERRUPT       Interrupt;
    KSPIN_LOCK        IsrLock;     /* synchronization with ISR */
    KDPC              CompletionDpc;
    PVIRTQUEUE        Queue;       /* virtio-rng has only the requestq (id 0) */
    KEVENT            DoneEvent;   /* signaled by the DPC when a fill completes */
    ULONG             LastLen;     /* bytes the device wrote on the last fill */
    PVOID             ScratchBuf;  /* NonPagedPool DMA target */
    PHYSICAL_ADDRESS  ScratchPaddr;
    ULONG             ScratchLen;  /* size of ScratchBuf */
} VIORNG_DEV, *PVIORNG_DEV;

#define VIORNG_SCRATCH_SIZE    4096   /* one page; DMA target */
#define VIORNG_RESEED_BYTES    64     /* entropy pulled per reseed */
/* Gap between reseeds (~2 min), as negative relative 100ns units. */
#define VIORNG_RESEED_INTERVAL (-((LONGLONG)120 * 10 * 1000 * 1000))
/* How long to wait for one fill before giving up (negative relative 100ns). */
#define VIORNG_FILL_TIMEOUT    (-((LONGLONG)5 * 10 * 1000 * 1000))

/* Driver-global slot for the (single) device we manage. NT 3.5 had
   no AddDevice routine; DriverEntry walks the bus + creates objects. */
static PVIORNG_DEV g_Dev = NULL;

/* ------------------------------------------------------------------ *
 * Forward declarations.
 * ------------------------------------------------------------------ */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath);

static BOOLEAN   VioRngIsr (PKINTERRUPT Interrupt, PVOID Context);
static VOID      VioRngDpc (PKDPC Dpc, PVOID Context, PVOID A1, PVOID A2);
static ULONG     VioRngReseedOnce(PVIORNG_DEV dev);
static VOID      VioRngPumpThread(PVOID Context);

static NTSTATUS  VioRngFindAndAttach(PDRIVER_OBJECT DriverObject,
                                     PUNICODE_STRING RegPath);

/* ------------------------------------------------------------------ *
 * DriverEntry — runs once at I/O Manager init time. Attaches to the
 * device and starts the entropy pump thread. No major-function
 * handlers: the driver has no openable device.
 * ------------------------------------------------------------------ */
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath)
{
    NTSTATUS st;
    HANDLE   h;
    ULONG    seeded;

    st = VioRngFindAndAttach(DriverObject, RegPath);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIORNG: no virtio-rng device found (st=0x%08x)\n", st);
        return st;
    }

    /* Seed the pool once, synchronously, at load — so there's real hardware
       entropy in immediately, without waiting for the first periodic tick. */
    seeded = VioRngReseedOnce(g_Dev);

    st = PsCreateSystemThread(&h, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                              VioRngPumpThread, g_Dev);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIORNG: pump thread create failed 0x%08x\n", st);
        return st;
    }
    ZwClose(h);

    DbgPrint("VIORNG: seeded %u bytes, pump running\n", seeded);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * PCI bus walk + device init. Walks bus 0, slots 0..31, looking for
 * vendor 0x1AF4 / device 0x1005 (legacy virtio-rng). On the first
 * match, allocates resources via HalAssignSlotResources, sets up
 * virtio-pci, creates the request queue, and registers the device.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioRngFindAndAttach(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath)
{
    PDEVICE_OBJECT devObj;
    PVIORNG_DEV    dev;
    NTSTATUS       st;
    ULONG          slot;
    PCI_COMMON_CONFIG cfg;
    ULONG          got;
    PCM_RESOURCE_LIST resources = NULL;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pd;
    ULONG          i;
    PUCHAR         ioBase = NULL;
    ULONG          intVector = 0;
    KIRQL          intLevel = 0;
    KAFFINITY      affinity = 0;

    /* (1) Find the device. Match modern (0x1044) OR transitional
       (0x1005) ID — QEMU's default for virtio-rng-pci is transitional,
       which advertises 0x1005 but still exposes the modern PCI caps
       we drive against. */
    for (slot = 0; slot < 32 * 8; slot++) {
        got = HalGetBusDataByOffset(PCIConfiguration, 0, slot,
                                    &cfg, 0, sizeof(cfg));
        if (got < 4)                                      continue;
        if (cfg.VendorID == 0xFFFF)                       continue;
        if (cfg.VendorID != VIRTIO_PCI_VENDOR_ID)         continue;
        if (cfg.DeviceID != VIRTIO_PCI_DEV_RNG &&
            cfg.DeviceID != VIRTIO_PCI_TRANS_RNG)         continue;
        break;
    }
    if (slot >= 32 * 8)
        return STATUS_NO_SUCH_DEVICE;

    /* (2) Ask HAL to assign + translate this slot's resources. */
    st = HalAssignSlotResources(RegPath, NULL, DriverObject, NULL,
                                PCIBus, 0, slot, &resources);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIORNG: HalAssignSlotResources failed 0x%08x\n", st);
        return st;
    }

    /* Walk the partials: we only need the interrupt resource — the
       modern transport reads BARs from PCI config space itself via
       VirtioPciInit's cap walk, so we don't pre-extract a port range
       here. */
    for (i = 0; i < resources->List[0].PartialResourceList.Count; i++) {
        pd = &resources->List[0].PartialResourceList.PartialDescriptors[i];
        if (pd->Type == CmResourceTypeInterrupt && intVector == 0) {
            intVector = pd->u.Interrupt.Vector;
            intLevel  = (KIRQL)pd->u.Interrupt.Level;
        }
    }
    if (!intVector) {
        DbgPrint("VIORNG: missing IRQ resource\n");
        ExFreePool(resources);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    /* The HalAssignSlotResources interrupt is bus-relative on NT 3.5
       PCI (= the IRQ line via INTA-D routing). Translate to a system
       vector + DIRQL + affinity — same pattern as serial.sys. */
    {
        ULONG sysVector;
        KIRQL sysIrql = 0;
        sysVector = HalGetInterruptVector(PCIBus, 0, intLevel, intVector,
                                          &sysIrql, &affinity);
        intVector = sysVector;
        intLevel  = sysIrql;
    }
    /* ioBase no longer needed — VirtioPciInit reads BARs from PCI
       config and MmMapIoSpace's them itself based on what the cap
       walk reports. */
    UNREFERENCED_PARAMETER(ioBase);

    /* (3) Create an UNNAMED device object — it only anchors the device
       extension and the interrupt; nothing ever opens it. */
    st = IoCreateDevice(DriverObject, sizeof(VIORNG_DEV), NULL,
                        FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIORNG: IoCreateDevice failed 0x%08x\n", st);
        ExFreePool(resources);
        return st;
    }

    dev = (PVIORNG_DEV)devObj->DeviceExtension;
    RtlZeroMemory(dev, sizeof(*dev));
    dev->DevObj = devObj;
    KeInitializeSpinLock(&dev->IsrLock);
    KeInitializeDpc(&dev->CompletionDpc, VioRngDpc, dev);
    KeInitializeEvent(&dev->DoneEvent, SynchronizationEvent, FALSE);

    /* (4) Init virtio_pci (modern transport — caps + MmMapIoSpace),
       then run the device handshake. */
    st = VirtioPciInit(&dev->Pci, 0, slot,
                       intVector, intLevel, affinity,
                       VIRTIO_ID_RNG);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIORNG: VirtioPciInit failed 0x%08x\n", st);
        goto fail_dev;
    }

    VirtioDevReset(&dev->Pci.Vdev);
    VirtioDevStatusUpdate(&dev->Pci.Vdev, VIRTIO_STATUS_ACK);
    VirtioDevStatusUpdate(&dev->Pci.Vdev,
                          VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    dev->Pci.Vdev.Features = VirtioFeatureGet(&dev->Pci.Vdev);
    VirtioFeatureSet(&dev->Pci.Vdev);

    /* (5) Find + set up the single request queue (queue id 0). */
    {
        u16 vqsize;
        st = VirtioFindVqs(&dev->Pci.Vdev, 1, &vqsize);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIORNG: VirtioFindVqs failed 0x%08x\n", st);
            goto fail_dev;
        }
        st = VirtioVqSetup(&dev->Pci.Vdev, 0, vqsize, NULL, &dev->Queue);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIORNG: VirtioVqSetup failed 0x%08x\n", st);
            goto fail_dev;
        }
        dev->Queue->Priv = dev;   /* DPC needs the device extension */

        DbgPrint("VIORNG: virtio-rng devid 0x%04x slot 0x%02x vec=%u requestq=%u\n",
                 cfg.DeviceID, slot, intVector, vqsize);
    }

    /* (6) Allocate a NonPagedPool scratch buffer for the device to
       write entropy into. Get its physical addr now since we'll need
       it for every enqueue. */
    dev->ScratchBuf = ExAllocatePoolWithTag(
        NonPagedPool, VIORNG_SCRATCH_SIZE, '0gnR');
    if (!dev->ScratchBuf) {
        st = STATUS_INSUFFICIENT_RESOURCES;
        goto fail_dev;
    }
    dev->ScratchPaddr = MmGetPhysicalAddress(dev->ScratchBuf);
    dev->ScratchLen   = VIORNG_SCRATCH_SIZE;

    /* (7) Wire the interrupt. After this, the device may fire — we
       must have everything else ready first. */
    st = IoConnectInterrupt(&dev->Interrupt, VioRngIsr, dev,
                            NULL, intVector, intLevel, intLevel,
                            LevelSensitive, TRUE, affinity, FALSE);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIORNG: IoConnectInterrupt failed 0x%08x\n", st);
        goto fail_dev;
    }

    /* (8) Mark the driver up — the device may now post used-ring entries. */
    VirtioDevDriverUp(&dev->Pci.Vdev);

    g_Dev = dev;
    ExFreePool(resources);
    return STATUS_SUCCESS;

fail_dev:
    if (dev->ScratchBuf) ExFreePool(dev->ScratchBuf);
    if (dev->Queue)      VirtioVqRelease(&dev->Pci.Vdev, dev->Queue);
    IoDeleteDevice(devObj);
    ExFreePool(resources);
    return st;
}

/* ------------------------------------------------------------------ *
 * One reseed: submit a write-only descriptor, wait for the completion
 * DPC to signal DoneEvent, then fold the bytes into the kernel RNG pool.
 * One request in flight at a time. Returns bytes absorbed. Runs at
 * PASSIVE_LEVEL (DriverEntry at load, and the pump thread thereafter).
 * ------------------------------------------------------------------ */
static ULONG
VioRngReseedOnce(PVIORNG_DEV dev)
{
    VIRTIO_SG_SEG  seg;
    VIRTIO_SG_LIST sg;
    KIRQL          irql;
    NTSTATUS       st;
    LARGE_INTEGER  timeout;

    KeClearEvent(&dev->DoneEvent);
    dev->LastLen = 0;

    seg.Paddr  = dev->ScratchPaddr;
    seg.Len    = VIORNG_RESEED_BYTES;
    sg.NumSegs = 1;
    sg.Segs    = &seg;

    /* read_bufs = 0 (driver→device), write_bufs = 1 (device→driver). */
    KeAcquireSpinLock(&dev->IsrLock, &irql);
    st = VirtqEnqueue(dev->Queue, dev, &sg, 0, 1);
    if (NT_SUCCESS(st)) {
        VirtqHostNotify(dev->Queue);
    }
    KeReleaseSpinLock(&dev->IsrLock, irql);

    if (!NT_SUCCESS(st)) {
        DbgPrint("VIORNG: enqueue failed 0x%08x\n", st);
        return 0;
    }

    /* Bounded wait so a wedged device can't hang boot (DriverEntry) or the
       pump thread forever — we just skip this round and try again later. */
    timeout.QuadPart = VIORNG_FILL_TIMEOUT;
    st = KeWaitForSingleObject(&dev->DoneEvent, Executive, KernelMode,
                               FALSE, &timeout);
    if (st == STATUS_TIMEOUT) {
        DbgPrint("VIORNG: fill timed out\n");
        return 0;
    }

    if (dev->LastLen > 0) {
        RngAddEntropy(dev->ScratchBuf, dev->LastLen);
    }
    return dev->LastLen;
}

/* ------------------------------------------------------------------ *
 * Entropy pump thread — tops the pool up periodically. The initial seed
 * already happened synchronously in DriverEntry, so this delays first.
 * ------------------------------------------------------------------ */
static VOID
VioRngPumpThread(PVOID Context)
{
    PVIORNG_DEV   dev = (PVIORNG_DEV)Context;
    LARGE_INTEGER interval;

    interval.QuadPart = VIORNG_RESEED_INTERVAL;

    for (;;) {
        KeDelayExecutionThread(KernelMode, FALSE, &interval);
        VioRngReseedOnce(dev);
    }
}

/* ------------------------------------------------------------------ *
 * Interrupt service routine — runs at DIRQL. Must do the bare minimum:
 * ack the device's ISR register (VirtioPciIsr does this) and queue a
 * DPC for the heavy work (used-ring drain + signalling the pump thread).
 * ------------------------------------------------------------------ */
static BOOLEAN
VioRngIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    PVIORNG_DEV dev = (PVIORNG_DEV)Context;
    int handled;

    UNREFERENCED_PARAMETER(Interrupt);

    handled = VirtioPciIsr(&dev->Pci);
    if (handled) {
        KeInsertQueueDpc(&dev->CompletionDpc, NULL, NULL);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ *
 * DPC — runs at DISPATCH_LEVEL. Drains the used ring, records how many
 * bytes the device wrote, and signals the pump thread (which does the
 * RngAddEntropy at PASSIVE_LEVEL with no lock held).
 * ------------------------------------------------------------------ */
static VOID
VioRngDpc(PKDPC Dpc, PVOID Context, PVOID A1, PVOID A2)
{
    PVIORNG_DEV dev = (PVIORNG_DEV)Context;
    PVOID       cookie;
    u32         len;
    NTSTATUS    st;
    KIRQL       irql;
    BOOLEAN     completed = FALSE;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(A1);
    UNREFERENCED_PARAMETER(A2);

    KeAcquireSpinLock(&dev->IsrLock, &irql);
    for (;;) {
        st = VirtqDequeue(dev->Queue, &cookie, &len);
        if (!NT_SUCCESS(st))
            break;
        dev->LastLen = len;       /* single request in flight */
        completed = TRUE;
    }
    KeReleaseSpinLock(&dev->IsrLock, irql);

    if (completed) {
        KeSetEvent(&dev->DoneEvent, IO_NO_INCREMENT, FALSE);
    }
}
