/*++

    vioinput.c — virtio-input device driver (skeleton).

    Brings up a virtio-input PCI device (1AF4:1052, modern only):
    walks caps, maps BARs, negotiates features, sets up the two
    virtqueues defined by the spec sec 5.8:

      queue 0  EventQ   - device -> driver  (input events flow in)
      queue 1  StatusQ  - driver -> device  (LED reports flow out)

    Each buffer is one struct virtio_input_event { type, code, value }
    (8 bytes). The driver pre-posts a pool of EventQ buffers at init
    time; the device fills them as events occur and ISR-fires; the
    DPC drains the used ring, hands each event to a per-class
    translator, and re-posts the buffer.

    This skeleton wires the bring-up + ISR/DPC + event dump only.
    Per-class translators (kbdclass / mouclass) layer in afterwards
    once we've confirmed the wiring on real EV_KEY / EV_REL traffic.

    Reference: kvm-guest-drivers-windows/vioinput/sys/Device.c
    (DPC + queue layout) — adapted to NT 3.5's older driver model
    (no WDF, no HIDCLASS, no PnP — DriverEntry walks PCI itself).

--*/

#include <ntddk.h>
#include "virtio.h"
#include "virtio_pci.h"
#include "virtio_ids.h"

/* ------------------------------------------------------------------ *
 * virtio-input wire format (spec sec 5.8).
 * ------------------------------------------------------------------ */

/* One event slot the device fills (or driver writes for status).
   Linux's input subsystem speaks the same struct, so the type/code
   namespace is the Linux one (EV_KEY / KEY_A / EV_REL / REL_X / ...). */
typedef struct _VIRTIO_INPUT_EVENT {
    USHORT  Type;
    USHORT  Code;
    ULONG   Value;
} VIRTIO_INPUT_EVENT, *PVIRTIO_INPUT_EVENT;

/* Event types we care about. */
#define EV_SYN          0x00    /* end-of-packet marker */
#define EV_KEY          0x01    /* key/button press/release; value=1/0 */
#define EV_REL          0x02    /* relative axis; value=delta */
#define EV_ABS          0x03    /* absolute axis; value=position */
#define EV_MSC          0x04    /* misc — scancode, etc. */
#define EV_LED          0x11    /* LED state (in StatusQ) */

/* Config-space selector layout (spec sec 5.8.4). The driver writes
   {Select, Subsel} into the device-config region, then reads back
   Size + the corresponding payload. Used to enumerate which keys /
   axes / LEDs the device supports — drives keyboard-vs-mouse
   classification. */
#define VIRTIO_INPUT_CFG_UNSET      0x00
#define VIRTIO_INPUT_CFG_ID_NAME    0x01
#define VIRTIO_INPUT_CFG_ID_SERIAL  0x02
#define VIRTIO_INPUT_CFG_ID_DEVIDS  0x03
#define VIRTIO_INPUT_CFG_PROP_BITS  0x10
#define VIRTIO_INPUT_CFG_EV_BITS    0x11
#define VIRTIO_INPUT_CFG_ABS_INFO   0x12

typedef struct _VIRTIO_INPUT_CONFIG {
    UCHAR   Select;
    UCHAR   Subsel;
    UCHAR   Size;
    UCHAR   Reserved[5];
    union {
        UCHAR   Bitmap[128];
        CHAR    String[128];
        struct {
            ULONG  Min;
            ULONG  Max;
            ULONG  Fuzz;
            ULONG  Flat;
            ULONG  Res;
        } AbsInfo;
        struct {
            USHORT BusType;
            USHORT Vendor;
            USHORT Product;
            USHORT Version;
        } Ids;
    } u;
} VIRTIO_INPUT_CONFIG, *PVIRTIO_INPUT_CONFIG;

/* Queue indices. */
#define VIRTIO_INPUT_Q_EVENT    0
#define VIRTIO_INPUT_Q_STATUS   1
#define VIRTIO_INPUT_NUM_VQS    2

