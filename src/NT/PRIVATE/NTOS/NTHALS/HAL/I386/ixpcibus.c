/*
 * ixpcibus.c — PCI bus handler for MicroNT HAL
 *
 * Direct PCI Type 1 config space access via CF8/CFC ports.
 * Bypasses ARC firmware configuration tree — probes hardware directly.
 *
 * Based on NT 3.5 HALX86 IXPCIBUS.C (Microsoft, Ken Reneris 1994).
 */

#include "halp.h"
#include "pci.h"
#include "pcip.h"

/* Spinlock functions from ntoskrnl */
KIRQL FASTCALL KfAcquireSpinLock(PKSPIN_LOCK SpinLock);
VOID FASTCALL KfReleaseSpinLock(PKSPIN_LOCK SpinLock, KIRQL OldIrql);
#define KeAcquireSpinLock(a,b) *(b) = KfAcquireSpinLock(a)
#define KeReleaseSpinLock(a,b) KfReleaseSpinLock(a,b)

/* PCI Type 1 config ports */
#define PCI_TYPE1_ADDR_PORT     ((PULONG) 0xCF8)
#define PCI_TYPE1_DATA_PORT     0xCFC

/* Spinlock for PCI config access serialization */
static KSPIN_LOCK HalpPCIConfigLock;

/* Config I/O function type: read or write one unit at Offset */
typedef ULONG (*FncConfigIO) (
    IN PPCIBUSDATA      BusData,
    IN PPCI_TYPE1_CFG_BITS PciCfg1,
    IN PUCHAR           Buffer,
    IN ULONG            Offset
    );

/* Dispatch table indexed by PCIDeref[Offset%4][Length%4] */
static UCHAR PCIDeref[4][4] = {
    { 0, 1, 2, 2 },
    { 1, 1, 1, 1 },
    { 2, 1, 2, 2 },
    { 1, 1, 1, 1 }
};

ULONG HalpGetPCIData (
    IN PBUSHANDLER BusHandler,
    IN PVOID RootHandler,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    );

ULONG HalpSetPCIData (
    IN PBUSHANDLER BusHandler,
    IN PVOID RootHandler,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    );

NTSTATUS HalpAssignPCISlotResources (
    IN PVOID BusHandler,
    IN PVOID RootHandler,
    IN PUNICODE_STRING RegistryPath,
    IN PUNICODE_STRING DriverClassName OPTIONAL,
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT DeviceObject OPTIONAL,
    IN ULONG SlotNumber,
    IN OUT PCM_RESOURCE_LIST *AllocatedResources
    );


/*
 * HalpInitializePciBus — probe for PCI Type 1 config access and
 * register bus handler(s). Called from HalReportResourceUsage.
 */
