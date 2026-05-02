/*++

    vionet.c — virtio-net NDIS 3.0 miniport for MicroNT.

    NDIS framework glue + virtio.lib for the queue/protocol work.
    tcpip.sys auto-binds when both are loaded.

    Two virtqueues:
        rxq (queue 0, device->driver) - device fills posted buffers
        txq (queue 1, driver->device) - driver posts to send

    Per-buffer wire format:
        struct virtio_net_hdr (12 bytes for legacy / 12 + 0 num_buffers
                               for modern when MRG_RXBUF disabled)
        Ethernet frame                 (14-byte L2 header + payload)

    Total per-buffer = 12 + 1518 = 1530 bytes (incl. 4-byte FCS slot,
    though virtio strips/inserts FCS itself - we work with 1514 max).

    NDIS 3 receive contract: DPC drains the ring, calls
    NdisMEthIndicateReceive once per frame with the entire frame as
    the lookahead. Protocol may then call our MPTransferData to copy
    into its own packet (memcpy in our case - frame is contiguous in
    a single virtio buffer). After the batch we call
    NdisMEthIndicateReceiveComplete, then re-post each buffer.

--*/

#include <ndis.h>
/* ndis.h uses pshpackN.h / poppack.h internally to set struct packing
   for its own wire-format types. The SDK's pshpack/poppack pair uses
   plain `#pragma pack(N)` not `pack(push,N)`, so a stray imbalance can
   leave packing != default after ndis.h returns. virtio.h's _VIRTIO_DEV
   layout is sensitive (Cops field offset shifts between pack(1) and
   pack(8)). Force pack back to default before pulling in virtio. */
#pragma pack()
#include "virtio.h"
#include "vio_pci.h"
#include "vio_ids.h"

/* ------------------------------------------------------------------ *
 * virtio-net wire format + feature bits.
 * ------------------------------------------------------------------ */

#pragma pack(push, 1)
typedef struct _VIRTIO_NET_HDR {
    UCHAR   Flags;
    UCHAR   GsoType;
    USHORT  HdrLen;
    USHORT  GsoSize;
    USHORT  CsumStart;
    USHORT  CsumOffset;
    USHORT  NumBuffers;     /* present when negotiated; we always set 1 */
} VIRTIO_NET_HDR, *PVIRTIO_NET_HDR;
#pragma pack(pop)

/* Device config (read-only after feature negotiate). */
#pragma pack(push, 1)
typedef struct _VIRTIO_NET_CONFIG {
    UCHAR   Mac[6];
    USHORT  Status;             /* 1 = LINK_UP */
    USHORT  MaxVqPairs;         /* only with VIRTIO_NET_F_MQ */
    USHORT  Mtu;                /* only with VIRTIO_NET_F_MTU */
} VIRTIO_NET_CONFIG, *PVIRTIO_NET_CONFIG;
#pragma pack(pop)

/* Feature bits we care about. NT 3.5's CL doesn't grok ULL, use ui64. */
#define VIRTIO_NET_F_MAC            (1ui64 <<  5)
#define VIRTIO_NET_F_STATUS         (1ui64 << 16)
#define VIRTIO_NET_F_MRG_RXBUF      (1ui64 << 15)

/* Network status bits. */
#define VIRTIO_NET_S_LINK_UP        0x0001

/* Queue indices. */
#define VIONET_Q_RX                 0
#define VIONET_Q_TX                 1
#define VIONET_NUM_VQS              2

/* Buffer pool sizes. RX is generous because tcpip can take time to
   process; TX is sized for typical bursts before completion. */
#define VIONET_RX_BUFS              64
#define VIONET_TX_BUFS              32

#define VIONET_FRAME_MAX            1514       /* Ethernet payload + L2 hdr */
#define VIONET_BUF_SIZE             (sizeof(VIRTIO_NET_HDR) + VIONET_FRAME_MAX)

/* Ethernet header size (used for the NDIS lookahead split). */
#define ETHER_HEADER_SIZE           14

/* ------------------------------------------------------------------ *
 * Per-adapter context.
 *
 * NDIS hands us back this pointer as MiniportAdapterContext on every
 * callback; we treat it like our DEVICE_OBJECT extension in the raw
 * IRP-style virtio drivers.
 * ------------------------------------------------------------------ */
typedef struct _VIONET_ADAPTER {
    VIRTIO_PCI_DEV    Pci;
    NDIS_HANDLE       MiniportHandle;        /* from MPInitialize */
    NDIS_HANDLE       WrapperConfigContext;  /* unused but stashed */

    NDIS_MINIPORT_INTERRUPT  Interrupt;
    BOOLEAN                  InterruptRegistered;
    BOOLEAN                  DriverUp;     /* TRUE once VirtioDevDriverUp ran */

    /* BUS-level IRQ saved at bring-up for NdisMRegisterInterrupt's
       internal HalGetInterruptVector translation — see comment at
       the call site. SYSTEM-level vec+irql live in adapter->Pci. */
    ULONG                    BusInterruptVector;
    KIRQL                    BusInterruptLevel;

    KSPIN_LOCK        Lock;        /* protects Tx free list, ring access */

    PVIRTQUEUE        RxQ;
    PVIRTQUEUE        TxQ;

    /* Rx buffer pool. Pre-posted to RxQ at init; refilled in the DPC
       after each frame is indicated. Cookie = buffer index. */
    PUCHAR            RxBufBase;
    PHYSICAL_ADDRESS  RxBufBasePaddr;
    ULONG             RxBufCount;

    /* Tx buffer pool. We copy the outgoing NDIS_PACKET into a TX
       buffer; the buffer index travels back to us via the used ring,
       at which point we call NdisMSendComplete on the saved packet. */
    PUCHAR            TxBufBase;
    PHYSICAL_ADDRESS  TxBufBasePaddr;
    ULONG             TxBufCount;
    PNDIS_PACKET     *TxPacket;       /* parallel array, size = TxBufCount */
    ULONG             TxFreeHead;     /* index of next free Tx buffer */
    ULONG             TxFreeCount;
    PULONG            TxFreeList;     /* small free-list ring of indices */

    /* Capabilities reported via OIDs. */
    UCHAR             CurrentAddr[6];
    UCHAR             PermanentAddr[6];
    ULONG             PacketFilter;
    USHORT            LinkStatus;     /* VIRTIO_NET_S_LINK_UP if up */
} VIONET_ADAPTER, *PVIONET_ADAPTER;

