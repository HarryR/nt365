/*++

    viorng.c — virtio-rng entropy device. The simplest possible
    consumer of the shared virtio.lib: one queue, one operation
    ("device, please write N random bytes into this buffer").

    Surfaces \Device\VirtioRng0 with IRP_MJ_READ. Lua opens it from
    user mode and reads bytes; the driver pushes the buffer into the
    request virtqueue, the device fills it, and completion bubbles
    back through the ISR/DPC chain to IoCompleteRequest.

    PCI device 1AF4:1005 (Red Hat / Qumranet, legacy virtio-rng).
    QEMU exposes this when invoked with:
        -device virtio-rng-pci,disable-modern=on,disable-legacy=off

    Single in-flight IRP for now — concurrent reads return
    STATUS_DEVICE_BUSY. Good enough for milestone "Lua reads 16 bytes
    of entropy"; multi-IRP queueing via IoStartPacket can come later.

--*/

#include <ntddk.h>
#include "virtio.h"
#include "vio_pci.h"
#include "vio_ids.h"

/* ------------------------------------------------------------------ *
 * Per-device extension. Pointed to by DEVICE_OBJECT->DeviceExtension.
 * ------------------------------------------------------------------ */
typedef struct _VIORNG_DEV {
    VIRTIO_PCI_DEV    Pci;
    PDEVICE_OBJECT    DevObj;
    PKINTERRUPT       Interrupt;
    KSPIN_LOCK        IsrLock;     /* synchronization with ISR */
    KDPC              CompletionDpc;
    PVIRTQUEUE        Queue;       /* virtio-rng has only the requestq (id 0) */
    PIRP              CurrentIrp;  /* single in-flight slot */
    PVOID             ScratchBuf;  /* NonPagedPool DMA target */
    PHYSICAL_ADDRESS  ScratchPaddr;
    ULONG             ScratchLen;  /* size of ScratchBuf */
} VIORNG_DEV, *PVIORNG_DEV;

#define VIORNG_SCRATCH_SIZE   4096   /* one page; max entropy per read */

/* Driver-global slot for the (single) device we manage. NT 3.5 had
   no AddDevice routine; DriverEntry walks the bus + creates objects. */
static PVIORNG_DEV g_Dev = NULL;

/* ------------------------------------------------------------------ *
 * Forward declarations.
 * ------------------------------------------------------------------ */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath);

static NTSTATUS  VioRngCreateClose(PDEVICE_OBJECT DevObj, PIRP Irp);
static NTSTATUS  VioRngRead       (PDEVICE_OBJECT DevObj, PIRP Irp);
static BOOLEAN   VioRngIsr        (PKINTERRUPT Interrupt, PVOID Context);
static VOID      VioRngDpc        (PKDPC Dpc, PVOID Context, PVOID A1, PVOID A2);

static NTSTATUS  VioRngFindAndAttach(PDRIVER_OBJECT DriverObject,
                                     PUNICODE_STRING RegPath);

