/*++

    vioinput.c — virtio-input device driver (skeleton).

    Brings up a virtio-input PCI device (1AF4:1052, modern only):
    walks caps, maps BARs, negotiates features, sets up the two
    virtqueues defined by the spec sec 5.8:

      queue 0  EventQ   - device -> driver  (input events flow in)
      queue 1  StatusQ  - driver -> device  (LED reports flow out)

    Each buffer is one struct virtio_input_event { type, code, value }
    (8 bytes). The driver pre-posts a pool of EventQ buffers at init
    time; the device fills them as events occur and ISR-fires; the
    DPC drains the used ring, hands each event to a per-class
    translator, and re-posts the buffer.

    This skeleton wires the bring-up + ISR/DPC + event dump only.
    Per-class translators (kbdclass / mouclass) layer in afterwards
    once we've confirmed the wiring on real EV_KEY / EV_REL traffic.

    Reference: kvm-guest-drivers-windows/vioinput/sys/Device.c
    (DPC + queue layout) — adapted to NT 3.5's older driver model
    (no WDF, no HIDCLASS, no PnP — DriverEntry walks PCI itself).

--*/

#include <ntddk.h>
#include <ntddkbd.h>
#include <ntddmou.h>
#include "kbdmou.h"      /* CONNECT_DATA, IOCTL_INTERNAL_*_CONNECT,
                            PSERVICE_CALLBACK_ROUTINE shared by kbd+mou ports */
#include "virtio.h"
#include "virtio_pci.h"
#include "virtio_ids.h"

/* ------------------------------------------------------------------ *
 * virtio-input wire format (spec sec 5.8).
 * ------------------------------------------------------------------ */

/* One event slot the device fills (or driver writes for status).
   Linux's input subsystem speaks the same struct, so the type/code
   namespace is the Linux one (EV_KEY / KEY_A / EV_REL / REL_X / ...). */
typedef struct _VIRTIO_INPUT_EVENT {
    USHORT  Type;
    USHORT  Code;
    ULONG   Value;
} VIRTIO_INPUT_EVENT, *PVIRTIO_INPUT_EVENT;

/* Event types we care about. */
#define EV_SYN          0x00    /* end-of-packet marker */
#define EV_KEY          0x01    /* key/button press/release; value=1/0 */
#define EV_REL          0x02    /* relative axis; value=delta */
#define EV_ABS          0x03    /* absolute axis; value=position */
#define EV_MSC          0x04    /* misc — scancode, etc. */
#define EV_LED          0x11    /* LED state (in StatusQ) */

/* Config-space selector layout (spec sec 5.8.4). The driver writes
   {Select, Subsel} into the device-config region, then reads back
   Size + the corresponding payload. Used to enumerate which keys /
   axes / LEDs the device supports — drives keyboard-vs-mouse
   classification. */
#define VIRTIO_INPUT_CFG_UNSET      0x00
#define VIRTIO_INPUT_CFG_ID_NAME    0x01
#define VIRTIO_INPUT_CFG_ID_SERIAL  0x02
#define VIRTIO_INPUT_CFG_ID_DEVIDS  0x03
#define VIRTIO_INPUT_CFG_PROP_BITS  0x10
#define VIRTIO_INPUT_CFG_EV_BITS    0x11
#define VIRTIO_INPUT_CFG_ABS_INFO   0x12

typedef struct _VIRTIO_INPUT_CONFIG {
    UCHAR   Select;
    UCHAR   Subsel;
    UCHAR   Size;
    UCHAR   Reserved[5];
    union {
        UCHAR   Bitmap[128];
        CHAR    String[128];
        struct {
            ULONG  Min;
            ULONG  Max;
            ULONG  Fuzz;
            ULONG  Flat;
            ULONG  Res;
        } AbsInfo;
        struct {
            USHORT BusType;
            USHORT Vendor;
            USHORT Product;
            USHORT Version;
        } Ids;
    } u;
} VIRTIO_INPUT_CONFIG, *PVIRTIO_INPUT_CONFIG;

/* Selected event-code namespaces - we only need the few that drive
   keyboard-vs-mouse-vs-tablet classification + later translation. */
#define REL_X           0x00
#define REL_Y           0x01
#define ABS_X           0x00
#define ABS_Y           0x01
#define BTN_MISC        0x100   /* first non-keyboard key code */

/* What the device looks like, once we've inspected its config. The
   per-class translator (kbdclass / mouclass binding) keys off this. */
typedef enum _VIOINPUT_CLASS {
    VIOINPUT_CLASS_UNKNOWN = 0,
    VIOINPUT_CLASS_KEYBOARD,
    VIOINPUT_CLASS_MOUSE,
    VIOINPUT_CLASS_TABLET
} VIOINPUT_CLASS;

/* ------------------------------------------------------------------ *
 * Linux input-event-codes -> NT class-driver translation tables.
 *
 * virtio-input emits Linux KEY_* / BTN_* codes; the device firmware
 * (or QEMU) has already done host-keyboard -> linux-keycode for us.
 * The tables here finish the journey: KEY_* -> AT scan-set-1 make
 * code (with E0 / E1 prefix flags) for kbdclass; BTN_LEFT / RIGHT /
 * MIDDLE / SIDE / EXTRA -> MOUSE_*_BUTTON_* flags for mouclass.
 *
 * Using a flat array (not C99 designated initialisers - NT 3.5's CL
 * pre-dates C99) so the index = Linux code, value = NT encoding.
 * Codes we don't translate are 0 and the per-class translator will
 * silently drop them (eventually behind a DBG-gated log).
 * ------------------------------------------------------------------ */

/* Encoding of one entry in VioInputKeyMap[]:
   bits  7..0  AT scan-set-1 make code
   bit   8     E0 prefix needed (extended key: arrows, R-Ctrl, etc.)
   bit   9     E1 prefix needed (Pause/Break - the only such key)
   0           code is unmapped or unsupported. */
#define VIOK_E0     0x100
#define VIOK_E1     0x200

/* 128 entries cover everything a virtio-keyboard typically exposes
   (Linux keyboard codes run 1..127; BTN_* codes start at 0x100).
   See linux/input-event-codes.h for the source of truth. */
