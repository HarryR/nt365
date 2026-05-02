/*++

    vioser.c — virtio-console (virtio-serial single-port). Surfaces
    \Device\VirtioCon0 with IRP_MJ_READ + IRP_MJ_WRITE. Lua reads
    bytes the host has typed/piped into the chardev and writes bytes
    that surface on the host's chardev sink.

    Single-port-only, no MULTIPORT feature negotiation, no control
    queue. virtio-console exposes:
        queue 0  receiveq (device → driver)
        queue 1  transmitq (driver → device)

    PCI device 1AF4:1003 (Red Hat, legacy virtio-console).
    QEMU command line:
        -device virtio-serial-pci,disable-modern=on,disable-legacy=off
        -chardev pty,id=vc0
        -device virtconsole,chardev=vc0

    Single in-flight IRP per queue direction. Read and write share the
    spinlock but use separate cookies so a pending Read can sit while
    Write progresses.

--*/

#include <ntddk.h>
#include "virtio.h"
#include "vio_pci.h"
#include "vio_ids.h"

/* virtio-console feature bits we *don't* want — keep us in single-port
   mode where queue 0 = rx, queue 1 = tx. */
#define VIRTIO_CONSOLE_F_MULTIPORT  1   /* clear in our negotiated set */

/* ------------------------------------------------------------------ *
 * Per-device extension.
 * ------------------------------------------------------------------ */
typedef struct _VIOSER_DEV {
    VIRTIO_PCI_DEV    Pci;
    PDEVICE_OBJECT    DevObj;
    PKINTERRUPT       Interrupt;
    KSPIN_LOCK        Lock;
    KDPC              CompletionDpc;

    PVIRTQUEUE        RxQ;
    PVIRTQUEUE        TxQ;

    PIRP              ReadIrp;       /* in-flight read, NULL if idle */
    PIRP              WriteIrp;      /* in-flight write */

    PVOID             RxBuf;         /* NonPagedPool, kept armed on the rx ring */
    PHYSICAL_ADDRESS  RxBufPaddr;
    ULONG             RxBufLen;
    BOOLEAN           RxArmed;       /* TRUE iff a buf is sitting on rx ring */

    PVOID             TxBuf;         /* scratch for writes */
    PHYSICAL_ADDRESS  TxBufPaddr;
    ULONG             TxBufLen;
} VIOSER_DEV, *PVIOSER_DEV;

#define VIOSER_BUF_SIZE  4096

static PVIOSER_DEV g_Dev = NULL;

/* ------------------------------------------------------------------ *
 * Forward decls.
 * ------------------------------------------------------------------ */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath);

static NTSTATUS  VioserCreateClose(PDEVICE_OBJECT DevObj, PIRP Irp);
static NTSTATUS  VioserRead       (PDEVICE_OBJECT DevObj, PIRP Irp);
static NTSTATUS  VioserWrite      (PDEVICE_OBJECT DevObj, PIRP Irp);
static BOOLEAN   VioserIsr        (PKINTERRUPT Interrupt, PVOID Context);
static VOID      VioserDpc        (PKDPC Dpc, PVOID Context, PVOID A1, PVOID A2);
static VOID      VioserArmRx      (PVIOSER_DEV dev);
static NTSTATUS  VioserFindAndAttach(PDRIVER_OBJECT DriverObject,
                                     PUNICODE_STRING RegPath);

