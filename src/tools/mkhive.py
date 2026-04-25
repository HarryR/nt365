#!/usr/bin/env python3
"""
mkhive.py - Generate an NT 3.5 registry hive from a declarative tree.

The high-level API is:
    hive = Hive("SYSTEM")
    hive["Select"].set_dword("current", 1)
    hive["Select"].set_dword("default", 1)
    hive["ControlSet001\\Control"]         # just creates the path
    hive["ControlSet001\\Services\\foo"].set_sz("Description", "Hello")
    hive.write("SYSTEM.hiv")

Nested paths auto-create intermediate keys. `hive[path]` always returns a Key
(existing or new). Values are set on keys via set_dword / set_sz / set_binary.

Running this module directly writes the minimal boot hive for MicroNT.

NT 3.5 format notes:
  - Hive version 1.2
  - Names stored compressed ASCII (KEY_COMP_NAME / VALUE_COMP_NAME)
  - Subkey index uses CM_KEY_INDEX_LEAF ("li" signature)
    (plain HCELL_INDEX entries, no hashes — "lf"/"lh" are post-XP)
  - Children must be sorted by name for binary search
"""

import struct
import time

# --- Registry value types (REG_*) ---
REG_NONE      = 0
REG_SZ        = 1
REG_EXPAND_SZ = 2
REG_BINARY    = 3
REG_DWORD     = 4
REG_MULTI_SZ  = 7

# --- CM_KEY_NODE flags ---
KEY_VOLATILE   = 0x0001
KEY_HIVE_EXIT  = 0x0002
KEY_HIVE_ENTRY = 0x0004
KEY_NO_DELETE  = 0x0008
KEY_SYM_LINK   = 0x0010
KEY_COMP_NAME  = 0x0020

# --- CM_KEY_VALUE flags ---
VALUE_COMP_NAME = 0x0001

# --- Layout constants ---
PAGE          = 4096
CELL_ALIGN    = 8
HBIN_HDR_SIZE = 32


def _align(n: int, a: int) -> int:
    return (n + a - 1) & ~(a - 1)


# ============================================================================
# High-level tree API
# ============================================================================

class Key:
    """A registry key: a map of subkeys and a list of values.

    Values are kept as a list (not dict) to preserve insertion order — useful
    since the kernel often expects specific value orderings.
    """
    __slots__ = ("subkeys", "values", "flags")

    subkeys: "dict[str, Key]"
    values:  "list[tuple[str, int, bytes]]"
    flags:   int

    def __init__(self) -> None:
        self.subkeys = {}
        self.values = []
        self.flags = 0

    def __getitem__(self, path: str) -> "Key":
        """Get or create a subkey by path (supports backslash separators)."""
        k: Key = self
        for part in path.split("\\"):
            if part == "":
                continue
            sub = k.subkeys.get(part)
            if sub is None:
                sub = Key()
                k.subkeys[part] = sub
            k = sub
        return k

    # --- Value setters ---

    def set_dword(self, name: str, value: int) -> "Key":
        self.values.append((name, REG_DWORD, struct.pack("<I", value & 0xFFFFFFFF)))
        return self

    def set_sz(self, name: str, value: str) -> "Key":
        data = value.encode("utf-16-le") + b"\x00\x00"
        self.values.append((name, REG_SZ, data))
        return self

    def set_expand_sz(self, name: str, value: str) -> "Key":
        data = value.encode("utf-16-le") + b"\x00\x00"
        self.values.append((name, REG_EXPAND_SZ, data))
        return self

    def set_multi_sz(self, name: str, strings: "list[str]") -> "Key":
        data = b"".join(s.encode("utf-16-le") + b"\x00\x00" for s in strings) + b"\x00\x00"
        self.values.append((name, REG_MULTI_SZ, data))
        return self

    def set_binary(self, name: str, data: bytes) -> "Key":
        self.values.append((name, REG_BINARY, bytes(data)))
        return self

    def set_value(self, name: str, vtype: int, data: bytes) -> "Key":
        """Generic value setter for non-standard types."""
        self.values.append((name, vtype, bytes(data)))
        return self


# ============================================================================
# Hive — declarative tree + binary serializer
# ============================================================================