static const USHORT VioInputKeyMap[128] = {
    /* 0x00 KEY_RESERVED      */ 0,
    /* 0x01 KEY_ESC           */ 0x01,
    /* 0x02 KEY_1             */ 0x02,
    /* 0x03 KEY_2             */ 0x03,
    /* 0x04 KEY_3             */ 0x04,
    /* 0x05 KEY_4             */ 0x05,
    /* 0x06 KEY_5             */ 0x06,
    /* 0x07 KEY_6             */ 0x07,
    /* 0x08 KEY_7             */ 0x08,
    /* 0x09 KEY_8             */ 0x09,
    /* 0x0a KEY_9             */ 0x0a,
    /* 0x0b KEY_0             */ 0x0b,
    /* 0x0c KEY_MINUS         */ 0x0c,
    /* 0x0d KEY_EQUAL         */ 0x0d,
    /* 0x0e KEY_BACKSPACE     */ 0x0e,
    /* 0x0f KEY_TAB           */ 0x0f,
    /* 0x10 KEY_Q             */ 0x10,
    /* 0x11 KEY_W             */ 0x11,
    /* 0x12 KEY_E             */ 0x12,
    /* 0x13 KEY_R             */ 0x13,
    /* 0x14 KEY_T             */ 0x14,
    /* 0x15 KEY_Y             */ 0x15,
    /* 0x16 KEY_U             */ 0x16,
    /* 0x17 KEY_I             */ 0x17,
    /* 0x18 KEY_O             */ 0x18,
    /* 0x19 KEY_P             */ 0x19,
    /* 0x1a KEY_LEFTBRACE     */ 0x1a,
    /* 0x1b KEY_RIGHTBRACE    */ 0x1b,
    /* 0x1c KEY_ENTER         */ 0x1c,
    /* 0x1d KEY_LEFTCTRL      */ 0x1d,
    /* 0x1e KEY_A             */ 0x1e,
    /* 0x1f KEY_S             */ 0x1f,
    /* 0x20 KEY_D             */ 0x20,
    /* 0x21 KEY_F             */ 0x21,
    /* 0x22 KEY_G             */ 0x22,
    /* 0x23 KEY_H             */ 0x23,
    /* 0x24 KEY_J             */ 0x24,
    /* 0x25 KEY_K             */ 0x25,
    /* 0x26 KEY_L             */ 0x26,
    /* 0x27 KEY_SEMICOLON     */ 0x27,
    /* 0x28 KEY_APOSTROPHE    */ 0x28,
    /* 0x29 KEY_GRAVE         */ 0x29,
    /* 0x2a KEY_LEFTSHIFT     */ 0x2a,
    /* 0x2b KEY_BACKSLASH     */ 0x2b,
    /* 0x2c KEY_Z             */ 0x2c,
    /* 0x2d KEY_X             */ 0x2d,
    /* 0x2e KEY_C             */ 0x2e,
    /* 0x2f KEY_V             */ 0x2f,
    /* 0x30 KEY_B             */ 0x30,
    /* 0x31 KEY_N             */ 0x31,
    /* 0x32 KEY_M             */ 0x32,
    /* 0x33 KEY_COMMA         */ 0x33,
    /* 0x34 KEY_DOT           */ 0x34,
    /* 0x35 KEY_SLASH         */ 0x35,
    /* 0x36 KEY_RIGHTSHIFT    */ 0x36,
    /* 0x37 KEY_KPASTERISK    */ 0x37,
    /* 0x38 KEY_LEFTALT       */ 0x38,
    /* 0x39 KEY_SPACE         */ 0x39,
    /* 0x3a KEY_CAPSLOCK      */ 0x3a,
    /* 0x3b KEY_F1            */ 0x3b,
    /* 0x3c KEY_F2            */ 0x3c,
    /* 0x3d KEY_F3            */ 0x3d,
    /* 0x3e KEY_F4            */ 0x3e,
    /* 0x3f KEY_F5            */ 0x3f,
    /* 0x40 KEY_F6            */ 0x40,
    /* 0x41 KEY_F7            */ 0x41,
    /* 0x42 KEY_F8            */ 0x42,
    /* 0x43 KEY_F9            */ 0x43,
    /* 0x44 KEY_F10           */ 0x44,
    /* 0x45 KEY_NUMLOCK       */ 0x45,
    /* 0x46 KEY_SCROLLLOCK    */ 0x46,
    /* 0x47 KEY_KP7           */ 0x47,
    /* 0x48 KEY_KP8           */ 0x48,
    /* 0x49 KEY_KP9           */ 0x49,
    /* 0x4a KEY_KPMINUS       */ 0x4a,
    /* 0x4b KEY_KP4           */ 0x4b,
    /* 0x4c KEY_KP5           */ 0x4c,
    /* 0x4d KEY_KP6           */ 0x4d,
    /* 0x4e KEY_KPPLUS        */ 0x4e,
    /* 0x4f KEY_KP1           */ 0x4f,
    /* 0x50 KEY_KP2           */ 0x50,
    /* 0x51 KEY_KP3           */ 0x51,
    /* 0x52 KEY_KP0           */ 0x52,
    /* 0x53 KEY_KPDOT         */ 0x53,
    /* 0x54 (gap)             */ 0,
    /* 0x55 KEY_ZENKAKUHANKAKU*/ 0,
    /* 0x56 KEY_102ND         */ 0x56,
    /* 0x57 KEY_F11           */ 0x57,
    /* 0x58 KEY_F12           */ 0x58,
    /* 0x59 KEY_RO            */ 0,
    /* 0x5a KEY_KATAKANA      */ 0,
    /* 0x5b KEY_HIRAGANA      */ 0,
    /* 0x5c KEY_HENKAN        */ 0,
    /* 0x5d KEY_KATAKANAHIRA  */ 0,
    /* 0x5e KEY_MUHENKAN      */ 0,
    /* 0x5f KEY_KPJPCOMMA     */ 0,
    /* 0x60 KEY_KPENTER       */ 0x1c | VIOK_E0,
    /* 0x61 KEY_RIGHTCTRL     */ 0x1d | VIOK_E0,
    /* 0x62 KEY_KPSLASH       */ 0x35 | VIOK_E0,
    /* 0x63 KEY_SYSRQ         */ 0x37 | VIOK_E0,
    /* 0x64 KEY_RIGHTALT      */ 0x38 | VIOK_E0,
    /* 0x65 KEY_LINEFEED      */ 0,
    /* 0x66 KEY_HOME          */ 0x47 | VIOK_E0,
    /* 0x67 KEY_UP            */ 0x48 | VIOK_E0,
    /* 0x68 KEY_PAGEUP        */ 0x49 | VIOK_E0,
    /* 0x69 KEY_LEFT          */ 0x4b | VIOK_E0,
    /* 0x6a KEY_RIGHT         */ 0x4d | VIOK_E0,
    /* 0x6b KEY_END           */ 0x4f | VIOK_E0,
    /* 0x6c KEY_DOWN          */ 0x50 | VIOK_E0,
    /* 0x6d KEY_PAGEDOWN      */ 0x51 | VIOK_E0,
    /* 0x6e KEY_INSERT        */ 0x52 | VIOK_E0,
    /* 0x6f KEY_DELETE        */ 0x53 | VIOK_E0,
    /* 0x70 KEY_MACRO         */ 0,
    /* 0x71 KEY_MUTE          */ 0,
    /* 0x72 KEY_VOLUMEDOWN    */ 0,
    /* 0x73 KEY_VOLUMEUP      */ 0,
    /* 0x74 KEY_POWER         */ 0,
    /* 0x75 KEY_KPEQUAL       */ 0,
    /* 0x76 KEY_KPPLUSMINUS   */ 0,
    /* 0x77 KEY_PAUSE         */ 0x1d | VIOK_E1,  /* needs E1 1D 45 sequence; classify-only here */
    /* 0x78 KEY_SCALE         */ 0,
    /* 0x79 KEY_KPCOMMA       */ 0,
    /* 0x7a KEY_HANGEUL       */ 0,
    /* 0x7b KEY_HANJA         */ 0,
    /* 0x7c KEY_YEN           */ 0,
    /* 0x7d KEY_LEFTMETA      */ 0x5b | VIOK_E0,
    /* 0x7e KEY_RIGHTMETA     */ 0x5c | VIOK_E0,
    /* 0x7f KEY_COMPOSE       */ 0x5d | VIOK_E0,
};

