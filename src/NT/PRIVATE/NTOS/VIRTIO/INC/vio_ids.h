/*++

    vio_ids.h — Device-class IDs from the virtio specification.
    Adapted from Unikraft (BSD-3-Clause) which adapted from Linux's
    include/uapi/linux/virtio_ids.h.

--*/

#ifndef _VIRTIO_IDS_H_
#define _VIRTIO_IDS_H_

#define VIRTIO_ID_INVALID        0
#define VIRTIO_ID_NET            1
#define VIRTIO_ID_BLOCK          2
#define VIRTIO_ID_CONSOLE        3
#define VIRTIO_ID_RNG            4
#define VIRTIO_ID_BALLOON        5
#define VIRTIO_ID_RPMSG          7
#define VIRTIO_ID_SCSI           8
#define VIRTIO_ID_9P             9
#define VIRTIO_ID_GPU            16
#define VIRTIO_ID_INPUT          18
#define VIRTIO_ID_VSOCK          19
#define VIRTIO_ID_FS             26

/* PCI vendor 0x1AF4 (Red Hat). Two device-ID ranges exist:
   - Modern (post-virtio-1.0): uniformly 0x1040 + virtio_class_id.
     The only option for modern-only classes (input, gpu, vsock, fs).
   - Legacy/transitional: 0x1000..0x103F, arbitrary per-class
     assignments. QEMU's default for the "classic" device classes is
     TRANSITIONAL, which advertises the legacy ID at the PCI level
     but ALSO exposes modern caps in config space. We match either
     ID and always drive via modern transport (the caps are there
     either way), so we work against both QEMU defaults and explicit
     `disable-legacy=on`. */
#define VIRTIO_PCI_VENDOR_ID         0x1AF4

/* Modern (preferred) IDs. */
#define VIRTIO_PCI_DEV_NET           (0x1040 + VIRTIO_ID_NET)      /* 0x1041 */
#define VIRTIO_PCI_DEV_BLOCK         (0x1040 + VIRTIO_ID_BLOCK)    /* 0x1042 */
#define VIRTIO_PCI_DEV_CONSOLE       (0x1040 + VIRTIO_ID_CONSOLE)  /* 0x1043 */
#define VIRTIO_PCI_DEV_RNG           (0x1040 + VIRTIO_ID_RNG)      /* 0x1044 */
#define VIRTIO_PCI_DEV_BALLOON       (0x1040 + VIRTIO_ID_BALLOON)  /* 0x1045 */
#define VIRTIO_PCI_DEV_SCSI          (0x1040 + VIRTIO_ID_SCSI)     /* 0x1048 */
#define VIRTIO_PCI_DEV_9P            (0x1040 + VIRTIO_ID_9P)       /* 0x1049 */
#define VIRTIO_PCI_DEV_GPU           (0x1040 + VIRTIO_ID_GPU)      /* 0x1050 */
#define VIRTIO_PCI_DEV_INPUT         (0x1040 + VIRTIO_ID_INPUT)    /* 0x1052 */
#define VIRTIO_PCI_DEV_VSOCK         (0x1040 + VIRTIO_ID_VSOCK)    /* 0x1053 */
#define VIRTIO_PCI_DEV_FS            (0x1040 + VIRTIO_ID_FS)       /* 0x105A */

/* Transitional IDs (= what QEMU's default exposes for these classes;
   match these too so we work without `disable-legacy=on`). */
#define VIRTIO_PCI_TRANS_NET         0x1000
#define VIRTIO_PCI_TRANS_BLOCK       0x1001
#define VIRTIO_PCI_TRANS_BALLOON     0x1002
#define VIRTIO_PCI_TRANS_CONSOLE     0x1003
#define VIRTIO_PCI_TRANS_SCSI        0x1004
#define VIRTIO_PCI_TRANS_RNG         0x1005
#define VIRTIO_PCI_TRANS_9P          0x1009

#endif /* _VIRTIO_IDS_H_ */