class Hive:
    """An NT 3.5 registry hive.

    Build it up by accessing keys via paths, then call write() or build()."""

    name: str
    root: Key
    _bin: bytearray

    def __init__(self, name: str = "SYSTEM") -> None:
        self.name = name
        self.root = Key()
        self.root.flags = KEY_HIVE_ENTRY
        self._bin = bytearray()

    def __getitem__(self, path: str) -> Key:
        return self.root[path]

    # --- Low-level cell construction ---------------------------------------

    def _alloc(self, payload_size: int) -> int:
        """Reserve a cell with 4-byte negative size prefix + zeroed payload.
        Returns the cell's hive index (offset relative to hbin start)."""
        total = _align(4 + payload_size, CELL_ALIGN)
        bin_offset = len(self._bin)
        self._bin.extend(struct.pack("<i", -total))
        self._bin.extend(b"\x00" * (total - 4))
        return HBIN_HDR_SIZE + bin_offset

    def _patch(self, cell: int, field_offset: int, data: bytes) -> None:
        """Overwrite bytes inside a previously-allocated cell."""
        pos = (cell - HBIN_HDR_SIZE) + 4 + field_offset
        self._bin[pos:pos + len(data)] = data

    # --- Cell factories (match NT 3.5 on-disk layouts) ---------------------

    def _nk(self, name: str, parent: int, flags: int,
            subkey_count: int, subkey_list: int,
            value_count: int, value_list: int,
            security: int = 0xFFFFFFFF,
            max_name_len: int = 0, max_class_len: int = 0,
            max_value_name_len: int = 0, max_value_data_len: int = 0) -> int:
        """CM_KEY_NODE ('nk') cell. 76 byte fixed header + compressed name.

        MaxNameLen/MaxValueNameLen are reported by RegQueryInfoKey in WCHARs
        (even for compressed-name keys — the kernel returns the length as if
        the name were stored uncompressed). MaxValueDataLen is in bytes.
        """
        name_b = name.encode("ascii")
        cell = self._alloc(76 + len(name_b))

        ts = int(time.time() * 10000000) + 116444736000000000
        hdr = (
            struct.pack("<2sH", b"nk", flags | KEY_COMP_NAME)  # +0   sig, flags
            + struct.pack("<Q", ts)                            # +4   LastWriteTime
            + struct.pack("<I", 0)                             # +12  Spare
            + struct.pack("<I", parent)                        # +16  Parent
            + struct.pack("<II", subkey_count, 0)              # +20  SubKeyCounts
            + struct.pack("<II", subkey_list, 0xFFFFFFFF)      # +28  SubKeyLists
            + struct.pack("<II", value_count, value_list)      # +36  ValueList
            + struct.pack("<II", security, 0xFFFFFFFF)         # +44  Security, Class
            + struct.pack("<IIII",                             # +52  MaxNameLen..
                          max_name_len, max_class_len,
                          max_value_name_len, max_value_data_len)
            + struct.pack("<I", 0)                             # +68  WorkVar
            + struct.pack("<HH", len(name_b), 0)               # +72  NameLength, ClassLength
            + name_b                                           # +76  Name
        )
        assert len(hdr) == 76 + len(name_b)
        self._patch(cell, 0, hdr)
        return cell

    def _vk(self, name: str, vtype: int, data: bytes) -> int:
        """CM_KEY_VALUE ('vk') cell. Small values (<= 4 bytes) stored inline."""
        name_b = name.encode("ascii")
        cell = self._alloc(20 + len(name_b))

        if len(data) <= 4:
            padded = (data + b"\x00\x00\x00\x00")[:4]
            data_off = struct.unpack("<I", padded)[0]
            data_len = len(data) | 0x80000000  # high bit: inline
        else:
            data_cell = self._alloc(len(data))
            self._patch(data_cell, 0, data)
            data_off = data_cell
            data_len = len(data)

        hdr = struct.pack(
            "<2sHIIIHH",
            b"vk",
            len(name_b),
            data_len,
            data_off,
            vtype,
            VALUE_COMP_NAME,
            0,
        ) + name_b
        self._patch(cell, 0, hdr)
        return cell

    def _value_list(self, cells: "list[int]") -> int:
        """Array of HCELL_INDEX referenced by an nk's ValueList."""
        cell = self._alloc(4 * len(cells))
        self._patch(cell, 0, b"".join(struct.pack("<I", c) for c in cells))
        return cell

    def _index_leaf(self, cells: "list[int]") -> int:
        """CM_KEY_INDEX ('li') — leaf of the subkey index.
        NT 3.5 does NOT support 'lf'/'lh' (those are XP+). Entries are plain
        HCELL_INDEX values, no hash. Caller must pass cells in name-sorted order."""
        cell = self._alloc(4 + 4 * len(cells))
        data = struct.pack("<2sH", b"li", len(cells))
        for c in cells:
            data += struct.pack("<I", c)
        self._patch(cell, 0, data)
        return cell

    def _ks(self, descriptor: bytes, ref_count: int) -> int:
        """CM_KEY_SECURITY ('ks') cell — wraps a self-relative SECURITY_DESCRIPTOR.
        Flink/Blink form a circular list; for a single shared SD they point to self.

          +0   USHORT Signature = "ks"
          +2   USHORT Reserved
          +4   HCELL_INDEX Flink
          +8   HCELL_INDEX Blink
          +12  ULONG ReferenceCount
          +16  ULONG DescriptorLength
          +20  SECURITY_DESCRIPTOR Descriptor (variable)
        """
        cell = self._alloc(20 + len(descriptor))
        # Patch Flink/Blink after allocation so they can point to self
        hdr = (
            struct.pack("<2sH", b"ks", 0)
            + struct.pack("<II", cell, cell)     # Flink/Blink = self
            + struct.pack("<II", ref_count, len(descriptor))
            + descriptor
        )
        self._patch(cell, 0, hdr)
        return cell

    @staticmethod
    def null_dacl_descriptor() -> bytes:
        """Build a minimal self-relative SECURITY_DESCRIPTOR.

        SeValidSecurityDescriptor (SE/CAPTURE.C:1979) requires:
          - Revision == SECURITY_DESCRIPTOR_REVISION (1)
          - Control & SE_SELF_RELATIVE (0x8000)
          - Owner SID present and valid  (MANDATORY)
          - Group/DACL optional
          - NULL DACL (offset 0 with SE_DACL_PRESENT) means 'allow all'

        Layout (32 bytes total):
          +0   SECURITY_DESCRIPTOR_RELATIVE (20 bytes)
          +20  Owner SID: S-1-5-18 (Local System, 12 bytes)
        """
        # S-1-5-18 (Local System) SID: Rev=1, SubAuthCount=1,
        # IdentifierAuthority={0,0,0,0,0,5}, SubAuthority[0]=18
        system_sid = struct.pack(
            "<BB6BI",
            1,                          # Revision
            1,                          # SubAuthorityCount
            0, 0, 0, 0, 0, 5,           # IdentifierAuthority
            18,                         # SubAuthority[0] = SYSTEM
        )

        sd_hdr = struct.pack(
            "<BBHIIII",
            1,           # Revision
            0,           # Sbz1
            0x8004,      # Control: SE_SELF_RELATIVE | SE_DACL_PRESENT
            20,          # Owner offset (points to SID at +20)
            0,           # Group offset (none)
            0,           # Sacl offset (none)
            0,           # Dacl offset (0 with DACL_PRESENT = NULL DACL = allow all)
        )
        return sd_hdr + system_sid

    # --- Tree walker -------------------------------------------------------

    def _emit_key(self, name: str, key: Key, parent_cell: int,
                  security_cell: int, is_root: bool = False) -> int:
        """Recursively emit cells for a key subtree. Returns the key's cell."""
        # Emit values
        value_cells = [self._vk(n, t, d) for (n, t, d) in key.values]
        value_list = self._value_list(value_cells) if value_cells else 0xFFFFFFFF

        # Children must be sorted alphabetically for binary search
        children = sorted(key.subkeys.items(), key=lambda kv: kv[0].upper())

        # RegQueryInfoKey returns these pre-computed maxes straight from the nk.
        # Name/ValueName lengths reported in WCHAR count (2 × char count for
        # our ASCII/UTF-8 input); ValueDataLen in bytes.
        max_name_len = max((len(n) * 2 for n, _ in children), default=0)
        max_value_name_len = max((len(n) * 2 for n, _, _ in key.values), default=0)
        max_value_data_len = max((len(d) for _, _, d in key.values), default=0)

        # Allocate the nk with placeholder subkey_list; patch after emitting children
        nk_cell = self._nk(
            name, parent_cell, key.flags,
            subkey_count=len(children),
            subkey_list=0xFFFFFFFF,
            value_count=len(value_cells),
            value_list=value_list,
            security=security_cell,
            max_name_len=max_name_len,
            max_value_name_len=max_value_name_len,
            max_value_data_len=max_value_data_len,
        )

        # Root's parent points to itself
        if is_root:
            self._patch(nk_cell, 16, struct.pack("<I", nk_cell))

        # Emit children (with us as their parent) and build the subkey index
        if children:
            child_cells = [self._emit_key(n, k, nk_cell, security_cell)
                           for (n, k) in children]
            sl_cell = self._index_leaf(child_cells)
            self._patch(nk_cell, 28, struct.pack("<I", sl_cell))  # SubKeyLists.Stable

        return nk_cell

    # --- Final assembly ----------------------------------------------------

    def build(self) -> bytes:
        """Serialize the tree into the final hive bytes."""
        self._bin = bytearray()

        # Allocate a single shared security descriptor cell referenced by every
        # key in the hive. CmCheckRegistry walks this circular list (via the
        # root key's Security pointer) and runs SeValidSecurityDescriptor on
        # each entry — so we need at least one valid SD.
        # ReferenceCount will be patched to the actual key count when known;
        # for now use a large value so decrement-on-delete never hits zero.
        sd = self.null_dacl_descriptor()
        security_cell = self._ks(sd, ref_count=0x10000)

        root_cell = self._emit_key(self.name, self.root, 0,
                                   security_cell, is_root=True)

        # Pad bin area to PAGE alignment, fill remaining space with one free cell
        total = _align(HBIN_HDR_SIZE + len(self._bin), PAGE)
        pad = total - HBIN_HDR_SIZE - len(self._bin)
        if pad >= 8:
            self._bin.extend(struct.pack("<i", pad))  # positive = free cell
            self._bin.extend(b"\x00" * (pad - 4))
        elif pad > 0:
            self._bin.extend(b"\x00" * pad)

        # hbin header
        hbin_size = HBIN_HDR_SIZE + len(self._bin)
        hbin = struct.pack("<4sIIII", b"hbin", 0, hbin_size, 0, 0)
        hbin += b"\x00" * (HBIN_HDR_SIZE - len(hbin))

        # regf base block
        ts = int(time.time() * 10000000) + 116444736000000000
        base = bytearray(PAGE)
        struct.pack_into("<4sII", base, 0, b"regf", 1, 1)  # sig, seq1, seq2
        struct.pack_into("<Q", base, 12, ts)               # timestamp
        struct.pack_into("<II", base, 20, 1, 2)            # major=1, minor=2
        struct.pack_into("<II", base, 28, 0, 1)            # type=0, format=1
        struct.pack_into("<I", base, 36, root_cell)        # root cell
        struct.pack_into("<I", base, 40, hbin_size)        # length of bins
        struct.pack_into("<I", base, 44, 1)                # cluster

        # Hive name at +48 (UTF-16LE, up to 64 bytes)
        fname = (self.name + "\0").encode("utf-16-le")
        base[48:48 + len(fname)] = fname

        # Checksum at +508 = XOR of DWORDs 0..507
        cksum = 0
        for i in range(0, 508, 4):
            cksum ^= struct.unpack_from("<I", base, i)[0]
        struct.pack_into("<I", base, 508, cksum & 0xFFFFFFFF)

        return bytes(base) + hbin + bytes(self._bin)

    def write(self, path: str) -> int:
        data = self.build()
        with open(path, "wb") as f:
            f.write(data)
        return len(data)