/* ------------------------------------------------------------------ *
 * DriverEntry.
 * ------------------------------------------------------------------ */
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath)
{
    NTSTATUS st;

    DbgPrint("VIOSER: DriverEntry\n");

    DriverObject->MajorFunction[IRP_MJ_CREATE] = VioserCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = VioserCreateClose;
    DriverObject->MajorFunction[IRP_MJ_READ]   = VioserRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE]  = VioserWrite;

    st = VioserFindAndAttach(DriverObject, RegPath);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOSER: no virtio-console device found (st=0x%08x)\n", st);
        return st;
    }

    DbgPrint("VIOSER: ready, \\Device\\VirtioCon0 alive\n");
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * PCI walk + init. Same shape as viorng. PCI device 1AF4:1003.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioserFindAndAttach(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath)
{
    UNICODE_STRING devName;
    PDEVICE_OBJECT devObj;
    PVIOSER_DEV    dev;
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
    u16            qsizes[2];

    /* (1) Find. Match modern (0x1043) OR transitional (0x1003) ID.
       QEMU's default virtio-serial-pci is transitional. */
    for (slot = 0; slot < 32 * 8; slot++) {
        got = HalGetBusDataByOffset(PCIConfiguration, 0, slot,
                                    &cfg, 0, sizeof(cfg));
        if (got < 4)                                      continue;
        if (cfg.VendorID == 0xFFFF)                       continue;
        if (cfg.VendorID != VIRTIO_PCI_VENDOR_ID)         continue;
        if (cfg.DeviceID != VIRTIO_PCI_DEV_CONSOLE &&
            cfg.DeviceID != VIRTIO_PCI_TRANS_CONSOLE)     continue;
        DbgPrint("VIOSER: matched virtio-console (devid 0x%04x) at bus0 slot 0x%02x\n",
                 cfg.DeviceID, slot);
        break;
    }
    if (slot >= 32 * 8)
        return STATUS_NO_SUCH_DEVICE;

    /* (2) Resources. */
    st = HalAssignSlotResources(RegPath, NULL, DriverObject, NULL,
                                PCIBus, 0, slot, &resources);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOSER: HalAssignSlotResources failed 0x%08x\n", st);
        return st;
    }
    for (i = 0; i < resources->List[0].PartialResourceList.Count; i++) {
        pd = &resources->List[0].PartialResourceList.PartialDescriptors[i];
        if (pd->Type == CmResourceTypeInterrupt && intVector == 0) {
            intVector = pd->u.Interrupt.Vector;
            intLevel  = (KIRQL)pd->u.Interrupt.Level;
        }
    }
    if (!intVector) {
        ExFreePool(resources);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    DbgPrint("VIOSER: bus IRQ vec=%u lvl=%u\n", intVector, intLevel);

    /* Translate bus-relative IRQ -> system vector + DIRQL. See viorng
       for the rationale; same translation applies here. */
    {
        ULONG sysVector;
        KIRQL sysIrql = 0;
        sysVector = HalGetInterruptVector(PCIBus, 0, intLevel, intVector,
                                          &sysIrql, &affinity);
        DbgPrint("VIOSER: -> system vec=%u irql=%u affinity=0x%x\n",
                 sysVector, sysIrql, (ULONG)affinity);
        intVector = sysVector;
        intLevel  = sysIrql;
    }
    /* ioBase no longer needed - VirtioPciInit handles BAR readout
       via PCI cap walk + MmMapIoSpace. */
    UNREFERENCED_PARAMETER(ioBase);

    /* (3) Device object. Buffered I/O. */
    RtlInitUnicodeString(&devName, L"\\Device\\VirtioCon0");
    st = IoCreateDevice(DriverObject, sizeof(VIOSER_DEV), &devName,
                        FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
    if (!NT_SUCCESS(st)) {
        ExFreePool(resources);
        return st;
    }
    devObj->Flags |= DO_BUFFERED_IO;

    dev = (PVIOSER_DEV)devObj->DeviceExtension;
    RtlZeroMemory(dev, sizeof(*dev));
    dev->DevObj = devObj;
    KeInitializeSpinLock(&dev->Lock);
    KeInitializeDpc(&dev->CompletionDpc, VioserDpc, dev);

    /* (4) Init virtio-pci (modern transport), run the handshake. */
    st = VirtioPciInit(&dev->Pci, 0, slot,
                       intVector, intLevel, affinity,
                       VIRTIO_ID_CONSOLE);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOSER: VirtioPciInit failed 0x%08x\n", st);
        goto fail_dev;
    }
    VirtioDevReset(&dev->Pci.Vdev);
    VirtioDevStatusUpdate(&dev->Pci.Vdev, VIRTIO_STATUS_ACK);
    VirtioDevStatusUpdate(&dev->Pci.Vdev,
                          VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    dev->Pci.Vdev.Features = VirtioFeatureGet(&dev->Pci.Vdev);
    /* Clear MULTIPORT — we want single-port semantics (rx=q0, tx=q1). */
    dev->Pci.Vdev.Features &= ~((u64)1 << VIRTIO_CONSOLE_F_MULTIPORT);
    DbgPrint("VIOSER: features (after mask) 0x%08x\n",
             (ULONG)dev->Pci.Vdev.Features);
    VirtioFeatureSet(&dev->Pci.Vdev);

    /* (5) Two queues. */
    st = VirtioFindVqs(&dev->Pci.Vdev, 2, qsizes);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOSER: VirtioFindVqs failed 0x%08x\n", st);
        goto fail_dev;
    }
    DbgPrint("VIOSER: rxq=%u tx q=%u descriptors\n", qsizes[0], qsizes[1]);

    st = VirtioVqSetup(&dev->Pci.Vdev, 0, qsizes[0], NULL, &dev->RxQ);
    if (!NT_SUCCESS(st)) goto fail_dev;
    dev->RxQ->Priv = dev;
    st = VirtioVqSetup(&dev->Pci.Vdev, 1, qsizes[1], NULL, &dev->TxQ);
    if (!NT_SUCCESS(st)) goto fail_dev;
    dev->TxQ->Priv = dev;

    /* (6) Pre-allocate rx + tx scratch buffers. */
    dev->RxBuf = ExAllocatePoolWithTag(NonPagedPool, VIOSER_BUF_SIZE, '0reS');
    dev->TxBuf = ExAllocatePoolWithTag(NonPagedPool, VIOSER_BUF_SIZE, '0reS');
    if (!dev->RxBuf || !dev->TxBuf) {
        st = STATUS_INSUFFICIENT_RESOURCES;
        goto fail_dev;
    }
    dev->RxBufPaddr = MmGetPhysicalAddress(dev->RxBuf);
    dev->TxBufPaddr = MmGetPhysicalAddress(dev->TxBuf);
    dev->RxBufLen   = VIOSER_BUF_SIZE;
    dev->TxBufLen   = VIOSER_BUF_SIZE;

    /* (7) IRQ. */
    st = IoConnectInterrupt(&dev->Interrupt, VioserIsr, dev,
                            NULL, intVector, intLevel, intLevel,
                            LevelSensitive, TRUE, affinity, FALSE);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOSER: IoConnectInterrupt failed 0x%08x\n", st);
        goto fail_dev;
    }

    /* (8) Driver-up — device may now produce used-ring entries. */
    VirtioDevDriverUp(&dev->Pci.Vdev);

    /* (9) Arm rx so any data the host pushes immediately lands in our
       scratch buffer. Host blocks until the ring has space available. */
    {
        KIRQL irql;
        KeAcquireSpinLock(&dev->Lock, &irql);
        VioserArmRx(dev);
        KeReleaseSpinLock(&dev->Lock, irql);
    }

    g_Dev = dev;
    ExFreePool(resources);
    return STATUS_SUCCESS;