/* Mouse button maps. Indexed by (linux_code - BTN_LEFT). Five entries
   covers BTN_LEFT/RIGHT/MIDDLE/SIDE/EXTRA — anything more exotic
   (BTN_FORWARD/BACK/TASK) is silently dropped. */
static const USHORT VioInputMouseButtonDown[5] = {
    MOUSE_LEFT_BUTTON_DOWN,    /* BTN_LEFT   = 0x110 */
    MOUSE_RIGHT_BUTTON_DOWN,   /* BTN_RIGHT  = 0x111 */
    MOUSE_MIDDLE_BUTTON_DOWN,  /* BTN_MIDDLE = 0x112 */
    MOUSE_BUTTON_4_DOWN,       /* BTN_SIDE   = 0x113 */
    MOUSE_BUTTON_5_DOWN,       /* BTN_EXTRA  = 0x114 */
};
static const USHORT VioInputMouseButtonUp[5] = {
    MOUSE_LEFT_BUTTON_UP,
    MOUSE_RIGHT_BUTTON_UP,
    MOUSE_MIDDLE_BUTTON_UP,
    MOUSE_BUTTON_4_UP,
    MOUSE_BUTTON_5_UP,
};

#define BTN_LEFT  0x110

/* Translate Linux KEY_* code into NT MakeCode + Flags suitable for
   KEYBOARD_INPUT_DATA. value=1 (press) -> KEY_MAKE, value=0 (release)
   -> KEY_BREAK. Returns FALSE if the code has no NT mapping. */
static BOOLEAN
VioInputTranslateKey(USHORT linux_code, ULONG value,
                     PUSHORT make_code_out, PUSHORT flags_out)
{
    USHORT enc;
    USHORT flags = (value == 0) ? KEY_BREAK : KEY_MAKE;

    if (linux_code >= 128) return FALSE;
    enc = VioInputKeyMap[linux_code];
    if (enc == 0) return FALSE;

    if (enc & VIOK_E0) flags |= KEY_E0;
    if (enc & VIOK_E1) flags |= KEY_E1;

    *make_code_out = (USHORT)(enc & 0xFF);
    *flags_out     = flags;
    return TRUE;
}

/* Translate Linux BTN_* code + value into a MOUSE_INPUT_DATA
   ButtonFlags bit. Returns 0 if the code has no NT mapping. */
static USHORT
VioInputTranslateMouseButton(USHORT linux_code, ULONG value)
{
    ULONG idx;
    if (linux_code < BTN_LEFT) return 0;
    idx = (ULONG)linux_code - BTN_LEFT;
    if (idx >= 5) return 0;
    return value ? VioInputMouseButtonDown[idx] : VioInputMouseButtonUp[idx];
}

/* Queue indices. */
#define VIRTIO_INPUT_Q_EVENT    0
#define VIRTIO_INPUT_Q_STATUS   1
#define VIRTIO_INPUT_NUM_VQS    2

/* How many events the device may have outstanding before the driver
   recycles a buffer. 64 is what the kvm reference uses; ample for
   any plausible mouse/keyboard burst. */
#define VIRTIO_INPUT_EVENT_BUFS 64

/* ------------------------------------------------------------------ *
 * Per-device extension.
 * ------------------------------------------------------------------ */
typedef struct _VIOINPUT_DEV {
    VIRTIO_PCI_DEV    Pci;
    PDEVICE_OBJECT    DevObj;
    PKINTERRUPT       Interrupt;
    KSPIN_LOCK        IsrLock;
    KDPC              EventDpc;
    PVIRTQUEUE        EventQ;
    PVIRTQUEUE        StatusQ;
    ULONG             Instance;     /* matches \Device\VirtioInput<N> name */
    VIOINPUT_CLASS    Class;        /* determined at attach via config probe */

    /* Class-driver binding. kbdclass / mouclass attach by sending
       IOCTL_INTERNAL_*_CONNECT on the port device's IRP path. We
       latch the CONNECT_DATA here; once ClassService is non-NULL the
       DPC delivers translated events through it. */
    CONNECT_DATA      KbdConnect;   /* keyboard-class binding */
    CONNECT_DATA      MouConnect;   /* mouse-class binding */

    /* Mouse event batching. virtio sends one event per axis / button
       and an EV_SYN marker to delimit packets - e.g. a single mouse
       motion-with-click is REL_X / REL_Y / BTN_LEFT / SYN. We
       accumulate axis deltas + button flag transitions into Pending
       and flush one MOUSE_INPUT_DATA to mouclass on each EV_SYN. */
    MOUSE_INPUT_DATA  PendingMouse;
    BOOLEAN           HasPendingMouse;

    /* Pool of event buffers. One contiguous NonPagedPool block —
       Buffers[i] is the i'th VIRTIO_INPUT_EVENT, BuffersPaddr its
       physical base. Cookie passed to the queue is the index, so
       Dequeue gives us back which slot fired. */
    PVIRTIO_INPUT_EVENT  Buffers;
    PHYSICAL_ADDRESS     BuffersPaddr;
    ULONG                NumBuffers;
} VIOINPUT_DEV, *PVIOINPUT_DEV;

