/*
 * bochsvga.c — Bochs VGA (stdvga) miniport driver for QEMU
 *
 * Programs the Bochs VGA dispi registers directly (I/O 0x1CE/0x1D0)
 * to set video modes. No INT10 BIOS dependency — works under UEFI.
 *
 * PCI device: 1234:1111 (QEMU stdvga / Bochs VGA)
 * Framebuffer: PCI BAR0 (linear, 32bpp)
 *
 * Pairs with FRAMEBUF.DLL (generic framebuffer GDI display driver).
 */

#include "dderror.h"
#include "devioctl.h"
#include "miniport.h"
#include "ntddvdeo.h"
#include "video.h"

ULONG DbgPrint(PCHAR Format, ...);

/* Bochs VGA dispi register interface */
#define VBE_DISPI_IOPORT_INDEX      0x01CE
#define VBE_DISPI_IOPORT_DATA       0x01D0

#define VBE_DISPI_INDEX_ID          0x0
#define VBE_DISPI_INDEX_XRES        0x1
#define VBE_DISPI_INDEX_YRES        0x2
#define VBE_DISPI_INDEX_BPP         0x3
#define VBE_DISPI_INDEX_ENABLE      0x4
#define VBE_DISPI_INDEX_BANK        0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET    0x8
#define VBE_DISPI_INDEX_Y_OFFSET    0x9

#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_LFB_ENABLED      0x40

#define BOCHS_VGA_VENDOR_ID         0x1234
#define BOCHS_VGA_DEVICE_ID         0x1111

/* Default mode */
#define DEFAULT_WIDTH               1024
#define DEFAULT_HEIGHT              768
#define DEFAULT_BPP                 32

typedef struct _BOCHS_DEVICE_EXTENSION {
    PHYSICAL_ADDRESS FrameBufferBase;
    ULONG            FrameBufferLength;
    PUSHORT          DispiIndexPort;
    PUSHORT          DispiDataPort;
    BOOLEAN          DispiIsMmio;
    ULONG            CurrentWidth;
    ULONG            CurrentHeight;
    ULONG            CurrentBpp;
} BOCHS_DEVICE_EXTENSION, *PBOCHS_DEVICE_EXTENSION;

/* Forward declarations */
VP_STATUS BochsFindAdapter(PVOID HwDeviceExtension,
    PVOID HwContext, PWSTR ArgumentString,
    PVIDEO_PORT_CONFIG_INFO ConfigInfo, PUCHAR Again);
BOOLEAN BochsInitialize(PVOID HwDeviceExtension);
BOOLEAN BochsStartIO(PVOID HwDeviceExtension,
    PVIDEO_REQUEST_PACKET RequestPacket);

/* Dispi register helpers — MMIO uses direct memory access,
 * legacy I/O uses port instructions */
static VOID DispiWrite(PBOCHS_DEVICE_EXTENSION Ext, USHORT Index, USHORT Value)
{
    if (Ext->DispiIsMmio) {
        /* MMIO: each register at base + index*2 (flat layout) */
        ((volatile USHORT *)Ext->DispiIndexPort)[Index] = Value;
    } else {
        VideoPortWritePortUshort(Ext->DispiIndexPort, Index);
        VideoPortWritePortUshort(Ext->DispiDataPort, Value);
    }
}

static USHORT DispiRead(PBOCHS_DEVICE_EXTENSION Ext, USHORT Index)
{
    if (Ext->DispiIsMmio) {
        return ((volatile USHORT *)Ext->DispiIndexPort)[Index];
    } else {
        VideoPortWritePortUshort(Ext->DispiIndexPort, Index);
        return VideoPortReadPortUshort(Ext->DispiDataPort);
    }
}