/* Address-of-buffer helpers. */
#define RxBufVa(a, i)     ((a)->RxBufBase + ((i) * VIONET_BUF_SIZE))
#define RxBufPaddr(a, i)  (((a)->RxBufBasePaddr.QuadPart) + ((i) * VIONET_BUF_SIZE))
#define TxBufVa(a, i)     ((a)->TxBufBase + ((i) * VIONET_BUF_SIZE))
#define TxBufPaddr(a, i)  (((a)->TxBufBasePaddr.QuadPart) + ((i) * VIONET_BUF_SIZE))

/* ------------------------------------------------------------------ *
 * Forward declarations of the NDIS Miniport callbacks.
 * ------------------------------------------------------------------ */
NDIS_STATUS DriverEntry(PVOID DriverObject, PVOID RegistryPath);

static NDIS_STATUS  MPInitialize        (PNDIS_STATUS OpenErrorStatus,
                                         PUINT SelectedMediumIndex,
                                         PNDIS_MEDIUM MediumArray,
                                         UINT MediumArraySize,
                                         NDIS_HANDLE MiniportAdapterHandle,
                                         NDIS_HANDLE WrapperConfigCtx);
static VOID         MPHalt              (NDIS_HANDLE Ctx);
static NDIS_STATUS  MPReset             (PBOOLEAN AddressingReset, NDIS_HANDLE Ctx);
static NDIS_STATUS  MPSend              (NDIS_HANDLE Ctx, PNDIS_PACKET Packet, UINT Flags);
static VOID         MPISR               (PBOOLEAN InterruptRecognized,
                                         PBOOLEAN QueueMiniportHandleInterrupt,
                                         NDIS_HANDLE Ctx);
static VOID         MPHandleInterrupt   (NDIS_HANDLE Ctx);
static VOID         MPDisableInterrupt  (NDIS_HANDLE Ctx);
static VOID         MPEnableInterrupt   (NDIS_HANDLE Ctx);
static BOOLEAN      MPCheckForHang      (NDIS_HANDLE Ctx);
static NDIS_STATUS  MPTransferData      (PNDIS_PACKET Packet, PUINT BytesTransferred,
                                         NDIS_HANDLE Ctx, NDIS_HANDLE RxCtx,
                                         UINT ByteOffset, UINT BytesToTransfer);
static NDIS_STATUS  MPQueryInformation  (NDIS_HANDLE Ctx, NDIS_OID Oid,
                                         PVOID Buf, ULONG BufLen,
                                         PULONG BytesWritten, PULONG BytesNeeded);
static NDIS_STATUS  MPSetInformation    (NDIS_HANDLE Ctx, NDIS_OID Oid,
                                         PVOID Buf, ULONG BufLen,
                                         PULONG BytesRead, PULONG BytesNeeded);

static NDIS_STATUS  VioNetFindAndAttach (PVIONET_ADAPTER adapter);
static VOID         VioNetReadDeviceConfig(PVIONET_ADAPTER adapter);
static NTSTATUS     VioNetPrepostRx     (PVIONET_ADAPTER adapter);
static VOID         VioNetDrainRx       (PVIONET_ADAPTER adapter);
static VOID         VioNetDrainTxComplete(PVIONET_ADAPTER adapter);
static VOID         VioNetTeardown      (PVIONET_ADAPTER adapter);

/* ------------------------------------------------------------------ *
 * DriverEntry — register characteristics, return.
 * ------------------------------------------------------------------ */
