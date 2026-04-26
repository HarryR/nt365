/*++

    ring.c — Virtqueue create / enqueue / dequeue / interrupt-arm. The
    algorithm-heavy part of the virtio shared library.

    Adapted from Unikraft's drivers/virtio/ring/virtio_ring.c
    (BSD-3-Clause), itself derived from FreeBSD. The algorithms are
    identical to the spec; the OS surface is NT 3.5 native:

        uk_malloc        →  ExAllocatePoolWithTag(NonPagedPool, ..., 'iVrn')
        uk_posix_memalign + page-aligned ring
                         →  MmAllocateContiguousMemorySpecifyCache
        uk_paging_virt_to_phys
                         →  MmGetPhysicalAddress
        uk_arch_*mb      →  KeMemoryBarrier / _WriteBarrier / _ReadBarrier
        uk_pr_*          →  DbgPrint via VirtioDbg/VirtioErr macros
        UK_TAILQ_*       →  LIST_ENTRY + InsertTailList/RemoveEntryList
        struct uk_alloc *a parameter
                         →  removed; we always use NonPagedPool

    Scope: split-ring only (no packed ring). Event-idx feature is in
    the negotiation surface but disabled by default — VirtqFeature-
    Negotiate masks it out.

--*/

#include "virtio.h"
#include "vring.h"

/* ------------------------------------------------------------------ *
 * Per-descriptor side-table — virtio gives each descriptor an index
 * 0..(num-1); we keep the driver cookie + chain-length-on-enqueue here
 * so we can free the chain on dequeue. Allocated in one slab with the
 * VIRTQUEUE_INTERNAL header.
 * ------------------------------------------------------------------ */
typedef struct _VIRTQ_DESC_INFO {
    PVOID Cookie;
    u16   DescCount;
} VIRTQ_DESC_INFO, *PVIRTQ_DESC_INFO;

/* ------------------------------------------------------------------ *
 * Internal queue state. The public PVIRTQUEUE handle (defined in
 * virtio.h) is the address of .Vq inside this struct — VQ_TO_INT
 * goes back to the wrapper.
 * ------------------------------------------------------------------ */
typedef struct _VIRTQUEUE_INTERNAL VIRTQUEUE_INTERNAL, *PVIRTQUEUE_INTERNAL;

struct _VIRTQUEUE_INTERNAL {
    VIRTQUEUE          Vq;             /* exposed handle */
    VRING              Ring;
    PVOID              VringMem;       /* contiguous backing buffer */
    PHYSICAL_ADDRESS   VringPaddr;     /* phys addr of .desc[0] */
    ULONG             VringBytes;     /* allocation size — needed by Mm free */
    u16                DescAvail;      /* free descriptors available */
    u16                HeadFreeDesc;   /* head of the free-descriptor list */
    u16                LastUsedDescIdx;
    u8                 UsesEventIdx;   /* not used; left in for compat */
    u16                LastNotifiedIdx;
    /* Per-descriptor side-table follows immediately after this struct.
       Length is `Ring.Num` entries — sized at allocation time. */
    VIRTQ_DESC_INFO    DescInfo[1];
};

#define VQ_TO_INT(vq) \
    VIRTIO_CONTAINER_OF(vq, VIRTQUEUE_INTERNAL, Vq)

#define VIRTQUEUE_END_OF_LIST  0xFFFF  /* free-list terminator */

/* ------------------------------------------------------------------ *
 * Forward decls for static helpers.
 * ------------------------------------------------------------------ */
static VOID VringInitFreeList(PVIRTQUEUE_INTERNAL vqi, u16 num);
static VOID VringPushAvail(PVIRTQUEUE_INTERNAL vqi, u16 head_idx);
static VOID VringDetachChain(PVIRTQUEUE_INTERNAL vqi, u16 head_idx);
static int  VringNotifyEnabled(PVIRTQUEUE_INTERNAL vqi);

/* ------------------------------------------------------------------ *
 * Public API.
 * ------------------------------------------------------------------ */

VOID
VirtqIntrDisable(PVIRTQUEUE vq)
{
    PVIRTQUEUE_INTERNAL vqi;

    ASSERT(vq != NULL);
    vqi = VQ_TO_INT(vq);

    /* We don't negotiate VIRTIO_F_EVENT_IDX, so just set the flag bit. */
    vqi->Ring.Avail->Flags |= VRING_AVAIL_F_NO_INTERRUPT;
}