/* Per-class port-name counters. kbdclass / mouclass enumerate
   \Device\KeyboardPort<K> / \Device\PointerPort<P> contiguously
   from 0; we hand out these indices to the symlinks we create
   alongside the primary \Device\VirtioInput<N> name. Module-static
   because DriverEntry's loop owns assignment. */
static ULONG g_NextKbdPort = 0;
static ULONG g_NextMouPort = 0;

/* ------------------------------------------------------------------ *
 * Forward declarations.
 * ------------------------------------------------------------------ */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath);

static NTSTATUS  VioInputCreateClose(PDEVICE_OBJECT DevObj, PIRP Irp);
static NTSTATUS  VioInputInternalIoctl(PDEVICE_OBJECT DevObj, PIRP Irp);
static BOOLEAN   VioInputIsr        (PKINTERRUPT Interrupt, PVOID Context);
static VOID      VioInputDpc        (PKDPC Dpc, PVOID Context, PVOID A1, PVOID A2);

static NTSTATUS  VioInputAttachOne     (PDRIVER_OBJECT DriverObject,
                                        PUNICODE_STRING RegPath,
                                        ULONG slot, ULONG instance);
static NTSTATUS  VioInputPrepostEventBufs(PVIOINPUT_DEV dev);
static NTSTATUS  VioInputCreateClassLink(PVIOINPUT_DEV dev,
                                         PUNICODE_STRING devName);
static VIOINPUT_CLASS VioInputClassify (PVIOINPUT_DEV dev);

/* ------------------------------------------------------------------ *
 * DriverEntry — runs once at I/O Manager init.
 * ------------------------------------------------------------------ */
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath)
{
    ULONG slot;
    ULONG instance = 0;
    PCI_COMMON_CONFIG cfg;
    ULONG got;

    DbgPrint("VIOINPUT: DriverEntry\n");

    DriverObject->MajorFunction[IRP_MJ_CREATE]                 = VioInputCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]                  = VioInputCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = VioInputInternalIoctl;

    /* Walk every PCI slot/function on bus 0 and bring up one
       \Device\VirtioInput<N> per virtio-input device found. A QEMU
       guest typically ships virtio-keyboard-pci + virtio-mouse-pci
       (sometimes virtio-tablet-pci) - all share VendorID/DeviceID
       1AF4:1052 and only differ in the EV_BITS bitmap exposed in
       the device-config region (queried in a later pass). */
    for (slot = 0; slot < 32 * 8; slot++) {
        got = HalGetBusDataByOffset(PCIConfiguration, 0, slot,
                                    &cfg, 0, sizeof(cfg));
        if (got < 4)                                continue;
        if (cfg.VendorID == 0xFFFF)                 continue;
        if (cfg.VendorID != VIRTIO_PCI_VENDOR_ID)   continue;
        if (cfg.DeviceID != VIRTIO_PCI_DEV_INPUT)   continue;

        if (NT_SUCCESS(VioInputAttachOne(DriverObject, RegPath,
                                         slot, instance))) {
            instance++;
        }
    }

    if (instance == 0) {
        DbgPrint("VIOINPUT: no virtio-input devices found\n");
        return STATUS_NO_SUCH_DEVICE;
    }

    DbgPrint("VIOINPUT: ready, %u device(s) listening on EventQ\n",
             instance);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * Bring up one virtio-input device at the given PCI slot. Caller
 * (DriverEntry) walked the bus, identified the slot as a 1AF4:1052
 * device, and assigned a 0-based instance number for naming
 * (\Device\VirtioInput<instance>).
 * ------------------------------------------------------------------ */
