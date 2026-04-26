/*++

    vring.h — Virtqueue ring layout (descriptors + avail ring + used ring).
    Spec-defined; same on every virtio transport. Adapted verbatim from
    Linux/Unikraft (BSD-3-Clause).

    The ring is one contiguous chunk of guest physical memory:

        struct vring_desc  desc[num];          // 16 bytes each
        u16                avail.flags;
        u16                avail.idx;
        u16                avail.ring[num];
        u16                used_event_idx;     // only if VIRTIO_F_EVENT_IDX
        char               pad[ to align ];
        u16                used.flags;
        u16                used.idx;
        struct vring_used_elem used.ring[num]; // 8 bytes each
        u16                avail_event_idx;    // only if VIRTIO_F_EVENT_IDX

    `num` is a power of two; PCI requires alignment of 4096 between the
    avail-ring-end and the used-ring-start.

--*/

#ifndef _VRING_H_
#define _VRING_H_

#include "virtio.h"

/* Descriptor flags */
#define VRING_DESC_F_NEXT       1     /* chain continues via .next */
#define VRING_DESC_F_WRITE      2     /* device writes (vs reads) this */
#define VRING_DESC_F_INDIRECT   4     /* points at a table of more descs */

/* Avail ring flags: don't interrupt me when consuming buffers. */
#define VRING_AVAIL_F_NO_INTERRUPT  1

/* Used ring flags: don't kick me when posting buffers. */
#define VRING_USED_F_NO_NOTIFY      1

/* ------------------------------------------------------------------ *
 * Wire-format ring structures. All little-endian (virtio spec, x86).
 * #pragma pack(1) — virtio specifies these are byte-packed wire layout.
 * ------------------------------------------------------------------ */
#include <pshpack1.h>

typedef struct _VRING_DESC {
    le64 Addr;       /* guest physical */
    le32 Len;
    le16 Flags;
    le16 Next;
} VRING_DESC, *PVRING_DESC;

typedef struct _VRING_AVAIL {
    le16 Flags;
    le16 Idx;
    le16 Ring[1];    /* [num], plus optional used_event after */
} VRING_AVAIL, *PVRING_AVAIL;

typedef struct _VRING_USED_ELEM {
    le32 Id;         /* head-of-chain descriptor index */
    le32 Len;        /* total bytes written by device */
} VRING_USED_ELEM, *PVRING_USED_ELEM;

typedef struct _VRING_USED {
    le16            Flags;
    le16            Idx;
    VRING_USED_ELEM Ring[1]; /* [num] */
} VRING_USED, *PVRING_USED;

#include <poppack.h>
/* poppack.h sets pack(2) in NT 3.5's SDK (pre-push/pop). Reset to
   /Zp8 default so VRING below matches caller layout. See virtio.h
   for the wider story. */
#pragma pack()

/* Logical ring view: pointers into the contiguous ring allocation. */
typedef struct _VRING {
    unsigned int  Num;
    PVRING_DESC   Desc;
    PVRING_AVAIL  Avail;
    PVRING_USED   Used;
} VRING, *PVRING;

/* ------------------------------------------------------------------ *
 * Layout helpers.
 * ------------------------------------------------------------------ */

/* Total bytes for a ring of `num` descriptors with `align` between
   the avail block and the used block. PCI uses align=4096. */
__inline unsigned int
VringSize(unsigned int num, unsigned long align)
{
    unsigned int size;

    size = num * sizeof(VRING_DESC);
    size += sizeof(VRING_AVAIL) + (num * sizeof(le16)) + sizeof(le16);
    size = (size + align - 1) & ~(align - 1);
    size += sizeof(VRING_USED) + (num * sizeof(VRING_USED_ELEM)) + sizeof(le16);
    return size;
}

/* Initialise a VRING from a contiguous allocation `p` of size VringSize().
   Bumps the avail/used pointers to the right offsets within `p`. */
__inline VOID
VringInit(PVRING vr, unsigned int num, PUCHAR p, unsigned long align)
{
    vr->Num   = num;
    vr->Desc  = (PVRING_DESC) p;
    vr->Avail = (PVRING_AVAIL)(p + num * sizeof(VRING_DESC));
    /* NT 3.5 has no ULONG_PTR — we're 32-bit only, ULONG suffices for
       the pointer-as-int arithmetic. */
    vr->Used  = (PVRING_USED)
        (((ULONG)&vr->Avail->Ring[num] + sizeof(le16) + align - 1)
         & ~((ULONG)align - 1));
}

/* Event-idx accessors (only valid when VIRTIO_F_EVENT_IDX negotiated). */
#define VRING_USED_EVENT(vr)  ((vr)->Avail->Ring[(vr)->Num])
#define VRING_AVAIL_EVENT(vr) (*(le16 *)&(vr)->Used->Ring[(vr)->Num])

#endif /* _VRING_H_ */