int
VirtqIntrEnable(PVIRTQUEUE vq)
{
    PVIRTQUEUE_INTERNAL vqi;

    ASSERT(vq != NULL);
    vqi = VQ_TO_INT(vq);

    if (VirtqHasData(vq)) {
        /* Already pending — caller should drain before re-arming. */
        return 1;
    }

    vqi->Ring.Avail->Flags &= (le16)(~VRING_AVAIL_F_NO_INTERRUPT);

    /* Per spec 3.2.2: enable + recheck to avoid missing an interrupt
       that fired between drain and arm. */
    VIRTIO_MB();
    if (VirtqHasData(vq)) {
        VirtqIntrDisable(vq);
        return 1;
    }
    return 0;
}

int
VirtqHasData(PVIRTQUEUE vq)
{
    PVIRTQUEUE_INTERNAL vqi;

    ASSERT(vq != NULL);
    vqi = VQ_TO_INT(vq);
    return (vqi->LastUsedDescIdx != vqi->Ring.Used->Idx);
}

int
VirtqIsFull(PVIRTQUEUE vq)
{
    PVIRTQUEUE_INTERNAL vqi;

    ASSERT(vq != NULL);
    vqi = VQ_TO_INT(vq);
    return (vqi->DescAvail == 0);
}

u64
VirtqFeatureNegotiate(u64 feature_set)
{
    u64 want;

    /* Mask down to the transport-defined low feature range plus the
       generic VIRTIO_F_VERSION_1 bit (which legacy doesn't actually
       care about — we keep it cleared for legacy peers anyway). */
    want = ((u64)1 << 28) - 1;       /* low 28 transport-feature bits */

    /* Don't enable EVENT_IDX or INDIRECT_DESC for now — keeps the
       enqueue/dequeue paths simple. Add support once a device needs them. */

    return feature_set & want;
}

VOID
VirtqHostNotify(PVIRTQUEUE vq)
{
    PVIRTQUEUE_INTERNAL vqi;

    ASSERT(vq != NULL);
    vqi = VQ_TO_INT(vq);

    /* Make sure the avail-ring writes are visible to the device before
       we kick. x86 has strong ordering for normal cacheable writes,
       but be explicit. */
    VIRTIO_MB();

    if (vq->NotifyHost && VringNotifyEnabled(vqi)) {
        vqi->LastNotifiedIdx = vqi->Ring.Avail->Idx;
        vq->NotifyHost(vq->Vdev, vq->QueueId);
    }
}

int
VirtqRingInterrupt(PVIRTQUEUE vq)
{
    ASSERT(vq != NULL);

    /* Spurious interrupt before the device made anything visible — the
       transport layer already acknowledged the ISR; just bail. */
    if (!VirtqHasData(vq))
        return 1;

    return vq->Callback ? vq->Callback(vq, vq->Priv) : 1;
}

NTSTATUS
VirtqEnqueue(
    PVIRTQUEUE      vq,
    PVOID           cookie,
    PVIRTIO_SG_LIST sg,
    u16             read_bufs,
    u16             write_bufs
    )
{
    PVIRTQUEUE_INTERNAL vqi;
    u32 total;
    u16 head, idx, i;
    PVRING_DESC desc;

    ASSERT(vq != NULL);
    ASSERT(sg != NULL);

    vqi = VQ_TO_INT(vq);
    total = (u32)read_bufs + (u32)write_bufs;

    if (total < 1 || total > vqi->Ring.Num) {
        DbgPrint("VIRTIO ring: enqueue bad descriptor count %u\n", total);
        return STATUS_INVALID_PARAMETER;
    }
    if (vqi->DescAvail < total) {
        DbgPrint("VIRTIO ring: enqueue full (need %u have %u)\n",
                 total, vqi->DescAvail);
        return STATUS_DEVICE_BUSY;
    }
    if (sg->NumSegs < total) {
        DbgPrint("VIRTIO ring: enqueue sglist short (%u, need %u)\n",
                 sg->NumSegs, total);
        return STATUS_INVALID_PARAMETER;
    }

    head = vqi->HeadFreeDesc;
    vqi->DescInfo[head].Cookie    = cookie;
    vqi->DescInfo[head].DescCount = (u16)total;

    /* Fill the descriptor chain. read_bufs come first (device reads),
       then write_bufs (device writes — VRING_DESC_F_WRITE flagged). */
    idx = head;
    for (i = 0; i < total; i++) {
        desc = &vqi->Ring.Desc[idx];
        desc->Addr  = (le64)sg->Segs[i].Paddr.QuadPart;
        desc->Len   = (le32)sg->Segs[i].Len;
        desc->Flags = 0;
        if (i >= read_bufs)
            desc->Flags |= VRING_DESC_F_WRITE;
        if (i < total - 1)
            desc->Flags |= VRING_DESC_F_NEXT;
        idx = desc->Next;       /* pre-linked free-list pointer */
    }

    vqi->HeadFreeDesc = idx;
    vqi->DescAvail   -= (u16)total;

    VringPushAvail(vqi, head);
    return STATUS_SUCCESS;
}