NDIS_STATUS
DriverEntry(PVOID DriverObject, PVOID RegistryPath)
{
    NDIS_HANDLE                     wrapperHandle;
    NDIS_MINIPORT_CHARACTERISTICS   chars;
    NDIS_STATUS                     status;

    DbgPrint("VIONET: DriverEntry\n");

    NdisInitializeWrapper(&wrapperHandle, DriverObject, RegistryPath, NULL);

    NdisZeroMemory(&chars, sizeof(chars));
    chars.MajorNdisVersion       = 3;
    chars.MinorNdisVersion       = 0;
    chars.CheckForHangHandler    = MPCheckForHang;
    chars.DisableInterruptHandler= MPDisableInterrupt;
    chars.EnableInterruptHandler = MPEnableInterrupt;
    chars.HaltHandler            = MPHalt;
    chars.HandleInterruptHandler = MPHandleInterrupt;
    chars.InitializeHandler      = MPInitialize;
    chars.ISRHandler             = MPISR;
    chars.QueryInformationHandler= MPQueryInformation;
    chars.ReconfigureHandler     = NULL;     /* no reconfig support */
    chars.ResetHandler           = MPReset;
    chars.SendHandler            = MPSend;
    chars.SetInformationHandler  = MPSetInformation;
    chars.TransferDataHandler    = MPTransferData;

    status = NdisMRegisterMiniport(wrapperHandle, &chars, sizeof(chars));
    if (status != NDIS_STATUS_SUCCESS) {
        DbgPrint("VIONET: NdisMRegisterMiniport failed 0x%08x\n", status);
        NdisTerminateWrapper(wrapperHandle, NULL);
        return status;
    }
    DbgPrint("VIONET: registered miniport (NDIS 3.0)\n");
    return NDIS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * MPInitialize — discover device, bring up virtio, allocate buffers,
 * connect interrupt. Called once per adapter (only one for us).
 * ------------------------------------------------------------------ */
static NDIS_STATUS
MPInitialize(PNDIS_STATUS OpenErrorStatus,
             PUINT SelectedMediumIndex,
             PNDIS_MEDIUM MediumArray,
             UINT MediumArraySize,
             NDIS_HANDLE MiniportAdapterHandle,
             NDIS_HANDLE WrapperConfigCtx)
{
    PVIONET_ADAPTER adapter;
    UINT            i;
    NDIS_STATUS     status;
    NTSTATUS        st;
    /* VpciVqsFind writes vq_size[0..num_vqs-1] - declare as array, not
       a single u16, otherwise the second write overruns the stack and
       corrupts adjacent locals. (Bug latent in viorng/vioser/vioinput
       too because their adjacent stack slot happens not to matter.) */
    u16             vqsize[VIONET_NUM_VQS];

    /* (1) Find Ndis802_3 in the medium array - that's the only one we support. */
    for (i = 0; i < MediumArraySize; i++) {
        if (MediumArray[i] == NdisMedium802_3) {
            *SelectedMediumIndex = i;
            break;
        }
    }
    if (i == MediumArraySize) {
        DbgPrint("VIONET: NdisMedium802_3 not in MediumArray\n");
        return NDIS_STATUS_UNSUPPORTED_MEDIA;
    }

    /* (2) Allocate adapter context. */
    {
        NDIS_PHYSICAL_ADDRESS highest;
        highest.QuadPart = (LONGLONG)-1;
        NdisAllocateMemory((PVOID *)&adapter, sizeof(VIONET_ADAPTER),
                           0, highest);
    }
    if (!adapter) {
        return NDIS_STATUS_RESOURCES;
    }
    NdisZeroMemory(adapter, sizeof(VIONET_ADAPTER));
    adapter->MiniportHandle       = MiniportAdapterHandle;
    adapter->WrapperConfigContext = WrapperConfigCtx;
    KeInitializeSpinLock(&adapter->Lock);

    /* (3) PCI walk + virtio bring-up. */
    st = VioNetFindAndAttach(adapter);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIONET: VioNetFindAndAttach failed 0x%08x\n", st);
        NdisFreeMemory(adapter, sizeof(*adapter), 0);
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    /* (4) Standard NDIS attribute set. NDIS 3 takes 4 args - the
           NDIS_ATTRIBUTE_* flag set landed in NDIS 4. Just pass
           BusMaster=TRUE here. */
    NdisMSetAttributes(adapter->MiniportHandle,
                       (NDIS_HANDLE)adapter,
                       TRUE,                /* BusMaster */
                       NdisInterfacePci);

    /* (5) Set up vqs. */
    st = VirtioFindVqs(&adapter->Pci.Vdev, VIONET_NUM_VQS, vqsize);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIONET: VirtioFindVqs failed 0x%08x\n", st);
        goto fail;
    }
    /* Use the smaller of rx/tx queue sizes; both should be the same
       on virtio-net but be safe. Cap at 64 for now (default is 256
       and we'd want to investigate virtio.lib's behaviour there). */
    {
        u16 sz = vqsize[0] < vqsize[1] ? vqsize[0] : vqsize[1];
        if (sz > 64) sz = 64;

        st = VirtioVqSetup(&adapter->Pci.Vdev, VIONET_Q_RX, sz, NULL,
                           &adapter->RxQ);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIONET: RxQ setup failed 0x%08x\n", st);
            goto fail;
        }
        adapter->RxQ->Priv = adapter;

        st = VirtioVqSetup(&adapter->Pci.Vdev, VIONET_Q_TX, sz, NULL,
                           &adapter->TxQ);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIONET: TxQ setup failed 0x%08x\n", st);
            goto fail;
        }
        adapter->TxQ->Priv = adapter;
    }

    /* (6) Allocate Rx + Tx buffer pools (NonPaged contiguous). */
    {
        ULONG bytes = VIONET_RX_BUFS * VIONET_BUF_SIZE;
        adapter->RxBufBase = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool, bytes, 'NoiV');
        if (!adapter->RxBufBase) goto fail_resources;
        adapter->RxBufBasePaddr = MmGetPhysicalAddress(adapter->RxBufBase);
        adapter->RxBufCount = VIONET_RX_BUFS;
    }
    {
        ULONG bytes = VIONET_TX_BUFS * VIONET_BUF_SIZE;
        adapter->TxBufBase = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool, bytes, 'NoiV');
        if (!adapter->TxBufBase) goto fail_resources;
        adapter->TxBufBasePaddr = MmGetPhysicalAddress(adapter->TxBufBase);
        adapter->TxBufCount = VIONET_TX_BUFS;
    }
    adapter->TxPacket = (PNDIS_PACKET *)ExAllocatePoolWithTag(
        NonPagedPool, VIONET_TX_BUFS * sizeof(PNDIS_PACKET), 'NoiV');
    adapter->TxFreeList = (PULONG)ExAllocatePoolWithTag(
        NonPagedPool, VIONET_TX_BUFS * sizeof(ULONG), 'NoiV');
    if (!adapter->TxPacket || !adapter->TxFreeList) goto fail_resources;
    NdisZeroMemory(adapter->TxPacket, VIONET_TX_BUFS * sizeof(PNDIS_PACKET));
    for (i = 0; i < VIONET_TX_BUFS; i++) {
        adapter->TxFreeList[i] = i;
    }
    adapter->TxFreeHead  = 0;
    adapter->TxFreeCount = VIONET_TX_BUFS;

    /* (7) Pre-post Rx buffers. */
    st = VioNetPrepostRx(adapter);
    if (!NT_SUCCESS(st)) goto fail;

    /* (8) Read MAC + link state from device config (post feature
           negotiation, so the fields are stable). */
    VioNetReadDeviceConfig(adapter);
    NdisMoveMemory(adapter->CurrentAddr, adapter->PermanentAddr, 6);

    /* (8b) virtio 1.0 §3.1.1 step 8: DRIVER_OK marks the device "live"
            — only after this can it generate INTx for used-ring
            updates. Set BEFORE NdisMRegisterInterrupt so NDIS' init
            CHECK_FOR_NORMAL_INTERRUPTS poll sees a hot device. */
    VirtioDevDriverUp(&adapter->Pci.Vdev);
    adapter->DriverUp = TRUE;
    VirtqHostNotify(adapter->RxQ);

    /* (8c) Pre-clear any stale INTx assertion before unmasking the IRQ.
            The device may have set its ISR Status during early
            bring-up activity (gratuitous ARP TX from tcpip, queue
            kicks, BIOS probes). In edge-mode PIC, an already-asserted
            line gives no inactive→active transition when we unmask,
            so no IRQ ever fires. Reading virtio's ISR Status register
            clears it on the device side and de-asserts INTx; the next
            genuine event then arrives as a fresh edge. NetKVM relies
            on MPISR doing this implicitly on every interrupt. */
    READ_REGISTER_UCHAR(adapter->Pci.Isr);

    /* (9) Connect interrupt. After this, the device may fire.
       NdisMRegisterInterrupt expects BUS-level vec/irql — it calls
       HalGetInterruptVector internally to translate to the system
       vector. Passing the already-translated system values causes
       a double conversion: NDIS computes a different (wrong)
       Vector and our KINTERRUPT lands in an unused IDT slot, so
       MPISR is never called. Use the bus-level values we saved
       from HalAssignSlotResources, NOT the system-level ones in
       adapter->Pci. (Direct IoConnectInterrupt callers — viorng,
       vioser, vioinput — DO pass system-level; only the NDIS
       wrapper does its own translation.) */
    status = NdisMRegisterInterrupt(&adapter->Interrupt,
                                    adapter->MiniportHandle,
                                    adapter->BusInterruptVector,
                                    adapter->BusInterruptLevel,
                                    TRUE,        /* RequestIsr  */
                                    TRUE,        /* SharedInterrupt */
                                    NdisInterruptLevelSensitive);
    if (status != NDIS_STATUS_SUCCESS) {
        DbgPrint("VIONET: NdisMRegisterInterrupt failed 0x%08x\n", status);
        goto fail;
    }
    adapter->InterruptRegistered = TRUE;

    DbgPrint("VIONET: ready, MAC=%02x:%02x:%02x:%02x:%02x:%02x link=%s\n",
             adapter->PermanentAddr[0], adapter->PermanentAddr[1],
             adapter->PermanentAddr[2], adapter->PermanentAddr[3],
             adapter->PermanentAddr[4], adapter->PermanentAddr[5],
             (adapter->LinkStatus & VIRTIO_NET_S_LINK_UP) ? "UP" : "DOWN");
    return NDIS_STATUS_SUCCESS;