static VOID BochsSetMode(PBOCHS_DEVICE_EXTENSION Ext,
    ULONG Width, ULONG Height, ULONG Bpp)
{
    DispiWrite(Ext, VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    DispiWrite(Ext, VBE_DISPI_INDEX_XRES, (USHORT)Width);
    DispiWrite(Ext, VBE_DISPI_INDEX_YRES, (USHORT)Height);
    DispiWrite(Ext, VBE_DISPI_INDEX_BPP, (USHORT)Bpp);
    DispiWrite(Ext, VBE_DISPI_INDEX_VIRT_WIDTH, (USHORT)Width);
    DispiWrite(Ext, VBE_DISPI_INDEX_VIRT_HEIGHT, (USHORT)Height);
    DispiWrite(Ext, VBE_DISPI_INDEX_X_OFFSET, 0);
    DispiWrite(Ext, VBE_DISPI_INDEX_Y_OFFSET, 0);
    DispiWrite(Ext, VBE_DISPI_INDEX_ENABLE,
               VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    Ext->CurrentWidth = Width;
    Ext->CurrentHeight = Height;
    Ext->CurrentBpp = Bpp;
}

static VOID BochsFillModeInfo(PBOCHS_DEVICE_EXTENSION Ext,
    PVIDEO_MODE_INFORMATION ModeInfo)
{
    VideoPortZeroMemory(ModeInfo, sizeof(VIDEO_MODE_INFORMATION));
    ModeInfo->Length = sizeof(VIDEO_MODE_INFORMATION);
    ModeInfo->ModeIndex = 0;
    ModeInfo->VisScreenWidth = Ext->CurrentWidth;
    ModeInfo->VisScreenHeight = Ext->CurrentHeight;
    ModeInfo->ScreenStride = Ext->CurrentWidth * (Ext->CurrentBpp / 8);
    ModeInfo->NumberOfPlanes = 1;
    ModeInfo->BitsPerPlane = Ext->CurrentBpp;
    ModeInfo->Frequency = 60;
    ModeInfo->XMillimeter = 320;
    ModeInfo->YMillimeter = 240;
    ModeInfo->NumberRedBits = 8;
    ModeInfo->NumberGreenBits = 8;
    ModeInfo->NumberBlueBits = 8;
    ModeInfo->RedMask   = 0x00FF0000;
    ModeInfo->GreenMask = 0x0000FF00;
    ModeInfo->BlueMask  = 0x000000FF;
    ModeInfo->AttributeFlags = VIDEO_MODE_COLOR | VIDEO_MODE_GRAPHICS;
    ModeInfo->VideoMemoryBitmapWidth = Ext->CurrentWidth;
    ModeInfo->VideoMemoryBitmapHeight = Ext->CurrentHeight;
}

/* DriverEntry — miniport entry point */
ULONG DriverEntry(PVOID Context1, PVOID Context2)
{
    VIDEO_HW_INITIALIZATION_DATA hwInitData;
    ULONG status;

    DbgPrint("BochsVGA: DriverEntry\n");

    VideoPortZeroMemory(&hwInitData, sizeof(VIDEO_HW_INITIALIZATION_DATA));
    hwInitData.HwInitDataSize = sizeof(VIDEO_HW_INITIALIZATION_DATA);
    hwInitData.HwFindAdapter = BochsFindAdapter;
    hwInitData.HwInitialize = BochsInitialize;
    hwInitData.HwStartIO = BochsStartIO;
    hwInitData.HwDeviceExtensionSize = sizeof(BOCHS_DEVICE_EXTENSION);
    hwInitData.AdapterInterfaceType = PCIBus;

    status = VideoPortInitialize(Context1, Context2, &hwInitData, NULL);
    DbgPrint("BochsVGA: VideoPortInitialize returned %08lx\n", status);
    return status;
}

/*
 * BochsFindAdapter — locate PCI device 1234:1111
 *
 * Based on ReactOS/uintOS bochsmp.c pattern:
 * - VideoPortGetAccessRanges with NULL vendor/device (slot from init)
 * - BAR0 = framebuffer, BAR1/BAR2 = dispi MMIO (or fallback to legacy I/O)
 */
VP_STATUS BochsFindAdapter(
    PVOID HwDeviceExtension,
    PVOID HwContext,
    PWSTR ArgumentString,
    PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    PUCHAR Again)
{
    PBOCHS_DEVICE_EXTENSION Ext = (PBOCHS_DEVICE_EXTENSION)HwDeviceExtension;
    VP_STATUS status;
    VIDEO_ACCESS_RANGE AccessRanges[3];
    USHORT VendorId = BOCHS_VGA_VENDOR_ID;
    USHORT DeviceId = BOCHS_VGA_DEVICE_ID;
    ULONG Slot = 0;
    USHORT DispiId;
    PVOID MappedDispi;

    DbgPrint("BochsVGA: BochsFindAdapter entered\n");
    VideoPortZeroMemory(AccessRanges, sizeof(AccessRanges));

    /* Find PCI device 1234:1111 and get its BARs */
    status = VideoPortGetAccessRanges(HwDeviceExtension,
                                      0, NULL,
                                      3, AccessRanges,
                                      &VendorId, &DeviceId, &Slot);

    if (status != NO_ERROR) {
        DbgPrint("BochsVGA: VideoPortGetAccessRanges failed %08lx\n", status);
        return ERROR_DEV_NOT_EXIST;
    }

    /* BAR0 = framebuffer (memory) */
    Ext->FrameBufferBase = AccessRanges[0].RangeStart;
    Ext->FrameBufferLength = AccessRanges[0].RangeLength;
    DbgPrint("BochsVGA: BAR0 fb=%08lx len=%08lx io=%d\n",
             AccessRanges[0].RangeStart.LowPart,
             AccessRanges[0].RangeLength,
             AccessRanges[0].RangeInIoSpace);
    DbgPrint("BochsVGA: BAR1 start=%08lx len=%08lx io=%d\n",
             AccessRanges[1].RangeStart.LowPart,
             AccessRanges[1].RangeLength,
             AccessRanges[1].RangeInIoSpace);
    DbgPrint("BochsVGA: BAR2 start=%08lx len=%08lx io=%d\n",
             AccessRanges[2].RangeStart.LowPart,
             AccessRanges[2].RangeLength,
             AccessRanges[2].RangeInIoSpace);

    /* Dispi interface: prefer BAR2 MMIO if available,
     * otherwise fall back to legacy I/O ports 0x1CE/0x1D0 */
    if (AccessRanges[2].RangeLength >= 2) {
        /* BAR2 = dispi registers (MMIO or I/O) */
        MappedDispi = VideoPortGetDeviceBase(HwDeviceExtension,
                                             AccessRanges[2].RangeStart,
                                             AccessRanges[2].RangeLength,
                                             AccessRanges[2].RangeInIoSpace);
    } else if (AccessRanges[1].RangeLength >= 2) {
        /* BAR1 = dispi registers */
        MappedDispi = VideoPortGetDeviceBase(HwDeviceExtension,
                                             AccessRanges[1].RangeStart,
                                             AccessRanges[1].RangeLength,
                                             AccessRanges[1].RangeInIoSpace);
    } else {
        /* Legacy I/O fallback */
        PHYSICAL_ADDRESS DispiAddr;
        DispiAddr.LowPart = VBE_DISPI_IOPORT_INDEX;
        DispiAddr.HighPart = 0;
        MappedDispi = VideoPortGetDeviceBase(HwDeviceExtension,
                                             DispiAddr, 4, TRUE);
    }

    if (!MappedDispi) {
        DbgPrint("BochsVGA: cannot map dispi interface\n");
        return ERROR_DEV_NOT_EXIST;
    }

    DbgPrint("BochsVGA: dispi mapped=%p\n", MappedDispi);

    /* For MMIO BARs (length >= 0x1000), the Bochs VGA dispi registers
     * are at offset 0x500 within the MMIO region. Each register is at
     * a fixed offset: 0x500 + (index * 2). No index/data port pair. */
    if (AccessRanges[1].RangeLength >= 0x1000 ||
        AccessRanges[2].RangeLength >= 0x1000) {
        /* Dump first few words at offset 0x500 for debugging */
        {
            volatile USHORT *base = (volatile USHORT *)((PUCHAR)MappedDispi + 0x500);
            DbgPrint("BochsVGA: MMIO@500: %04x %04x %04x %04x %04x\n",
                     base[0], base[1], base[2], base[3], base[4]);
        }
        Ext->DispiIndexPort = (PUSHORT)((PUCHAR)MappedDispi + 0x500);
        Ext->DispiDataPort  = (PUSHORT)((PUCHAR)MappedDispi + 0x500);
        Ext->DispiIsMmio = TRUE;
        DbgPrint("BochsVGA: using MMIO dispi at offset 0x500\n");
    } else {
        /* Legacy I/O: index at +0, data at +2 */
        Ext->DispiIndexPort = (PUSHORT)((PUCHAR)MappedDispi + 0);
        Ext->DispiDataPort  = (PUSHORT)((PUCHAR)MappedDispi + 2);
        Ext->DispiIsMmio = FALSE;
    }

    /* Verify the device responds */
    DispiId = DispiRead(Ext, VBE_DISPI_INDEX_ID);
    DbgPrint("BochsVGA: dispi ID = %04x\n", DispiId);

    if ((DispiId & 0xFFF0) != 0xB0C0) {
        DbgPrint("BochsVGA: bad dispi ID %04x (expected B0C0-B0CF)\n", DispiId);
        return ERROR_DEV_NOT_EXIST;
    }

    *Again = FALSE;
    return NO_ERROR;
}

/* BochsInitialize — set initial video mode */
BOOLEAN BochsInitialize(PVOID HwDeviceExtension)
{
    PBOCHS_DEVICE_EXTENSION Ext = (PBOCHS_DEVICE_EXTENSION)HwDeviceExtension;

    BochsSetMode(Ext, DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_BPP);

    DbgPrint("BochsVGA: initialized %dx%dx%d\n",
                     DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_BPP);
    return TRUE;
}

/* BochsStartIO — handle display IOCTLs from GDI (via FRAMEBUF.DLL) */
BOOLEAN BochsStartIO(
    PVOID HwDeviceExtension,
    PVIDEO_REQUEST_PACKET RequestPacket)
{
    PBOCHS_DEVICE_EXTENSION Ext = (PBOCHS_DEVICE_EXTENSION)HwDeviceExtension;
    VP_STATUS status = NO_ERROR;

    switch (RequestPacket->IoControlCode) {

    case IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES:
    {
        PVIDEO_NUM_MODES NumModes;
        if (RequestPacket->OutputBufferLength < sizeof(VIDEO_NUM_MODES)) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        NumModes = (PVIDEO_NUM_MODES)RequestPacket->OutputBuffer;
        NumModes->NumModes = 1;
        NumModes->ModeInformationLength = sizeof(VIDEO_MODE_INFORMATION);
        RequestPacket->StatusBlock->Information = sizeof(VIDEO_NUM_MODES);
        break;
    }

    case IOCTL_VIDEO_QUERY_AVAIL_MODES:
    case IOCTL_VIDEO_QUERY_CURRENT_MODE:
    {
        PVIDEO_MODE_INFORMATION ModeInfo;
        if (RequestPacket->OutputBufferLength < sizeof(VIDEO_MODE_INFORMATION)) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        ModeInfo = (PVIDEO_MODE_INFORMATION)RequestPacket->OutputBuffer;
        BochsFillModeInfo(Ext, ModeInfo);
        RequestPacket->StatusBlock->Information = sizeof(VIDEO_MODE_INFORMATION);
        break;
    }

    case IOCTL_VIDEO_SET_CURRENT_MODE:
    {
        /* Only one mode — just re-apply it */
        BochsSetMode(Ext, Ext->CurrentWidth, Ext->CurrentHeight, Ext->CurrentBpp);
        break;
    }

    case IOCTL_VIDEO_MAP_VIDEO_MEMORY:
    {
        PVIDEO_MEMORY_INFORMATION MemInfo;
        PVIDEO_MEMORY MemReq;
        VP_STATUS mapStatus;
        ULONG inIoSpace = 0;
        PVOID VirtualBase = NULL;
        ULONG MapLength;

        if (RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY) ||
            RequestPacket->OutputBufferLength < sizeof(VIDEO_MEMORY_INFORMATION)) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }

        MemReq = (PVIDEO_MEMORY)RequestPacket->InputBuffer;
        MemInfo = (PVIDEO_MEMORY_INFORMATION)RequestPacket->OutputBuffer;

        MapLength = Ext->FrameBufferLength;
        VirtualBase = NULL;

        mapStatus = VideoPortMapMemory(HwDeviceExtension,
                                       Ext->FrameBufferBase,
                                       &MapLength,
                                       &inIoSpace,
                                       &VirtualBase);

        if (mapStatus != NO_ERROR) {
            status = mapStatus;
            break;
        }

        MemInfo->VideoRamBase = VirtualBase;
        MemInfo->VideoRamLength = MapLength;
        MemInfo->FrameBufferBase = VirtualBase;
        MemInfo->FrameBufferLength = Ext->CurrentWidth *
            Ext->CurrentHeight * (Ext->CurrentBpp / 8);

        RequestPacket->StatusBlock->Information = sizeof(VIDEO_MEMORY_INFORMATION);
        break;
    }

    case IOCTL_VIDEO_UNMAP_VIDEO_MEMORY:
    {
        PVIDEO_MEMORY MemReq;
        if (RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY)) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        MemReq = (PVIDEO_MEMORY)RequestPacket->InputBuffer;
        VideoPortUnmapMemory(HwDeviceExtension, MemReq->RequestedVirtualAddress, 0);
        break;
    }

    case IOCTL_VIDEO_RESET_DEVICE:
        /* Nothing to do — mode persists */
        break;

    case IOCTL_VIDEO_SET_COLOR_REGISTERS:
        /* FRAMEBUF uses direct color, no palette */
        break;

    default:
        status = ERROR_INVALID_FUNCTION;
        break;
    }

    RequestPacket->StatusBlock->Status = status;
    return TRUE;
}