/* How many events the device may have outstanding before the driver
   recycles a buffer. 64 is what the kvm reference uses; ample for
   any plausible mouse/keyboard burst. */
#define VIRTIO_INPUT_EVENT_BUFS 64

/* ------------------------------------------------------------------ *
 * Per-device extension.
 * ------------------------------------------------------------------ */
typedef struct _VIOINPUT_DEV {
    VIRTIO_PCI_DEV    Pci;
    PDEVICE_OBJECT    DevObj;
    PKINTERRUPT       Interrupt;
    KSPIN_LOCK        IsrLock;
    KDPC              EventDpc;
    PVIRTQUEUE        EventQ;
    PVIRTQUEUE        StatusQ;
    ULONG             Instance;     /* matches \Device\VirtioInput<N> name */

    /* Pool of event buffers. One contiguous NonPagedPool block —
       Buffers[i] is the i'th VIRTIO_INPUT_EVENT, BuffersPaddr its
       physical base. Cookie passed to the queue is the index, so
       Dequeue gives us back which slot fired. */
    PVIRTIO_INPUT_EVENT  Buffers;
    PHYSICAL_ADDRESS     BuffersPaddr;
    ULONG                NumBuffers;
} VIOINPUT_DEV, *PVIOINPUT_DEV;

/* ------------------------------------------------------------------ *
 * Forward declarations.
 * ------------------------------------------------------------------ */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath);

static NTSTATUS  VioInputCreateClose(PDEVICE_OBJECT DevObj, PIRP Irp);
static BOOLEAN   VioInputIsr        (PKINTERRUPT Interrupt, PVOID Context);
static VOID      VioInputDpc        (PKDPC Dpc, PVOID Context, PVOID A1, PVOID A2);

static NTSTATUS  VioInputAttachOne     (PDRIVER_OBJECT DriverObject,
                                        PUNICODE_STRING RegPath,
                                        ULONG slot, ULONG instance);
static NTSTATUS  VioInputPrepostEventBufs(PVIOINPUT_DEV dev);