fail_resources:
    status = NDIS_STATUS_RESOURCES;
fail:
    VioNetTeardown(adapter);
    return (status != NDIS_STATUS_SUCCESS) ? status : NDIS_STATUS_FAILURE;
}

/* ------------------------------------------------------------------ *
 * PCI bus walk + virtio init. Same shape as viorng/vioser/vioinput.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioNetFindAndAttach(PVIONET_ADAPTER adapter)
{
    ULONG          slot;
    PCI_COMMON_CONFIG cfg;
    ULONG          got;
    PCM_RESOURCE_LIST resources = NULL;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pd;
    ULONG          intVector = 0;
    KIRQL          intLevel  = 0;
    KAFFINITY      affinity  = 0;
    ULONG          i;
    NTSTATUS       st;
    USHORT         devid_modern   = VIRTIO_PCI_DEV_NET;        /* 0x1041 */
    USHORT         devid_classic  = VIRTIO_PCI_TRANS_NET;      /* 0x1000 */

    /* Find first 1AF4:1041 / 1AF4:1000 on bus 0. */
    for (slot = 0; slot < 32 * 8; slot++) {
        got = HalGetBusDataByOffset(PCIConfiguration, 0, slot,
                                    &cfg, 0, sizeof(cfg));
        if (got < 4)                              continue;
        if (cfg.VendorID == 0xFFFF)               continue;
        if (cfg.VendorID != VIRTIO_PCI_VENDOR_ID) continue;
        if (cfg.DeviceID != devid_modern &&
            cfg.DeviceID != devid_classic)        continue;
        DbgPrint("VIONET: matched virtio-net (devid 0x%04x) at bus0 slot 0x%02x\n",
                 cfg.DeviceID, slot);
        break;
    }
    if (slot >= 32 * 8) return STATUS_NO_SUCH_DEVICE;

    /* Slot resources via HAL (NDIS doesn't expose this API directly
       on NT 3.5). Same pattern viorng/vioser use. */
    {
        UNICODE_STRING regPath;
        RtlInitUnicodeString(&regPath,
            L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\vionet");
        st = HalAssignSlotResources(&regPath, NULL, NULL, NULL,
                                    PCIBus, 0, slot, &resources);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIONET: HalAssignSlotResources failed 0x%08x\n", st);
            return st;
        }
    }
    for (i = 0; i < resources->List[0].PartialResourceList.Count; i++) {
        pd = &resources->List[0].PartialResourceList.PartialDescriptors[i];
        if (pd->Type == CmResourceTypeInterrupt && intVector == 0) {
            intVector = pd->u.Interrupt.Vector;
            intLevel  = (KIRQL)pd->u.Interrupt.Level;
        }
    }
    ExFreePool(resources);
    if (!intVector) {
        DbgPrint("VIONET: missing IRQ resource\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    /* Save the bus-level IRQ for NdisMRegisterInterrupt. NDIS' wrapper
       calls HalGetInterruptVector internally to convert bus → system,
       so we MUST pass BUS-level values (= what HalAssignSlotResources
       returned). Drivers using IoConnectInterrupt directly (vioinput,
       viorng, vioser) pass SYSTEM-level values instead. */
    adapter->BusInterruptVector = intVector;
    adapter->BusInterruptLevel  = intLevel;

    {
        ULONG sysVector;
        KIRQL sysIrql = 0;
        sysVector = HalGetInterruptVector(PCIBus, 0, intLevel, intVector,
                                          &sysIrql, &affinity);
        DbgPrint("VIONET: bus IRQ %u/%u -> system vec=%u irql=%u affinity=0x%x\n",
                 intVector, intLevel, sysVector, sysIrql, (ULONG)affinity);
        intVector = sysVector;
        intLevel  = sysIrql;
    }

    /* virtio_pci modern transport bring-up. Stores SYSTEM-level vec+irql
       in adapter->Pci for other code paths. */
    st = VirtioPciInit(&adapter->Pci, 0, slot,
                       intVector, intLevel, affinity,
                       VIRTIO_ID_NET);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIONET: VirtioPciInit failed 0x%08x\n", st);
        return st;
    }

    VirtioDevReset(&adapter->Pci.Vdev);
    VirtioDevStatusUpdate(&adapter->Pci.Vdev, VIRTIO_STATUS_ACK);
    VirtioDevStatusUpdate(&adapter->Pci.Vdev,
                          VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    adapter->Pci.Vdev.Features = VirtioFeatureGet(&adapter->Pci.Vdev);
    /* Whitelist only the bits we actually implement: MAC (bit 5),
       STATUS (bit 20), VERSION_1 (bit 32). Everything else (MRG_RXBUF,
       all TSO/CSUM/CTRL/MQ, RING_PACKED, NOTIFICATION_DATA,
       ACCESS_PLATFORM, RING_RESET) must NOT be ack'd or the device
       will drive us in a mode we don't speak — e.g. RING_PACKED would
       silently break us because our split-ring code can't see packed
       used-ring entries. */
    adapter->Pci.Vdev.Features &=
        ((u64)1 << 5)  |       /* MAC    */
        ((u64)1 << 20) |       /* STATUS */
        ((u64)1 << 32);        /* VERSION_1 */
    VirtioFeatureSet(&adapter->Pci.Vdev);

    /* virtio 1.0 §3.1.1 step 5: set FEATURES_OK BEFORE any queue
       setup. Without this, modern devices accept queue programming
       but never consider the queue "armed" — they never produce
       used-ring entries or assert INTx. Step 6 then says re-read the
       status and confirm FEATURES_OK is still set; the device clears
       it if our feature subset is unacceptable. DRIVER_OK comes much
       later (after queues + RX buffers + interrupt registration). */
    VirtioDevStatusUpdate(&adapter->Pci.Vdev,
                          VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                          VIRTIO_STATUS_FEATURES_OK);
    if (!(VirtioDevStatusGet(&adapter->Pci.Vdev) &
          VIRTIO_STATUS_FEATURES_OK)) {
        DbgPrint("VIONET: device rejected our feature subset\n");
        return STATUS_DEVICE_NOT_READY;
    }

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * Read MAC + link status out of device config space.
 * ------------------------------------------------------------------ */
static VOID
VioNetReadDeviceConfig(PVIONET_ADAPTER adapter)
{
    UCHAR mac[6];
    USHORT status;

    /* Offset 0..5 is mac[6]; offset 6 is status (USHORT). */
    VirtioConfigGet(&adapter->Pci.Vdev, 0, mac, 6, 1);
    NdisMoveMemory(adapter->PermanentAddr, mac, 6);

    if (adapter->Pci.Vdev.Features & VIRTIO_NET_F_STATUS) {
        VirtioConfigGet(&adapter->Pci.Vdev, 6, &status, 2, 2);
        adapter->LinkStatus = status;
    } else {
        adapter->LinkStatus = VIRTIO_NET_S_LINK_UP;  /* assume up */
    }
}

/* ------------------------------------------------------------------ *
 * Pre-post all Rx buffers to the rxq. Cookie = buffer index.
 * Each buffer is described as one device-writable segment covering
 * the full vnet hdr + frame area.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioNetPrepostRx(PVIONET_ADAPTER adapter)
{
    VIRTIO_SG_SEG  seg;
    VIRTIO_SG_LIST sg;
    ULONG i;
    NTSTATUS st;

    sg.NumSegs = 1;
    sg.Segs    = &seg;
    seg.Len    = VIONET_BUF_SIZE;

    for (i = 0; i < adapter->RxBufCount; i++) {
        seg.Paddr.QuadPart = RxBufPaddr(adapter, i);
        /* Cookie carries the buffer index. Add 1 so cookie!=NULL even
           for slot 0 (some asserts rely on cookie!=NULL). Subtract on
           dequeue. */
        st = VirtqEnqueue(adapter->RxQ, (PVOID)(ULONG)(i + 1), &sg, 0, 1);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIONET: RxQ pre-post %u/%u failed 0x%08x\n",
                     i, adapter->RxBufCount, st);
            return st;
        }
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * NDIS callbacks: Halt / Reset / Send / ISR / DPC / OIDs.
 * ------------------------------------------------------------------ */