# ============================================================================
# Minimal boot hive for MicroNT
# ============================================================================

# Boot defaults for Control\Init\{Exe,Args,Stdio}. The kernel spawns
# this image as the initial user-mode process (INIT.C::QueryInitConfig),
# with the Args appended to CommandLine and the NT-device Stdio opened
# inheritable and wired into Standard{Input,Output,Error}.
#
# run.exe is the native-subsystem LuaJIT (imports ntdll only). main.lua
# is its entry script. Serial0 gives the Lua process a raw-mode COM1 for
# stdin/out/err — works alongside DbgPrint on the same port.
DEFAULT_INIT_EXE   = "lua\\run.exe"
DEFAULT_INIT_ARGS  = "\\SystemRoot\\lua\\main.lua"
DEFAULT_INIT_STDIO = "\\Device\\Serial0"


def build_system_hive(init_exe: str | None = None,
                      init_args: str | None = None,
                      init_stdio: str | None = None) -> Hive:
    """Return the SYSTEM hive with the minimum NT 3.5 needs to boot
    straight into our native Lua userland.

    Control\\Init\\Exe tells the kernel (INIT.C::QueryInitConfig) to
    spawn our image directly instead of falling back to smss.exe; we
    have no Win32 subsystem, no session manager, no winlogon. Defaults
    come from DEFAULT_INIT_{EXE,ARGS,STDIO}; callers override per-run
    via CLI flags (e.g. cr's Makefile iteration loop).

    The kernel searches this hive during Phase 0/1 for:
      Select\\{current,default,lastknowngood,failed}
      ControlSet001\\Control

    Names are lowercase to match the kernel's case-insensitive lookups
    without depending on NLS case tables being fully live.
    """
    init_exe   = init_exe   if init_exe   is not None else DEFAULT_INIT_EXE
    init_args  = init_args  if init_args  is not None else DEFAULT_INIT_ARGS
    init_stdio = init_stdio if init_stdio is not None else DEFAULT_INIT_STDIO

    h = Hive("SYSTEM")

    h["Select"] \
        .set_dword("current",       1) \
        .set_dword("default",       1) \
        .set_dword("lastknowngood", 1) \
        .set_dword("failed",        0)

    control = h["ControlSet001\\Control"]

    # Control\Init — kernel's initial-process configuration, read by
    # INIT.C::QueryInitConfig. Exe is the image path (SystemRoot-
    # relative here; mkhive prepends \SystemRoot\). Args is the argv
    # tail. Stdio is an NT device path the kernel opens inheritable
    # and pipes into ProcessParameters.Standard{Input,Output,Error}.
    init = control["Init"]
    init.set_sz("Exe",   f"\\SystemRoot\\{init_exe}")
    init.set_sz("Args",  init_args)
    init.set_sz("Stdio", init_stdio)

    # Environment: the UEFI loader doesn't populate SystemDrive, so set
    # it here to match the DOS Devices C: symlink below. Missing
    # SystemDrive would leave %SystemRoot% unexpanded.
    control["Session Manager\\Environment"] \
        .set_sz("SystemDrive", "C:") \
        .set_expand_sz("SystemRoot", "%SystemDrive%\\") \
        .set_expand_sz("Path", "%SystemRoot%\\System32")

    # (smss-related Session Manager config — SubSystems, DOS Devices,
    # KnownDlls, Memory Management, FileRenameOperations — all lived
    # here. They were consumed exclusively by smss.exe at boot; with
    # no smss in the image the kernel doesn't read any of it, so
    # they're omitted.)

    # ServiceGroupOrder controls the order system-start drivers are loaded.
    # Video Init (port driver) must load before Video (miniports). Virtio
    # group sits after Extended base so PCI bus-walk drivers (viorng,
    # vioser, ...) load once the kernel + HAL are fully up.
    control["ServiceGroupOrder"] \
        .set_multi_sz("List", [
            "Base",
            "Extended base",
            "Virtio",
            "File System",
            "Video Init",
            "Video",
            "Keyboard Class",
            "Pointer Class",
        ])

    # GroupOrderList: CmpFindDrivers requires this key to exist under
    # Control, even if no per-group tag ordering is needed. Each value
    # is a REG_BINARY array of ULONGs (tag order); empty = no ordering.
    control["GroupOrderList"]

    # Services keys for boot drivers. IopInitializeBootDrivers opens each
    # BOOT_DRIVER_LIST_ENTRY's RegistryPath; IopGetDriverNameFromKeyNode reads
    # Type to decide whether to put the driver under \Driver or \FileSystem.
    # Values from NTDDK.H:
    #   SERVICE_KERNEL_DRIVER      = 1
    #   SERVICE_FILE_SYSTEM_DRIVER = 2
    #   SERVICE_BOOT_START         = 0
    #   SERVICE_SYSTEM_START       = 1
    #   SERVICE_ERROR_NORMAL       = 1
    services = h["ControlSet001\\Services"]
    services["atdisk"] \
        .set_dword("Type",         1) \
        .set_dword("Start",        0) \
        .set_dword("ErrorControl", 1)
    services["null"] \
        .set_dword("Type",         1) \
        .set_dword("Start",        1) \
        .set_dword("ErrorControl", 1)
    services["fastfat"] \
        .set_dword("Type",         2) \
        .set_dword("Start",        0) \
        .set_dword("ErrorControl", 1)
    npfs = services["npfs"]
    npfs.set_dword("Type",         2) \
        .set_dword("Start",        1) \
        .set_dword("ErrorControl", 1) \
        .set_sz("Group", "File System")
    services["msfs"] \
        .set_dword("Type",         2) \
        .set_dword("Start",        1) \
        .set_dword("ErrorControl", 1) \
        .set_sz("Group", "File System")

    # Serial — NT 3.5's COM port driver. SERVICE_AUTO_START (=1) means the
    # I/O manager loads it in Phase 1 after the registry is up; we don't
    # need serial during kernel init (COM1/COM2 are KD/HAL debug channels
    # owned outside the NT I/O subsystem). "Extended base" group loads
    # right after "Base", before file systems get mounted.
    # serial.sys walks HKLM\Hardware\Description\System\MultifunctionAdapter\N\
    # SerialController\M\ConfigurationData at DriverEntry to learn which
    # UARTs to claim — our UEFI loader emits those nodes.
    services["Serial"] \
        .set_dword("Type",         1) \
        .set_dword("Start",        1) \
        .set_dword("ErrorControl", 1) \
        .set_sz("Group", "Extended base")

    # virtio-rng — entropy device. Surfaces \Device\VirtioRng0; user
    # mode reads bytes from it. SERVICE_AUTO_START (=1) so it loads
    # alongside other Phase 1 drivers; depends on the HAL having
    # already enumerated the PCI bus.
    services["viorng"] \
        .set_dword("Type",         1) \
        .set_dword("Start",        1) \
        .set_dword("ErrorControl", 1) \
        .set_sz("Group", "Virtio")

    # virtio-console — single-port virtio-serial. Surfaces
    # \Device\VirtioCon0 with read + write paths; couples to QEMU's
    # virtconsole chardev (configured in boot.sh).
    services["vioser"] \
        .set_dword("Type",         1) \
        .set_dword("Start",        1) \
        .set_dword("ErrorControl", 1) \
        .set_sz("Group", "Virtio")

    # virtio-input — keyboard / mouse / tablet via virtio-keyboard-pci,
    # virtio-mouse-pci, etc. (modern device ID 0x1052). At skeleton
    # stage just dumps events to the kernel debug log; future passes
    # bind to kbdclass / mouclass via IOCTL_INTERNAL_*_CONNECT.
    services["vioinput"] \
        .set_dword("Type",         1) \
        .set_dword("Start",        1) \
        .set_dword("ErrorControl", 1) \
        .set_sz("Group", "Virtio")

    # (Input + video drivers — i8042prt, kbdclass, mouclass, videoprt,
    # bochsvga — ship on disk but are not auto-started. The eventual
    # pure-Lua UI layer will register + start them from userland when
    # it's ready. Adding Start=1 entries here prematurely would fire
    # driver init on every boot with nothing in userland to consume
    # the resulting devices.)

    return h