/* ------------------------------------------------------------------ *
 * DriverEntry — runs once at I/O Manager init.
 * ------------------------------------------------------------------ */
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath)
{
    ULONG slot;
    ULONG instance = 0;
    PCI_COMMON_CONFIG cfg;
    ULONG got;

    DbgPrint("VIOINPUT: DriverEntry\n");

    DriverObject->MajorFunction[IRP_MJ_CREATE] = VioInputCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = VioInputCreateClose;

    /* Walk every PCI slot/function on bus 0 and bring up one
       \Device\VirtioInput<N> per virtio-input device found. A QEMU
       guest typically ships virtio-keyboard-pci + virtio-mouse-pci
       (sometimes virtio-tablet-pci) - all share VendorID/DeviceID
       1AF4:1052 and only differ in the EV_BITS bitmap exposed in
       the device-config region (queried in a later pass). */
    for (slot = 0; slot < 32 * 8; slot++) {
        got = HalGetBusDataByOffset(PCIConfiguration, 0, slot,
                                    &cfg, 0, sizeof(cfg));
        if (got < 4)                                continue;
        if (cfg.VendorID == 0xFFFF)                 continue;
        if (cfg.VendorID != VIRTIO_PCI_VENDOR_ID)   continue;
        if (cfg.DeviceID != VIRTIO_PCI_DEV_INPUT)   continue;

        if (NT_SUCCESS(VioInputAttachOne(DriverObject, RegPath,
                                         slot, instance))) {
            instance++;
        }
    }

    if (instance == 0) {
        DbgPrint("VIOINPUT: no virtio-input devices found\n");
        return STATUS_NO_SUCH_DEVICE;
    }

    DbgPrint("VIOINPUT: ready, %u device(s) listening on EventQ\n",
             instance);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * Bring up one virtio-input device at the given PCI slot. Caller
 * (DriverEntry) walked the bus, identified the slot as a 1AF4:1052
 * device, and assigned a 0-based instance number for naming
 * (\Device\VirtioInput<instance>).
 * ------------------------------------------------------------------ */
static NTSTATUS
VioInputAttachOne(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath,
                  ULONG slot, ULONG instance)
{
    UNICODE_STRING devName;
    WCHAR          devNameBuf[32];
    PDEVICE_OBJECT devObj;
    PVIOINPUT_DEV  dev;
    NTSTATUS       st;
    PCM_RESOURCE_LIST resources = NULL;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pd;
    ULONG          i;
    ULONG          intVector = 0;
    KIRQL          intLevel = 0;
    KAFFINITY      affinity = 0;
    u16            vqsize;

    DbgPrint("VIOINPUT: attaching instance %u at bus0 slot 0x%02x\n",
             instance, slot);

    /* (1) HAL resource arbitration — we only use the IRQ. */
    st = HalAssignSlotResources(RegPath, NULL, DriverObject, NULL,
                                PCIBus, 0, slot, &resources);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: HalAssignSlotResources failed 0x%08x\n",
                 instance, st);
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
        DbgPrint("VIOINPUT[%u]: missing IRQ resource\n", instance);
        ExFreePool(resources);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    {
        ULONG sysVector;
        KIRQL sysIrql = 0;
        sysVector = HalGetInterruptVector(PCIBus, 0, intLevel, intVector,
                                          &sysIrql, &affinity);
        DbgPrint("VIOINPUT[%u]: bus IRQ %u/%u -> system vec=%u irql=%u affinity=0x%x\n",
                 instance, intVector, intLevel,
                 sysVector, sysIrql, (ULONG)affinity);
        intVector = sysVector;
        intLevel  = sysIrql;
    }

    /* (2) Create the device object. Name carries the instance index
       so each virtio-input function ends up at its own \Device entry.
       RtlIntegerToUnicodeString overwrites Destination (does not
       append), so build the number into a scratch buffer first then
       append both halves into devName. */
    {
        UNICODE_STRING base, num;
        WCHAR          numBuf[16];

        RtlInitUnicodeString(&base, L"\\Device\\VirtioInput");

        num.Buffer        = numBuf;
        num.MaximumLength = sizeof(numBuf);
        num.Length        = 0;
        RtlIntegerToUnicodeString(instance, 10, &num);

        devName.Buffer        = devNameBuf;
        devName.MaximumLength = sizeof(devNameBuf);
        devName.Length        = 0;
        RtlAppendUnicodeStringToString(&devName, &base);
        RtlAppendUnicodeStringToString(&devName, &num);
    }
    st = IoCreateDevice(DriverObject, sizeof(VIOINPUT_DEV), &devName,
                        FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: IoCreateDevice failed 0x%08x\n",
                 instance, st);
        ExFreePool(resources);
        return st;
    }
    devObj->Flags |= DO_BUFFERED_IO;

    dev = (PVIOINPUT_DEV)devObj->DeviceExtension;
    RtlZeroMemory(dev, sizeof(*dev));
    dev->DevObj   = devObj;
    dev->Instance = instance;
    KeInitializeSpinLock(&dev->IsrLock);
    KeInitializeDpc(&dev->EventDpc, VioInputDpc, dev);

    /* (3) virtio_pci modern-transport bring-up. */
    st = VirtioPciInit(&dev->Pci, 0, slot,
                       intVector, intLevel, affinity,
                       VIRTIO_ID_INPUT);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: VirtioPciInit failed 0x%08x\n",
                 instance, st);
        goto fail_dev;
    }

    VirtioDevReset(&dev->Pci.Vdev);
    VirtioDevStatusUpdate(&dev->Pci.Vdev, VIRTIO_STATUS_ACK);
    VirtioDevStatusUpdate(&dev->Pci.Vdev,
                          VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    dev->Pci.Vdev.Features = VirtioFeatureGet(&dev->Pci.Vdev);
    DbgPrint("VIOINPUT[%u]: device features 0x%08x\n",
             instance, (ULONG)dev->Pci.Vdev.Features);
    /* No driver-side feature bits to opt into beyond VIRTIO_F_VERSION_1
       (handled by the modern transport itself). */
    VirtioFeatureSet(&dev->Pci.Vdev);

    /* (4) Both vqs. */
    st = VirtioFindVqs(&dev->Pci.Vdev, VIRTIO_INPUT_NUM_VQS, &vqsize);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: VirtioFindVqs failed 0x%08x\n", instance, st);
        goto fail_dev;
    }
    DbgPrint("VIOINPUT[%u]: vq descriptors=%u\n", instance, vqsize);

    st = VirtioVqSetup(&dev->Pci.Vdev, VIRTIO_INPUT_Q_EVENT,
                       vqsize, NULL, &dev->EventQ);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: EventQ setup failed 0x%08x\n", instance, st);
        goto fail_dev;
    }
    dev->EventQ->Priv = dev;

    st = VirtioVqSetup(&dev->Pci.Vdev, VIRTIO_INPUT_Q_STATUS,
                       vqsize, NULL, &dev->StatusQ);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: StatusQ setup failed 0x%08x\n", instance, st);
        goto fail_dev;
    }
    dev->StatusQ->Priv = dev;

    /* (5) Allocate + pre-post the event buffer pool. The device must
       have descriptors in the available ring before it can deliver
       events; we top it back up from the DPC after each drain. */
    st = VioInputPrepostEventBufs(dev);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: pre-post EventQ buffers failed 0x%08x\n",
                 instance, st);
        goto fail_dev;
    }

    /* (6) Wire the interrupt. After this, the device may fire. */
    st = IoConnectInterrupt(&dev->Interrupt, VioInputIsr, dev,
                            NULL, intVector, intLevel, intLevel,
                            LevelSensitive, TRUE, affinity, FALSE);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: IoConnectInterrupt failed 0x%08x\n",
                 instance, st);
        goto fail_dev;
    }

    /* (7) Mark driver up — the device now starts delivering events
       into the buffers we pre-posted. */
    VirtioDevDriverUp(&dev->Pci.Vdev);
    VirtqHostNotify(dev->EventQ);

    ExFreePool(resources);
    return STATUS_SUCCESS;

