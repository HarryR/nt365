/*
 * stubs.c - MicroNT HAL stub functions
 *
 * Correct signatures from HAL.H / NTDDK.H.
 * These return success/zero to keep the kernel happy during early boot.
 */

#include "halp.h"

/* ===== Stall / Performance ===== */

VOID
KeStallExecutionProcessor(
    IN ULONG Microseconds
    )
{
    ULONG i;
    for (i = 0; i < Microseconds; i++) {
        HalpReadPort(0x80);     /* ~1us per port read */
    }
}

LARGE_INTEGER
KeQueryPerformanceCounter(
    OUT PLARGE_INTEGER PerformanceFrequency OPTIONAL
    )
{
    LARGE_INTEGER li;
    li.QuadPart = 0;
    if (PerformanceFrequency) {
        PerformanceFrequency->QuadPart = 1000000;
    }
    return li;
}

VOID
KeFlushWriteBuffer(VOID)
{
}

/* ===== Timer / Clock / Profile ===== */

ULONG
HalSetTimeIncrement(
    IN ULONG DesiredIncrement
    )
{
    return 100000;  /* 10ms in 100ns units */
}

VOID
HalCalibratePerformanceCounter(
    IN volatile PLONG Number
    )
{
}

VOID
HalStartProfileInterrupt(
    IN ULONG Reserved
    )
{
}

VOID
HalStopProfileInterrupt(
    IN ULONG Reserved
    )
{
}

ULONG
HalSetProfileInterval(
    IN ULONG Interval
    )
{
    return Interval;
}

/* ===== RTC ===== */

BOOLEAN
HalQueryRealTimeClock(
    OUT PTIME_FIELDS TimeFields
    )
{
    return FALSE;
}

BOOLEAN
HalSetRealTimeClock(
    IN PTIME_FIELDS TimeFields
    )
{
    return FALSE;
}

/* ===== Bus / Resources ===== */

BOOLEAN
HalTranslateBusAddress(
    IN INTERFACE_TYPE InterfaceType,
    IN ULONG BusNumber,
    IN PHYSICAL_ADDRESS BusAddress,
    IN OUT PULONG AddressSpace,
    OUT PPHYSICAL_ADDRESS TranslatedAddress
    )
{
    *TranslatedAddress = BusAddress;
    return TRUE;
}

ULONG
HalGetBusData(
    IN BUS_DATA_TYPE BusDataType,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Length
    )
{
    return 0;
}

ULONG
HalGetBusDataByOffset(
    IN BUS_DATA_TYPE BusDataType,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    )
{
    return 0;
}

ULONG
HalSetBusData(
    IN BUS_DATA_TYPE BusDataType,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Length
    )
{
    return 0;
}

ULONG
HalSetBusDataByOffset(
    IN BUS_DATA_TYPE BusDataType,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    )
{
    return 0;
}

NTSTATUS
HalAdjustResourceList(
    IN OUT PIO_RESOURCE_REQUIREMENTS_LIST *pResourceList
    )
{
    return 0;  /* STATUS_SUCCESS */
}

NTSTATUS
HalAssignSlotResources(
    IN PUNICODE_STRING RegistryPath,
    IN PUNICODE_STRING DriverClassName OPTIONAL,
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT DeviceObject OPTIONAL,
    IN INTERFACE_TYPE BusType,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN OUT PCM_RESOURCE_LIST *AllocatedResources
    )
{
    return 0xC0000001;  /* STATUS_UNSUCCESSFUL */
}

VOID
HalReportResourceUsage(VOID)
{
}

/* HalGetInterruptVector is in interrupt.c */

/* ===== DMA ===== */

NTSTATUS
HalAllocateAdapterChannel(
    IN PADAPTER_OBJECT AdapterObject,
    IN PWAIT_CONTEXT_BLOCK Wcb,
    IN ULONG NumberOfMapRegisters,
    IN PDRIVER_CONTROL ExecutionRoutine
    )
{
    return 0xC0000001;
}

PVOID
HalAllocateCommonBuffer(
    IN PADAPTER_OBJECT AdapterObject,
    IN ULONG Length,
    OUT PPHYSICAL_ADDRESS LogicalAddress,
    IN BOOLEAN CacheEnabled
    )
{
    return NULL;
}