static NTSTATUS
VioInputAttachOne(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegPath,
                  ULONG slot, ULONG instance)
{
    UNICODE_STRING devName;
    WCHAR          devNameBuf[32];
    PDEVICE_OBJECT devObj;
    PVIOINPUT_DEV  dev;
    NTSTATUS       st;
    PCM_RESOURCE_LIST resources = NULL;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pd;
    ULONG          i;
    ULONG          intVector = 0;
    KIRQL          intLevel = 0;
    KAFFINITY      affinity = 0;
    /* VpciVqsFind writes vq_size[0..num_vqs-1]; declare as array. */
    u16            vqsizes[VIRTIO_INPUT_NUM_VQS];
    u16            vqsize;

    DbgPrint("VIOINPUT: attaching instance %u at bus0 slot 0x%02x\n",
             instance, slot);

    /* (1) HAL resource arbitration — we only use the IRQ. */
    st = HalAssignSlotResources(RegPath, NULL, DriverObject, NULL,
                                PCIBus, 0, slot, &resources);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: HalAssignSlotResources failed 0x%08x\n",
                 instance, st);
        return st;
    }
    for (i = 0; i < resources->List[0].PartialResourceList.Count; i++) {
        pd = &resources->List[0].PartialResourceList.PartialDescriptors[i];
        if (pd->Type == CmResourceTypeInterrupt && intVector == 0) {
            intVector = pd->u.Interrupt.Vector;
            intLevel  = (KIRQL)pd->u.Interrupt.Level;
        }
    }
    if (!intVector) {
        DbgPrint("VIOINPUT[%u]: missing IRQ resource\n", instance);
        ExFreePool(resources);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    {
        ULONG sysVector;
        KIRQL sysIrql = 0;
        sysVector = HalGetInterruptVector(PCIBus, 0, intLevel, intVector,
                                          &sysIrql, &affinity);
        DbgPrint("VIOINPUT[%u]: bus IRQ %u/%u -> system vec=%u irql=%u affinity=0x%x\n",
                 instance, intVector, intLevel,
                 sysVector, sysIrql, (ULONG)affinity);
        intVector = sysVector;
        intLevel  = sysIrql;
    }

    /* (2) Create the device object. Name carries the instance index
       so each virtio-input function ends up at its own \Device entry.
       RtlIntegerToUnicodeString overwrites Destination (does not
       append), so build the number into a scratch buffer first then
       append both halves into devName. */
    {
        UNICODE_STRING base, num;
        WCHAR          numBuf[16];

        RtlInitUnicodeString(&base, L"\\Device\\VirtioInput");

        num.Buffer        = numBuf;
        num.MaximumLength = sizeof(numBuf);
        num.Length        = 0;
        RtlIntegerToUnicodeString(instance, 10, &num);

        devName.Buffer        = devNameBuf;
        devName.MaximumLength = sizeof(devNameBuf);
        devName.Length        = 0;
        RtlAppendUnicodeStringToString(&devName, &base);
        RtlAppendUnicodeStringToString(&devName, &num);
    }
    st = IoCreateDevice(DriverObject, sizeof(VIOINPUT_DEV), &devName,
                        FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: IoCreateDevice failed 0x%08x\n",
                 instance, st);
        ExFreePool(resources);
        return st;
    }
    devObj->Flags |= DO_BUFFERED_IO;

    dev = (PVIOINPUT_DEV)devObj->DeviceExtension;
    RtlZeroMemory(dev, sizeof(*dev));
    dev->DevObj   = devObj;
    dev->Instance = instance;
    KeInitializeSpinLock(&dev->IsrLock);
    KeInitializeDpc(&dev->EventDpc, VioInputDpc, dev);

    /* (3) virtio_pci modern-transport bring-up. */
    st = VirtioPciInit(&dev->Pci, 0, slot,
                       intVector, intLevel, affinity,
                       VIRTIO_ID_INPUT);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: VirtioPciInit failed 0x%08x\n",
                 instance, st);
        goto fail_dev;
    }

    VirtioDevReset(&dev->Pci.Vdev);
    VirtioDevStatusUpdate(&dev->Pci.Vdev, VIRTIO_STATUS_ACK);
    VirtioDevStatusUpdate(&dev->Pci.Vdev,
                          VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    dev->Pci.Vdev.Features = VirtioFeatureGet(&dev->Pci.Vdev);
    DbgPrint("VIOINPUT[%u]: device features 0x%08x\n",
             instance, (ULONG)dev->Pci.Vdev.Features);
    /* No driver-side feature bits to opt into beyond VIRTIO_F_VERSION_1
       (handled by the modern transport itself). */
    VirtioFeatureSet(&dev->Pci.Vdev);

    /* (4) Probe device config to learn what kind of input device we
       got - the same 1AF4:1052 PCI ID covers keyboard, mouse, tablet,
       joystick, etc. The class drives which NT class driver we bind
       to in the kbdclass / mouclass pass. */
    dev->Class = VioInputClassify(dev);

    /* Expose a class-driver-friendly name. kbdclass scans
       \Device\KeyboardPort<K>, mouclass scans \Device\PointerPort<P>
       starting at 0; symlink each input device into the namespace so
       the class drivers find us without having to know about
       \Device\VirtioInput<N>. Errors are non-fatal - the device
       still exists, just won't pair with a class driver. */
    VioInputCreateClassLink(dev, &devName);

    /* (5) Both vqs. */
    st = VirtioFindVqs(&dev->Pci.Vdev, VIRTIO_INPUT_NUM_VQS, vqsizes);
    /* Both queues typically have the same depth; pick min defensively. */
    vqsize = (vqsizes[0] < vqsizes[1]) ? vqsizes[0] : vqsizes[1];
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: VirtioFindVqs failed 0x%08x\n", instance, st);
        goto fail_dev;
    }
    DbgPrint("VIOINPUT[%u]: vq descriptors=%u\n", instance, vqsize);

    st = VirtioVqSetup(&dev->Pci.Vdev, VIRTIO_INPUT_Q_EVENT,
                       vqsize, NULL, &dev->EventQ);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: EventQ setup failed 0x%08x\n", instance, st);
        goto fail_dev;
    }
    dev->EventQ->Priv = dev;

    st = VirtioVqSetup(&dev->Pci.Vdev, VIRTIO_INPUT_Q_STATUS,
                       vqsize, NULL, &dev->StatusQ);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: StatusQ setup failed 0x%08x\n", instance, st);
        goto fail_dev;
    }
    dev->StatusQ->Priv = dev;

    /* (6) Allocate + pre-post the event buffer pool. The device must
       have descriptors in the available ring before it can deliver
       events; we top it back up from the DPC after each drain. */
    st = VioInputPrepostEventBufs(dev);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: pre-post EventQ buffers failed 0x%08x\n",
                 instance, st);
        goto fail_dev;
    }

    /* (7) Wire the interrupt. After this, the device may fire. */
    st = IoConnectInterrupt(&dev->Interrupt, VioInputIsr, dev,
                            NULL, intVector, intLevel, intLevel,
                            LevelSensitive, TRUE, affinity, FALSE);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: IoConnectInterrupt failed 0x%08x\n",
                 instance, st);
        goto fail_dev;
    }

    /* (8) Mark driver up — the device now starts delivering events
       into the buffers we pre-posted. */
    VirtioDevDriverUp(&dev->Pci.Vdev);
    VirtqHostNotify(dev->EventQ);

    ExFreePool(resources);
    return STATUS_SUCCESS;

fail_dev:
    if (dev->Buffers)  ExFreePool(dev->Buffers);
    if (dev->EventQ)   VirtioVqRelease(&dev->Pci.Vdev, dev->EventQ);
    if (dev->StatusQ)  VirtioVqRelease(&dev->Pci.Vdev, dev->StatusQ);
    IoDeleteDevice(devObj);
    ExFreePool(resources);
    return st;
}

/* ------------------------------------------------------------------ *
 * Config-space probing + device classification.
 *
 * virtio-input exposes a bank of selectors at the head of its
 * device-config region:
 *
 *   +0  Select  (u8)   <- driver writes
 *   +1  Subsel  (u8)   <- driver writes
 *   +2  Size    (u8)   <- device reports payload length for current pair
 *   +3  Reserved[5]
 *   +8  payload (u128 / bitmap / absinfo / ids depending on Select)
 *
 * To enumerate which keys / axes the device supports, the driver
 * writes (Select=EV_BITS, Subsel=EV_KEY|EV_REL|EV_ABS) and reads back
 * Size + a bitmap. We don't need the full HID report descriptor the
 * kvm reference builds - just enough to decide:
 *
 *   has REL_X + REL_Y                  -> mouse
 *   has ABS_X + ABS_Y, no REL          -> tablet
 *   has any EV_KEY code below BTN_MISC -> keyboard
 *
 * Classification drives which class driver (kbdclass / mouclass) we
 * later bind via IOCTL_INTERNAL_*_CONNECT.
 * ------------------------------------------------------------------ */