VOID
HalpInitializePciBus(VOID)
{
    PBUSHANDLER Bus;
    PPCIBUSDATA BusData;
    ULONG VendorId;
    ULONG MaxBus;
    ULONG i;

    KeInitializeSpinLock(&HalpPCIConfigLock);

    /* Probe: write to CF8, read back to verify Type 1 access works */
    WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, 0x80000000);
    if (READ_PORT_ULONG(PCI_TYPE1_ADDR_PORT) != 0x80000000) {
        DbgPrint("HAL: PCI Type 1 config access not available\n");
        return;
    }
    WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, 0);

    /* Check bus 0 device 0 vendor ID */
    WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, 0x80000000);
    VendorId = READ_PORT_ULONG((PULONG)PCI_TYPE1_DATA_PORT) & 0xFFFF;
    if (VendorId == 0xFFFF || VendorId == 0) {
        DbgPrint("HAL: PCI bus 0 device 0 not present (vendor=%04lx)\n", VendorId);
        return;
    }

    DbgPrint("HAL: PCI Type 1 detected, bus 0 device 0 vendor=%04lx\n", VendorId);

    /* Scan for max bus number by checking bus N device 0.
     * Q35 typically has bus 0 only (unless PCI bridges exist). */
    MaxBus = 0;
    for (i = 1; i < 256; i++) {
        PCI_TYPE1_CFG_BITS cfg;
        ULONG vid;
        cfg.u.AsULONG = 0;
        cfg.u.bits.BusNumber = i;
        cfg.u.bits.Enable = 1;
        WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, cfg.u.AsULONG);
        vid = READ_PORT_ULONG((PULONG)PCI_TYPE1_DATA_PORT) & 0xFFFF;
        if (vid != 0xFFFF && vid != 0) {
            MaxBus = i;
        }
    }
    WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, 0);

    DbgPrint("HAL: PCI max bus = %d\n", MaxBus);

    /* Dump all devices on each bus */
    for (i = 0; i <= MaxBus; i++) {
        ULONG dev, func;
        for (dev = 0; dev < 32; dev++) {
            for (func = 0; func < 8; func++) {
                PCI_TYPE1_CFG_BITS cfg;
                ULONG data;
                cfg.u.AsULONG = 0;
                cfg.u.bits.BusNumber = i;
                cfg.u.bits.DeviceNumber = dev;
                cfg.u.bits.FunctionNumber = func;
                cfg.u.bits.Enable = 1;
                WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, cfg.u.AsULONG);
                data = READ_PORT_ULONG((PULONG)PCI_TYPE1_DATA_PORT);
                if ((data & 0xFFFF) != 0xFFFF && (data & 0xFFFF) != 0) {
                    ULONG class;
                    cfg.u.bits.RegisterNumber = 2;
                    WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, cfg.u.AsULONG);
                    class = READ_PORT_ULONG((PULONG)PCI_TYPE1_DATA_PORT);
                    {
                        ULONG bar0, bar1, bar2;
                        cfg.u.bits.RegisterNumber = 4; /* BAR0 at offset 0x10 */
                        WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, cfg.u.AsULONG);
                        bar0 = READ_PORT_ULONG((PULONG)PCI_TYPE1_DATA_PORT);
                        cfg.u.bits.RegisterNumber = 5; /* BAR1 at offset 0x14 */
                        WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, cfg.u.AsULONG);
                        bar1 = READ_PORT_ULONG((PULONG)PCI_TYPE1_DATA_PORT);
                        cfg.u.bits.RegisterNumber = 6; /* BAR2 at offset 0x18 */
                        WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, cfg.u.AsULONG);
                        bar2 = READ_PORT_ULONG((PULONG)PCI_TYPE1_DATA_PORT);
                        DbgPrint("HAL: PCI %d:%d.%d %04x:%04x class=%02x.%02x BAR=%08x/%08x/%08x\n",
                                 i, dev, func,
                                 data & 0xFFFF, (data >> 16) & 0xFFFF,
                                 (class >> 24) & 0xFF, (class >> 16) & 0xFF,
                                 bar0, bar1, bar2);
                    }
                }
                if (func == 0) {
                    PCI_TYPE1_CFG_BITS hdr;
                    ULONG hdrtype;
                    hdr.u.AsULONG = 0;
                    hdr.u.bits.BusNumber = i;
                    hdr.u.bits.DeviceNumber = dev;
                    hdr.u.bits.FunctionNumber = 0;
                    hdr.u.bits.RegisterNumber = 3;
                    hdr.u.bits.Enable = 1;
                    WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, hdr.u.AsULONG);
                    hdrtype = READ_PORT_ULONG((PULONG)PCI_TYPE1_DATA_PORT);
                    if (!((hdrtype >> 16) & 0x80)) break;
                }
            }
        }
    }
    WRITE_PORT_ULONG(PCI_TYPE1_ADDR_PORT, 0);

    /* Register PCI bus handlers */
    for (i = 0; i <= MaxBus; i++) {
        Bus = HalpAllocateBusHandler(
            PCIBus,                     /* InterfaceType */
            PCIConfiguration,           /* BusDataType */
            i,                          /* BusNumber */
            Internal,                   /* ParentBusDataType */
            0,                          /* ParentBusNumber */
            sizeof(PCIBUSDATA)          /* BusSpecificData */
        );

        BusData = (PPCIBUSDATA) Bus->BusData;
        BusData->Config.Type1.Address = PCI_TYPE1_ADDR_PORT;
        BusData->Config.Type1.Data = PCI_TYPE1_DATA_PORT;
        BusData->MaxDevice = PCI_MAX_DEVICES;

        /* Allow the full 32-bit address range for memory and I/O.
         * On UEFI/Q35 there are no ISA holes to worry about. */
        BusData->MemoryBase = 0;
        BusData->MemoryLimit = 0xFFFFFFFF;
        BusData->PFMemoryBase = 0;
        BusData->PFMemoryLimit = 0xFFFFFFFF;
        BusData->IOBase = 0;
        BusData->IOLimit = 0xFFFF;

        Bus->GetBusData = (PGETSETBUSDATA) HalpGetPCIData;
        Bus->SetBusData = (PGETSETBUSDATA) HalpSetPCIData;
        Bus->TranslateBusAddress = (PTRANSLATEBUSADDRESS) HalpTranslatePCIBusAddress;
        Bus->AdjustResourceList = (PADJUSTRESOURCELIST) HalpAdjustPCIResourceList;
        Bus->AssignSlotResources = (PASSIGNSLOTRESOURCES) HalpAssignPCISlotResources;
        Bus->GetInterruptVector = (PGETINTERRUPTVECTOR) HalpGetPCIIntOnISABus;

        HalpSetBusHandlerParent(Bus, Bus->ParentHandler);
    }

    DbgPrint("HAL: registered %d PCI bus(es)\n", MaxBus + 1);

    /*
     * Populate the ARC configuration tree so IoQueryDeviceDescription(PCIBus)
     * finds our bus. VideoPortInitialize uses this to discover adapters.
     * Create: \Hardware\Description\System\MultifunctionAdapter\0
     *   Identifier = "PCI"
     *   ConfigurationData = CM_FULL_RESOURCE_DESCRIPTOR (empty)
     */
    {
        UNICODE_STRING Name;
        OBJECT_ATTRIBUTES ObjAttr;
        HANDLE MfKey, BusKey;
        NTSTATUS st;
        ULONG disp;

        /* Create intermediate keys if they don't exist */
        {
            static WCHAR *paths[] = {
                L"\\Registry\\Machine\\Hardware",
                L"\\Registry\\Machine\\Hardware\\Description",
                L"\\Registry\\Machine\\Hardware\\Description\\System",
                NULL
            };
            ULONG pi;
            for (pi = 0; paths[pi]; pi++) {
                HANDLE tmp;
                RtlInitUnicodeString(&Name, paths[pi]);
                InitializeObjectAttributes(&ObjAttr, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
                if (NT_SUCCESS(ZwCreateKey(&tmp, KEY_WRITE, &ObjAttr, 0, NULL, REG_OPTION_VOLATILE, &disp))) {
                    ZwClose(tmp);
                }
            }
        }

        RtlInitUnicodeString(&Name,
            L"\\Registry\\Machine\\Hardware\\Description\\System\\MultifunctionAdapter");
        InitializeObjectAttributes(&ObjAttr, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
        st = ZwCreateKey(&MfKey, KEY_WRITE, &ObjAttr, 0, NULL, REG_OPTION_VOLATILE, &disp);
        if (NT_SUCCESS(st)) {
            RtlInitUnicodeString(&Name, L"0");
            InitializeObjectAttributes(&ObjAttr, &Name, OBJ_CASE_INSENSITIVE, MfKey, NULL);
            st = ZwCreateKey(&BusKey, KEY_WRITE, &ObjAttr, 0, NULL, REG_OPTION_VOLATILE, &disp);
            if (NT_SUCCESS(st)) {
                WCHAR PciId[] = L"PCI";
                CM_FULL_RESOURCE_DESCRIPTOR CmDesc;

                RtlInitUnicodeString(&Name, L"Identifier");
                ZwSetValueKey(BusKey, &Name, 0, REG_SZ, PciId, sizeof(PciId));

                RtlZeroMemory(&CmDesc, sizeof(CmDesc));
                CmDesc.InterfaceType = PCIBus;
                CmDesc.BusNumber = 0;
                RtlInitUnicodeString(&Name, L"Configuration Data");
                ZwSetValueKey(BusKey, &Name, 0, REG_FULL_RESOURCE_DESCRIPTOR,
                              &CmDesc, sizeof(CmDesc));

                DbgPrint("HAL: created MultifunctionAdapter\\0 PCI ConfigData\n");
                ZwClose(BusKey);
            }
            ZwClose(MfKey);
        }
    }
}


/* ===== PCI config read/write ===== */

VOID
HalpPCISynchronizeType1(
    IN PBUSHANDLER BusHandler,
    IN PCI_SLOT_NUMBER Slot,
    IN PKIRQL Irql,
    IN PVOID State
    )
{
    PPCI_TYPE1_CFG_BITS Cfg = (PPCI_TYPE1_CFG_BITS) State;
    PPCIBUSDATA BusData = (PPCIBUSDATA) BusHandler->BusData;

    KeAcquireSpinLock(&HalpPCIConfigLock, Irql);

    Cfg->u.AsULONG = 0;
    Cfg->u.bits.BusNumber = BusHandler->BusNumber;
    Cfg->u.bits.DeviceNumber = Slot.u.bits.DeviceNumber;
    Cfg->u.bits.FunctionNumber = Slot.u.bits.FunctionNumber;
    Cfg->u.bits.Enable = TRUE;
}

VOID
HalpPCIReleaseSynchronzationType1(
    IN PBUSHANDLER BusHandler,
    IN KIRQL Irql
    )
{
    PPCIBUSDATA BusData = (PPCIBUSDATA) BusHandler->BusData;
    WRITE_PORT_ULONG(BusData->Config.Type1.Address, 0);
    KeReleaseSpinLock(&HalpPCIConfigLock, Irql);
}


ULONG
HalpPCIReadUcharType1(
    IN PPCIBUSDATA BusData,
    IN PPCI_TYPE1_CFG_BITS PciCfg1,
    IN PUCHAR Buffer,
    IN ULONG Offset
    )
{
    ULONG i = Offset % sizeof(ULONG);
    PciCfg1->u.bits.RegisterNumber = Offset / sizeof(ULONG);
    WRITE_PORT_ULONG(BusData->Config.Type1.Address, PciCfg1->u.AsULONG);
    *Buffer = READ_PORT_UCHAR((PUCHAR)(BusData->Config.Type1.Data + i));
    return sizeof(UCHAR);
}

ULONG
HalpPCIReadUshortType1(
    IN PPCIBUSDATA BusData,
    IN PPCI_TYPE1_CFG_BITS PciCfg1,
    IN PUCHAR Buffer,
    IN ULONG Offset
    )
{
    ULONG i = Offset % sizeof(ULONG);
    PciCfg1->u.bits.RegisterNumber = Offset / sizeof(ULONG);
    WRITE_PORT_ULONG(BusData->Config.Type1.Address, PciCfg1->u.AsULONG);
    *((PUSHORT)Buffer) = READ_PORT_USHORT((PUSHORT)(BusData->Config.Type1.Data + i));
    return sizeof(USHORT);
}

ULONG
HalpPCIReadUlongType1(
    IN PPCIBUSDATA BusData,
    IN PPCI_TYPE1_CFG_BITS PciCfg1,
    IN PUCHAR Buffer,
    IN ULONG Offset
    )
{
    PciCfg1->u.bits.RegisterNumber = Offset / sizeof(ULONG);
    WRITE_PORT_ULONG(BusData->Config.Type1.Address, PciCfg1->u.AsULONG);
    *((PULONG)Buffer) = READ_PORT_ULONG((PULONG)BusData->Config.Type1.Data);
    return sizeof(ULONG);
}

ULONG
HalpPCIWriteUcharType1(
    IN PPCIBUSDATA BusData,
    IN PPCI_TYPE1_CFG_BITS PciCfg1,
    IN PUCHAR Buffer,
    IN ULONG Offset
    )
{
    ULONG i = Offset % sizeof(ULONG);
    PciCfg1->u.bits.RegisterNumber = Offset / sizeof(ULONG);
    WRITE_PORT_ULONG(BusData->Config.Type1.Address, PciCfg1->u.AsULONG);
    WRITE_PORT_UCHAR((PUCHAR)(BusData->Config.Type1.Data + i), *Buffer);
    return sizeof(UCHAR);
}

ULONG
HalpPCIWriteUshortType1(
    IN PPCIBUSDATA BusData,
    IN PPCI_TYPE1_CFG_BITS PciCfg1,
    IN PUCHAR Buffer,
    IN ULONG Offset
    )
{
    ULONG i = Offset % sizeof(ULONG);
    PciCfg1->u.bits.RegisterNumber = Offset / sizeof(ULONG);
    WRITE_PORT_ULONG(BusData->Config.Type1.Address, PciCfg1->u.AsULONG);
    WRITE_PORT_USHORT((PUSHORT)(BusData->Config.Type1.Data + i), *((PUSHORT)Buffer));
    return sizeof(USHORT);
}

ULONG
HalpPCIWriteUlongType1(
    IN PPCIBUSDATA BusData,
    IN PPCI_TYPE1_CFG_BITS PciCfg1,
    IN PUCHAR Buffer,
    IN ULONG Offset
    )
{
    PciCfg1->u.bits.RegisterNumber = Offset / sizeof(ULONG);
    WRITE_PORT_ULONG(BusData->Config.Type1.Address, PciCfg1->u.AsULONG);
    WRITE_PORT_ULONG((PULONG)BusData->Config.Type1.Data, *((PULONG)Buffer));
    return sizeof(ULONG);
}


/* ===== HalpReadPCIConfig / HalpWritePCIConfig ===== */

static VOID
HalpPCIConfig(
    IN PBUSHANDLER BusHandler,
    IN PCI_SLOT_NUMBER Slot,
    IN PUCHAR Buffer,
    IN ULONG Offset,
    IN ULONG Length,
    IN FncConfigIO ConfigIO[3]
    )
{
    KIRQL OldIrql;
    PCI_TYPE1_CFG_BITS PciCfg1;
    PPCIBUSDATA BusData;
    ULONG i;

    BusData = (PPCIBUSDATA) BusHandler->BusData;

    HalpPCISynchronizeType1(BusHandler, Slot, &OldIrql, &PciCfg1);

    while (Length) {
        i = PCIDeref[Offset % sizeof(ULONG)][Length % sizeof(ULONG)];
        i = ConfigIO[i](BusData, &PciCfg1, Buffer, Offset);
        Offset += i;
        Buffer += i;
        Length -= i;
    }

    HalpPCIReleaseSynchronzationType1(BusHandler, OldIrql);
}

VOID
HalpReadPCIConfig(
    IN PBUSHANDLER BusHandler,
    IN PCI_SLOT_NUMBER Slot,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    )
{
    if (!BusHandler->BusData) {
        RtlFillMemory(Buffer, Length, 0xFF);
        return;
    }
    {
        static FncConfigIO ReadIO[3] = {
            HalpPCIReadUlongType1,
            HalpPCIReadUcharType1,
            HalpPCIReadUshortType1
        };
        HalpPCIConfig(BusHandler, Slot, Buffer, Offset, Length, ReadIO);
    }
}

VOID
HalpWritePCIConfig(
    IN PBUSHANDLER BusHandler,
    IN PCI_SLOT_NUMBER Slot,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    )
{
    if (!BusHandler->BusData) {
        return;
    }
    {
        static FncConfigIO WriteIO[3] = {
            HalpPCIWriteUlongType1,
            HalpPCIWriteUcharType1,
            HalpPCIWriteUshortType1
        };
        HalpPCIConfig(BusHandler, Slot, Buffer, Offset, Length, WriteIO);
    }
}


/* ===== GetBusData / SetBusData for PCI ===== */

ULONG
HalpGetPCIData(
    IN PBUSHANDLER BusHandler,
    IN PVOID RootHandler,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    )
{
    PCI_SLOT_NUMBER Slot;
    UCHAR Header[PCI_COMMON_HDR_LENGTH];
    ULONG ReadLen;

    if (Length == 0) return 0;

    Slot.u.AsULONG = SlotNumber;
    if (Slot.u.bits.DeviceNumber >= PCI_MAX_DEVICES) return 0;

    /* Read the header to validate the device exists */
    HalpReadPCIConfig(BusHandler, Slot, Header, 0, PCI_COMMON_HDR_LENGTH);
    if (*((PUSHORT)Header) == 0xFFFF) {
        RtlFillMemory(Buffer, Length, 0xFF);
        return 2;  /* return 2 = device doesn't exist */
    }

    /* Read requested range */
    ReadLen = (Offset + Length > PCI_COMMON_HDR_LENGTH) ?
              PCI_COMMON_HDR_LENGTH - Offset : Length;
    if (ReadLen > 0 && Offset < PCI_COMMON_HDR_LENGTH) {
        HalpReadPCIConfig(BusHandler, Slot, Buffer, Offset, ReadLen);
    }

    return ReadLen;
}

ULONG
HalpSetPCIData(
    IN PBUSHANDLER BusHandler,
    IN PVOID RootHandler,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    )
{
    PCI_SLOT_NUMBER Slot;
    ULONG WriteLen;

    if (Length == 0) return 0;

    Slot.u.AsULONG = SlotNumber;
    if (Slot.u.bits.DeviceNumber >= PCI_MAX_DEVICES) return 0;

    WriteLen = (Offset + Length > PCI_COMMON_HDR_LENGTH) ?
               PCI_COMMON_HDR_LENGTH - Offset : Length;
    if (WriteLen > 0 && Offset < PCI_COMMON_HDR_LENGTH) {
        HalpWritePCIConfig(BusHandler, Slot, Buffer, Offset, WriteLen);
    }

    return WriteLen;
}


/* ===== Stub AssignSlotResources for PCI ===== */

NTSTATUS
HalpAssignPCISlotResources(
    IN PVOID BusHandlerV,
    IN PVOID RootHandlerV,
    IN PUNICODE_STRING RegistryPath,
    IN PUNICODE_STRING DriverClassName OPTIONAL,
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT DeviceObject OPTIONAL,
    IN ULONG SlotNumber,
    IN OUT PCM_RESOURCE_LIST *AllocatedResources
    )
{
    PBUSHANDLER Handler = (PBUSHANDLER)BusHandlerV;
    PCI_SLOT_NUMBER Slot;
    PCI_COMMON_CONFIG PciConfig;
    ULONG NumBars, i, ResCount;
    ULONG ListSize;
    PCM_RESOURCE_LIST CmList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc;

    Slot.u.AsULONG = SlotNumber;
    HalpReadPCIConfig(Handler, Slot, &PciConfig, 0, PCI_COMMON_HDR_LENGTH);
    if (PciConfig.VendorID == PCI_INVALID_VENDORID) {
        return STATUS_NO_SUCH_DEVICE;
    }

    /* Count BARs (Type 0 has 6, skip 64-bit high halves) */
    NumBars = PCI_TYPE0_ADDRESSES;
    ResCount = 0;
    for (i = 0; i < NumBars; i++) {
        if (PciConfig.u.type0.BaseAddresses[i]) ResCount++;
    }
    /* Add one for the interrupt if present */
    if (PciConfig.u.type0.InterruptLine && PciConfig.u.type0.InterruptLine != 0xFF) {
        ResCount++;
    }

    ListSize = sizeof(CM_RESOURCE_LIST) +
               (ResCount ? (ResCount - 1) : 0) * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    CmList = (PCM_RESOURCE_LIST)ExAllocatePool(PagedPool, ListSize);
    if (!CmList) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(CmList, ListSize);

    CmList->Count = 1;
    CmList->List[0].InterfaceType = PCIBus;
    CmList->List[0].BusNumber = Handler->BusNumber;
    CmList->List[0].PartialResourceList.Count = ResCount;

    Desc = CmList->List[0].PartialResourceList.PartialDescriptors;

    for (i = 0; i < NumBars; i++) {
        ULONG Bar = PciConfig.u.type0.BaseAddresses[i];
        ULONG BarSize, Mask, BarOffset;
        if (!Bar) continue;

        /* BAR sizing: write all-1s, read back mask, restore original */
        BarOffset = FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[i]);
        Mask = 0xFFFFFFFF;
        HalpWritePCIConfig(Handler, Slot, &Mask, BarOffset, sizeof(ULONG));
        HalpReadPCIConfig(Handler, Slot, &Mask, BarOffset, sizeof(ULONG));
        HalpWritePCIConfig(Handler, Slot, &Bar, BarOffset, sizeof(ULONG));

        if (Bar & PCI_ADDRESS_IO_SPACE) {
            Mask &= ~3;
            BarSize = (~Mask + 1) & 0xFFFF;
            Desc->Type = CmResourceTypePort;
            Desc->Flags = CM_RESOURCE_PORT_IO;
            Desc->u.Port.Start.LowPart = Bar & ~3;
            Desc->u.Port.Start.HighPart = 0;
            Desc->u.Port.Length = BarSize;
        } else {
            Mask &= ~0xF;
            BarSize = ~Mask + 1;
            Desc->Type = CmResourceTypeMemory;
            Desc->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
            Desc->u.Memory.Start.LowPart = Bar & ~0xF;
            Desc->u.Memory.Start.HighPart = 0;
            Desc->u.Memory.Length = BarSize;
        }
        Desc++;
    }

    if (PciConfig.u.type0.InterruptLine && PciConfig.u.type0.InterruptLine != 0xFF) {
        Desc->Type = CmResourceTypeInterrupt;
        Desc->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        Desc->u.Interrupt.Level = PciConfig.u.type0.InterruptLine;
        Desc->u.Interrupt.Vector = PciConfig.u.type0.InterruptLine;
        Desc->u.Interrupt.Affinity = 1;
        Desc++;
    }

    *AllocatedResources = CmList;
    return STATUS_SUCCESS;
}


