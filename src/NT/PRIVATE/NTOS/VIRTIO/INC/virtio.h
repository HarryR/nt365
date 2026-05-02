/*++

    virtio.h — MicroNT virtio shared library, public API.

    Adapted from Unikraft's drivers/virtio/ tree (BSD-3-Clause). The
    protocol structures and algorithms come from the virtio 1.0/1.1
    specification; the OS bindings are NT 3.5 native (ntddk.h).

    Layering:
      virtio.h     — types, virtio_dev struct, public API (this file)
      vio_ids.h    — device-class IDs (NET=1, BLOCK=2, CONSOLE=3, RNG=4, ...)
      vio_pci.h    — PCI legacy I/O register offsets, BAR layout
      vring.h      — descriptor/avail/used ring layout (for impl + device
                     drivers that touch the ring directly)

    Scope: legacy PCI transport only. No MMIO, no modern transport,
    no MSI/MSI-X (NT 3.5 HAL doesn't speak any of those). Packed-ring
    (virtio 1.1) deferred — split-ring is sufficient for our devices.

--*/

#ifndef _VIRTIO_H_
#define _VIRTIO_H_

#include <ntddk.h>

/* NT 3.5's SDK pshpack*.h / poppack.h pair predates MSVC's pack(push)/
   pack(pop) convention: poppack.h unconditionally sets pack(2) instead
   of popping. So any file that includes ntddk.h is left at pack=2,
   regardless of the /Zp8 cmdline default. Reset pack so VIRTIO_DEV /
   VIRTQUEUE / etc. below have a known layout, identical between this
   file and any caller that includes it. (Localised fix; SDK-wide
   replacement of pshpack/poppack with the MSVC 4.2 push/pop versions
   is the eventual root-cause fix.) */
#pragma pack()

/* ------------------------------------------------------------------ *
 * Bare types. Unikraft uses __u8/__u16/__u32/__u64 + __virtio_le16/etc.
 * NT already has UCHAR/USHORT/ULONG/ULONGLONG; we alias the virtio-spec
 * names onto them so the algorithm code reads naturally. x86 is little-
 * endian, so __virtio_le* is the same shape as __u*.
 * ------------------------------------------------------------------ */
typedef UCHAR     u8;
typedef USHORT    u16;
typedef ULONG     u32;
typedef ULONGLONG u64;

typedef u16 le16;
typedef u32 le32;
typedef u64 le64;

/* ------------------------------------------------------------------ *
 * Forward declarations.
 * ------------------------------------------------------------------ */
struct _VIRTIO_DEV;
typedef struct _VIRTIO_DEV   VIRTIO_DEV,  *PVIRTIO_DEV;
typedef struct _VIRTQUEUE    VIRTQUEUE,   *PVIRTQUEUE;

/* Callback invoked from VirtqRingInterrupt() when this queue has used
   descriptors waiting. Driver-supplied; runs at the IRQL of the ISR/DPC
   that drove the interrupt path. Returns nonzero if the callback ran. */
typedef int (*PVIRTQ_CALLBACK)(PVIRTQUEUE vq, PVOID priv);

/* Notify-host hook installed by the transport (PCI legacy here): kicks
   the device after we've added descriptors to the avail ring. */
typedef int (*PVIRTQ_NOTIFY_HOST)(PVIRTIO_DEV vdev, u16 queue_id);

/* ------------------------------------------------------------------ *
 * Scatter-gather list. Tiny — we never need more than a handful of
 * segments per request for our devices (rng = 1, console = 1).
 * ------------------------------------------------------------------ */
typedef struct _VIRTIO_SG_SEG {
    PHYSICAL_ADDRESS Paddr;
    u32              Len;
} VIRTIO_SG_SEG, *PVIRTIO_SG_SEG;

typedef struct _VIRTIO_SG_LIST {
    u16            NumSegs;
    VIRTIO_SG_SEG *Segs;          /* caller-owned array */
} VIRTIO_SG_LIST, *PVIRTIO_SG_LIST;

/* ------------------------------------------------------------------ *
 * Virtio config-ops vtable. Currently implemented by VirtioPci*; future
 * MMIO transport would supply its own.
 * ------------------------------------------------------------------ */
typedef struct _VIRTIO_CONFIG_OPS {
    VOID    (*DeviceReset) (PVIRTIO_DEV vdev);
    NTSTATUS(*ConfigSet)   (PVIRTIO_DEV vdev, u16 offset, VOID *buf, u32 len);
    NTSTATUS(*ConfigGet)   (PVIRTIO_DEV vdev, u16 offset, VOID *buf, u32 len, u8 type_len);
    u64     (*FeaturesGet) (PVIRTIO_DEV vdev);
    VOID    (*FeaturesSet) (PVIRTIO_DEV vdev);
    u8      (*StatusGet)   (PVIRTIO_DEV vdev);
    VOID    (*StatusSet)   (PVIRTIO_DEV vdev, u8 status);
    NTSTATUS(*VqsFind)     (PVIRTIO_DEV vdev, u16 num_vqs, u16 *vq_size);
    NTSTATUS(*VqSetup)     (PVIRTIO_DEV vdev, u16 queue_id, u16 num_desc,
                            PVIRTQ_CALLBACK callback, PVIRTQUEUE *out_vq);
    VOID    (*VqRelease)   (PVIRTIO_DEV vdev, PVIRTQUEUE vq);
} VIRTIO_CONFIG_OPS, *PVIRTIO_CONFIG_OPS;

