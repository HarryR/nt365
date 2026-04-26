/*++

    virtio_pci.h — Modern (virtio 1.0+) PCI transport definitions.

    Modern transport replaces the legacy single-I/O-BAR fixed-offset
    register layout with PCI capabilities pointing at MMIO regions for
    common config, queue notify, ISR status, and device-specific
    config. Walks the standard PCI capability list at config offset
    0x34 looking for vendor-specific (cap_vndr=0x09) entries with our
    cfg_type tags.

    Per-device-class IDs are uniformly 0x1040 + virtio_class_id; we
    don't speak legacy IDs (0x1000..0x103F) at all.

    Spec: virtio 1.2 §4.1.4. Adapted from Unikraft + spec headers
    (BSD-3 / GPL-with-syscall-exception).

--*/

#ifndef _VIRTIO_PCI_H_
#define _VIRTIO_PCI_H_

#include "virtio.h"

/* ------------------------------------------------------------------ *
 * Modern PCI device IDs: 0x1040 + virtio device-class ID.
 * Examples: NET=1 → 0x1041, BLOCK=2 → 0x1042, CONSOLE=3 → 0x1043,
 * RNG=4 → 0x1044, INPUT=18 → 0x1052, GPU=16 → 0x1050.
 * ------------------------------------------------------------------ */
#define VIRTIO_PCI_MODERN_ID(class)  ((u16)(0x1040 + (class)))

/* No-MSI-X sentinel for queue_msix_vector / msix_config. */
#define VIRTIO_MSI_NO_VECTOR         0xFFFF

/* PCI capability discovery. */
#define PCI_CAP_PTR_OFFSET           0x34   /* CapPtr in standard cfg space */
#define PCI_CAP_ID_VENDOR_SPECIFIC   0x09   /* cap_vndr value for virtio caps */

/* virtio cap.cfg_type values (spec §4.1.4.1). */
#define VIRTIO_PCI_CAP_COMMON_CFG    1
#define VIRTIO_PCI_CAP_NOTIFY_CFG    2
#define VIRTIO_PCI_CAP_ISR_CFG       3
#define VIRTIO_PCI_CAP_DEVICE_CFG    4
#define VIRTIO_PCI_CAP_PCI_CFG       5      /* I/O fallback — we don't use */
#define VIRTIO_PCI_CAP_SHARED_MEM    8      /* virtio-fs etc. — defer */

/* ------------------------------------------------------------------ *
 * Wire-format virtio_pci_cap (spec §4.1.4.1). The cap is found by
 * walking the standard PCI capability list; the first byte we see is
 * the standard PCI cap header (cap_vndr + cap_next), the rest is
 * virtio-specific.
 * ------------------------------------------------------------------ */
#include <pshpack1.h>

typedef struct _VIRTIO_PCI_CAP {
    u8  CapVndr;        /* 0x09 */
    u8  CapNext;        /* offset of next PCI cap, 0 = end */
    u8  CapLen;         /* size of this cap (>= 16) */
    u8  CfgType;        /* VIRTIO_PCI_CAP_* */
    u8  Bar;            /* BAR index 0..5 */
    u8  Padding[3];
    u32 Offset;         /* offset within the BAR */
    u32 Length;         /* length of the region */
} VIRTIO_PCI_CAP, *PVIRTIO_PCI_CAP;

/* NOTIFY cap extends with a multiplier byte. */
typedef struct _VIRTIO_PCI_NOTIFY_CAP {
    VIRTIO_PCI_CAP Cap;
    u32            NotifyOffMultiplier;
} VIRTIO_PCI_NOTIFY_CAP, *PVIRTIO_PCI_NOTIFY_CAP;

/* Common config region — MMIO-mapped via the COMMON_CFG cap. */
typedef struct _VIRTIO_PCI_COMMON_CFG {
    le32 DeviceFeatureSelect;   /* r/w window into 64+ bit feature space */
    le32 DeviceFeature;         /* r/o */
    le32 GuestFeatureSelect;    /* r/w */
    le32 GuestFeature;          /* r/w */
    le16 MsixConfig;            /* set to VIRTIO_MSI_NO_VECTOR */
    le16 NumQueues;             /* r/o */
    u8   DeviceStatus;          /* r/w */
    u8   ConfigGeneration;      /* r/o */

    /* Queue regs — selected by QueueSelect, then read/write the rest */
    le16 QueueSelect;           /* r/w */
    le16 QueueSize;             /* r/w (driver may shrink) */
    le16 QueueMsixVector;       /* set to VIRTIO_MSI_NO_VECTOR */
    le16 QueueEnable;           /* r/w (1 once driver has set up) */
    le16 QueueNotifyOff;        /* r/o, multiplied by NotifyOffMultiplier */
    le64 QueueDesc;             /* r/w guest paddr of descriptor ring */
    le64 QueueDriver;           /* r/w guest paddr of avail ring */
    le64 QueueDevice;           /* r/w guest paddr of used ring */
} VIRTIO_PCI_COMMON_CFG, *PVIRTIO_PCI_COMMON_CFG;