/* ------------------------------------------------------------------ *
 * DriverEntry — runs once at I/O Manager init time.
 * ------------------------------------------------------------------ */
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath)
{
    NTSTATUS st;

    DbgPrint("VIORNG: DriverEntry\n");

    DriverObject->MajorFunction[IRP_MJ_CREATE] = VioRngCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = VioRngCreateClose;
    DriverObject->MajorFunction[IRP_MJ_READ]   = VioRngRead;

    st = VioRngFindAndAttach(DriverObject, RegPath);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIORNG: no virtio-rng device found (st=0x%08x)\n", st);
        return st;
    }

    DbgPrint("VIORNG: ready, \\Device\\VirtioRng0 alive\n");
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
    UNICODE_STRING devName;
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
        DbgPrint("VIORNG: matched virtio-rng (devid 0x%04x) at bus0 slot 0x%02x\n",
                 cfg.DeviceID, slot);
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
        DbgPrint("VIORNG: bus IRQ %u/%u -> system vec=%u irql=%u affinity=0x%x\n",
                 intVector, intLevel, sysVector, sysIrql, (ULONG)affinity);
        intVector = sysVector;
        intLevel  = sysIrql;
    }
    /* ioBase no longer needed — VirtioPciInit reads BARs from PCI
       config and MmMapIoSpace's them itself based on what the cap
       walk reports. */
    UNREFERENCED_PARAMETER(ioBase);

    /* (3) Create the device object. Buffered I/O — NT will copy
       between user buffer and Irp->AssociatedIrp.SystemBuffer for us. */
    RtlInitUnicodeString(&devName, L"\\Device\\VirtioRng0");
    st = IoCreateDevice(DriverObject, sizeof(VIORNG_DEV), &devName,
                        FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIORNG: IoCreateDevice failed 0x%08x\n", st);
        ExFreePool(resources);
        return st;
    }
    devObj->Flags |= DO_BUFFERED_IO;

    dev = (PVIORNG_DEV)devObj->DeviceExtension;
    RtlZeroMemory(dev, sizeof(*dev));
    dev->DevObj = devObj;
    KeInitializeSpinLock(&dev->IsrLock);
    KeInitializeDpc(&dev->CompletionDpc, VioRngDpc, dev);

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
    DbgPrint("VIORNG: device features 0x%08x\n",
             (ULONG)dev->Pci.Vdev.Features);
    VirtioFeatureSet(&dev->Pci.Vdev);

    /* (5) Find + set up the single request queue (queue id 0). */
    {
        u16 vqsize;
        st = VirtioFindVqs(&dev->Pci.Vdev, 1, &vqsize);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIORNG: VirtioFindVqs failed 0x%08x\n", st);
            goto fail_dev;
        }
        DbgPrint("VIORNG: requestq has %u descriptors\n", vqsize);

        st = VirtioVqSetup(&dev->Pci.Vdev, 0, vqsize, NULL, &dev->Queue);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIORNG: VirtioVqSetup failed 0x%08x\n", st);
            goto fail_dev;
        }
        dev->Queue->Priv = dev;   /* DPC needs the device extension */
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
 * IRP_MJ_CREATE / IRP_MJ_CLOSE — trivial.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioRngCreateClose(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DevObj);
    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * IRP_MJ_READ — submit a write-only descriptor pointing at our scratch
 * buffer; device fills it; DPC completes the IRP.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioRngRead(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    PVIORNG_DEV         dev = (PVIORNG_DEV)DevObj->DeviceExtension;
    PIO_STACK_LOCATION  sp  = IoGetCurrentIrpStackLocation(Irp);
    ULONG               len = sp->Parameters.Read.Length;
    NTSTATUS            st;
    KIRQL               irql;
    VIRTIO_SG_SEG       seg;
    VIRTIO_SG_LIST      sg;

    if (len == 0 || len > dev->ScratchLen) {
        Irp->IoStatus.Status      = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&dev->IsrLock, &irql);
    if (dev->CurrentIrp) {
        KeReleaseSpinLock(&dev->IsrLock, irql);
        Irp->IoStatus.Status      = STATUS_DEVICE_BUSY;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_BUSY;
    }
    dev->CurrentIrp = Irp;

    seg.Paddr = dev->ScratchPaddr;
    seg.Len   = len;
    sg.NumSegs = 1;
    sg.Segs    = &seg;

    /* read_bufs = 0 (driver→device), write_bufs = 1 (device→driver). */
    st = VirtqEnqueue(dev->Queue, Irp, &sg, 0, 1);
    if (!NT_SUCCESS(st)) {
        dev->CurrentIrp = NULL;
        KeReleaseSpinLock(&dev->IsrLock, irql);
        Irp->IoStatus.Status      = st;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return st;
    }

    IoMarkIrpPending(Irp);
    VirtqHostNotify(dev->Queue);
    KeReleaseSpinLock(&dev->IsrLock, irql);
    return STATUS_PENDING;
}

/* ------------------------------------------------------------------ *
 * Interrupt service routine — runs at DIRQL. Must do the bare minimum:
 * ack the device's ISR register (VirtioPciIsr does this) and queue a
 * DPC for the heavy work (used-ring drain + IRP completion).
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
 * DPC — runs at DISPATCH_LEVEL. Drains the used ring, copies entropy
 * into the IRP's SystemBuffer, completes the IRP.
 * ------------------------------------------------------------------ */
static VOID
VioRngDpc(PKDPC Dpc, PVOID Context, PVOID A1, PVOID A2)
{
    PVIORNG_DEV dev = (PVIORNG_DEV)Context;
    PVOID       cookie;
    u32         len;
    NTSTATUS    st;
    PIRP        irp;
    KIRQL       irql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(A1);
    UNREFERENCED_PARAMETER(A2);

    KeAcquireSpinLock(&dev->IsrLock, &irql);
    for (;;) {
        st = VirtqDequeue(dev->Queue, &cookie, &len);
        if (!NT_SUCCESS(st))
            break;
        irp = (PIRP)cookie;
        if (irp != dev->CurrentIrp) {
            DbgPrint("VIORNG DPC: cookie mismatch %p vs %p\n",
                     irp, dev->CurrentIrp);
            continue;
        }
        /* Copy entropy from scratch into the IRP's SystemBuffer
           (BUFFERED_IO; the I/O Manager copies SystemBuffer back to
           the user-mode buffer on completion). */
        if (irp->AssociatedIrp.SystemBuffer && len > 0) {
            ULONG copyLen = len;
            if (copyLen > IoGetCurrentIrpStackLocation(irp)
                            ->Parameters.Read.Length) {
                copyLen = IoGetCurrentIrpStackLocation(irp)
                            ->Parameters.Read.Length;
            }
            RtlCopyMemory(irp->AssociatedIrp.SystemBuffer,
                          dev->ScratchBuf, copyLen);
            irp->IoStatus.Status      = STATUS_SUCCESS;
            irp->IoStatus.Information = copyLen;
        } else {
            irp->IoStatus.Status      = STATUS_DEVICE_DATA_ERROR;
            irp->IoStatus.Information = 0;
        }
        dev->CurrentIrp = NULL;
        KeReleaseSpinLock(&dev->IsrLock, irql);
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        KeAcquireSpinLock(&dev->IsrLock, &irql);
    }
    KeReleaseSpinLock(&dev->IsrLock, irql);
}