static VOID
MPHalt(NDIS_HANDLE Ctx)
{
    PVIONET_ADAPTER adapter = (PVIONET_ADAPTER)Ctx;
    DbgPrint("VIONET: MPHalt\n");
    VioNetTeardown(adapter);
}

static NDIS_STATUS
MPReset(PBOOLEAN AddressingReset, NDIS_HANDLE Ctx)
{
    UNREFERENCED_PARAMETER(Ctx);
    *AddressingReset = FALSE;
    /* No real reset for now - virtio devices don't typically need
       runtime reset. Returning SUCCESS lets NDIS proceed. */
    return NDIS_STATUS_SUCCESS;
}

static VOID
VioNetTeardown(PVIONET_ADAPTER adapter)
{
    if (!adapter) return;
    if (adapter->InterruptRegistered) {
        NdisMDeregisterInterrupt(&adapter->Interrupt);
        adapter->InterruptRegistered = FALSE;
    }
    /* Modern virtio: device reset (status←0) MUST precede queue release,
       since queue_enable is a one-shot latch and can't be cleared per
       queue without VIRTIO_F_RING_RESET. The reset clears it for all
       queues at once. */
    if (adapter->DriverUp) {
        VirtioDevReset(&adapter->Pci.Vdev);
        if (adapter->RxQ) VirtioVqRelease(&adapter->Pci.Vdev, adapter->RxQ);
        if (adapter->TxQ) VirtioVqRelease(&adapter->Pci.Vdev, adapter->TxQ);
    }
    if (adapter->RxBufBase)   ExFreePool(adapter->RxBufBase);
    if (adapter->TxBufBase)   ExFreePool(adapter->TxBufBase);
    if (adapter->TxPacket)    ExFreePool(adapter->TxPacket);
    if (adapter->TxFreeList)  ExFreePool(adapter->TxFreeList);
    NdisFreeMemory(adapter, sizeof(*adapter), 0);
}

