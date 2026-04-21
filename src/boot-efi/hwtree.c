#include "hwtree.h"
#include "arena.h"
#include "log.h"
#include "mmu.h"       /* KSEG0_BASE */
#include "uart.h"
#include "nt.h"

/* Every CONFIGURATION_COMPONENT_DATA link (Parent/Child/Sibling) is a
 * KSEG0 VA on the wire. Because arena_alloc returns phys-addressed
 * pointers whose KSEG0 alias is just `phys | KSEG0_BASE`, the cast is
 * direct — no lookup, no map. */
static UINT32 kseg0_of(void *phys_ptr) {
    return (UINT32)((UINTN)phys_ptr | KSEG0_BASE);
}

/* --- Disk (root node's ConfigurationData blob) -------------------------- */

/* atdisk.sys reads the root's ConfigurationData as:
 *   CM_FULL_RESOURCE_DESCRIPTOR
 *   CM_INT13_DRIVE_PARAMETER[N]
 *
 * CmpInitializeRegistryNode (NTOS/CONFIG/CMCONFIG.C:657-705) expects the
 * node to hand over a CM_PARTIAL_RESOURCE_LIST — the kernel prepends
 * the InterfaceType+BusNumber header itself. So we emit just the partial
 * list + the INT13 param struct as the trailing DeviceSpecific payload. */
static void *build_disk_blob(const hwtree_disk_info *disk,
                             UINTN *out_size_bytes) {
    struct __attribute__((aligned(4))) {
        NT_CM_PARTIAL_RESOURCE_LIST partial;
        NT_CM_INT13_DRIVE_PARAMETER drive0;
    } *blob = arena_alloc(sizeof *blob, 4);
    if (!blob) return 0;

    blob->partial.Version  = 1;
    blob->partial.Revision = 1;
    blob->partial.Count    = 1;
    blob->partial.PartialDescriptors[0].Type             = NT_CmResourceTypeDeviceSpecific;
    blob->partial.PartialDescriptors[0].ShareDisposition = 0;
    blob->partial.PartialDescriptors[0].Flags            = 0;
    blob->partial.PartialDescriptors[0].u.DeviceSpecificData.DataSize =
        sizeof(NT_CM_INT13_DRIVE_PARAMETER);

    /* Fabricate CHS from the UEFI-reported block count. atdisk's
     * PartitionLength is derived from cyl*heads*spt*block_size, so the
     * CHS cube must cover the whole disk. Fix heads=16 / spt=63 (BIOS
     * INT 13h translation ceiling), cyls = ceil(blocks / (heads*spt)).
     * 32-bit math only: freestanding ia32 has no __udivdi3, and 32-bit
     * sector counts cover up to 4 TiB @ 512 B — plenty. */
    {
        UINT32 spt = 63, heads = 16;
        UINT32 blocks_u32 = (disk->total_blocks > 0xFFFFFFFFULL)
                          ? 0xFFFFFFFFu
                          : (UINT32)disk->total_blocks;
        UINT32 cyls = (blocks_u32 + (heads * spt) - 1) / (heads * spt);
        if (cyls == 0) cyls = 1;

        blob->drive0.DriveSelect      = 0x80;              /* INT13 disk 0 */
        blob->drive0.MaxCylinders     = cyls - 1;          /* 0-based */
        blob->drive0.SectorsPerTrack  = (UINT16)spt;
        blob->drive0.MaxHeads         = (UINT16)(heads - 1);
        blob->drive0.NumberDrives     = 1;

        BXLOG(L"DiskController 0: %u cyl x %u head x %u sec", cyls, heads, spt);
    }

    *out_size_bytes = sizeof *blob;
    return blob;
}

/* --- Serial controller (per-UART resource list) ------------------------- */

/* 2-descriptor partial resource list: Port range + Interrupt. */
static void *build_serial_blob(UINT16 io_base, UINT8 irq,
                               UINTN *out_size_bytes) {
    struct __attribute__((aligned(4))) {
        NT_CM_PARTIAL_RESOURCE_LIST       hdr;      /* Count=2, Descriptors[0] */
        NT_CM_PARTIAL_RESOURCE_DESCRIPTOR desc1;    /* trailing [1] */
    } *rlist = arena_alloc(sizeof *rlist, 4);
    if (!rlist) return 0;

    rlist->hdr.Version  = 1;
    rlist->hdr.Revision = 1;
    rlist->hdr.Count    = 2;

    /* Descriptor 0: I/O port range (base .. base+7, 8 bytes). */
    rlist->hdr.PartialDescriptors[0].Type             = NT_CmResourceTypePort;
    rlist->hdr.PartialDescriptors[0].ShareDisposition = NT_CmResourceShareDriverExclusive;
    rlist->hdr.PartialDescriptors[0].Flags            = NT_CM_RESOURCE_PORT_IO;
    rlist->hdr.PartialDescriptors[0].u.Port.Start     = io_base;
    rlist->hdr.PartialDescriptors[0].u.Port.Length    = 8;

    /* Descriptor 1: Interrupt (Level=Vector=IRQ, edge-triggered, single-CPU). */
    rlist->desc1.Type             = NT_CmResourceTypeInterrupt;
    rlist->desc1.ShareDisposition = NT_CmResourceShareDeviceExclusive;
    rlist->desc1.Flags            = NT_CM_RESOURCE_INTERRUPT_LATCHED;
    rlist->desc1.u.Interrupt.Level    = irq;
    rlist->desc1.u.Interrupt.Vector   = irq;
    rlist->desc1.u.Interrupt.Affinity = 1;

    *out_size_bytes = sizeof *rlist;
    return rlist;
}

