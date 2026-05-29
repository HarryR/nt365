/*
 * stubs.c - MicroNT HAL stub functions
 *
 * Correct signatures from HAL.H / NTDDK.H.
 * These return success/zero to keep the kernel happy during early boot.
 */

#define _NTSYSTEM_
#include "halp.h"
#include "ntdddisk.h"
#include "ntddft.h"

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
    HalpSerialPrint("HAL: SetTimeIncrement\r\n");
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

/* ===== Bus / Resources ===== */

/* HalTranslateBusAddress, HalGetBusData, HalSetBusData,
 * HalAdjustResourceList, HalAssignSlotResources are now
 * provided by ixbusdat.c (bus handler dispatch). */

/* IDT usage tracking (referenced by ixsysbus.c) — stub for now */
IDTUsage HalpIDTUsage[256] = {0};

/* Resource list limits (referenced by ixpciint.c) — no ISA limits */
NTSTATUS
HalpAdjustResourceListLimits(
    IN PBUSHANDLER BusHandler,
    IN PBUSHANDLER RootHandler,
    IN OUT PIO_RESOURCE_REQUIREMENTS_LIST *pResourceList,
    IN ULONG MinimumMemoryAddress,
    IN ULONG MaximumMemoryAddress,
    IN ULONG MinimumPrefetchMemoryAddress,
    IN ULONG MaximumPrefetchMemoryAddress,
    IN BOOLEAN LimitedIO,
    IN ULONG MinimumIoAddress,
    IN ULONG MaximumIoAddress,
    IN PUCHAR IrqTable,
    IN ULONG IrqTableSize,
    IN ULONG MinimumDmaChannel,
    IN ULONG MaximumDmaChannel
    )
{
    return STATUS_SUCCESS;
}

extern VOID HalpReportPs2Devices(VOID);

VOID
HalReportResourceUsage(VOID)
{
    HalpSerialPrint("HAL: ReportResourceUsage\r\n");
    HalpInitializePciBus();
    HalpReportPs2Devices();
}

/* HalGetInterruptVector is in ixintr.c */

/* DMA primitives moved to dma.c (HalGetAdapter / HalAllocateCommonBuffer
 * / HalAllocateAdapterChannel / IoMapTransfer / IoFlushAdapterBuffers /
 * IoFreeAdapterChannel / IoFreeMapRegisters / HalReadDmaCounter /
 * HalAllocateCrashDumpRegisters / HalFlushCommonBuffer / HalFreeCommonBuffer). */

/* ===== IO Manager partition stubs ===== */

/*
 * IoAssignDriveLetters - minimal MicroNT implementation.
 *
 * The standard NT 3.5 routine walks LoaderBlock->ArcDiskInformation,
 * reads partition tables, and assigns letters across multiple disks.
 * MicroNT boots from a single FAT16 partition (the IDE volume QEMU
 * exposes), and the only consumer of drive-letter resolution today
 * is the toolchain we run under self-host.  So we just create
 *
 *     \DosDevices\C: -> <NtDeviceName>
 *
 * where NtDeviceName is the boot device the kernel passes us
 * (typically "\Device\Harddisk0\Partition1").  Without this symlink,
 * Win32 children's CreateFile("C:\...") - which goes through
 * RtlDosPathNameToNtPathName_U + the "\DosDevices\" prefix - cannot
 * resolve, breaking fopen / GetCurrentDirectory / GetModuleFileName
 * for any Win32 toolchain process.
 *
 * NtSystemPath / NtSystemPathString are already populated by INIT.C
 * (which sprintf's "C:%s" + LoaderBlock->NtBootPathName before we
 * run); leaving the OUT params untouched is correct.
 */
VOID
IoAssignDriveLetters(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PSTRING NtDeviceName,
    OUT PUCHAR NtSystemPath,
    OUT PSTRING NtSystemPathString
    )
{
    UNICODE_STRING linkName;
    UNICODE_STRING targetName;
    ANSI_STRING    ansiTarget;
    NTSTATUS       status;

    /* NtDeviceName is ANSI (PSTRING).  Convert to UNICODE_STRING for
     * IoCreateSymbolicLink.  RtlAnsiStringToUnicodeString allocates
     * the wide buffer when the third arg is TRUE; we free it below. */
    ansiTarget.Buffer        = NtDeviceName->Buffer;
    ansiTarget.Length        = NtDeviceName->Length;
    ansiTarget.MaximumLength = NtDeviceName->MaximumLength;

    status = RtlAnsiStringToUnicodeString(&targetName, &ansiTarget, TRUE);
    if (!NT_SUCCESS(status)) {
        return;
    }

    /* \DosDevices\C: -> NtDeviceName.  Errors (already-exists,
     * out-of-pool) are ignored — there's no recovery path here, and a
     * missing symlink will surface as a CreateFile failure later. */
    RtlInitUnicodeString(&linkName, L"\\DosDevices\\C:");
    (VOID) IoCreateSymbolicLink(&linkName, &targetName);

    RtlFreeUnicodeString(&targetName);
}