/* Writes (Select, Subsel) to the device-config region, then reads
   Size. Returns the size byte the device reports (0 = nothing). */
static UCHAR
VioInputSelectAndSize(PVIOINPUT_DEV dev, UCHAR select, UCHAR subsel)
{
    UCHAR size = 0;
    VirtioConfigSet(&dev->Pci.Vdev,
                    FIELD_OFFSET(VIRTIO_INPUT_CONFIG, Select), &select, 1);
    VirtioConfigSet(&dev->Pci.Vdev,
                    FIELD_OFFSET(VIRTIO_INPUT_CONFIG, Subsel), &subsel, 1);
    VirtioConfigGet(&dev->Pci.Vdev,
                    FIELD_OFFSET(VIRTIO_INPUT_CONFIG, Size), &size, 1, 1);
    return size;
}

static UCHAR
VioInputQueryEvBits(PVIOINPUT_DEV dev, UCHAR subsel,
                    UCHAR *bm_out, UCHAR bm_max)
{
    UCHAR size;
    size = VioInputSelectAndSize(dev, VIRTIO_INPUT_CFG_EV_BITS, subsel);
    if (size == 0)        return 0;
    if (size > bm_max)    size = bm_max;
    VirtioConfigGet(&dev->Pci.Vdev,
                    FIELD_OFFSET(VIRTIO_INPUT_CONFIG, u),
                    bm_out, size, 1);
    return size;
}

/* Read the device-name string into a NUL-terminated C buffer.
   Returns the byte count the device reported (0 if it has no name). */
static UCHAR
VioInputQueryName(PVIOINPUT_DEV dev, CHAR *name_out, UCHAR name_max)
{
    UCHAR size;
    size = VioInputSelectAndSize(dev, VIRTIO_INPUT_CFG_ID_NAME, 0);
    if (size == 0 || name_max < 1) {
        if (name_max >= 1) name_out[0] = 0;
        return 0;
    }
    if (size > name_max - 1) size = name_max - 1;
    VirtioConfigGet(&dev->Pci.Vdev,
                    FIELD_OFFSET(VIRTIO_INPUT_CONFIG, u),
                    name_out, size, 1);
    name_out[size] = 0;
    return size;
}

static BOOLEAN
VioInputBitTest(const UCHAR *bm, UCHAR nbytes, ULONG bit)
{
    if ((bit >> 3) >= nbytes) return FALSE;
    return (bm[bit >> 3] & (1 << (bit & 7))) != 0;
}

static VIOINPUT_CLASS
VioInputClassify(PVIOINPUT_DEV dev)
{
    /* Linux keys go up to KEY_MAX = 0x2FF; bitmap of all keys is
       96 bytes. Buttons (BTN_MISC=0x100 and up) live in the upper
       half. We fetch up to 96 bytes - enough to span both regions. */
    UCHAR keyBits[96];
    UCHAR relBits[8];
    UCHAR absBits[8];
    UCHAR keyN, relN, absN, i;
    BOOLEAN hasRelXY, hasAbsXY, hasNonButtonKey;
    CHAR  nameBuf[64];
    VIOINPUT_CLASS klass;
    const char *klassName;

    RtlZeroMemory(keyBits, sizeof(keyBits));
    RtlZeroMemory(relBits, sizeof(relBits));
    RtlZeroMemory(absBits, sizeof(absBits));
    nameBuf[0] = 0;

    keyN = VioInputQueryEvBits(dev, EV_KEY, keyBits, sizeof(keyBits));
    relN = VioInputQueryEvBits(dev, EV_REL, relBits, sizeof(relBits));
    absN = VioInputQueryEvBits(dev, EV_ABS, absBits, sizeof(absBits));
    VioInputQueryName(dev, nameBuf, sizeof(nameBuf));

    hasRelXY = VioInputBitTest(relBits, relN, REL_X) &&
               VioInputBitTest(relBits, relN, REL_Y);
    hasAbsXY = VioInputBitTest(absBits, absN, ABS_X) &&
               VioInputBitTest(absBits, absN, ABS_Y);

    /* Any non-zero byte in keyBits[0..31] means at least one key
       below BTN_MISC (0x100) is supported - that's a keyboard. Bytes
       32+ cover button codes 0x100+ which are mouse/joystick buttons. */
    hasNonButtonKey = FALSE;
    for (i = 0; i < keyN && (i * 8) < BTN_MISC; i++) {
        if (keyBits[i] != 0) { hasNonButtonKey = TRUE; break; }
    }

    if (hasRelXY)              klass = VIOINPUT_CLASS_MOUSE;
    else if (hasAbsXY)         klass = VIOINPUT_CLASS_TABLET;
    else if (hasNonButtonKey)  klass = VIOINPUT_CLASS_KEYBOARD;
    else                       klass = VIOINPUT_CLASS_UNKNOWN;

    switch (klass) {
        case VIOINPUT_CLASS_KEYBOARD: klassName = "keyboard"; break;
        case VIOINPUT_CLASS_MOUSE:    klassName = "mouse";    break;
        case VIOINPUT_CLASS_TABLET:   klassName = "tablet";   break;
        default:                       klassName = "unknown";  break;
    }

    DbgPrint("VIOINPUT[%u]: name='%s' -> %s "
             "(keybytes=%u relbytes=%u absbytes=%u)\n",
             dev->Instance, nameBuf, klassName, keyN, relN, absN);
    return klass;
}

