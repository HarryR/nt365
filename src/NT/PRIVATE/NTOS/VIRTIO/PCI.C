/*++

    pci.c - Modern (virtio 1.0+) PCI transport. Implements the
    VIRTIO_CONFIG_OPS vtable on top of NT 3.5's HAL PCI config space
    + MmMapIoSpace-mapped MMIO regions.

    Walks the standard PCI capability list at config offset 0x34 to
    find the virtio-specific MMIO regions:
      VIRTIO_PCI_CAP_COMMON_CFG  - 56-byte common config struct
      VIRTIO_PCI_CAP_NOTIFY_CFG  - kick region + per-cap notify_off_mul
      VIRTIO_PCI_CAP_ISR_CFG     - single ISR byte (read-clears)
      VIRTIO_PCI_CAP_DEVICE_CFG  - device-class-specific config

    Each cap names a BAR + offset + length; we MmMapIoSpace each used
    BAR exactly once and stash region pointers in the VIRTIO_PCI_DEV.

    INTx interrupts only - MSI-X is optional in the modern spec, and
    NT 3.5's HAL doesn't speak it. We set queue_msix_vector +
    msix_config to VIRTIO_MSI_NO_VECTOR (0xFFFF).

    Adapted from Unikraft drivers/virtio/pci/virtio_pci.c (BSD-3) and
    the virtio 1.2 spec sec 4.1.

--*/

#include "virtio.h"
#include "virtio_pci.h"

/*
 * Per-cap / per-BAR walk traces are noisy once more than one virtio
 * device exists - hide them behind DBG. Errors and the one-line
 * post-init summary stay visible at all build levels.
 */
#if DBG
#define VTRACE(args) DbgPrint args
#else
#define VTRACE(args) ((void)0)
#endif

/* ------------------------------------------------------------------ *
 * Forward decls - vtable members.
 * ------------------------------------------------------------------ */
static VOID     VpciDeviceReset (PVIRTIO_DEV vdev);
static NTSTATUS VpciConfigSet   (PVIRTIO_DEV vdev, u16 offset, VOID *buf, u32 len);
static NTSTATUS VpciConfigGet   (PVIRTIO_DEV vdev, u16 offset, VOID *buf, u32 len, u8 type_len);
static u64      VpciFeaturesGet (PVIRTIO_DEV vdev);
static VOID     VpciFeaturesSet (PVIRTIO_DEV vdev);
static u8       VpciStatusGet   (PVIRTIO_DEV vdev);
static VOID     VpciStatusSet   (PVIRTIO_DEV vdev, u8 status);
static NTSTATUS VpciVqsFind     (PVIRTIO_DEV vdev, u16 num_vqs, u16 *vq_size);
static NTSTATUS VpciVqSetup     (PVIRTIO_DEV vdev, u16 queue_id, u16 num_desc,
                                 PVIRTQ_CALLBACK callback, PVIRTQUEUE *out_vq);
static VOID     VpciVqRelease   (PVIRTIO_DEV vdev, PVIRTQUEUE vq);
static int      VpciNotify      (PVIRTIO_DEV vdev, u16 queue_id);

static NTSTATUS VpciWalkCaps    (PVIRTIO_PCI_DEV vpdev);
static NTSTATUS VpciMapBars     (PVIRTIO_PCI_DEV vpdev);
static NTSTATUS VpciReadBar     (ULONG bus, ULONG slot, ULONG bar_idx,
                                 PHYSICAL_ADDRESS *out_paddr, ULONG *out_len);

static VIRTIO_CONFIG_OPS VpciModernOps = {
    VpciDeviceReset,
    VpciConfigSet,
    VpciConfigGet,
    VpciFeaturesGet,
    VpciFeaturesSet,
    VpciStatusGet,
    VpciStatusSet,
    VpciVqsFind,
    VpciVqSetup,
    VpciVqRelease,
};