NTSTATUS
VirtqDequeue(
    PVIRTQUEUE  vq,
    PVOID      *cookie,
    u32        *len
    )
{
    PVIRTQUEUE_INTERNAL vqi;
    u16 used_idx, head_idx;
    PVRING_USED_ELEM elem;

    ASSERT(vq != NULL);
    ASSERT(cookie != NULL);

    vqi = VQ_TO_INT(vq);
    if (!VirtqHasData(vq))
        return STATUS_NO_MORE_ENTRIES;

    used_idx = vqi->LastUsedDescIdx & (u16)(vqi->Ring.Num - 1);
    elem = &vqi->Ring.Used->Ring[used_idx];

    /* The device wrote the elem before bumping used.idx; we already
       saw the new idx via VirtqHasData(), now make sure we read the
       elem fields with up-to-date ordering. */
    VIRTIO_RMB();

    head_idx = (u16)elem->Id;
    if (len)
        *len = elem->Len;
    *cookie = vqi->DescInfo[head_idx].Cookie;
    vqi->DescInfo[head_idx].Cookie = NULL;

    VringDetachChain(vqi, head_idx);

    vqi->LastUsedDescIdx++;
    return STATUS_SUCCESS;
}

NTSTATUS
VirtqCreate(
    u16                queue_id,
    u16                nr_descs,
    u32                align,
    PVIRTQ_CALLBACK    callback,
    PVIRTQ_NOTIFY_HOST notify,
    PVIRTIO_DEV        vdev,
    PVIRTQUEUE        *out_vq
    )
{
    PVIRTQUEUE_INTERNAL vqi;
    ULONG              hdr_bytes;
    ULONG              ring_bytes;
    PHYSICAL_ADDRESS    high_paddr;

    ASSERT(out_vq != NULL);
    *out_vq = NULL;

    /* Power-of-two requirement on nr_descs (vring math depends on it). */
    if (nr_descs == 0 || (nr_descs & (nr_descs - 1)) != 0) {
        DbgPrint("VIRTIO ring: nr_descs %u not power of two\n", nr_descs);
        return STATUS_INVALID_PARAMETER;
    }

    /* (1) Allocate the queue header + DescInfo side-table from
       NonPagedPool. Side-table sizing: nr_descs entries trailing
       struct VIRTQUEUE_INTERNAL (which itself reserves [1]). */
    hdr_bytes = sizeof(VIRTQUEUE_INTERNAL) +
                (nr_descs - 1) * sizeof(VIRTQ_DESC_INFO);
    vqi = (PVIRTQUEUE_INTERNAL)ExAllocatePoolWithTag(
        NonPagedPool, hdr_bytes, VIRTIO_POOL_TAG);
    if (!vqi)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(vqi, hdr_bytes);

    /* (2) Allocate the vring backing memory: must be physically
       contiguous, page-aligned, and reachable by a 32-bit PFN
       (paddr >> 12 must fit in 32 bits — legacy virtio constraint).
       NT 3.5 has only the basic MmAllocateContiguousMemory; cache
       attribute and lowest-address controls weren't added until NT 5. */
    ring_bytes = VringSize(nr_descs, align);
    high_paddr.HighPart = 0;
    high_paddr.LowPart  = 0xFFFFFFFF;       /* < 4 GiB */
    vqi->VringMem = MmAllocateContiguousMemory(ring_bytes, high_paddr);
    if (!vqi->VringMem) {
        ExFreePool(vqi);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(vqi->VringMem, ring_bytes);
    vqi->VringBytes = ring_bytes;
    vqi->VringPaddr = MmGetPhysicalAddress(vqi->VringMem);

    /* (3) Initialise the ring view + free-descriptor chain. */
    VringInit(&vqi->Ring, nr_descs,
              (PUCHAR)vqi->VringMem, align);
    VringInitFreeList(vqi, nr_descs);

    /* (4) Wire the public handle. */
    vqi->Vq.Vdev       = vdev;
    vqi->Vq.QueueId    = queue_id;
    vqi->Vq.Callback   = callback;
    vqi->Vq.NotifyHost = notify;
    InitializeListHead(&vqi->Vq.QueueLink);

    *out_vq = &vqi->Vq;
    return STATUS_SUCCESS;
}

VOID
VirtqDestroy(PVIRTQUEUE vq)
{
    PVIRTQUEUE_INTERNAL vqi;

    if (!vq)
        return;

    vqi = VQ_TO_INT(vq);
    if (vqi->VringMem) {
        MmFreeContiguousMemory(vqi->VringMem);
    }
    ExFreePool(vqi);
}

PHYSICAL_ADDRESS
VirtqGetRingPaddr(PVIRTQUEUE vq)
{
    PVIRTQUEUE_INTERNAL vqi;

    ASSERT(vq != NULL);
    vqi = VQ_TO_INT(vq);
    return vqi->VringPaddr;
}

PHYSICAL_ADDRESS
VirtqGetAvailPaddr(PVIRTQUEUE vq)
{
    PVIRTQUEUE_INTERNAL vqi;
    PHYSICAL_ADDRESS    p;

    ASSERT(vq != NULL);
    vqi = VQ_TO_INT(vq);
    p.QuadPart = vqi->VringPaddr.QuadPart +
                 ((PUCHAR)vqi->Ring.Avail - (PUCHAR)vqi->Ring.Desc);
    return p;
}

PHYSICAL_ADDRESS
VirtqGetUsedPaddr(PVIRTQUEUE vq)
{
    PVIRTQUEUE_INTERNAL vqi;
    PHYSICAL_ADDRESS    p;

    ASSERT(vq != NULL);
    vqi = VQ_TO_INT(vq);
    p.QuadPart = vqi->VringPaddr.QuadPart +
                 ((PUCHAR)vqi->Ring.Used - (PUCHAR)vqi->Ring.Desc);
    return p;
}

/* ------------------------------------------------------------------ *
 * Internal helpers.
 * ------------------------------------------------------------------ */

/* Chain all descriptors into a free-list via .Next. Last entry's .Next
   is set to VIRTQUEUE_END_OF_LIST so we crash visibly if we exhaust
   without checking DescAvail first. */
static VOID
VringInitFreeList(PVIRTQUEUE_INTERNAL vqi, u16 num)
{
    u16 i;

    vqi->DescAvail       = num;
    vqi->HeadFreeDesc    = 0;
    vqi->LastUsedDescIdx = 0;

    for (i = 0; i < num - 1; i++)
        vqi->Ring.Desc[i].Next = (le16)(i + 1);
    vqi->Ring.Desc[num - 1].Next = VIRTQUEUE_END_OF_LIST;
}

/* Append head_idx to the avail ring + bump avail.idx. The spec is
   strict about the order: write into avail.ring[]/use a write barrier
   THEN bump avail.idx — otherwise the device may see an old descriptor
   index for the new idx. */
static VOID
VringPushAvail(PVIRTQUEUE_INTERNAL vqi, u16 head_idx)
{
    u16 avail_idx;

    avail_idx = vqi->Ring.Avail->Idx & (u16)(vqi->Ring.Num - 1);
    vqi->Ring.Avail->Ring[avail_idx] = head_idx;
    VIRTIO_WMB();
    vqi->Ring.Avail->Idx++;
}

/* Walk a chain starting at head_idx, returning all its descriptors
   to the free list. */
static VOID
VringDetachChain(PVIRTQUEUE_INTERNAL vqi, u16 head_idx)
{
    PVRING_DESC      desc;
    PVIRTQ_DESC_INFO info;

    desc = &vqi->Ring.Desc[head_idx];
    info = &vqi->DescInfo[head_idx];
    vqi->DescAvail = (u16)(vqi->DescAvail + info->DescCount);
    info->DescCount--;
    while (desc->Flags & VRING_DESC_F_NEXT) {
        desc = &vqi->Ring.Desc[desc->Next];
        info->DescCount--;
    }
    ASSERT(info->DescCount == 0);

    /* Push chain on top of free-list. */
    desc->Next = vqi->HeadFreeDesc;
    vqi->HeadFreeDesc = head_idx;
}

/* Should we kick the device after pushing avail? With EVENT_IDX off,
   we honour the simple VRING_USED_F_NO_NOTIFY flag the device sets. */
static int
VringNotifyEnabled(PVIRTQUEUE_INTERNAL vqi)
{
    return ((vqi->Ring.Used->Flags & VRING_USED_F_NO_NOTIFY) == 0);
}