/* ------------------------------------------------------------------ *
 * Class-driver visibility. kbdclass / mouclass enumerate port
 * devices by walking \Device\KeyboardPort<K> / \Device\PointerPort<P>
 * starting at 0 and stopping at the first name that doesn't open.
 *
 * Our primary device name is \Device\VirtioInput<N> (one global
 * counter, set by the outer DriverEntry loop). Here we add a second
 * name as an Object-Manager symbolic link pointing back to the
 * device, picked from the per-class counter so the class drivers
 * see contiguous numbering.
 *
 * Devices we can't classify (Class == UNKNOWN) get no symlink -
 * they remain reachable via the VirtioInput<N> name for diagnostic
 * read paths but no class driver attaches.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioInputCreateClassLink(PVIOINPUT_DEV dev, PUNICODE_STRING devName)
{
    UNICODE_STRING base, num, link;
    WCHAR          numBuf[16];
    WCHAR          linkBuf[40];
    NTSTATUS       st;
    ULONG          portIdx;
    const WCHAR    *baseStr;

    switch (dev->Class) {
        case VIOINPUT_CLASS_KEYBOARD:
            baseStr = L"\\Device\\KeyboardPort";
            portIdx = g_NextKbdPort++;
            break;
        case VIOINPUT_CLASS_MOUSE:
        case VIOINPUT_CLASS_TABLET:
            baseStr = L"\\Device\\PointerPort";
            portIdx = g_NextMouPort++;
            break;
        default:
            return STATUS_SUCCESS;  /* unclassified - no symlink */
    }

    RtlInitUnicodeString(&base, baseStr);

    num.Buffer        = numBuf;
    num.MaximumLength = sizeof(numBuf);
    num.Length        = 0;
    RtlIntegerToUnicodeString(portIdx, 10, &num);

    link.Buffer        = linkBuf;
    link.MaximumLength = sizeof(linkBuf);
    link.Length        = 0;
    RtlAppendUnicodeStringToString(&link, &base);
    RtlAppendUnicodeStringToString(&link, &num);

    st = IoCreateSymbolicLink(&link, devName);
    if (!NT_SUCCESS(st)) {
        DbgPrint("VIOINPUT[%u]: IoCreateSymbolicLink %wZ -> %wZ failed 0x%08x\n",
                 dev->Instance, &link, devName, st);
        return st;
    }
    DbgPrint("VIOINPUT[%u]: %wZ -> %wZ (class symlink)\n",
             dev->Instance, &link, devName);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * IRP_MJ_INTERNAL_DEVICE_CONTROL.
 *
 * kbdclass / mouclass send IOCTL_INTERNAL_*_CONNECT here, carrying a
 * CONNECT_DATA in Type3InputBuffer (METHOD_NEITHER). We latch the
 * ClassDeviceObject + ClassService pointer; from then on the DPC
 * path delivers translated input events through ClassService.
 *
 * Disconnect / Enable / Disable IOCTLs are no-ops at this stage -
 * we leave the stream running until driver unload. (The kvm
 * reference does the same.)
 * ------------------------------------------------------------------ */
static NTSTATUS
VioInputInternalIoctl(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    PVIOINPUT_DEV       dev = (PVIOINPUT_DEV)DevObj->DeviceExtension;
    PIO_STACK_LOCATION  sp  = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS            st  = STATUS_SUCCESS;

    switch (sp->Parameters.DeviceIoControl.IoControlCode) {
        case IOCTL_INTERNAL_KEYBOARD_CONNECT:
            if (dev->Class != VIOINPUT_CLASS_KEYBOARD) {
                st = STATUS_NO_SUCH_DEVICE;
                break;
            }
            if (sp->Parameters.DeviceIoControl.InputBufferLength
                    < sizeof(CONNECT_DATA)) {
                st = STATUS_INVALID_PARAMETER;
                break;
            }
            if (dev->KbdConnect.ClassService != NULL) {
                st = STATUS_SHARING_VIOLATION;
                break;
            }
            dev->KbdConnect = *((PCONNECT_DATA)
                sp->Parameters.DeviceIoControl.Type3InputBuffer);
            DbgPrint("VIOINPUT[%u]: kbdclass connected (devobj=%p svc=%p)\n",
                     dev->Instance,
                     dev->KbdConnect.ClassDeviceObject,
                     dev->KbdConnect.ClassService);
            break;

        case IOCTL_INTERNAL_MOUSE_CONNECT:
            if (dev->Class != VIOINPUT_CLASS_MOUSE &&
                dev->Class != VIOINPUT_CLASS_TABLET) {
                st = STATUS_NO_SUCH_DEVICE;
                break;
            }
            if (sp->Parameters.DeviceIoControl.InputBufferLength
                    < sizeof(CONNECT_DATA)) {
                st = STATUS_INVALID_PARAMETER;
                break;
            }
            if (dev->MouConnect.ClassService != NULL) {
                st = STATUS_SHARING_VIOLATION;
                break;
            }
            dev->MouConnect = *((PCONNECT_DATA)
                sp->Parameters.DeviceIoControl.Type3InputBuffer);
            DbgPrint("VIOINPUT[%u]: mouclass connected (devobj=%p svc=%p)\n",
                     dev->Instance,
                     dev->MouConnect.ClassDeviceObject,
                     dev->MouConnect.ClassService);
            break;

        case IOCTL_INTERNAL_KEYBOARD_DISCONNECT:
        case IOCTL_INTERNAL_MOUSE_DISCONNECT:
        case IOCTL_INTERNAL_KEYBOARD_ENABLE:
        case IOCTL_INTERNAL_KEYBOARD_DISABLE:
        case IOCTL_INTERNAL_MOUSE_ENABLE:
        case IOCTL_INTERNAL_MOUSE_DISABLE:
            /* No-op - we're always live; class drivers buffer/drop as needed. */
            break;

        default:
            st = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status      = st;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return st;
}

/* ------------------------------------------------------------------ *
 * Allocate the event buffer pool and feed every slot to the EventQ.
 * Cookie = buffer index (encoded as a pointer); Dequeue returns it
 * so we can find which slot the device wrote and re-post it.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioInputPrepostEventBufs(PVIOINPUT_DEV dev)
{
    ULONG poolBytes = VIRTIO_INPUT_EVENT_BUFS * sizeof(VIRTIO_INPUT_EVENT);
    ULONG i;
    NTSTATUS st;
    VIRTIO_SG_SEG  seg;
    VIRTIO_SG_LIST sg;

    dev->Buffers = (PVIRTIO_INPUT_EVENT)ExAllocatePoolWithTag(
        NonPagedPool, poolBytes, 'pInV');
    if (!dev->Buffers) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(dev->Buffers, poolBytes);

    dev->BuffersPaddr = MmGetPhysicalAddress(dev->Buffers);
    dev->NumBuffers   = VIRTIO_INPUT_EVENT_BUFS;

    sg.NumSegs = 1;
    sg.Segs    = &seg;

    for (i = 0; i < dev->NumBuffers; i++) {
        seg.Paddr.QuadPart =
            dev->BuffersPaddr.QuadPart + (i * sizeof(VIRTIO_INPUT_EVENT));
        seg.Len = sizeof(VIRTIO_INPUT_EVENT);

        /* read_bufs = 0 (driver -> device), write_bufs = 1
           (device fills our buffer). Cookie = (PVOID)(ULONG)i. */
        st = VirtqEnqueue(dev->EventQ, (PVOID)(ULONG)i,
                          &sg, 0, 1);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIOINPUT[%u]: EventQ enqueue %u/%u failed 0x%08x\n",
                     dev->Instance, i, dev->NumBuffers, st);
            return st;
        }
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * IRP_MJ_CREATE / IRP_MJ_CLOSE — placeholder; the real consumers
 * (kbdclass / mouclass) bind via IOCTL_INTERNAL_*_CONNECT later.
 * ------------------------------------------------------------------ */