/* ------------------------------------------------------------------ *
 * Common-cfg field accessors. WRITE_REGISTER_x / READ_REGISTER_x are
 * the NT-canonical MMIO primitives; they take a pointer to the typed
 * register and the value. Pointer arithmetic via the struct member
 * gives us correct offsets without manual byte math.
 *
 * 64-bit writes (queue_desc/driver/device) are split into two ULONG
 * writes (low then high) - NT 3.5 has no 64-bit MMIO helper, and
 * spec sec 4.1.4.3.1 explicitly allows the split as long as low precedes
 * high (which is naturally enforced by program order on x86).
 * ------------------------------------------------------------------ */

__inline VOID
VpciCommonW32(PULONG reg, ULONG val)
{
    WRITE_REGISTER_ULONG(reg, val);
}

__inline VOID
VpciCommonW16(PUSHORT reg, USHORT val)
{
    WRITE_REGISTER_USHORT(reg, val);
}

__inline VOID
VpciCommonW8(PUCHAR reg, UCHAR val)
{
    WRITE_REGISTER_UCHAR(reg, val);
}

__inline ULONG
VpciCommonR32(PULONG reg)
{
    return READ_REGISTER_ULONG(reg);
}

__inline USHORT
VpciCommonR16(PUSHORT reg)
{
    return READ_REGISTER_USHORT(reg);
}

__inline UCHAR
VpciCommonR8(PUCHAR reg)
{
    return READ_REGISTER_UCHAR(reg);
}

__inline VOID
VpciCommonW64(PULONG reg_lo, ULONGLONG val)
{
    WRITE_REGISTER_ULONG(reg_lo,     (ULONG)(val & 0xFFFFFFFF));
    WRITE_REGISTER_ULONG(reg_lo + 1, (ULONG)(val >> 32));
}

/* ------------------------------------------------------------------ *
 * Status / reset.
 * ------------------------------------------------------------------ */

static u8
VpciStatusGet(PVIRTIO_DEV vdev)
{
    PVIRTIO_PCI_DEV vpdev = VIRTIO_TO_PCI(vdev);
    return VpciCommonR8(&vpdev->Common->DeviceStatus);
}

static VOID
VpciStatusSet(PVIRTIO_DEV vdev, u8 status)
{
    PVIRTIO_PCI_DEV vpdev = VIRTIO_TO_PCI(vdev);
    u8 cur;

    ASSERT(status != VIRTIO_STATUS_RESET);   /* use VpciDeviceReset */
    cur = VpciCommonR8(&vpdev->Common->DeviceStatus);
    VpciCommonW8(&vpdev->Common->DeviceStatus, (UCHAR)(cur | status));
}

static VOID
VpciDeviceReset(PVIRTIO_DEV vdev)
{
    PVIRTIO_PCI_DEV vpdev = VIRTIO_TO_PCI(vdev);
    ULONG i;
    u8 status;

    /* Spec sec 4.1.4.3.2: write 0 to status, poll until device clears it. */
    VpciCommonW8(&vpdev->Common->DeviceStatus, VIRTIO_STATUS_RESET);
    for (i = 0; i < 1000000; i++) {
        status = VpciCommonR8(&vpdev->Common->DeviceStatus);
        if (status == VIRTIO_STATUS_RESET)
            return;
    }
    DbgPrint("VIRTIO pci: reset stuck at status 0x%02x\n", status);
}

/* ------------------------------------------------------------------ *
 * Features. Modern uses 64-bit features via the select/window pair -
 * write select=0 to access bits 0..31, select=1 for bits 32..63.
 * ------------------------------------------------------------------ */

static u64
VpciFeaturesGet(PVIRTIO_DEV vdev)
{
    PVIRTIO_PCI_DEV vpdev = VIRTIO_TO_PCI(vdev);
    u64 features;

    VpciCommonW32(&vpdev->Common->DeviceFeatureSelect, 0);
    features = VpciCommonR32(&vpdev->Common->DeviceFeature);

    VpciCommonW32(&vpdev->Common->DeviceFeatureSelect, 1);
    features |= ((u64)VpciCommonR32(&vpdev->Common->DeviceFeature)) << 32;

    return features;
}