fail_dev:
    if (dev->RxBuf)  ExFreePool(dev->RxBuf);
    if (dev->TxBuf)  ExFreePool(dev->TxBuf);
    if (dev->RxQ)    VirtioVqRelease(&dev->Pci.Vdev, dev->RxQ);
    if (dev->TxQ)    VirtioVqRelease(&dev->Pci.Vdev, dev->TxQ);
    IoDeleteDevice(devObj);
    ExFreePool(resources);
    return st;
}

/* Submit our pre-allocated rx buffer to the receive queue. Caller
   holds dev->Lock. */
static VOID
VioserArmRx(PVIOSER_DEV dev)
{
    VIRTIO_SG_SEG  seg;
    VIRTIO_SG_LIST sg;
    NTSTATUS       st;

    if (dev->RxArmed)
        return;

    seg.Paddr = dev->RxBufPaddr;
    seg.Len   = dev->RxBufLen;
    sg.NumSegs = 1;
    sg.Segs    = &seg;

    /* read_bufs=0, write_bufs=1 — host writes data into our buffer. */
    st = VirtqEnqueue(dev->RxQ, dev->RxBuf, &sg, 0, 1);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOSER: ArmRx enqueue failed 0x%08x\n", st);
        return;
    }
    dev->RxArmed = TRUE;
    VirtqHostNotify(dev->RxQ);
}

/* ------------------------------------------------------------------ *
 * Create / Close — trivial.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioserCreateClose(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DevObj);
    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * IRP_MJ_READ — wait for the host to have written data into our pre-
 * armed RxBuf, then copy to SystemBuffer + complete.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioserRead(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    PVIOSER_DEV         dev = (PVIOSER_DEV)DevObj->DeviceExtension;
    PIO_STACK_LOCATION  sp  = IoGetCurrentIrpStackLocation(Irp);
    ULONG               len = sp->Parameters.Read.Length;
    KIRQL               irql;

    if (len == 0) {
        Irp->IoStatus.Status      = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    KeAcquireSpinLock(&dev->Lock, &irql);
    if (dev->ReadIrp) {
        KeReleaseSpinLock(&dev->Lock, irql);
        Irp->IoStatus.Status      = STATUS_DEVICE_BUSY;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_BUSY;
    }
    dev->ReadIrp = Irp;
    IoMarkIrpPending(Irp);
    /* If rx isn't armed (could happen post-completion if we forget to
       re-arm), arm it now. The DPC keeps it armed in steady state. */
    VioserArmRx(dev);
    KeReleaseSpinLock(&dev->Lock, irql);
    return STATUS_PENDING;
}