/* ------------------------------------------------------------------ *
 * MPSend - copy the NDIS_PACKET into a TX buffer, enqueue, kick.
 *
 * Returning NDIS_STATUS_PENDING tells NDIS we'll complete via
 * NdisMSendComplete from the DPC.
 * ------------------------------------------------------------------ */
static NDIS_STATUS
MPSend(NDIS_HANDLE Ctx, PNDIS_PACKET Packet, UINT Flags)
{
    PVIONET_ADAPTER adapter = (PVIONET_ADAPTER)Ctx;
    KIRQL           irql;
    ULONG           idx;
    PUCHAR          buf;
    PVIRTIO_NET_HDR hdr;
    PUCHAR          frameDst;
    UINT            totalLen = 0;
    PNDIS_BUFFER    ndisBuf;
    UINT            bufCount, bufLen;
    PVOID           bufVa;
    VIRTIO_SG_SEG   seg;
    VIRTIO_SG_LIST  sg;
    NTSTATUS        st;

    UNREFERENCED_PARAMETER(Flags);

    KeAcquireSpinLock(&adapter->Lock, &irql);

    if (adapter->TxFreeCount == 0) {
        KeReleaseSpinLock(&adapter->Lock, irql);
        return NDIS_STATUS_RESOURCES;     /* drop / caller queues */
    }

    idx = adapter->TxFreeList[adapter->TxFreeHead];
    adapter->TxFreeHead = (adapter->TxFreeHead + 1) % adapter->TxBufCount;
    adapter->TxFreeCount--;

    buf      = TxBufVa(adapter, idx);
    hdr      = (PVIRTIO_NET_HDR)buf;
    frameDst = buf + sizeof(VIRTIO_NET_HDR);

    /* Zero the vnet header (no GSO, no checksum offload). NumBuffers
       is unused without MRG_RXBUF. */
    NdisZeroMemory(hdr, sizeof(VIRTIO_NET_HDR));

    /* Walk the NDIS_BUFFER chain and concatenate into the TX buffer. */
    NdisQueryPacket(Packet, NULL, &bufCount, &ndisBuf, &totalLen);
    if (totalLen > VIONET_FRAME_MAX) {
        adapter->TxFreeList[(adapter->TxFreeHead + adapter->TxFreeCount)
                            % adapter->TxBufCount] = idx;
        adapter->TxFreeCount++;
        KeReleaseSpinLock(&adapter->Lock, irql);
        return NDIS_STATUS_FAILURE;
    }
    {
        UINT off = 0;
        while (ndisBuf) {
            NdisQueryBuffer(ndisBuf, &bufVa, &bufLen);
            NdisMoveMemory(frameDst + off, bufVa, bufLen);
            off += bufLen;
            NdisGetNextBuffer(ndisBuf, &ndisBuf);
        }
    }

    /* Track the NDIS_PACKET so the DPC can complete it on TX done. */
    adapter->TxPacket[idx] = Packet;

    /* Enqueue: 1 read-segment (driver->device), 0 write-segments. */
    seg.Paddr.QuadPart = TxBufPaddr(adapter, idx);
    seg.Len            = sizeof(VIRTIO_NET_HDR) + totalLen;
    sg.NumSegs         = 1;
    sg.Segs            = &seg;

    st = VirtqEnqueue(adapter->TxQ, (PVOID)(ULONG)(idx + 1), &sg, 1, 0);
    if (!NT_SUCCESS(st)) {
        adapter->TxPacket[idx] = NULL;
        adapter->TxFreeList[(adapter->TxFreeHead + adapter->TxFreeCount)
                            % adapter->TxBufCount] = idx;
        adapter->TxFreeCount++;
        KeReleaseSpinLock(&adapter->Lock, irql);
        return NDIS_STATUS_FAILURE;
    }

    VirtqHostNotify(adapter->TxQ);
    KeReleaseSpinLock(&adapter->Lock, irql);
    return NDIS_STATUS_PENDING;
}

/* ------------------------------------------------------------------ *
 * Top-half ISR. Stays minimal: ack the device's ISR Status register
 * (which de-asserts INTx) and tell NDIS whether to schedule the DPC.
 * VirtioPciIsr returns non-zero when this device's ISR Status had a
 * bit set — i.e. the IRQ was ours. With shared IRQs the chain
 * dispatcher (KiChainedDispatch) walks every chained driver until
 * one returns TRUE for a level-mode IRQ; on edge-mode the chain
 * walks fully regardless. Either way, returning FALSE here keeps
 * us off the spurious-interrupt path.
 * ------------------------------------------------------------------ */
static VOID
MPISR(PBOOLEAN InterruptRecognized,
      PBOOLEAN QueueMiniportHandleInterrupt,
      NDIS_HANDLE Ctx)
{
    PVIONET_ADAPTER adapter = (PVIONET_ADAPTER)Ctx;
    int handled = VirtioPciIsr(&adapter->Pci);
    *InterruptRecognized           = (BOOLEAN)(handled != 0);
    *QueueMiniportHandleInterrupt  = (BOOLEAN)(handled != 0);
}

/* ------------------------------------------------------------------ *
 * MPHandleInterrupt (DPC). Drain TX completions, then RX. Indicate
 * received frames to NDIS.
 * ------------------------------------------------------------------ */