#include <poppack.h>
/* poppack.h sets pack(2) in NT 3.5's SDK (pre-push/pop). Reset to
   /Zp8 default so VIRTIO_PCI_DEV / VIRTIO_PCI_BAR_MAP below match
   callers' layout. See virtio.h for the wider story. */
#pragma pack()

/* ------------------------------------------------------------------ *
 * Per-device wrapper. Embeds the generic VIRTIO_DEV; each driver
 * (viorng/vioser/etc.) allocates one of these, calls VirtioPciInit,
 * then drives via the standard Cops API.
 *
 * MMIO mappings — each BAR may carry one or more virtio regions; we
 * map each used BAR exactly once on probe and store the kernel VA
 * for unmapping at teardown.
 * ------------------------------------------------------------------ */
#define VIRTIO_PCI_NUM_BARS  6

typedef struct _VIRTIO_PCI_BAR_MAP {
    PUCHAR           VirtAddr;     /* MmMapIoSpace base, NULL if unused */
    PHYSICAL_ADDRESS PhysAddr;
    ULONG            Length;
} VIRTIO_PCI_BAR_MAP, *PVIRTIO_PCI_BAR_MAP;

typedef struct _VIRTIO_PCI_DEV {
    VIRTIO_DEV              Vdev;

    /* PCI location — needed for cap walking + diagnostic logs. */
    ULONG                   BusNumber;
    ULONG                   SlotNumber;

    /* Resource info from HalAssignSlotResources. */
    ULONG                   InterruptVector;
    KIRQL                   InterruptLevel;
    KAFFINITY               InterruptAffinity;

    /* Mapped MMIO BARs (only used ones are non-NULL). */
    VIRTIO_PCI_BAR_MAP      Bars[VIRTIO_PCI_NUM_BARS];

    /* Pointers into the mapped BARs at the per-region offsets the
       capability list told us. */
    PVIRTIO_PCI_COMMON_CFG  Common;       /* COMMON_CFG region */
    PUCHAR                  Notify;       /* NOTIFY_CFG region base */
    ULONG                   NotifyOffMul; /* multiplier (from NOTIFY cap tail) */
    PUCHAR                  Isr;          /* ISR byte */
    PUCHAR                  DeviceCfg;    /* device-specific config region */
    ULONG                   DeviceCfgLen;
} VIRTIO_PCI_DEV, *PVIRTIO_PCI_DEV;

#define VIRTIO_TO_PCI(vdev) \
    VIRTIO_CONTAINER_OF(vdev, VIRTIO_PCI_DEV, Vdev)

/* ------------------------------------------------------------------ *
 * Public API. The driver collects PCI bus + slot + IRQ resources from
 * HalGetBusDataByOffset / HalAssignSlotResources / HalGetInterrupt-
 * Vector itself, then hands them to VirtioPciInit. We:
 *   - read the PCI BARs from the device's config space
 *   - walk the cap list, find the four config regions we care about
 *   - MmMapIoSpace each used BAR
 *   - wire vpdev->{Common, Notify, Isr, DeviceCfg}
 *   - install the modern Cops vtable
 *
 * After return: vpdev is ready for VirtioDevReset → DRIVER → FEATURES_OK
 * → queue setup → DRIVER_OK exactly like the legacy path.
 * ------------------------------------------------------------------ */
NTSTATUS
VirtioPciInit(
    PVIRTIO_PCI_DEV vpdev,
    ULONG           bus_number,
    ULONG           slot_number,
    ULONG           interrupt_vector,
    KIRQL           interrupt_level,
    KAFFINITY       interrupt_affinity,
    u16             device_id          /* virtio device-class ID */
    );

/* Symmetric teardown — unmaps any MmMapIoSpace'd BARs. */
VOID VirtioPciCleanup(PVIRTIO_PCI_DEV vpdev);

/* Common transport ISR — call from the device driver's KSERVICE_ROUTINE.
   Reads ISR byte (clears on read) and dispatches the per-queue
   callbacks. Returns nonzero if the interrupt was for this device. */
int VirtioPciIsr(PVIRTIO_PCI_DEV vpdev);

#endif /* _VIRTIO_PCI_H_ */