/* ------------------------------------------------------------------ *
 * IRP_MJ_WRITE — copy SystemBuffer to TxBuf, submit to tx queue.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioserWrite(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    PVIOSER_DEV         dev = (PVIOSER_DEV)DevObj->DeviceExtension;
    PIO_STACK_LOCATION  sp  = IoGetCurrentIrpStackLocation(Irp);
    ULONG               len = sp->Parameters.Write.Length;
    KIRQL               irql;
    NTSTATUS            st;
    VIRTIO_SG_SEG       seg;
    VIRTIO_SG_LIST      sg;

    if (len == 0 || len > dev->TxBufLen) {
        Irp->IoStatus.Status      = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&dev->Lock, &irql);
    if (dev->WriteIrp) {
        KeReleaseSpinLock(&dev->Lock, irql);
        Irp->IoStatus.Status      = STATUS_DEVICE_BUSY;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_BUSY;
    }

    /* Copy the user data into our DMA-safe scratch. */
    RtlCopyMemory(dev->TxBuf, Irp->AssociatedIrp.SystemBuffer, len);

    seg.Paddr = dev->TxBufPaddr;
    seg.Len   = len;
    sg.NumSegs = 1;
    sg.Segs    = &seg;

    /* read_bufs=1, write_bufs=0 — device reads our data and prints it. */
    st = VirtqEnqueue(dev->TxQ, Irp, &sg, 1, 0);
    if (!NT_SUCCESS(st)) {
        KeReleaseSpinLock(&dev->Lock, irql);
        Irp->IoStatus.Status      = st;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return st;
    }

    dev->WriteIrp = Irp;
    IoMarkIrpPending(Irp);
    VirtqHostNotify(dev->TxQ);
    KeReleaseSpinLock(&dev->Lock, irql);
    return STATUS_PENDING;
}

/* ------------------------------------------------------------------ *
 * ISR + DPC. ISR queues DPC on any matched interrupt.
 * ------------------------------------------------------------------ */
static BOOLEAN
VioserIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    PVIOSER_DEV dev = (PVIOSER_DEV)Context;
    int handled;

    UNREFERENCED_PARAMETER(Interrupt);
    handled = VirtioPciIsr(&dev->Pci);
    if (handled) {
        KeInsertQueueDpc(&dev->CompletionDpc, NULL, NULL);
        return TRUE;
    }
    return FALSE;
}

static VOID
VioserDpc(PKDPC Dpc, PVOID Context, PVOID A1, PVOID A2)
{
    PVIOSER_DEV dev = (PVIOSER_DEV)Context;
    PVOID       cookie;
    u32         used_len;
    NTSTATUS    st;
    PIRP        irp;
    KIRQL       irql;
    ULONG       userLen;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(A1);
    UNREFERENCED_PARAMETER(A2);

    KeAcquireSpinLock(&dev->Lock, &irql);

    /* Drain rx — host pushed data into RxBuf. */
    for (;;) {
        st = VirtqDequeue(dev->RxQ, &cookie, &used_len);
        if (!NT_SUCCESS(st))
            break;
        dev->RxArmed = FALSE;
        irp = dev->ReadIrp;
        if (irp && irp->AssociatedIrp.SystemBuffer && used_len > 0) {
            userLen = IoGetCurrentIrpStackLocation(irp)
                        ->Parameters.Read.Length;
            if (used_len < userLen) userLen = used_len;
            RtlCopyMemory(irp->AssociatedIrp.SystemBuffer,
                          dev->RxBuf, userLen);
            irp->IoStatus.Status      = STATUS_SUCCESS;
            irp->IoStatus.Information = userLen;
            dev->ReadIrp = NULL;
            KeReleaseSpinLock(&dev->Lock, irql);
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            KeAcquireSpinLock(&dev->Lock, &irql);
        } else {
            /* No reader — drop the data on the floor. Re-arm. */
            DbgPrint("VIOSER DPC: rx %u bytes with no reader, dropping\n",
                     used_len);
        }
        /* Re-arm rx for the next batch. */
        VioserArmRx(dev);
    }

    /* Drain tx — device finished consuming a write buffer. */
    for (;;) {
        st = VirtqDequeue(dev->TxQ, &cookie, &used_len);
        if (!NT_SUCCESS(st))
            break;
        irp = (PIRP)cookie;
        if (irp == dev->WriteIrp) {
            irp->IoStatus.Status      = STATUS_SUCCESS;
            irp->IoStatus.Information =
                IoGetCurrentIrpStackLocation(irp)->Parameters.Write.Length;
            dev->WriteIrp = NULL;
            KeReleaseSpinLock(&dev->Lock, irql);
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            KeAcquireSpinLock(&dev->Lock, &irql);
        } else {
            DbgPrint("VIOSER DPC: tx cookie mismatch\n");
        }
    }

    KeReleaseSpinLock(&dev->Lock, irql);
}
