/*
 * ARC hardware-inventory tree — the replacement for NTDETECT.COM.
 *
 * Under legacy BIOS, NTDETECT.COM probed the machine and wrote an ARC-
 * style CONFIGURATION_COMPONENT_DATA tree into the LoaderBlock. The
 * kernel's CmpInitializeRegistryNode (NTOS/CONFIG/CMCONFIG.C:657) walks
 * that tree and materialises
 *
 *   \Registry\Machine\Hardware\Description\System\
 *     MultifunctionAdapter\N\
 *       SerialController\M\  — ConfigurationData = partial resource list
 *       DiskController\M\    — ConfigurationData = INT13 drive params
 *       …
 *
 * Drivers (serial.sys, atdisk.sys, …) read those registry keys at
 * DriverEntry time to learn which hardware to claim.
 *
 * Under UEFI we synthesize the equivalent tree here — no NTDETECT.
 * Allocations come from the arena; the returned root-node KSEG0 VA is
 * stored into LOADER_PARAMETER_BLOCK.ConfigurationRoot by the caller.
 */
#ifndef _BOOT_EFI_HWTREE_H_
#define _BOOT_EFI_HWTREE_H_

#include <efi.h>

typedef struct {
    UINT64 total_blocks;   /* boot disk total sector count (UEFI BlockIo) */
    UINT32 block_size;     /* typically 512 */
} hwtree_disk_info;

/* Emit the full tree:
 *   SystemClass/ArcSystem (root, ConfigurationData = INT13 drive params)
 *     AdapterClass/MultifunctionAdapter (Identifier="ISA")
 *       ControllerClass/SerialController (one per probed UART)
 *
 * Nodes + resource blobs go into the arena. Each emitted node is logged
 * on com1. Returns the KSEG0 VA of the root node, or 0 on arena OOM. */
UINT32 hwtree_build(const hwtree_disk_info *disk);

#endif