/* --- Node helpers ------------------------------------------------------- */

static CONFIGURATION_COMPONENT_DATA *make_node(
        UINT32 parent_kseg0,
        CONFIGURATION_CLASS cls, CONFIGURATION_TYPE type, UINT32 key,
        const char *identifier,
        void *config_data, UINTN config_data_len) {
    CONFIGURATION_COMPONENT_DATA *n = arena_alloc(sizeof *n, 4);
    if (!n) return 0;
    n->Parent                           = parent_kseg0;
    n->ComponentEntry.Class             = cls;
    n->ComponentEntry.Type              = type;
    n->ComponentEntry.Key               = key;
    if (identifier) {
        char *id = arena_dup_ascii(identifier);
        if (!id) return 0;
        n->ComponentEntry.Identifier       = kseg0_of(id);
        /* IdentifierLength = strlen + 1 (includes NUL); matches kernel's
         * expectation when it hashes the identifier into the registry key. */
        UINTN idlen = 0;
        while (identifier[idlen]) idlen++;
        n->ComponentEntry.IdentifierLength = (UINT32)(idlen + 1);
    }
    if (config_data) {
        n->ComponentEntry.ConfigurationDataLength = (UINT32)config_data_len;
        n->ConfigurationData                      = kseg0_of(config_data);
    }
    return n;
}

static void link_as_child(CONFIGURATION_COMPONENT_DATA *parent_phys,
                          CONFIGURATION_COMPONENT_DATA *child_phys) {
    parent_phys->Child = kseg0_of(child_phys);
}

static void link_as_next_sibling(CONFIGURATION_COMPONENT_DATA *sib_phys,
                                 CONFIGURATION_COMPONENT_DATA *new_phys) {
    sib_phys->Sibling = kseg0_of(new_phys);
}

/* --- Public entry ------------------------------------------------------- */

UINT32 hwtree_build(const hwtree_disk_info *disk) {
    /* 1. Root: SystemClass/ArcSystem with INT13 drive params. */
    UINTN disk_blob_sz = 0;
    void *disk_blob = build_disk_blob(disk, &disk_blob_sz);
    if (!disk_blob) return 0;

    CONFIGURATION_COMPONENT_DATA *root = make_node(
        0, SystemClass, ArcSystem, 0, 0, disk_blob, disk_blob_sz);
    if (!root) return 0;

    /* 2. AdapterClass/MultifunctionAdapter ("ISA"). No ConfigurationData;
     *    the Identifier alone marks it as the ISA bus. */
    CONFIGURATION_COMPONENT_DATA *isa = make_node(
        kseg0_of(root), AdapterClass, MultifunctionAdapter, 0, "ISA", 0, 0);
    if (!isa) return 0;
    link_as_child(root, isa);

    /* 3. Probe each standard ISA COM base; emit a SerialController child
     *    under isa for each live UART. */
    static const struct { UINT16 base; UINT8 irq; const char *name; } COM[] = {
        { 0x3F8, 4, "COM1" },
        { 0x2F8, 3, "COM2" },
        { 0x3E8, 4, "COM3" },
        { 0x2E8, 3, "COM4" },
    };
    CONFIGURATION_COMPONENT_DATA *last_sib = 0;
    UINT32 emitted = 0;

    for (UINTN i = 0; i < sizeof(COM) / sizeof(COM[0]); i++) {
        if (!uart_probe(COM[i].base)) continue;

        UINTN rlist_sz = 0;
        void *rlist = build_serial_blob(COM[i].base, COM[i].irq, &rlist_sz);
        if (!rlist) return 0;

        CONFIGURATION_COMPONENT_DATA *ctrl = make_node(
            kseg0_of(isa), ControllerClass, SerialController, emitted,
            COM[i].name, rlist, rlist_sz);
        if (!ctrl) return 0;

        if (last_sib) link_as_next_sibling(last_sib, ctrl);
        else          link_as_child(isa, ctrl);
        last_sib = ctrl;
        emitted++;

        BXLOG(L"SerialController %a @ 0x%x IRQ=%u",
              COM[i].name, COM[i].base, COM[i].irq);
    }
    if (emitted == 0) BXLOG(L"no UARTs detected");

    return kseg0_of(root);
}