PVOID
HalAllocateCrashDumpRegisters(
    IN PADAPTER_OBJECT AdapterObject,
    IN PULONG NumberOfMapRegisters
    )
{
    return NULL;
}

BOOLEAN
HalFlushCommonBuffer(
    IN PADAPTER_OBJECT AdapterObject,
    IN ULONG Length,
    IN PHYSICAL_ADDRESS LogicalAddress,
    IN PVOID VirtualAddress
    )
{
    return TRUE;
}

VOID
HalFreeCommonBuffer(
    IN PADAPTER_OBJECT AdapterObject,
    IN ULONG Length,
    IN PHYSICAL_ADDRESS LogicalAddress,
    IN PVOID VirtualAddress,
    IN BOOLEAN CacheEnabled
    )
{
}

PADAPTER_OBJECT
HalGetAdapter(
    IN PDEVICE_DESCRIPTION DeviceDescription,
    IN OUT PULONG NumberOfMapRegisters
    )
{
    return NULL;
}

ULONG
HalReadDmaCounter(
    IN PADAPTER_OBJECT AdapterObject
    )
{
    return 0;
}

/* ===== IO Manager DMA/partition stubs ===== */

BOOLEAN
IoFlushAdapterBuffers(
    IN PADAPTER_OBJECT AdapterObject,
    IN PMDL Mdl,
    IN PVOID MapRegisterBase,
    IN PVOID CurrentVa,
    IN ULONG Length,
    IN BOOLEAN WriteToDevice
    )
{
    return TRUE;
}

VOID
IoFreeAdapterChannel(
    IN PADAPTER_OBJECT AdapterObject
    )
{
}

VOID
IoFreeMapRegisters(
    IN PADAPTER_OBJECT AdapterObject,
    IN PVOID MapRegisterBase,
    IN ULONG NumberOfMapRegisters
    )
{
}

PHYSICAL_ADDRESS
IoMapTransfer(
    IN PADAPTER_OBJECT AdapterObject,
    IN PMDL Mdl,
    IN PVOID MapRegisterBase,
    IN PVOID CurrentVa,
    IN OUT PULONG Length,
    IN BOOLEAN WriteToDevice
    )
{
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = 0;
    return pa;
}

VOID
IoAssignDriveLetters(
    IN PVOID LoaderBlock,
    IN PVOID NtDeviceName,
    OUT PVOID NtSystemPath,
    OUT PVOID NtSystemPathString
    )
{
}

NTSTATUS
IoReadPartitionTable(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG SectorSize,
    IN BOOLEAN ReturnRecognizedPartitions,
    OUT PVOID *PartitionBuffer
    )
{
    return 0xC0000001;
}

NTSTATUS
IoSetPartitionInformation(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG SectorSize,
    IN ULONG PartitionNumber,
    IN ULONG PartitionType
    )
{
    return 0xC0000001;
}

NTSTATUS
IoWritePartitionTable(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG SectorSize,
    IN ULONG SectorsPerTrack,
    IN ULONG NumberOfHeads,
    IN PVOID PartitionBuffer
    )
{
    return 0xC0000001;
}

/* ===== Misc ===== */

BOOLEAN
HalAllProcessorsStarted(VOID)
{
    return TRUE;
}

BOOLEAN
HalStartNextProcessor(
    IN PVOID LoaderBlock,
    IN PVOID ProcessorState
    )
{
    return FALSE;
}

VOID
HalReturnToFirmware(
    IN FIRMWARE_REENTRY Routine
    )
{
    HalpSerialPrint("HAL: ReturnToFirmware\r\n");
    HalpWritePort(0x64, 0xFE);     /* Reset via 8042 keyboard controller */
    _asm { cli }
    _asm { hlt }
}

BOOLEAN
HalMakeBeep(
    IN ULONG Frequency
    )
{
    return FALSE;
}

VOID
HalProcessorIdle(VOID)
{
    _asm { sti }
    _asm { hlt }
}

ARC_STATUS
HalGetEnvironmentVariable(
    IN PCHAR Variable,
    IN USHORT Length,
    OUT PCHAR Buffer
    )
{
    return 1;  /* ENOMEM */
}