static VOID
VpciFeaturesSet(PVIRTIO_DEV vdev)
{
    PVIRTIO_PCI_DEV vpdev = VIRTIO_TO_PCI(vdev);
    u64 feat;

    /* Mask through queue layer first (no-op today; kept for future
       VIRTIO_F_EVENT_IDX / INDIRECT_DESC negotiation). */
    feat = VirtqFeatureNegotiate(vdev->Features);

    /* Modern spec REQUIRES guest to ack VIRTIO_F_VERSION_1 (bit 32).
       Without it, modern devices refuse to leave FEATURES_OK. */
    feat |= ((u64)1 << VIRTIO_F_VERSION_1);
    vdev->Features = feat;

    VpciCommonW32(&vpdev->Common->GuestFeatureSelect, 0);
    VpciCommonW32(&vpdev->Common->GuestFeature, (u32)(feat & 0xFFFFFFFF));
    VpciCommonW32(&vpdev->Common->GuestFeatureSelect, 1);
    VpciCommonW32(&vpdev->Common->GuestFeature, (u32)(feat >> 32));
}

/* ------------------------------------------------------------------ *
 * Device-specific config. The DEVICE_CFG region is a flat MMIO area
 * the driver reads/writes byte-by-byte (small fields) or in larger
 * units (capacity, etc). Per spec sec 4.1.4.4, multi-byte reads should
 * be re-read on config_generation changes; for single-byte reads or
 * stable fields it's not required.
 * ------------------------------------------------------------------ */