static NTSTATUS
VioInputCreateClose(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DevObj);
    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ *
 * Interrupt service routine.
 * ------------------------------------------------------------------ */
static BOOLEAN
VioInputIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    PVIOINPUT_DEV dev = (PVIOINPUT_DEV)Context;
    int handled;

    UNREFERENCED_PARAMETER(Interrupt);

    handled = VirtioPciIsr(&dev->Pci);
    if (handled) {
        KeInsertQueueDpc(&dev->EventDpc, NULL, NULL);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ *
 * DPC — drain EventQ, log each event, re-post the buffer.
 *
 * Once kbdclass / mouclass binding lands, the DbgPrint here is
 * replaced with a per-class dispatcher (Kbd / Mouse / Tablet) that
 * accumulates events between EV_SYN markers and flushes a packet
 * to the bound class driver on EV_SYN.
 * ------------------------------------------------------------------ */
static VOID
VioInputDpc(PKDPC Dpc, PVOID Context, PVOID A1, PVOID A2)
{
    PVIOINPUT_DEV dev = (PVIOINPUT_DEV)Context;
    PVOID         cookie;
    u32           used_len;
    NTSTATUS      st;
    KIRQL         irql;
    ULONG         idx;
    PVIRTIO_INPUT_EVENT ev;
    VIRTIO_SG_SEG  seg;
    VIRTIO_SG_LIST sg;
    BOOLEAN        kicked = FALSE;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(A1);
    UNREFERENCED_PARAMETER(A2);

    sg.NumSegs = 1;
    sg.Segs    = &seg;

    KeAcquireSpinLock(&dev->IsrLock, &irql);
    for (;;) {
        st = VirtqDequeue(dev->EventQ, &cookie, &used_len);
        if (!NT_SUCCESS(st))
            break;

        idx = (ULONG)cookie;
        if (idx >= dev->NumBuffers) {
            DbgPrint("VIOINPUT[%u] DPC: bad cookie %u\n", dev->Instance, idx);
            continue;
        }
        ev = &dev->Buffers[idx];

        /* Translate + dispatch. Keyboard delivery is per-event
           (kbdclass buffers internally). Mouse delivery accumulates
           per-axis / per-button events into PendingMouse and flushes
           one MOUSE_INPUT_DATA on EV_SYN. */
        if (dev->Class == VIOINPUT_CLASS_KEYBOARD &&
            ev->Type == EV_KEY &&
            dev->KbdConnect.ClassService != NULL) {
            USHORT mk, fl;
            if (VioInputTranslateKey(ev->Code, ev->Value, &mk, &fl)) {
                KEYBOARD_INPUT_DATA kbd;
                ULONG               consumed = 0;
                kbd.UnitId           = (USHORT)dev->Instance;
                kbd.MakeCode         = mk;
                kbd.Flags            = fl;
                kbd.Reserved         = 0;
                kbd.ExtraInformation = 0;
                ((PSERVICE_CALLBACK_ROUTINE)dev->KbdConnect.ClassService)(
                    dev->KbdConnect.ClassDeviceObject,
                    &kbd, &kbd + 1, &consumed);
            }
        }
        else if (dev->Class == VIOINPUT_CLASS_MOUSE &&
                 dev->MouConnect.ClassService != NULL) {
            switch (ev->Type) {
                case EV_REL:
                    if (ev->Code == REL_X) {
                        dev->PendingMouse.LastX += (LONG)ev->Value;
                        dev->HasPendingMouse = TRUE;
                    } else if (ev->Code == REL_Y) {
                        dev->PendingMouse.LastY += (LONG)ev->Value;
                        dev->HasPendingMouse = TRUE;
                    }
                    /* REL_WHEEL / REL_HWHEEL: NT 3.5's MOUSE_INPUT_DATA
                       has no wheel flag (added in NT4) — silently drop
                       until we have a use for it from the Lua side. */
                    break;

                case EV_KEY: {
                    USHORT bf = VioInputTranslateMouseButton(ev->Code,
                                                             ev->Value);
                    if (bf) {
                        dev->PendingMouse.Buttons |= bf;
                        dev->HasPendingMouse = TRUE;
                    }
                    break;
                }

                case EV_SYN:
                    if (dev->HasPendingMouse) {
                        ULONG consumed = 0;
                        dev->PendingMouse.UnitId = (USHORT)dev->Instance;
                        dev->PendingMouse.Flags  = MOUSE_MOVE_RELATIVE;
                        ((PSERVICE_CALLBACK_ROUTINE)
                            dev->MouConnect.ClassService)(
                            dev->MouConnect.ClassDeviceObject,
                            &dev->PendingMouse,
                            &dev->PendingMouse + 1,
                            &consumed);
                        RtlZeroMemory(&dev->PendingMouse,
                                      sizeof(dev->PendingMouse));
                        dev->HasPendingMouse = FALSE;
                    }
                    break;
            }
        }

        /* Re-post this buffer for the next event. */
        seg.Paddr.QuadPart =
            dev->BuffersPaddr.QuadPart + (idx * sizeof(VIRTIO_INPUT_EVENT));
        seg.Len = sizeof(VIRTIO_INPUT_EVENT);
        st = VirtqEnqueue(dev->EventQ, (PVOID)(ULONG)idx,
                          &sg, 0, 1);
        if (!NT_SUCCESS(st)) {
            DbgPrint("VIOINPUT[%u] DPC: re-post idx=%u failed 0x%08x\n",
                     dev->Instance, idx, st);
            /* Buffer leaks from the EventQ until next reset; live
               with that for the skeleton, fix in the kbdclass pass. */
        } else {
            kicked = TRUE;
        }
    }
    if (kicked) VirtqHostNotify(dev->EventQ);
    KeReleaseSpinLock(&dev->IsrLock, irql);
}