ARC_STATUS
HalSetEnvironmentVariable(
    IN PCHAR Variable,
    IN PCHAR Value
    )
{
    return 1;  /* ENOMEM */
}

VOID
HalRequestIpi(
    IN ULONG Mask
    )
{
}

/* HalHandleNMI is in interrupt.c */

/* ===== KD port (kernel debugger serial) ===== */

ULONG KdComPortInUse = 0;

BOOLEAN
KdPortInitialize(
    IN PVOID DebugParameters,
    IN PVOID LoaderBlock,
    IN BOOLEAN Initialize
    )
{
    return Initialize;
}

ULONG
KdPortGetByte(
    OUT PUCHAR Input
    )
{
    if (HalpReadPort(COM1_PORT + 5) & 0x01) {
        *Input = HalpReadPort(COM1_PORT);
        return 1;  /* CP_GET_SUCCESS */
    }
    return 0;  /* CP_GET_NODATA */
}

ULONG
KdPortPollByte(
    OUT PUCHAR Input
    )
{
    return KdPortGetByte(Input);
}

VOID
KdPortPutByte(
    IN UCHAR Output
    )
{
    HalpSerialPutChar((CHAR)Output);
}

VOID KdPortRestore(VOID) {}
VOID KdPortSave(VOID) {}

/* ===== Port I/O ===== */

UCHAR
READ_PORT_UCHAR(
    IN PUCHAR Port
    )
{
    return HalpReadPort((USHORT)(ULONG)Port);
}

USHORT
READ_PORT_USHORT(
    IN PUSHORT Port
    )
{
    USHORT val; USHORT p = (USHORT)(ULONG)Port;
    _asm { mov dx, p }
    _asm { in ax, dx }
    _asm { mov val, ax }
    return val;
}

ULONG
READ_PORT_ULONG(
    IN PULONG Port
    )
{
    ULONG val; USHORT p = (USHORT)(ULONG)Port;
    _asm { mov dx, p }
    _asm { in eax, dx }
    _asm { mov val, eax }
    return val;
}

VOID
WRITE_PORT_UCHAR(
    IN PUCHAR Port,
    IN UCHAR Value
    )
{
    HalpWritePort((USHORT)(ULONG)Port, Value);
}

VOID
WRITE_PORT_USHORT(
    IN PUSHORT Port,
    IN USHORT Value
    )
{
    USHORT p = (USHORT)(ULONG)Port;
    _asm { mov dx, p }
    _asm { mov ax, Value }
    _asm { out dx, ax }
}

VOID
WRITE_PORT_ULONG(
    IN PULONG Port,
    IN ULONG Value
    )
{
    USHORT p = (USHORT)(ULONG)Port;
    _asm { mov dx, p }
    _asm { mov eax, Value }
    _asm { out dx, eax }
}

/* Buffer versions */
VOID READ_PORT_BUFFER_UCHAR(IN PUCHAR Port, IN PUCHAR Buffer, IN ULONG Count) {
    while (Count--) *Buffer++ = READ_PORT_UCHAR(Port);
}
VOID READ_PORT_BUFFER_USHORT(IN PUSHORT Port, IN PUSHORT Buffer, IN ULONG Count) {
    while (Count--) *Buffer++ = READ_PORT_USHORT(Port);
}
VOID READ_PORT_BUFFER_ULONG(IN PULONG Port, IN PULONG Buffer, IN ULONG Count) {
    while (Count--) *Buffer++ = READ_PORT_ULONG(Port);
}
VOID WRITE_PORT_BUFFER_UCHAR(IN PUCHAR Port, IN PUCHAR Buffer, IN ULONG Count) {
    while (Count--) WRITE_PORT_UCHAR(Port, *Buffer++);
}
VOID WRITE_PORT_BUFFER_USHORT(IN PUSHORT Port, IN PUSHORT Buffer, IN ULONG Count) {
    while (Count--) WRITE_PORT_USHORT(Port, *Buffer++);
}
VOID WRITE_PORT_BUFFER_ULONG(IN PULONG Port, IN PULONG Buffer, IN ULONG Count) {
    while (Count--) WRITE_PORT_ULONG(Port, *Buffer++);
}