def main() -> None:
    import argparse
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import libversion as lv

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("output", nargs="?", default="SYSTEM",
                    help="path to write the hive to")
    ap.add_argument("--init-exe", default=None, metavar="PATH",
                    help="SystemRoot-relative path to the initial "
                         "user-mode process exe. Control\\Init\\Exe "
                         f"is written as \\SystemRoot\\<PATH>. "
                         f"Default: {DEFAULT_INIT_EXE!r}.")
    ap.add_argument("--init-args", default=None, metavar="ARGS",
                    help="Control\\Init\\Args — argv tail appended to "
                         "Exe's command line, whitespace-separated. "
                         f"Default: {DEFAULT_INIT_ARGS!r}.")
    ap.add_argument("--init-stdio", default=None, metavar="NTPATH",
                    help="Control\\Init\\Stdio — NT device path opened "
                         "inheritable by the kernel and wired into the "
                         "init process's stdin/stdout/stderr handles. "
                         f"Default: {DEFAULT_INIT_STDIO!r}.")
    args = ap.parse_args()

    # Banner the stamp we're building against so CI logs pin hive <-> binary
    # pairing.
    try:
        stamp = lv.parse_ntverp()
        print(f"build stamp: NT {stamp.version_str} "
              f"{stamp.current_build_number} {stamp.channel} {stamp.sha}")
    except Exception as e:
        print(f"build stamp: unavailable ({e})")

    h = build_system_hive(init_exe=args.init_exe,
                          init_args=args.init_args,
                          init_stdio=args.init_stdio)
    size = h.write(args.output)
    print(f"SYSTEM hive: {size} bytes -> {args.output}")


if __name__ == "__main__":
    main()