/* ------------------------------------------------------------------ *
 * VIRTIO_DEV — common header all transports embed at the front of
 * their per-device struct. Device drivers see this; transport-specific
 * fields are in the transport's wrapper struct (e.g. VIRTIO_PCI_DEV).
 * ------------------------------------------------------------------ */
/* Driver-side lifecycle state. Kept distinct from the device's own
   STATUS register bits (VIRTIO_STATUS_*) which travel out to the
   hardware. The enum tag prefix is `Virtio*State*` to avoid clashing
   with the Virtio* function names below. */
typedef enum _VIRTIO_DEV_STATE {
    VirtioStateReset       = 0,
    VirtioStateInitialized = 1,
    VirtioStateConfigured  = 2,
    VirtioStateRunning     = 3,
    VirtioStateStopped     = 4,
} VIRTIO_DEV_STATE;

struct _VIRTIO_DEV {
    u64                Features;        /* negotiated feature bits */
    LIST_ENTRY         VqList;          /* head of VIRTQUEUE.QueueLink */
    PVOID              Priv;            /* driver-private cookie */
    u16                DeviceId;        /* virtio device-class ID (1=net, 4=rng, ...) */
    PVIRTIO_CONFIG_OPS Cops;            /* transport vtable */
    VIRTIO_DEV_STATE   State;
};

/* Status-bit values written to VIRTIO_PCI_STATUS during init handshake. */
#define VIRTIO_STATUS_RESET         0x00
#define VIRTIO_STATUS_ACK           0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_FEATURES_OK   0x08
#define VIRTIO_STATUS_NEEDS_RESET   0x40
#define VIRTIO_STATUS_FAILED        0x80

/* Generic transport-level features, common to all virtio devices. */
#define VIRTIO_F_VERSION_1          32
#define VIRTIO_F_INDIRECT_DESC      28
#define VIRTIO_F_EVENT_IDX          29
#define VIRTIO_F_ANY_LAYOUT         27

#define VIRTIO_HAS_FEATURE(features, bit) \
    (((features) >> (bit)) & 1)

/* ------------------------------------------------------------------ *
 * VIRTQUEUE — public handle that device drivers + the transport pass
 * around. The implementation in ring.c embeds this in a larger struct
 * (VIRTQUEUE_INTERNAL) that holds the actual ring state, recovered
 * via CONTAINING_RECORD. Drivers only touch the fields below.
 * ------------------------------------------------------------------ */
struct _VIRTQUEUE {
    PVIRTIO_DEV         Vdev;
    u16                 QueueId;
    PVIRTQ_NOTIFY_HOST  NotifyHost;
    PVIRTQ_CALLBACK     Callback;
    LIST_ENTRY          QueueLink;     /* sit on vdev->VqList */
    PVOID               Priv;          /* driver-private cookie */
};

/* ------------------------------------------------------------------ *
 * Allocation tag. ExAllocatePoolWithTag in our virtio code uses this
 * so kernel-mode pool tracking attributes leaks correctly.
 * ------------------------------------------------------------------ */
#define VIRTIO_POOL_TAG  '0iVT'   /* "TVi0" little-endian — VirtIO base */

/* ------------------------------------------------------------------ *
 * Memory barriers. NT 3.5's CL 8.50 has neither _ReadBarrier/
 * _WriteBarrier intrinsics (MSVC 2003+) nor KeMemoryBarrier (NT 5+).
 *
 * x86 with normal cacheable WB memory has Total Store Ordering: stores
 * from any one CPU appear in program order to all observers. We're
 * always single-CPU under QEMU on NT 3.5 (no SMP HAL), and virtio
 * devices read guest memory directly through QEMU's emulator path —
 * no cache-coherence shenanigans. A no-op suffices at runtime; the
 * only risk is compiler reordering of the C-level statements.
 *
 * If we ever observe a reordering bug, swap these for `_asm { }`
 * (an inline-asm block is a compiler barrier in CL even when empty).
 * ------------------------------------------------------------------ */
#define VIRTIO_MB()   ((void)0)
#define VIRTIO_WMB()  ((void)0)
#define VIRTIO_RMB()  ((void)0)

/* ------------------------------------------------------------------ *
 * Container-of: NT 3.5 ddk doesn't provide CONTAINING_RECORD's exact
 * Linux-style equivalent, but CONTAINING_RECORD is in ntddk.h and
 * does the same thing. Alias for clarity.
 * ------------------------------------------------------------------ */
#define VIRTIO_CONTAINER_OF(ptr, type, member)  CONTAINING_RECORD(ptr, type, member)