fail_dev:
    if (dev->Buffers)  ExFreePool(dev->Buffers);
    if (dev->EventQ)   VirtioVqRelease(&dev->Pci.Vdev, dev->EventQ);
    if (dev->StatusQ)  VirtioVqRelease(&dev->Pci.Vdev, dev->StatusQ);
    IoDeleteDevice(devObj);
    ExFreePool(resources);
    return st;
}

/* ------------------------------------------------------------------ *
 * Allocate the event buffer pool and feed every slot to the EventQ.
 * Cookie = buffer index (encoded as a pointer); Dequeue returns it
 * so we can find which slot the device wrote and re-post it.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioInputPrepostEventBufs(PVIOINPUT_DEV dev)
{
    ULONG poolBytes = VIRTIO_INPUT_EVENT_BUFS * sizeof(VIRTIO_INPUT_EVENT);
    ULONG i;
    NTSTATUS st;
    VIRTIO_SG_SEG  seg;
    VIRTIO_SG_LIST sg;

    dev->Buffers = (PVIRTIO_INPUT_EVENT)ExAllocatePoolWithTag(
        NonPagedPool, poolBytes, 'pInV');
    if (!dev->Buffers) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(dev->Buffers, poolBytes);

    dev->BuffersPaddr = MmGetPhysicalAddress(dev->Buffers);
    dev->NumBuffers   = VIRTIO_INPUT_EVENT_BUFS;

    sg.NumSegs = 1;
    sg.Segs    = &seg;

    for (i = 0; i < dev->NumBuffers; i++) {
        seg.Paddr.QuadPart =
            dev->BuffersPaddr.QuadPart + (i * sizeof(VIRTIO_INPUT_EVENT));
        seg.Len = sizeof(VIRTIO_INPUT_EVENT);

        /* read_bufs = 0 (driver -> device), write_bufs = 1
           (device fills our buffer). Cookie = (PVOID)(ULONG)i. */
        st = VirtqEnqueue(dev->EventQ, (PVOID)(ULONG)i,
                          &sg, 0, 1);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIOINPUT[%u]: EventQ enqueue %u/%u failed 0x%08x\n",
                     dev->Instance, i, dev->NumBuffers, st);
            return st;
        }
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * IRP_MJ_CREATE / IRP_MJ_CLOSE — placeholder; the real consumers
 * (kbdclass / mouclass) bind via IOCTL_INTERNAL_*_CONNECT later.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioInputCreateClose(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DevObj);
    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * Interrupt service routine.
 * ------------------------------------------------------------------ */