static VOID
MPHandleInterrupt(NDIS_HANDLE Ctx)
{
    PVIONET_ADAPTER adapter = (PVIONET_ADAPTER)Ctx;
    VioNetDrainTxComplete(adapter);
    VioNetDrainRx(adapter);
}

static VOID
VioNetDrainTxComplete(PVIONET_ADAPTER adapter)
{
    PVOID    cookie;
    u32      used_len;
    NTSTATUS st;
    ULONG    idx;
    PNDIS_PACKET pkt;
    KIRQL    irql;

    UNREFERENCED_PARAMETER(used_len);

    KeAcquireSpinLock(&adapter->Lock, &irql);
    for (;;) {
        st = VirtqDequeue(adapter->TxQ, &cookie, &used_len);
        if (!NT_SUCCESS(st)) break;
        if (!cookie) continue;
        idx = (ULONG)cookie - 1;
        if (idx >= adapter->TxBufCount) continue;
        pkt = adapter->TxPacket[idx];
        adapter->TxPacket[idx] = NULL;

        /* Return buffer to free list. */
        adapter->TxFreeList[(adapter->TxFreeHead + adapter->TxFreeCount)
                            % adapter->TxBufCount] = idx;
        adapter->TxFreeCount++;

        /* Send-complete callback runs outside the spinlock to
           avoid recursive acquisition if the protocol re-sends. */
        KeReleaseSpinLock(&adapter->Lock, irql);
        if (pkt) {
            NdisMSendComplete(adapter->MiniportHandle, pkt,
                              NDIS_STATUS_SUCCESS);
        }
        KeAcquireSpinLock(&adapter->Lock, &irql);
    }
    KeReleaseSpinLock(&adapter->Lock, irql);
}

static VOID
VioNetDrainRx(PVIONET_ADAPTER adapter)
{
    PVOID    cookie;
    u32      used_len;
    NTSTATUS st;
    ULONG    idx;
    PUCHAR   frameStart;
    UINT     frameLen;
    BOOLEAN  anyIndicated = FALSE;

    for (;;) {
        st = VirtqDequeue(adapter->RxQ, &cookie, &used_len);
        if (!NT_SUCCESS(st)) break;
        /* Cookie was stored as (i+1) so it's never NULL — see VioNetPrepostRx. */
        if (!cookie) continue;
        idx = (ULONG)cookie - 1;
        if (idx >= adapter->RxBufCount) continue;
        if (used_len < sizeof(VIRTIO_NET_HDR) + ETHER_HEADER_SIZE) {
            /* Runt - re-post and move on. */
            goto repost;
        }

        frameStart = RxBufVa(adapter, idx) + sizeof(VIRTIO_NET_HDR);
        frameLen   = used_len - sizeof(VIRTIO_NET_HDR);

        /* Indicate the entire frame as both header and lookahead.
           Protocol may pull MPTransferData for the rest of a partial
           lookahead - we just memcpy the whole thing. RxCtx (the
           cookie passed back to MPTransferData) is the buffer index
           encoded as a pointer. */
        NdisMEthIndicateReceive(adapter->MiniportHandle,
                                (NDIS_HANDLE)(ULONG)idx,
                                frameStart,
                                ETHER_HEADER_SIZE,
                                frameStart + ETHER_HEADER_SIZE,
                                frameLen   - ETHER_HEADER_SIZE,
                                frameLen   - ETHER_HEADER_SIZE);
        anyIndicated = TRUE;

repost:
        {
            VIRTIO_SG_SEG  seg;
            VIRTIO_SG_LIST sg;
            seg.Paddr.QuadPart = RxBufPaddr(adapter, idx);
            seg.Len            = VIONET_BUF_SIZE;
            sg.NumSegs         = 1;
            sg.Segs            = &seg;
            VirtqEnqueue(adapter->RxQ, (PVOID)(ULONG)(idx + 1), &sg, 0, 1);
        }
    }

    if (anyIndicated) {
        NdisMEthIndicateReceiveComplete(adapter->MiniportHandle);
        VirtqHostNotify(adapter->RxQ);
    }
}

/* ------------------------------------------------------------------ *
 * MPTransferData - protocol calls this to copy bytes from a partial-
 * indicated frame into its own NDIS_PACKET. RxCtx is our buffer
 * index (encoded as a pointer in NdisMEthIndicateReceive).
 * ------------------------------------------------------------------ */
static NDIS_STATUS
MPTransferData(PNDIS_PACKET Packet, PUINT BytesTransferred,
               NDIS_HANDLE Ctx, NDIS_HANDLE RxCtx,
               UINT ByteOffset, UINT BytesToTransfer)
{
    PVIONET_ADAPTER adapter = (PVIONET_ADAPTER)Ctx;
    ULONG           idx     = (ULONG)RxCtx;
    PUCHAR          src;
    PNDIS_BUFFER    ndisBuf;
    UINT            bufCount, totalLen;
    PVOID           bufVa;
    UINT            bufLen, copied = 0, want = BytesToTransfer;

    if (idx >= adapter->RxBufCount) {
        *BytesTransferred = 0;
        return NDIS_STATUS_FAILURE;
    }

    /* Frame body lives just past the vnet hdr in our scratch buffer.
       The protocol's offset is into the L3+ portion (after the L2
       header it already saw via NdisMEthIndicateReceive). */
    src = RxBufVa(adapter, idx) + sizeof(VIRTIO_NET_HDR)
        + ETHER_HEADER_SIZE + ByteOffset;

    NdisQueryPacket(Packet, NULL, &bufCount, &ndisBuf, &totalLen);
    while (ndisBuf && want) {
        NdisQueryBuffer(ndisBuf, &bufVa, &bufLen);
        if (bufLen > want) bufLen = want;
        NdisMoveMemory(bufVa, src + copied, bufLen);
        copied += bufLen;
        want   -= bufLen;
        NdisGetNextBuffer(ndisBuf, &ndisBuf);
    }
    *BytesTransferred = copied;
    return NDIS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * Disable / Enable interrupt. virtio's INTx is pin-based - clearing
 * PCI command-MEMORY isn't appropriate; the per-virtqueue NoNotify
 * flag and the device's MSIX vectors (which we don't use) are the
 * right knobs. The simplest correct thing: leave the device
 * unchanged. NDIS uses these around its own critical sections.
 * ------------------------------------------------------------------ */
