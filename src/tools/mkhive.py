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

PROFILES = ("micront", "headless", "gui")


def build_micront_system_hive(profile: str = "headless",
                              init_exe: str | None = None,
                              init_args: str | None = None,
                              init_stdio: str | None = None) -> Hive:
    """Return a Hive populated with the minimum NT 3.5 needs to boot.

    `profile` selects how much of the Win32 subsystem gets wired up:

      micront   — no Win32 subsystem at all. smss comes up but has
                  nothing to hand off to. Any init program is a
                  native NT binary (linked against nt.lib).
      headless  — Win32 base: csrss + basesrv (kernel32-only, no
                  user32/gdi32/console).
      gui       — headless + winsrv (USER, GDI, console servers) +
                  winlogon.

    The kernel searches this hive during Phase 0/1 for:
      Select\\{current,default,lastknowngood,failed}
      ControlSet001\\Control

    Names are lowercase to match the kernel's case-insensitive lookups
    without depending on NLS case tables being fully live.
    """
    if profile not in PROFILES:
        raise ValueError(f"profile must be one of {PROFILES}, got {profile!r}")
    h = Hive("SYSTEM")

    h["Select"] \
        .set_dword("current",       1) \
        .set_dword("default",       1) \
        .set_dword("lastknowngood", 1) \
        .set_dword("failed",        0)

    control = h["ControlSet001\\Control"]

    # MicroNT: Control\Init\* overrides the kernel's initial user-mode
    # process configuration (INIT.C QueryInitConfig, which opens this
    # subkey once and queries three values):
    #
    #   Exe    SystemRoot-relative path (mkhive prepends \SystemRoot\).
    #          Written as a full NT path to the registry.
    #   Args   Verbatim argv tail, appended to CommandLine after a space.
    #   Stdio  NT device path (e.g. "\Device\Serial0"). Kernel opens
    #          inheritable + raw-mode timeouts; handle lands in
    #          ProcessParameters.Standard{Input,Output,Error}.
    #
    # Absent → kernel falls back to \SystemRoot\System32\smss.exe with
    # no args and no stdio.
    if init_exe:
        init = control["Init"]
        init.set_sz("Exe", f"\\SystemRoot\\{init_exe}")
        if init_args:
            init.set_sz("Args", init_args)
        if init_stdio:
            init.set_sz("Stdio", init_stdio)

    # Session Manager minimal config so smss.exe doesn't try to spawn
    # programs we don't have (autochk.exe, csrss.exe, etc.).
    sm = control["Session Manager"]

    # Empty BootExecute — skip autocheck.
    sm.set_multi_sz("BootExecute", [])

    # Session Manager\Execute: smss reads this as REG_MULTI_SZ and the
    # LAST entry becomes InitialCommand (see SMINIT.C:718-726). Empty list
    # → smss defaults to "winlogon.exe" (line 753), which then launches
    # lsass.exe via the SOFTWARE hive Winlogon\System value. Irrelevant to
    # micront (bypasses smss entirely via Control\InitExe).
    sm.set_multi_sz("Execute", [])

    # SystemDrive gets set by the full NTLDR/OSLOADER at boot time from
    # the ARC boot device — under our UEFI loader it stays unset, so we
    # hardcode it here matching the DOS Devices C: symlink below.
    # Without SystemDrive, SystemRoot expands to a literal "%SystemDrive%\"
    # and every subsequent expansion (e.g. csrss.exe's image path) fails.
    sm["Environment"] \
        .set_sz("SystemDrive", "C:") \
        .set_expand_sz("SystemRoot", "%SystemDrive%\\") \
        .set_expand_sz("Path", "%SystemRoot%\\System32")

    # Win32 subsystem registration. smss reads SubSystems\Required at
    # SmpInit, and for each name it resolves SubSystems\<Name> as the
    # command line to launch. The launch string's first token is the
    # image path; the rest are arguments csrsrv parses:
    #   ObjectDirectory  — NT object-namespace dir for csrss's LPC ports
    #                       (unrelated to filesystem layout)
    #   SharedSection    — three comma-separated sizes (KB) for the 3
    #                       shared sections csrss creates (SB, SM, View)
    #   Windows=On       — enable the Windows subsystem
    #   ServerDll=N,I    — per-server DLL to load at startup; index I is
    #                       the API-table slot. basesrv owns slot 1
    #                       (kernel32's base services). We skip winsrv
    #                       (slots 2+3 = USER/GDI/Console) entirely —
    #                       headless Win32 doesn't need it.
    #   ServerDllInitialization — entry-point fn each ServerDll exports;
    #                             "CsrServerInitialization" is the
    #                             csrsrv-side default.
    sm_sub = sm["SubSystems"]

    # Required subsystems: smss loads these by name before any app runs.
    # Micront profile has none — smss just sails past SmpLoadSubSystems
    # and falls straight through to Execute/InitialCommand.
    if profile == "micront":
        sm_sub.set_multi_sz("Required", [])
    else:
        sm_sub.set_multi_sz("Required", ["Windows"])
    sm_sub.set_multi_sz("Optional", [])

    if profile != "micront":
        # Win32 subsystem registration. The launch string's first token
        # is the image path; the rest are arguments csrsrv parses:
        #   ObjectDirectory  — NT object-namespace dir for csrss's LPC
        #                       ports (unrelated to filesystem layout)
        #   SharedSection    — three sizes (KB) for the 3 shared sections
        #   ServerDll=N,I    — per-server DLL; basesrv owns slot 1. GUI
        #                       profile adds winsrv with slots 2+3 for
        #                       USER and Console.
        # IMPORTANT: these indices are ABI constants, not arbitrary. They
        # must match the #defines in WINSS.H:
        #   BASESRV_SERVERDLL_INDEX  = 1
        #   CONSRV_SERVERDLL_INDEX   = 2
        #   USERSRV_SERVERDLL_INDEX  = 3
        #   GDISRV_SERVERDLL_INDEX   = 4
        # Client DLLs (user32, kernel32) use these hardcoded indices to
        # look up per-process and per-thread data. If the indices here
        # don't match, the server DLL writes sizeof(PROCESSINFO) bytes
        # into a smaller slot's allocation — silent heap corruption.
        server_dlls = "ServerDll=basesrv,1 "
        if profile == "gui":
            server_dlls += (
                "ServerDll=winsrv:ConServerDllInitialization,2 "
                "ServerDll=winsrv:UserServerDllInitialization,3 "
                "ServerDll=winsrv:GdiServerDllInitialization,4 "
            )
        sm_sub.set_expand_sz(
            "Windows",
            "%SystemRoot%\\system32\\csrss.exe "
            "ObjectDirectory=\\Windows "
            "SharedSection=1024,3072,512 "
            "Windows=On "
            "SubSystemType=Windows "
            + server_dlls +
            "ProfileControl=Off "
            "MaxRequestThreads=16"
        )

    # DOS Devices — smss creates \DosDevices\<Name> symlinks pointing at the
    # given NT device path. Without a C: symlink, RtlDosPathNameToNtPathName_U
    # fails to resolve "C:\System32" and SmpInitializeKnownDlls returns
    # STATUS_OBJECT_PATH_NOT_FOUND (c000003a).
    sm["DOS Devices"] \
        .set_sz("C:", "\\Device\\Harddisk0\\Partition1") \
        .set_sz("PIPE", "\\Device\\NamedPipe") \
        .set_sz("MAILSLOT", "\\Device\\Mailslot")

    # KnownDlls: SmpInitializeKnownDlls reads DllDirectory to locate the
    # KnownDlls filesystem directory. Missing => conversion of NULL path
    # returns STATUS_OBJECT_NAME_INVALID (c0000033). Point at System32.
    sm["KnownDlls"] \
        .set_expand_sz("DllDirectory", "%SystemRoot%\\System32")

    # Memory Management + FileRenameOperations subkeys — SmpRegistryConfigurationTable
    # queries both via RTL_QUERY_REGISTRY_SUBKEY with no OPTIONAL flag; any
    # missing subkey => STATUS_OBJECT_NAME_NOT_FOUND and SmpInit aborts.
    sm["Memory Management"] \
        .set_multi_sz("PagingFiles", [])
    sm["FileRenameOperations"]

    # ServiceGroupOrder controls the order system-start drivers are loaded.
    # Video Init (port driver) must load before Video (miniports).
    control["ServiceGroupOrder"] \
        .set_multi_sz("List", [
            "Base",
            "Extended base",
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
    # NPFS pipe name aliases — the RPC client libraries use service-
    # specific pipe names (lsarpc, samr, etc.) but all security services
    # in lsass.exe share the single "lsass" pipe endpoint. NPFS
    # translates these at open time via NpTranslateAlias.
    # Format: value name = target pipe, value data = alias names
    # (REG_MULTI_SZ). Verified against reference OAK/BIN/SYSTEM hive.
    npfs["Aliases"] \
        .set_multi_sz("lsass", ["netlogon", "lsarpc", "samr"])
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

    if profile != "micront":
        # LSA configuration — auth packages list and product options.
        # LsapConfigurePackages reads Control\Lsa\Authentication Packages.
        # msv1_0 is the standard NT LAN Manager auth package.
        control["Lsa"] \
            .set_multi_sz("Authentication Packages", ["msv1_0"])

        # LanmanWorkstation\Parameters — LsapDbSetDomainInfo (Pass 2 of
        # LSA auto-install) reads the Account Domain SID from here.
        # No Domain/DomainId (primary domain) — MicroNT is standalone,
        # never domain-joined. LsapDbSetDomainInfo is patched to skip
        # primary domain setup when these are absent.
        # AccountDomainId format: space-separated decimal, 6 authority
        # bytes then sub-authorities (tokenized by LsapDbGetNextValueToken).
        # S-1-5-21-1-2-3 = authority 0 0 0 0 0 5, sub-auths 21 1 2 3.
        services["LanmanWorkstation\\Parameters"] \
            .set_sz("AccountDomainId", "0 0 0 0 0 5 21 1 2 3")

        # ProductOptions — LsapDbInitializeServer reads ProductType.
        # "WinNt" = standalone workstation, "LanManNt" = domain controller.
        control["ProductOptions"] \
            .set_sz("ProductType", "WinNt")

        # ComputerName — GetComputerNameW reads this; lsass calls it during
        # LsapDbInitializeServer(2). Missing key = null deref in kernel32.
        control["ComputerName\\ComputerName"] \
            .set_sz("ComputerName", "MICRONT")

    if profile == "gui":
        # Video: Bochs VGA miniport (QEMU stdvga, PCI 1234:1111).
        # videoprt.sys is the video port framework loaded implicitly.
        services["videoprt"] \
            .set_dword("Type",         1) \
            .set_dword("Start",        1) \
            .set_dword("ErrorControl", 1) \
            .set_sz("Group", "Video Init")
        bochsvga = services["bochsvga"]
        bochsvga \
            .set_dword("Type",         1) \
            .set_dword("Start",        1) \
            .set_dword("ErrorControl", 1) \
            .set_sz("Group", "Video") \
            .set_sz("ImagePath", "System32\\Drivers\\bochsvga.sys")
        # Device0 subkey tells USER server which display driver DLL to load.
        bochsvga["Device0"] \
            .set_multi_sz("InstalledDisplayDrivers", ["framebuf"]) \
            .set_dword("DefaultSettings.XResolution", 1024) \
            .set_dword("DefaultSettings.YResolution", 768) \
            .set_dword("DefaultSettings.BitsPerPel", 32)

        # Input: PS/2 keyboard + mouse
        services["i8042prt"] \
            .set_dword("Type",         1) \
            .set_dword("Start",        1) \
            .set_dword("ErrorControl", 1) \
            .set_sz("Group", "Keyboard Class")
        services["kbdclass"] \
            .set_dword("Type",         1) \
            .set_dword("Start",        1) \
            .set_dword("ErrorControl", 1) \
            .set_sz("Group", "Keyboard Class")
        # kbdclass's KbdConfiguration opens Services\kbdclass\Parameters
        # to read KeyboardDataQueueSize / MaximumPortsServiced /
        # KeyboardDeviceBaseName / ConnectMultiplePorts. All values are
        # RTL_QUERY_REGISTRY_OPTIONAL — but the subkey itself must exist
        # or RtlQueryRegistryValues returns STATUS_OBJECT_NAME_NOT_FOUND
        # before DriverEntry can fall back to defaults. Presence alone
        # is enough; no values needed.
        services["kbdclass\\Parameters"]
        services["mouclass"] \
            .set_dword("Type",         1) \
            .set_dword("Start",        1) \
            .set_dword("ErrorControl", 1) \
            .set_sz("Group", "Pointer Class")
        services["mouclass\\Parameters"]

    return h


def build_micront_software_hive(profile: str = "headless") -> Hive:
    """Return a SOFTWARE hive with the minimum keys NT needs to boot.

    The kernel loads this from %SystemRoot%\\System32\\config\\SOFTWARE
    and mounts it at \\Registry\\Machine\\Software.

    The most critical consumer is winlogon.exe which reads its startup
    configuration from Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon
    via GetProfileString("WINLOGON", ...).
    """
    h = Hive("SOFTWARE")

    cv = h["Microsoft\\Windows NT\\CurrentVersion"]

    cv.set_sz("CurrentVersion", "3.5") \
      .set_sz("CurrentBuildNumber", "807") \
      .set_sz("CurrentType", "Uniprocessor Free") \
      .set_sz("SystemRoot", "C:\\")

    if profile != 'micront':
        # Winlogon configuration — winlogon reads these via GetProfileString
        # (WINLOGON section → IniFileMapping → this registry path).
        wl = cv["Winlogon"]

        # "System" is the list of processes winlogon starts before auth init.
        # lsass.exe is the security subsystem; must be running before
        # LsaRegisterLogonProcess can succeed.
        wl.set_sz("System", "lsass.exe")

        # "Userinit" runs after a successful logon to set up the user env.
        wl.set_sz("Userinit", "userinit.exe,")

        # "Shell" is what userinit launches as the user's desktop shell.
        # progman.exe = NT 3.5's Program Manager (staged in System32/).
        # Switched to cmd.exe temporarily while debugging progman's
        # invisible-window issue — cmd goes through csrss's console
        # code path instead of USER/GDI, so it's a good control.
        wl.set_sz("Shell", "cmd.exe")

        # ServiceControllerStart — winlogon starts this before lsass.
        # We don't have services.exe yet; winlogon logs a warning but
        # continues.
        wl.set_sz("ServiceControllerStart", "")

    if profile == "gui":
        # GRE_Initialize — font file paths for the GDI engine.
        # Without these, GRE falls back to the winsrv.dll embedded font.
        # SSERIFE.FON contains "MS Sans Serif" (the canonical NT 3.5 dialog
        # font); COURE.FON is Courier; SMALLE.FON holds the tiny-point
        # Small Fonts face.
        gre = cv["GRE_Initialize"]
        gre.set_sz("FONTS.FON", "C:\\System32\\sserife.fon") \
           .set_sz("FIXEDFON.FON", "C:\\System32\\coure.fon") \
           .set_sz("OEMFONT.FON", "C:\\System32\\coure.fon")

        # Font substitution table — only logical faces that aren't
        # physically present need aliasing.
        cv["FontSubstitutes"] \
            .set_sz("MS Shell Dlg", "MS Sans Serif") \
            .set_sz("Helv", "MS Sans Serif") \
            .set_sz("Tms Rmn", "MS Serif") \
            .set_sz("Courier New", "Courier")

        # Program Manager common groups — progman's LoadCommonGroups()
        # (PMINIT.C:913) enumerates subkeys of HKLM\SOFTWARE\Program Groups
        # and loads each subkey's default (unnamed) REG_BINARY value as a
        # raw GROUPDEF blob (the on-disk .GRP file format). Without at
        # least one entry here, progman starts with a blank MDI client
        # — no group windows, nothing to click. Seed the canonical
        # "Main" group from the in-tree MAIN.GRP shipped with PROGMAN.
        from pathlib import Path as _Path
        main_grp = _Path(__file__).resolve().parent.parent / \
            "NT" / "PRIVATE" / "WINDOWS" / "SHELL" / "PROGMAN" / "MAIN.GRP"
        h["Program Groups\\Main"].set_binary("", main_grp.read_bytes())

    if profile != 'micront':
        # IniFileMapping — BaseSrvInitializeIniFileMappings reads this tree
        # to map GetProfileString("section","key") calls to registry paths.
        # Format: value "" (default) = "SYS:registrypath" maps the whole
        # section. SYS: = HKLM, USR: = HKCU.
        ifm = h["Microsoft\\Windows NT\\CurrentVersion\\IniFileMapping\\win.ini"]
        ifm["windows"] \
            .set_sz("", "SYS:Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows")
        ifm["WINLOGON"] \
            .set_sz("", "SYS:Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon")

    return h


def build_micront_default_hive(profile: str = "headless") -> Hive:
    """Return the DEFAULT user hive mounted at \\Registry\\User\\.Default.

    USERSRV accesses per-user UI policy through this hive whenever no
    interactive user is logged on — which, for the Welcome dialog path,
    is always. PROFILE.C::aFastRegMap hardcodes the section roots with a
    leading 'U' meaning HKCU, and before logon HKCU == .Default. So every
    one of these section reads lands here:

        PMAP_COLORS      → Control Panel\\Colors
        PMAP_DESKTOP     → Control Panel\\Desktop
        PMAP_CURSORS     → Control Panel\\Cursors
        PMAP_BEEP        → Control Panel\\Sound
        PMAP_MOUSE       → Control Panel\\Mouse
        PMAP_KEYBOARD    → Control Panel\\Keyboard
        (etc. — see WINSRV.H PMAP_* constants)

    Without a DEFAULT hive mounted, every FastGetProfileStringW call
    that targets these sections fails to open its cached key, silently
    returns the caller's hardcoded default, and the UI renders with
    whatever Microsoft's engineer typed into the fallback literal in
    1992. That's how MicroNT ended up with invisible dialog frames —
    Color\\Window defaulted to RGB(255,255,255) (white), Color\\Desktop
    to some gray, and the dialog frame brushes were NULL because no
    real sysColors seeding ever happened.

    NT 3.5 typically loads DEFAULT from %SystemRoot%\\System32\\config\\
    DEFAULT, as a HIVE_LIST_ENTRY in CmpMachineHiveList (CMDAT.C line 206:
    { L"DEFAULT", L"USER\\.DEFAULT", ... }). This builder emits that
    hive file.

    Populating this file is equivalent to running the Control Panel's
    "Appearance" and "Desktop" applets once, with the classic Windows 3.1
    grey/blue scheme. The color values are the 21 REG_SZ entries the
    USERSRV string table identifies by name (STR_SCROLLBAR..STR_BTNHIGHLIGHT
    in USER/SERVER/RES.RC), stored as space-separated decimal R G B.
    """
    h = Hive("DEFAULT")

    if profile != "gui":
        # Only GUI profile needs these — headless/micront never touches
        # PMAP_COLORS / PMAP_DESKTOP and so never mounts .Default.
        return h

    # ---- Control Panel\Colors ----
    # Classic Windows 3.1 / NT 3.5 "Windows Default" scheme. Values are
    # space-separated decimal R G B as REG_SZ — the format CI_GetClrVal
    # (USER/SERVER/INIT.C:1645) parses. Value names are from RES.RC:197–217.
    colors = h["Control Panel\\Colors"]
    colors.set_sz("Scrollbar",         "192 192 192")  # COLOR_SCROLLBAR
    colors.set_sz("Background",        "0 128 128")    # COLOR_BACKGROUND (teal desktop)
    colors.set_sz("ActiveTitle",       "0 0 128")      # COLOR_ACTIVECAPTION (dark blue)
    colors.set_sz("InactiveTitle",     "128 128 128")  # COLOR_INACTIVECAPTION
    colors.set_sz("Menu",              "192 192 192")  # COLOR_MENU
    colors.set_sz("Window",            "255 255 255")  # COLOR_WINDOW (dialog background)
    colors.set_sz("WindowFrame",       "0 0 0")        # COLOR_WINDOWFRAME (border line)
    colors.set_sz("MenuText",          "0 0 0")        # COLOR_MENUTEXT
    colors.set_sz("WindowText",        "0 0 0")        # COLOR_WINDOWTEXT
    colors.set_sz("TitleText",         "255 255 255")  # COLOR_CAPTIONTEXT
    colors.set_sz("ActiveBorder",      "192 192 192")  # COLOR_ACTIVEBORDER
    colors.set_sz("InactiveBorder",    "192 192 192")  # COLOR_INACTIVEBORDER
    colors.set_sz("AppWorkspace",      "128 128 128")  # COLOR_APPWORKSPACE (MDI backdrop)
    colors.set_sz("Hilight",           "0 0 128")      # COLOR_HIGHLIGHT (selection bar)
    colors.set_sz("HilightText",       "255 255 255")  # COLOR_HIGHLIGHTTEXT
    colors.set_sz("ButtonFace",        "192 192 192")  # COLOR_BTNFACE (button grey)
    colors.set_sz("ButtonShadow",      "128 128 128")  # COLOR_BTNSHADOW
    colors.set_sz("GrayText",          "128 128 128")  # COLOR_GRAYTEXT (disabled)
    colors.set_sz("ButtonText",        "0 0 0")        # COLOR_BTNTEXT
    colors.set_sz("InactiveTitleText", "192 192 192")  # COLOR_INACTIVECAPTIONTEXT
    colors.set_sz("ButtonHilight",     "255 255 255")  # COLOR_BTNHIGHLIGHT

    # ---- Control Panel\Desktop ----
    # Window manager policy that USER reads via PMAP_DESKTOP. BorderWidth
    # is the multiplier applied to the display driver's cxBorder to yield
    # SM_CXFRAME — without this, SERVER.C:2150 defaults to 3 anyway, but
    # explicit beats implicit; also covers CaretBlinkRate, DragFullWindows,
    # etc. that other sites in USERSRV read.
    desktop = h["Control Panel\\Desktop"]
    desktop.set_sz("BorderWidth",        "3")
    desktop.set_sz("CursorBlinkRate",    "530")
    desktop.set_sz("ScreenSaveTimeOut",  "900")
    desktop.set_sz("ScreenSaveActive",   "0")
    desktop.set_sz("Wallpaper",          "(None)")
    desktop.set_sz("TileWallpaper",      "0")
    desktop.set_sz("WallpaperStyle",     "0")
    desktop.set_sz("GridGranularity",    "0")
    desktop.set_sz("DragFullWindows",    "0")
    desktop.set_sz("FontSmoothing",      "0")

    # ---- Control Panel\Cursors / Mouse / Keyboard / Sound ----
    # Empty but present, so FastGetProfileStringW gets a real key handle
    # and falls through to the caller's default instead of erroring.
    h["Control Panel\\Cursors"]
    h["Control Panel\\Mouse"]
    h["Control Panel\\Keyboard"]
    h["Control Panel\\Sound"]
    h["Control Panel\\Sounds"]
    h["Control Panel\\Icons"]

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
    ap.add_argument("--profile", choices=PROFILES, default="headless",
                    help="which registry layout to emit (default: headless)")
    ap.add_argument("--init-exe", default=None, metavar="PATH",
                    help="micront only: SystemRoot-relative path to the "
                         "initial user-mode process exe (e.g. "
                         "'lua\\jit.exe'). Control\\InitExe is written as "
                         "\\SystemRoot\\<PATH>.")
    ap.add_argument("--init-args", default=None, metavar="ARGS",
                    help="micront only: Control\\Init\\Args — argv tail "
                         "appended to Exe's command line, whitespace-"
                         "separated. Only meaningful with --init-exe.")
    ap.add_argument("--init-stdio", default=None, metavar="NTPATH",
                    help="micront only: Control\\Init\\Stdio — NT device "
                         "path (e.g. \\Device\\Serial0) opened inheritable "
                         "by the kernel and wired into the init process's "
                         "stdin/stdout/stderr handles.")
    args = ap.parse_args()

    # Banner the stamp we're building against so CI logs pin hive <-> binary
    # pairing. When the SOFTWARE hive lands, populate CurrentVersion /
    # CurrentBuildNumber / CSDVersion from `stamp` so SRVINIT's registry
    # read hits live values instead of the NTVERP-derived fallback.
    try:
        stamp = lv.parse_ntverp()
        print(f"build stamp: NT {stamp.version_str} "
              f"{stamp.current_build_number} {stamp.channel} {stamp.sha}")
    except Exception as e:
        print(f"build stamp: unavailable ({e})")

    h = build_micront_system_hive(profile=args.profile,
                                  init_exe=args.init_exe,
                                  init_args=args.init_args,
                                  init_stdio=args.init_stdio)
    size = h.write(args.output)
    print(f"SYSTEM hive ({args.profile}): {size} bytes -> {args.output}")

    # SOFTWARE hive is Win32-userland only; micront has no consumer of it.
    if args.profile != "micront":
        sw = build_micront_software_hive(profile=args.profile)
        sw_path = args.output.replace("SYSTEM", "SOFTWARE") if "SYSTEM" in args.output else "SOFTWARE"
        size = sw.write(sw_path)
        print(f"SOFTWARE hive ({args.profile}): {size} bytes -> {sw_path}")

    # DEFAULT hive is what gets mounted at \Registry\User\.Default before
    # any interactive logon — USERSRV's PMAP_COLORS/PMAP_DESKTOP/etc.
    # reads land here when nobody's logged in. Kernel-side CMDAT.C's
    # CmpMachineHiveList expects %SystemRoot%\System32\config\DEFAULT
    # to exist; without it mounts silently fall through to caller defaults.
    if args.profile == "gui":
        df = build_micront_default_hive(profile=args.profile)
        df_path = args.output.replace("SYSTEM", "DEFAULT") if "SYSTEM" in args.output else "DEFAULT"
        size = df.write(df_path)
        print(f"DEFAULT hive ({args.profile}): {size} bytes -> {df_path}")


if __name__ == "__main__":
    main()