static BOOLEAN
VioInputIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    PVIOINPUT_DEV dev = (PVIOINPUT_DEV)Context;
    int handled;

    UNREFERENCED_PARAMETER(Interrupt);

    handled = VirtioPciIsr(&dev->Pci);
    if (handled) {
        KeInsertQueueDpc(&dev->EventDpc, NULL, NULL);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ *
 * DPC — drain EventQ, log each event, re-post the buffer.
 *
 * Once kbdclass / mouclass binding lands, the DbgPrint here is
 * replaced with a per-class dispatcher (Kbd / Mouse / Tablet) that
 * accumulates events between EV_SYN markers and flushes a packet
 * to the bound class driver on EV_SYN.
 * ------------------------------------------------------------------ */
static VOID
VioInputDpc(PKDPC Dpc, PVOID Context, PVOID A1, PVOID A2)
{
    PVIOINPUT_DEV dev = (PVIOINPUT_DEV)Context;
    PVOID         cookie;
    u32           used_len;
    NTSTATUS      st;
    KIRQL         irql;
    ULONG         idx;
    PVIRTIO_INPUT_EVENT ev;
    VIRTIO_SG_SEG  seg;
    VIRTIO_SG_LIST sg;
    BOOLEAN        kicked = FALSE;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(A1);
    UNREFERENCED_PARAMETER(A2);

    sg.NumSegs = 1;
    sg.Segs    = &seg;

    KeAcquireSpinLock(&dev->IsrLock, &irql);
    for (;;) {
        st = VirtqDequeue(dev->EventQ, &cookie, &used_len);
        if (!NT_SUCCESS(st))
            break;

        idx = (ULONG)cookie;
        if (idx >= dev->NumBuffers) {
            DbgPrint("VIOINPUT[%u] DPC: bad cookie %u\n", dev->Instance, idx);
            continue;
        }
        ev = &dev->Buffers[idx];

        /* Skeleton dump - filtered out once translators land.
           Suppress EV_SYN flood (one per packet) by default. */
        if (ev->Type != EV_SYN) {
            DbgPrint("VIOINPUT[%u]: ev type=0x%02x code=0x%04x value=%d\n",
                     dev->Instance, ev->Type, ev->Code, (int)ev->Value);
        }

        /* Re-post this buffer for the next event. */
        seg.Paddr.QuadPart =
            dev->BuffersPaddr.QuadPart + (idx * sizeof(VIRTIO_INPUT_EVENT));
        seg.Len = sizeof(VIRTIO_INPUT_EVENT);
        st = VirtqEnqueue(dev->EventQ, (PVOID)(ULONG)idx,
                          &sg, 0, 1);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIOINPUT[%u] DPC: re-post idx=%u failed 0x%08x\n",
                     dev->Instance, idx, st);
            /* Buffer leaks from the EventQ until next reset; live
               with that for the skeleton, fix in the kbdclass pass. */
        } else {
            kicked = TRUE;
        }
    }
    if (kicked) VirtqHostNotify(dev->EventQ);
    KeReleaseSpinLock(&dev->IsrLock, irql);
}