static VOID  MPDisableInterrupt(NDIS_HANDLE Ctx) { UNREFERENCED_PARAMETER(Ctx); }
static VOID  MPEnableInterrupt (NDIS_HANDLE Ctx) { UNREFERENCED_PARAMETER(Ctx); }

static BOOLEAN
MPCheckForHang(NDIS_HANDLE Ctx)
{
    UNREFERENCED_PARAMETER(Ctx);
    return FALSE;   /* we never report hung */
}

/* ------------------------------------------------------------------ *
 * OID query / set. Just enough for tcpip.sys to bind.
 * ------------------------------------------------------------------ */
#define VIONET_VENDOR_ID        0x001AF400      /* OUI 1A:F4:00 */
#define VIONET_LINK_SPEED       1000000         /* 100 Mbit/s in 100bps units */
#define VIONET_MAX_LIST_SIZE    32

#define COPY_OID_OUT(_v) \
    do { ULONG _val = (ULONG)(_v);                                      \
         if (BufLen < sizeof(_val)) {                                   \
             *BytesNeeded = sizeof(_val); return NDIS_STATUS_INVALID_LENGTH; } \
         NdisMoveMemory(Buf, &_val, sizeof(_val));                      \
         *BytesWritten = sizeof(_val); return NDIS_STATUS_SUCCESS; } while (0)

static NDIS_STATUS
MPQueryInformation(NDIS_HANDLE Ctx, NDIS_OID Oid,
                   PVOID Buf, ULONG BufLen,
                   PULONG BytesWritten, PULONG BytesNeeded)
{
    PVIONET_ADAPTER adapter = (PVIONET_ADAPTER)Ctx;
    *BytesWritten = 0;
    *BytesNeeded  = 0;

    switch (Oid) {
        case OID_GEN_HARDWARE_STATUS:        COPY_OID_OUT(NdisHardwareStatusReady);
        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:           COPY_OID_OUT(NdisMedium802_3);
        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_CURRENT_LOOKAHEAD:      COPY_OID_OUT(VIONET_FRAME_MAX);
        case OID_GEN_MAXIMUM_FRAME_SIZE:     COPY_OID_OUT(VIONET_FRAME_MAX - ETHER_HEADER_SIZE);
        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:     COPY_OID_OUT(VIONET_FRAME_MAX);
        case OID_GEN_TRANSMIT_BUFFER_SPACE:  COPY_OID_OUT(VIONET_TX_BUFS * VIONET_BUF_SIZE);
        case OID_GEN_RECEIVE_BUFFER_SPACE:   COPY_OID_OUT(VIONET_RX_BUFS * VIONET_BUF_SIZE);
        case OID_GEN_VENDOR_ID:              COPY_OID_OUT(VIONET_VENDOR_ID);
        case OID_GEN_LINK_SPEED:             COPY_OID_OUT(VIONET_LINK_SPEED);
        case OID_GEN_MAC_OPTIONS:
            COPY_OID_OUT(NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_RECEIVE_SERIALIZED |
                         NDIS_MAC_OPTION_NO_LOOPBACK);
        case OID_GEN_DRIVER_VERSION:         COPY_OID_OUT(0x0300);  /* NDIS 3.0 */
        case OID_GEN_CURRENT_PACKET_FILTER:  COPY_OID_OUT(adapter->PacketFilter);
        case OID_802_3_MAXIMUM_LIST_SIZE:    COPY_OID_OUT(VIONET_MAX_LIST_SIZE);

        case OID_GEN_VENDOR_DESCRIPTION:
            if (BufLen < 16) { *BytesNeeded = 16; return NDIS_STATUS_INVALID_LENGTH; }
            NdisMoveMemory(Buf, "MicroNT virtnet", 16);
            *BytesWritten = 16;
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_PERMANENT_ADDRESS:
        case OID_802_3_CURRENT_ADDRESS:
            if (BufLen < 6) { *BytesNeeded = 6; return NDIS_STATUS_INVALID_LENGTH; }
            NdisMoveMemory(Buf,
                           (Oid == OID_802_3_PERMANENT_ADDRESS)
                               ? adapter->PermanentAddr
                               : adapter->CurrentAddr,
                           6);
            *BytesWritten = 6;
            return NDIS_STATUS_SUCCESS;

        /* Statistics - return 0 for now. tcpip uses these for ipconfig. */
        case OID_GEN_XMIT_OK:
        case OID_GEN_RCV_OK:
        case OID_GEN_XMIT_ERROR:
        case OID_GEN_RCV_ERROR:
        case OID_GEN_RCV_NO_BUFFER:
        case OID_802_3_RCV_ERROR_ALIGNMENT:
        case OID_802_3_XMIT_ONE_COLLISION:
        case OID_802_3_XMIT_MORE_COLLISIONS:
            COPY_OID_OUT(0);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static NDIS_STATUS
MPSetInformation(NDIS_HANDLE Ctx, NDIS_OID Oid,
                 PVOID Buf, ULONG BufLen,
                 PULONG BytesRead, PULONG BytesNeeded)
{
    PVIONET_ADAPTER adapter = (PVIONET_ADAPTER)Ctx;
    *BytesRead   = 0;
    *BytesNeeded = 0;

    switch (Oid) {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (BufLen < sizeof(ULONG)) {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            NdisMoveMemory(&adapter->PacketFilter, Buf, sizeof(ULONG));
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            /* Accept silently - we always indicate the full frame. */
            if (BufLen < sizeof(ULONG)) {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_MULTICAST_LIST:
            /* No multicast filter on the device side yet; accept the
               list but rely on the protocol's promiscuous fallback or
               software filter. */
            *BytesRead = BufLen;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}