NTSTATUS
IoReadPartitionTable(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG SectorSize,
    IN BOOLEAN ReturnRecognizedPartitions,
    OUT PDRIVE_LAYOUT_INFORMATION *PartitionBuffer
    )
{
#define GET_STARTING_SECTOR( p ) (                  \
    RtlConvertUlongToLargeInteger(                  \
        (ULONG) (p->StartingSectorLsb0) +           \
        (ULONG) (p->StartingSectorLsb1 << 8) +      \
        (ULONG) (p->StartingSectorMsb0 << 16) +     \
        (ULONG) (p->StartingSectorMsb1 << 24) ) )

#define GET_PARTITION_LENGTH( p ) (                 \
    RtlConvertUlongToLargeInteger(                  \
        (ULONG) (p->PartitionLengthLsb0) +          \
        (ULONG) (p->PartitionLengthLsb1 << 8) +     \
        (ULONG) (p->PartitionLengthMsb0 << 16) +    \
        (ULONG) (p->PartitionLengthMsb1 << 24) ) )

#define ADD( a, b ) ( RtlLargeIntegerAdd( a, b ) )
#define SUBTRACT( a, b ) ( RtlLargeIntegerSubtract( a, b ) )
#define MULTIPLY( a, b ) ( RtlExtendedIntegerMultiply( a, (LONG) b ) )

    ULONG partitionBufferSize = PARTITION_BUFFER_SIZE;
    PDRIVE_LAYOUT_INFORMATION newPartitionBuffer = NULL;
    LARGE_INTEGER partitionTableOffset;
    LARGE_INTEGER volumeStartOffset;
    LARGE_INTEGER tempSize;
    BOOLEAN primaryPartitionTable;
    LONG partitionNumber;
    PUCHAR readBuffer = (PUCHAR) NULL;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    PIRP irp;
    PPARTITION_DESCRIPTOR partitionTableEntry;
    CCHAR partitionEntry;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG readSize;
    PPARTITION_INFORMATION partitionInfo;

    *PartitionBuffer = ExAllocatePool( NonPagedPool, partitionBufferSize );
    if (*PartitionBuffer == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    readSize = (SectorSize >= 512) ? SectorSize : 512;
    partitionTableOffset = RtlConvertUlongToLargeInteger( 0 );
    primaryPartitionTable = TRUE;
    volumeStartOffset = partitionTableOffset;
    partitionNumber = -1;

    readBuffer = ExAllocatePool( NonPagedPoolCacheAligned, PAGE_SIZE );
    if (readBuffer == NULL) {
        ExFreePool( *PartitionBuffer );
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    do {
        DbgPrint("IoReadPartitionTable: read offset=0x%x\n",
                 partitionTableOffset.LowPart);
        KeInitializeEvent( &event, NotificationEvent, FALSE );

        irp = IoBuildSynchronousFsdRequest( IRP_MJ_READ,
                                            DeviceObject,
                                            readBuffer,
                                            readSize,
                                            &partitionTableOffset,
                                            &event,
                                            &ioStatus );

        status = IoCallDriver( DeviceObject, irp );
        DbgPrint("IoReadPartitionTable: IoCallDriver rs=0x%x\n", status);

        if (status == STATUS_PENDING) {
            (VOID) KeWaitForSingleObject( &event, Executive, KernelMode,
                                          FALSE, (PLARGE_INTEGER) NULL );
            status = ioStatus.Status;
            DbgPrint("IoReadPartitionTable: wait done rs=0x%x\n", status);
        }

        if (!NT_SUCCESS( status )) {
            break;
        }

        if (((PUSHORT) readBuffer)[BOOT_SIGNATURE_OFFSET] != BOOT_RECORD_SIGNATURE) {
            break;
        }

        if (RtlLargeIntegerEqualToZero( partitionTableOffset )) {
            (*PartitionBuffer)->Signature =
                ((PULONG) readBuffer)[PARTITION_TABLE_OFFSET/2-1];
        }

        partitionTableEntry = (PPARTITION_DESCRIPTOR)
            &(((PUSHORT) readBuffer)[PARTITION_TABLE_OFFSET]);

        for (partitionEntry = 1;
             partitionEntry <= NUM_PARTITION_TABLE_ENTRIES;
             partitionEntry++, partitionTableEntry++) {

            if (ReturnRecognizedPartitions) {
                if ((partitionTableEntry->PartitionType == PARTITION_ENTRY_UNUSED) ||
                    (partitionTableEntry->PartitionType == PARTITION_EXTENDED)) {
                    continue;
                }
            }

            partitionNumber++;

            if (((partitionNumber * sizeof( PARTITION_INFORMATION )) +
                 sizeof( DRIVE_LAYOUT_INFORMATION )) > (ULONG) partitionBufferSize) {
                newPartitionBuffer = ExAllocatePool( NonPagedPool,
                                                     partitionBufferSize << 1 );
                if (newPartitionBuffer == NULL) {
                    --partitionNumber;
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                RtlCopyMemory( newPartitionBuffer, *PartitionBuffer,
                               partitionBufferSize );
                ExFreePool( *PartitionBuffer );
                *PartitionBuffer = newPartitionBuffer;
                partitionBufferSize <<= 1;
            }

            partitionInfo = &(*PartitionBuffer)->PartitionEntry[partitionNumber];
            partitionInfo->PartitionType = partitionTableEntry->PartitionType;
            partitionInfo->RewritePartition = FALSE;

            if (partitionTableEntry->PartitionType != PARTITION_ENTRY_UNUSED) {
                partitionInfo->BootIndicator =
                    partitionTableEntry->ActiveFlag & PARTITION_ACTIVE_FLAG ?
                        (BOOLEAN) TRUE : (BOOLEAN) FALSE;

                if (partitionTableEntry->PartitionType == PARTITION_EXTENDED) {
                    partitionInfo->RecognizedPartition = FALSE;
                } else {
                    partitionInfo->RecognizedPartition = TRUE;
                }

                tempSize = ADD( MULTIPLY( GET_STARTING_SECTOR( partitionTableEntry ),
                                          SectorSize ),
                                partitionTableOffset );
                partitionInfo->StartingOffset = tempSize;

                tempSize = MULTIPLY( GET_PARTITION_LENGTH( partitionTableEntry ),
                                     SectorSize );
                partitionInfo->PartitionLength = tempSize;

                tempSize = SUBTRACT( partitionInfo->StartingOffset,
                                     partitionTableOffset );
                partitionInfo->HiddenSectors =
                    LiDiv( tempSize, LiFromUlong( SectorSize ) ).LowPart;
            } else {
                partitionInfo->BootIndicator = FALSE;
                partitionInfo->RecognizedPartition = FALSE;
                partitionInfo->StartingOffset = RtlConvertLongToLargeInteger( 0 );
                partitionInfo->PartitionLength = RtlConvertLongToLargeInteger( 0 );
                partitionInfo->HiddenSectors = 0;
            }
        }

        if (!NT_SUCCESS( status )) {
            break;
        }

        partitionTableEntry = (PPARTITION_DESCRIPTOR)
            &(((PUSHORT) readBuffer)[PARTITION_TABLE_OFFSET]);
        partitionTableOffset = RtlConvertUlongToLargeInteger( 0 );

        for (partitionEntry = 1;
             partitionEntry <= NUM_PARTITION_TABLE_ENTRIES;
             partitionEntry++, partitionTableEntry++) {

            if (partitionTableEntry->PartitionType == PARTITION_EXTENDED) {
                partitionTableOffset = ADD( volumeStartOffset,
                    MULTIPLY( GET_STARTING_SECTOR( partitionTableEntry ),
                              SectorSize ) );
                if (primaryPartitionTable) {
                    volumeStartOffset = partitionTableOffset;
                }
                break;
            }
        }

        primaryPartitionTable = FALSE;

    } while (partitionTableOffset.HighPart | partitionTableOffset.LowPart);

    DbgPrint("IoReadPartitionTable: loop exit, partitions=%d\n", partitionNumber + 1);
    (*PartitionBuffer)->PartitionCount = ++partitionNumber;

    if (!partitionNumber) {
        (*PartitionBuffer)->Signature = 0;
    }

    if (readBuffer != NULL) {
        ExFreePool( readBuffer );
    }

    return status;

#undef GET_STARTING_SECTOR
#undef GET_PARTITION_LENGTH
#undef ADD
#undef SUBTRACT
#undef MULTIPLY
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
    IN struct _DRIVE_LAYOUT_INFORMATION *PartitionBuffer
    )
{
    return 0xC0000001;
}

/* ===== Misc ===== */

BOOLEAN
HalAllProcessorsStarted(VOID)
{
    HalpSerialPrint("HAL: AllProcessorsStarted\r\n");
    return TRUE;
}

BOOLEAN
HalStartNextProcessor(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PKPROCESSOR_STATE ProcessorState
    )
{
    return FALSE;
}

VOID
HalReturnToFirmware(
    IN FIRMWARE_REENTRY Routine
    )
{
    /* A 6-byte IDTR image (limit 0, base 0): loading it makes every
     * exception undeliverable, so the next fault escalates straight to a
     * triple fault -> CPU reset. */
    static UCHAR NullIdt[6] = { 0, 0, 0, 0, 0, 0 };

    HalpSerialPrint("HAL: ReturnToFirmware!\r\n");

    /* Legacy + modern I/O reset paths: the 8042 pulse resets pc/q35 (and
     * real PCs) immediately, so we never fall past it there.  microvm has
     * neither an 8042 nor a 0xCF9 reset register, so both are no-ops and we
     * fall through to the triple fault below. */
    HalpWritePort(0x64,  0xFE);    /* 8042 keyboard-controller CPU reset */
    HalpWritePort(0xCF9, 0x0E);    /* ICH/PCH reset control register */

    /* Universal fallback (incl. microvm): zero-length IDT + an exception
     * -> triple fault -> reset.  qemu with -no-reboot exits here. */
    _asm {
        cli
        lidt    fword ptr NullIdt
        int     3
    }

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

/* HalHandleNMI is in ixintr.c */

/* ===== KD port (kernel debugger serial) ===== */

ULONG KdComPortInUse = 0;

BOOLEAN
KdPortInitialize(
    IN PDEBUG_PARAMETERS DebugParameters,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN BOOLEAN Initialize
    )
{
    HalpSerialPrint(Initialize ? "HAL: KdPortInitialize(TRUE)\r\n"
                               : "HAL: KdPortInitialize(FALSE)\r\n");
    if (Initialize) {
        /* Initialize COM1 for KD communication */
        HalpWritePort(COM1_PORT + 1, 0x00);  /* Disable interrupts */
        HalpWritePort(COM1_PORT + 3, 0x80);  /* DLAB on */
        HalpWritePort(COM1_PORT + 0, 0x01);  /* 115200 baud */
        HalpWritePort(COM1_PORT + 1, 0x00);
        HalpWritePort(COM1_PORT + 3, 0x03);  /* 8N1, DLAB off */
        HalpWritePort(COM1_PORT + 2, 0xC7);  /* Enable FIFO */
        HalpWritePort(COM1_PORT + 4, 0x0B);  /* DTR + RTS + OUT2 */
        HalpSerialPrint("HAL: COM1 initialized for KD\r\n");
    }
    return Initialize;
}

static ULONG KdpGetByteCount = 0;

ULONG
KdPortGetByte(
    OUT PUCHAR Input
    )
{
    /* Poll COM1 for up to ~1 second (matching kernel's timeout expectation).
     * The kernel's KdpReceivePacketLeader assumes each KdPortGetByte call
     * waits before returning NODATA. Without polling, ACK packets from
     * the debugger arrive after the kernel has already timed out. */
    ULONG i;
    for (i = 0; i < 1000000; i++) {
        if (HalpReadPort(COM1_PORT + 5) & 0x01) {
            *Input = HalpReadPort(COM1_PORT);
            KdpGetByteCount++;
            if (KdpGetByteCount <= 64) {
                /* Trace received bytes to COM2 as hex */
                CHAR hex[8];
                UCHAR nibh = (*Input >> 4) & 0x0F;
                UCHAR nibl = *Input & 0x0F;
                hex[0] = nibh < 10 ? '0'+nibh : 'A'+nibh-10;
                hex[1] = nibl < 10 ? '0'+nibl : 'A'+nibl-10;
                hex[2] = ' ';
                hex[3] = '\0';
                HalpSerialPrint(hex);
            }
            return 1;  /* CP_GET_SUCCESS */
        }
        HalpReadPort(0x80);  /* ~1us delay */
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
    /* KD uses COM1 directly — not HalpSerialPutChar which is on COM2 */
    while (!(HalpReadPort(COM1_PORT + 5) & 0x20))
        ;
    HalpWritePort(COM1_PORT, Output);
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