/* ------------------------------------------------------------------ *
 * Logging — direct DbgPrint calls. NT 3.5 CL doesn't support C99
 * variadic preprocessor macros; just call DbgPrint at the use site
 * with the prefix baked in.
 * ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ *
 * Virtqueue API. The opaque PVIRTQUEUE is allocated by VirtqCreate
 * and freed by VirtqDestroy. Implementation detail in vring.h.
 * ------------------------------------------------------------------ */

/*  Create a virtqueue with `nr_descs` descriptors. `align` is the
    transport-imposed alignment between the avail and used rings
    (legacy PCI = 4096; other transports may differ). `notify` is the
    transport's "kick the device" hook; `callback` is the driver's
    used-ring drainer (called from VirtqRingInterrupt()). The vring
    backing memory is allocated physically contiguous via
    MmAllocateContiguousMemory. */
NTSTATUS
VirtqCreate(
    u16                queue_id,
    u16                nr_descs,
    u32                align,
    PVIRTQ_CALLBACK    callback,
    PVIRTQ_NOTIFY_HOST notify,
    PVIRTIO_DEV        vdev,
    PVIRTQUEUE        *out_vq
    );

VOID VirtqDestroy(PVIRTQUEUE vq);

/* Submit a descriptor chain to the avail ring. read_bufs are read-by-
   device segments first, then write_bufs are write-by-device segments.
   `cookie` is opaque driver state we'll hand back on dequeue. */
NTSTATUS
VirtqEnqueue(
    PVIRTQUEUE      vq,
    PVOID           cookie,
    PVIRTIO_SG_LIST sg,
    u16             read_bufs,
    u16             write_bufs
    );

/* Dequeue one used-ring entry. Returns STATUS_NO_MORE_ENTRIES when
   nothing's ready. *cookie gets the cookie passed at enqueue time;
   *len gets the bytes-written-by-device count. */
NTSTATUS
VirtqDequeue(
    PVIRTQUEUE  vq,
    PVOID      *cookie,
    u32        *len
    );

/* Has-data fast probe: nonzero if at least one used-ring entry is ready. */
int VirtqHasData(PVIRTQUEUE vq);

/* Is the avail ring saturated? */
int VirtqIsFull(PVIRTQUEUE vq);

/* Notify the device that we've added work to the avail ring. */
VOID VirtqHostNotify(PVIRTQUEUE vq);

/* Physical addresses of the three rings (descriptor / avail / used).
   Modern transport writes them as three independent 64-bit registers
   (queue_desc / queue_driver / queue_device); legacy crammed them into
   one PFN slot. They're contiguous in our backing allocation either
   way, computed at VringInit time. */
PHYSICAL_ADDRESS VirtqGetRingPaddr (PVIRTQUEUE vq);   /* desc[0]  */
PHYSICAL_ADDRESS VirtqGetAvailPaddr(PVIRTQUEUE vq);   /* avail.*  */
PHYSICAL_ADDRESS VirtqGetUsedPaddr (PVIRTQUEUE vq);   /* used.*   */

/* Disable / enable interrupts on this queue (avail ring flags). */
VOID VirtqIntrDisable(PVIRTQUEUE vq);
int  VirtqIntrEnable(PVIRTQUEUE vq);

/* Called from the transport ISR when this queue may have used entries.
   Invokes the per-queue callback if data is present. */
int VirtqRingInterrupt(PVIRTQUEUE vq);

/* Mask off transport-level features the queue layer doesn't support
   (VIRTIO_F_EVENT_IDX is the main one we currently negotiate to off). */
u64 VirtqFeatureNegotiate(u64 feature_set);

/* ------------------------------------------------------------------ *
 * Common bus / status helpers (BUS.C). These wrap Cops vtable calls
 * with NULL checks, matching Unikraft's virtio_dev_status_update etc.
 * ------------------------------------------------------------------ */
NTSTATUS VirtioDevReset       (PVIRTIO_DEV vdev);
NTSTATUS VirtioDevStatusUpdate(PVIRTIO_DEV vdev, u8 status);
u8       VirtioDevStatusGet   (PVIRTIO_DEV vdev);
u64      VirtioFeatureGet     (PVIRTIO_DEV vdev);
VOID     VirtioFeatureSet     (PVIRTIO_DEV vdev);
NTSTATUS VirtioConfigGet      (PVIRTIO_DEV vdev, u16 offset, VOID *buf,
                               u32 len, u8 type_len);
NTSTATUS VirtioConfigSet      (PVIRTIO_DEV vdev, u16 offset, VOID *buf, u32 len);
NTSTATUS VirtioFindVqs        (PVIRTIO_DEV vdev, u16 total_vqs, u16 *vq_size);
NTSTATUS VirtioVqSetup        (PVIRTIO_DEV vdev, u16 vq_id, u16 nr_desc,
                               PVIRTQ_CALLBACK callback, PVIRTQUEUE *out_vq);
VOID     VirtioVqRelease      (PVIRTIO_DEV vdev, PVIRTQUEUE vq);
VOID     VirtioDevDriverUp    (PVIRTIO_DEV vdev);

#endif /* _VIRTIO_H_ */