static NTSTATUS
VpciConfigGet(PVIRTIO_DEV vdev, u16 offset, VOID *buf, u32 len, u8 type_len)
{
    PVIRTIO_PCI_DEV vpdev = VIRTIO_TO_PCI(vdev);
    PUCHAR src;
    PUCHAR dst = (PUCHAR)buf;
    u32    i;

    UNREFERENCED_PARAMETER(type_len);

    if (!vpdev->DeviceCfg)
        return STATUS_NOT_SUPPORTED;
    if ((u32)offset + len > vpdev->DeviceCfgLen)
        return STATUS_INVALID_PARAMETER;

    src = vpdev->DeviceCfg + offset;
    if (len == 1) {
        dst[0] = VpciCommonR8(src);
    } else if (len == 2) {
        *(USHORT*)dst = VpciCommonR16((PUSHORT)src);
    } else if (len == 4) {
        *(ULONG*)dst = VpciCommonR32((PULONG)src);
    } else {
        for (i = 0; i < len; i++)
            dst[i] = VpciCommonR8(src + i);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
VpciConfigSet(PVIRTIO_DEV vdev, u16 offset, VOID *buf, u32 len)
{
    PVIRTIO_PCI_DEV vpdev = VIRTIO_TO_PCI(vdev);
    PUCHAR src = (PUCHAR)buf;
    PUCHAR dst;
    u32    i;

    if (!vpdev->DeviceCfg)
        return STATUS_NOT_SUPPORTED;
    if ((u32)offset + len > vpdev->DeviceCfgLen)
        return STATUS_INVALID_PARAMETER;

    dst = vpdev->DeviceCfg + offset;
    for (i = 0; i < len; i++)
        VpciCommonW8(dst + i, src[i]);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * Queue setup. Modern lets the driver:
 *   - shrink QueueSize (we don't, just use what the device offers)
 *   - place desc/avail/used at three independent paddrs
 *   - mark each queue Enable=1 individually
 * ------------------------------------------------------------------ */

static int
VpciNotify(PVIRTIO_DEV vdev, u16 queue_id)
{
    PVIRTIO_PCI_DEV vpdev = VIRTIO_TO_PCI(vdev);
    USHORT          notify_off;
    PUSHORT         kick;

    /* Each queue has its own notify offset (read once at setup time).
       We re-read here since we don't cache per-queue; cheap. */
    VpciCommonW16(&vpdev->Common->QueueSelect, queue_id);
    notify_off = VpciCommonR16(&vpdev->Common->QueueNotifyOff);
    kick = (PUSHORT)(vpdev->Notify + (ULONG)notify_off * vpdev->NotifyOffMul);
    VpciCommonW16(kick, queue_id);
    return 0;
}

static NTSTATUS
VpciVqsFind(PVIRTIO_DEV vdev, u16 num_vqs, u16 *vq_size)
{
    PVIRTIO_PCI_DEV vpdev = VIRTIO_TO_PCI(vdev);
    u16 i;
    USHORT total_q;

    total_q = VpciCommonR16(&vpdev->Common->NumQueues);
    if (num_vqs > total_q) {
        DbgPrint("VIRTIO pci: requested %u queues, device offers %u\n",
                 num_vqs, total_q);
        return STATUS_DEVICE_NOT_READY;
    }

    for (i = 0; i < num_vqs; i++) {
        VpciCommonW16(&vpdev->Common->QueueSelect, i);
        vq_size[i] = VpciCommonR16(&vpdev->Common->QueueSize);
        if (vq_size[i] == 0) {
            DbgPrint("VIRTIO pci: queue %u not available\n", i);
            return STATUS_DEVICE_NOT_READY;
        }
    }
    return STATUS_SUCCESS;
}

/* Modern vring is split: desc/avail/used live at three separate
   paddrs. Our VirtqCreate still allocates them as one contiguous
   block (legacy-compatible layout) - we just hand the device three
   paddrs computed from the same base. */
static NTSTATUS
VpciVqSetup(
    PVIRTIO_DEV     vdev,
    u16             queue_id,
    u16             num_desc,
    PVIRTQ_CALLBACK callback,
    PVIRTQUEUE     *out_vq
    )
{
    PVIRTIO_PCI_DEV  vpdev = VIRTIO_TO_PCI(vdev);
    PVIRTQUEUE       vq;
    NTSTATUS         st;
    PHYSICAL_ADDRESS desc_paddr;
    PHYSICAL_ADDRESS avail_paddr;
    PHYSICAL_ADDRESS used_paddr;

    /* Modern relaxes the legacy 4096-alignment-between-rings; the
       common 64-byte alignment satisfies all rings independently.
       Keep 4096 for now - VringInit's offset math depends on it and
       the wasted page is in the noise. */
    st = VirtqCreate(queue_id, num_desc, 4096,
                     callback, VpciNotify, vdev, &vq);
    if (!NT_SUCCESS(st))
        return st;

    desc_paddr  = VirtqGetRingPaddr(vq);
    avail_paddr = VirtqGetAvailPaddr(vq);
    used_paddr  = VirtqGetUsedPaddr(vq);

    /* Tell the device about this queue. Spec sec 4.1.4.3.2 ordering:
       select → set sizes/paddrs → MSI vector → enable. */
    VpciCommonW16(&vpdev->Common->QueueSelect,      queue_id);
    VpciCommonW16(&vpdev->Common->QueueSize,        num_desc);
    VpciCommonW16(&vpdev->Common->QueueMsixVector,  VIRTIO_MSI_NO_VECTOR);
    VpciCommonW64((PULONG)&vpdev->Common->QueueDesc,
                  (ULONGLONG)desc_paddr.QuadPart);
    VpciCommonW64((PULONG)&vpdev->Common->QueueDriver,
                  (ULONGLONG)avail_paddr.QuadPart);
    VpciCommonW64((PULONG)&vpdev->Common->QueueDevice,
                  (ULONGLONG)used_paddr.QuadPart);
    VpciCommonW16(&vpdev->Common->QueueEnable,      1);

    InsertTailList(&vdev->VqList, &vq->QueueLink);
    *out_vq = vq;
    return STATUS_SUCCESS;
}

static VOID
VpciVqRelease(PVIRTIO_DEV vdev, PVIRTQUEUE vq)
{
    PVIRTIO_PCI_DEV vpdev = VIRTIO_TO_PCI(vdev);

    /* Disable the queue. Modern spec sec 4.1.4.3.2: a disabled queue
       must be re-set-up before reuse; for our teardown that's fine. */
    VpciCommonW16(&vpdev->Common->QueueSelect, vq->QueueId);
    VpciCommonW16(&vpdev->Common->QueueEnable, 0);

    RemoveEntryList(&vq->QueueLink);
    VirtqDestroy(vq);
}

/* ------------------------------------------------------------------ *
 * PCI capability walking. Caps live in standard PCI config space
 * (reachable via HalGetBusDataByOffset). The cap pointer is at byte
 * 0x34; from there it's a linked list of {cap_vndr, cap_next, ...}.
 * We collect the four virtio caps we care about, then map any BAR
 * they reference.
 * ------------------------------------------------------------------ */

static NTSTATUS
VpciWalkCaps(PVIRTIO_PCI_DEV vpdev)
{
    UCHAR cap_off;
    UCHAR cap_vndr;
    ULONG iter;
    VIRTIO_PCI_CAP        cap;
    VIRTIO_PCI_NOTIFY_CAP ncap;
    ULONG got;
    USHORT pci_status;

    /* Verify PCI Status[4] (Capabilities List Available) is set —
       per PCI spec the cap pointer at 0x34 is undefined otherwise. */
    got = HalGetBusDataByOffset(PCIConfiguration, vpdev->BusNumber,
                                vpdev->SlotNumber, &pci_status, 0x06, 2);
    VTRACE(("VIRTIO pci: PCI Status reg = 0x%04x got=%u (caps-list-avail bit=%u)\n",
             pci_status, got, (pci_status >> 4) & 1));

    /* Read CapPtr (1 byte at config offset 0x34). */
    got = HalGetBusDataByOffset(PCIConfiguration, vpdev->BusNumber,
                                vpdev->SlotNumber, &cap_off,
                                PCI_CAP_PTR_OFFSET, 1);
    VTRACE(("VIRTIO pci: CapPtr@0x34 = 0x%02x got=%u\n", cap_off, got));
    if (got < 1 || cap_off == 0) {
        DbgPrint("VIRTIO pci: no capabilities at slot 0x%x\n",
                 vpdev->SlotNumber);
        return STATUS_NOT_SUPPORTED;
    }

    /* Walk; cap with cap_next == 0 terminates. Cap chain is supposed
       to be acyclic + bounded but we cap iterations defensively. */
    for (iter = 0; iter < 256 && cap_off != 0; iter++) {
        got = HalGetBusDataByOffset(PCIConfiguration, vpdev->BusNumber,
                                    vpdev->SlotNumber, &cap, cap_off,
                                    sizeof(VIRTIO_PCI_CAP));
        if (got < sizeof(VIRTIO_PCI_CAP)) {
            DbgPrint("VIRTIO pci: short cap read at off 0x%x got=%u\n",
                     cap_off, got);
            return STATUS_DEVICE_DATA_ERROR;
        }
        cap_vndr = cap.CapVndr;
        VTRACE(("VIRTIO pci: cap @off=0x%02x vndr=0x%02x next=0x%02x len=%u cfg_type=%u\n",
                cap_off, cap_vndr, cap.CapNext, cap.CapLen, cap.CfgType));
        if (cap_vndr == PCI_CAP_ID_VENDOR_SPECIFIC) {
            /* virtio cap. Bar must be in range; some BIOSes emit
               garbage caps - sanity-check before believing them. */
            if (cap.Bar >= VIRTIO_PCI_NUM_BARS) {
                DbgPrint("VIRTIO pci: cap.bar %u out of range\n", cap.Bar);
                cap_off = cap.CapNext;
                continue;
            }
            VTRACE(("VIRTIO pci: cap type=%u bar=%u offset=0x%x len=%u\n",
                    cap.CfgType, cap.Bar, cap.Offset, cap.Length));
            switch (cap.CfgType) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                vpdev->Bars[cap.Bar].Length = cap.Offset + cap.Length;
                vpdev->Common = (PVIRTIO_PCI_COMMON_CFG)
                                ((ULONG)cap.Bar << 28 | cap.Offset);
                /* Encode bar+offset temporarily; resolve to VA after BARs
                   are mapped (we don't know the VA yet - VpciMapBars
                   fills Bars[i].VirtAddr, then we patch up). */
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                /* Notify cap has an extra u32 (NotifyOffMultiplier)
                   after the standard 16 bytes. */
                got = HalGetBusDataByOffset(PCIConfiguration,
                                            vpdev->BusNumber,
                                            vpdev->SlotNumber, &ncap,
                                            cap_off,
                                            sizeof(VIRTIO_PCI_NOTIFY_CAP));
                if (got < sizeof(VIRTIO_PCI_NOTIFY_CAP))
                    return STATUS_DEVICE_DATA_ERROR;
                vpdev->Bars[cap.Bar].Length = cap.Offset + cap.Length;
                vpdev->Notify = (PUCHAR)((ULONG)cap.Bar << 28 | cap.Offset);
                vpdev->NotifyOffMul = ncap.NotifyOffMultiplier;
                break;
            case VIRTIO_PCI_CAP_ISR_CFG:
                vpdev->Bars[cap.Bar].Length = cap.Offset + cap.Length;
                vpdev->Isr = (PUCHAR)((ULONG)cap.Bar << 28 | cap.Offset);
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                vpdev->Bars[cap.Bar].Length = cap.Offset + cap.Length;
                vpdev->DeviceCfg = (PUCHAR)((ULONG)cap.Bar << 28 | cap.Offset);
                vpdev->DeviceCfgLen = cap.Length;
                break;
            default:
                /* PCI_CFG, SHARED_MEM, anything we don't speak - ignore. */
                break;
            }
        }
        cap_off = cap.CapNext;
    }

    if (!vpdev->Common || !vpdev->Notify || !vpdev->Isr) {
        DbgPrint("VIRTIO pci: missing cap (common=%p notify=%p isr=%p)\n",
                 vpdev->Common, vpdev->Notify, vpdev->Isr);
        return STATUS_DEVICE_NOT_READY;
    }
    return STATUS_SUCCESS;
}

/* Read one BAR's physical address and length from PCI config space.
   Handles both 32-bit and 64-bit memory BARs (the latter use BAR(N)
   for low 32 bits and BAR(N+1) for high 32 bits, with type bits
   [2:1] = 0b10 in BAR(N)). UEFI is expected to have already programmed
   the BAR; we don't allocate addresses, just read what's there. */
static NTSTATUS
VpciReadBar(ULONG bus, ULONG slot, ULONG bar_idx,
            PHYSICAL_ADDRESS *out_paddr, ULONG *out_len)
{
    ULONG bar_off = 0x10 + bar_idx * 4;
    ULONG orig_lo, orig_hi = 0;
    ULONG probe_lo, probe_hi = 0;
    ULONGLONG size, mask;
    UCHAR type_bits;

    if (HalGetBusDataByOffset(PCIConfiguration, bus, slot,
                              &orig_lo, bar_off, 4) != 4)
        return STATUS_DEVICE_DATA_ERROR;

    VTRACE(("VIRTIO pci: BAR%u@0x%02x raw=0x%08x\n",
            bar_idx, bar_off, orig_lo));

    if (orig_lo & 0x1) {
        /* I/O BAR - virtio modern caps don't target I/O. */
        out_paddr->QuadPart = 0;
        *out_len = 0;
        return STATUS_SUCCESS;
    }

    type_bits = (UCHAR)((orig_lo >> 1) & 0x3);   /* 00=32-bit, 10=64-bit */

    /* Probe BAR(N) for size: write all-1s, read mask, restore. */
    probe_lo = 0xFFFFFFFF;
    HalSetBusDataByOffset(PCIConfiguration, bus, slot, &probe_lo, bar_off, 4);
    HalGetBusDataByOffset(PCIConfiguration, bus, slot, &probe_lo, bar_off, 4);
    HalSetBusDataByOffset(PCIConfiguration, bus, slot, &orig_lo,  bar_off, 4);

    if (type_bits == 2) {
        /* 64-bit BAR - read + probe the high half too. */
        if (HalGetBusDataByOffset(PCIConfiguration, bus, slot,
                                  &orig_hi, bar_off + 4, 4) != 4)
            return STATUS_DEVICE_DATA_ERROR;
        probe_hi = 0xFFFFFFFF;
        HalSetBusDataByOffset(PCIConfiguration, bus, slot, &probe_hi,
                              bar_off + 4, 4);
        HalGetBusDataByOffset(PCIConfiguration, bus, slot, &probe_hi,
                              bar_off + 4, 4);
        HalSetBusDataByOffset(PCIConfiguration, bus, slot, &orig_hi,
                              bar_off + 4, 4);
        VTRACE(("VIRTIO pci: BAR%u 64-bit; high half raw=0x%08x probe=0x%08x\n",
                bar_idx + 1, orig_hi, probe_hi));
    }

    /* Compute size: 64-bit mask (probe_hi:probe_lo&~0xF), invert + 1. */
    mask = ((ULONGLONG)probe_hi << 32) | (ULONGLONG)(probe_lo & ~0xFul);
    size = (~mask) + 1;

    out_paddr->HighPart = (LONG)orig_hi;
    out_paddr->LowPart  = orig_lo & ~0xFul;
    *out_len = (size > 0xFFFFFFFFul) ? 0xFFFFFFFFul : (ULONG)size;

    VTRACE(("VIRTIO pci: BAR%u resolved paddr=0x%08x:%08x size=0x%x type=%u\n",
            bar_idx, (ULONG)out_paddr->HighPart, out_paddr->LowPart,
            *out_len, type_bits));
    return STATUS_SUCCESS;
}

/* Map every BAR our cap walk identified as containing a virtio region.
   After this, vpdev->Bars[i].VirtAddr holds the kernel VA for each
   used BAR; we then resolve the {Common,Notify,Isr,DeviceCfg} pointers
   from the encoded (bar << 28 | offset) form to actual VAs. */
static NTSTATUS
VpciMapBars(PVIRTIO_PCI_DEV vpdev)
{
    ULONG i;
    PHYSICAL_ADDRESS paddr;
    ULONG bar_full_len;
    NTSTATUS st;

    for (i = 0; i < VIRTIO_PCI_NUM_BARS; i++) {
        if (vpdev->Bars[i].Length == 0)
            continue;

        st = VpciReadBar(vpdev->BusNumber, vpdev->SlotNumber, i,
                         &paddr, &bar_full_len);
        if (!NT_SUCCESS(st))
            return st;
        if (bar_full_len == 0) {
            DbgPrint("VIRTIO pci: cap targets BAR%u but BAR is empty\n", i);
            return STATUS_DEVICE_NOT_READY;
        }
        /* The cap might reference only a portion; map the whole BAR
           to be safe (notify caps span the BAR with a multiplier).
           Bar.Length was set by cap walk to (offset + length) which
           is enough for that cap; bar_full_len is the BAR's actual
           reservation. Use whichever is bigger. */
        if (bar_full_len > vpdev->Bars[i].Length)
            vpdev->Bars[i].Length = bar_full_len;

        vpdev->Bars[i].PhysAddr = paddr;
        /* NT 3.5 MmMapIoSpace takes BOOLEAN CacheEnable (NT 4 added the
           MEMORY_CACHING_TYPE enum). FALSE = uncached, what we want for
           MMIO. */
        vpdev->Bars[i].VirtAddr = (PUCHAR)MmMapIoSpace(
            paddr, vpdev->Bars[i].Length, FALSE);
        if (!vpdev->Bars[i].VirtAddr) {
            DbgPrint("VIRTIO pci: MmMapIoSpace BAR%u (paddr=0x%x len=%u) failed\n",
                     i, paddr.LowPart, vpdev->Bars[i].Length);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        VTRACE(("VIRTIO pci: BAR%u paddr=0x%x len=%u -> VA %p\n",
                i, paddr.LowPart, vpdev->Bars[i].Length,
                vpdev->Bars[i].VirtAddr));
    }

    /* Resolve region pointers from (bar << 28 | offset) → actual VA. */
    {
        ULONG enc;
        ULONG     bar;
        ULONG     off;

        if (vpdev->Common) {
            enc = (ULONG)vpdev->Common;
            bar = (ULONG)(enc >> 28); off = (ULONG)(enc & 0x0FFFFFFFul);
            vpdev->Common = (PVIRTIO_PCI_COMMON_CFG)
                            (vpdev->Bars[bar].VirtAddr + off);
        }
        if (vpdev->Notify) {
            enc = (ULONG)vpdev->Notify;
            bar = (ULONG)(enc >> 28); off = (ULONG)(enc & 0x0FFFFFFFul);
            vpdev->Notify = vpdev->Bars[bar].VirtAddr + off;
        }
        if (vpdev->Isr) {
            enc = (ULONG)vpdev->Isr;
            bar = (ULONG)(enc >> 28); off = (ULONG)(enc & 0x0FFFFFFFul);
            vpdev->Isr = vpdev->Bars[bar].VirtAddr + off;
        }
        if (vpdev->DeviceCfg) {
            enc = (ULONG)vpdev->DeviceCfg;
            bar = (ULONG)(enc >> 28); off = (ULONG)(enc & 0x0FFFFFFFul);
            vpdev->DeviceCfg = vpdev->Bars[bar].VirtAddr + off;
        }
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * Public init / ISR / cleanup.
 * ------------------------------------------------------------------ */

NTSTATUS
VirtioPciInit(
    PVIRTIO_PCI_DEV vpdev,
    ULONG           bus_number,
    ULONG           slot_number,
    ULONG           interrupt_vector,
    KIRQL           interrupt_level,
    KAFFINITY       interrupt_affinity,
    u16             device_id
    )
{
    NTSTATUS st;

    ASSERT(vpdev != NULL);
    RtlZeroMemory(vpdev, sizeof(*vpdev));

    vpdev->BusNumber         = bus_number;
    vpdev->SlotNumber        = slot_number;
    vpdev->InterruptVector   = interrupt_vector;
    vpdev->InterruptLevel    = interrupt_level;
    vpdev->InterruptAffinity = interrupt_affinity;

    /* Walk caps + map BARs. */
    st = VpciWalkCaps(vpdev);
    if (!NT_SUCCESS(st)) return st;
    st = VpciMapBars(vpdev);
    if (!NT_SUCCESS(st)) {
        VirtioPciCleanup(vpdev);
        return st;
    }

    /* Disable any MSI-X (we don't use it). */
    VpciCommonW16(&vpdev->Common->MsixConfig, VIRTIO_MSI_NO_VECTOR);

    vpdev->Vdev.Cops     = &VpciModernOps;
    vpdev->Vdev.DeviceId = device_id;
    vpdev->Vdev.State    = VirtioStateReset;
    InitializeListHead(&vpdev->Vdev.VqList);

    DbgPrint("VIRTIO pci: bus%u slot 0x%x devid 0x%x ready "
             "(common=%p notify=%p isr=%p devcfg=%p)\n",
             vpdev->BusNumber, vpdev->SlotNumber, device_id,
             vpdev->Common, vpdev->Notify, vpdev->Isr, vpdev->DeviceCfg);

    return STATUS_SUCCESS;
}

VOID
VirtioPciCleanup(PVIRTIO_PCI_DEV vpdev)
{
    ULONG i;

    for (i = 0; i < VIRTIO_PCI_NUM_BARS; i++) {
        if (vpdev->Bars[i].VirtAddr) {
            MmUnmapIoSpace(vpdev->Bars[i].VirtAddr,
                           vpdev->Bars[i].Length);
            vpdev->Bars[i].VirtAddr = NULL;
        }
    }
}

int
VirtioPciIsr(PVIRTIO_PCI_DEV vpdev)
{
    UCHAR        isr;
    PLIST_ENTRY  link;
    PVIRTQUEUE   vq;
    int          handled = 0;

    ASSERT(vpdev != NULL);
    if (!vpdev->Isr) return 0;

    /* Reading the ISR byte clears it (modern same as legacy). */
    isr = READ_REGISTER_UCHAR(vpdev->Isr);

    if (isr & 0x02) {
        DbgPrint("VIRTIO pci: ISR config-change on dev %p (unhandled)\n",
                 vpdev);
        handled = 1;
    }
    if (isr & 0x01) {
        for (link = vpdev->Vdev.VqList.Flink;
             link != &vpdev->Vdev.VqList;
             link = link->Flink) {
            vq = CONTAINING_RECORD(link, VIRTQUEUE, QueueLink);
            handled |= VirtqRingInterrupt(vq);
        }
    }
    return handled;
}
