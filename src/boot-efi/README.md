# MicroNT UEFI Loader

A from-scratch gnu-efi PE32 loader that brings up NT 3.5 (x86) under OVMF-ia32 + QEMU. Follows the OSLOADER pattern: write KSEG0 pointers directly into NT structures — no post-paging fixup pass.

Targets: QEMU `-machine q35` with `OVMF32_CODE_4M.secboot.fd` firmware. No effort spent on bare-metal portability; behavior of real UEFI implementations is not in scope.

## Build + run

```sh
make            # builds BOOTIA32.EFI + esp.img
./boot.sh       # runs under QEMU, serial to stdio
GDB=1 ./boot.sh # same, but freezes CPU and listens on :1234 for gdb
./debug.sh      # one-shot: starts QEMU paused, runs gdb.script, captures
```

**Never run qemu directly** — use `boot.sh`. It sets the OVMF pflash vars, muxes COM1 + COM2 to stdio, handles `GDB=1` toggling, and keeps the logs in sensible places. Running QEMU by hand will diverge from what CI + GDB expect.

## High-level flow

`efi_main` (`main.c`) orchestrates:

1. **`com1_init`** — serial alive before anything else; every log line goes to COM1 so we can see what the loader did even after `ExitBootServices`.
2. **`fs_init`** — locate the ESP via `EFI_LOADED_IMAGE_PROTOCOL` on our own handle, then open the simple-file-system on that device.
3. **File reads** — `fs_read` pulls the kernel + HAL + boot drivers + SYSTEM hive into `AllocatePages`'d buffers, each tagged with a `PageKind`.
4. **NLS concat** — `fs_read_into` reads the three code-page files
   (`c_1252`, `c_437`, `l_intl`) into **one contiguous PK_NLS allocation**.
   This is not cosmetic: NT's `Phase1Initialization` computes
   `UnicodeCaseTableDataOffset = UnicodeCaseTableData - AnsiCodePageData` and indexes from the base (`NTOS/INIT/INIT.C:392`). If the three pointers aren't into the same contiguous block, Phase 1 will dereference a bogus VA computed via offset arithmetic.
5. **`pe_stage`** — parse each PE, alloc at `ImageBase & ~KSEG0_BASE` (via `mmu_alloc_at` — required for `/FIXED` images like `ntoskrnl.exe` with no `.reloc`), copy headers + sections, apply base relocations.
6. **`pe_resolve_imports`** — two-pass so `ntoskrnl <-> hal` circular imports resolve.
7. **`mmu_alloc_reserved`** — PD, per-alias PT pools, PCR, SUD, TSS, idle stack, GDT, IDT. Each tagged `PK_MEMORY_DATA` / `PK_PCR`.
8. **`loaderblock_build` + `loaderblock_wire_modules`** — arena-allocated `LOADER_PARAMETER_BLOCK` with KSEG0 pointers written in place.
9. **`memmap_capture`** — last UEFI allocation. MapKey is valid until the next `AllocatePages`/`AllocatePool`, so this MUST be the last UEFI service call before `ExitBootServices`.
10. **`loaderblock_link_memmap`** — pure arena writes, no UEFI calls.
11. **`ExitBootServices`** — point of no return. UEFI services are gone.
12. **`mmu_build_and_activate`** — populate the PD/PTs + GDT/IDT, `lgdt`, `lidt`, `mov cr3`, far-jmp into our segments.
13. **`handoff`** (asm) — switch to a KSEG0 stack alias, push the loader block, `call KiSystemStartup`.

## Memory layout at handoff

```
Virtual                      Physical (QEMU default 128 MB)
┌─────────────────────────┐
│ 0x00000000..0x0FFFFFFF  │  Identity map, torn down early in MM init.
│                         │
│ 0x80000000..0x80FFFFFF  │  First 16 MiB of KSEG0 — the range NT 3.5
│                         │  copies into every new process's PD. Anything
│                         │  the kernel accesses after a CR3 switch
│                         │  (kernel img, HAL, drivers, TSS, PCR, GDT,
│                         │  IDT, PTs, idle stack) MUST live here.
│                         │  Enforced via mmu_alloc_below(.., 0x1000000).
│                         │
│ 0x81000000..0x8FFFFFFF  │  KSEG0 of phys ≥ 16 MiB — ONLY the LPB arena,
│                         │  NLS block, and Phase-0-only scratch. Kernel
│                         │  touches none of this post-MmFreeLoaderBlock.
│                         │  **Only REGISTERED pages are mapped** — see
│                         │  "The KSEG0 trap".
│                         │
│ 0xC0000000..0xC03FFFFF  │  Self-map (PDE[768] = PD).
│ 0xFFC00000..0xFFFFFFFF  │  HAL PT (PDE[1023]). PCR at 0xFFDFF000, SUD at
│                         │  0xFFDF0000.
└─────────────────────────┘
```

All NT structures the kernel reads go into a single arena
(`loaderblock.c`) whose phys base we register as `PK_MEMORY_DATA`. Pointers
are KSEG0-relative at write time — `kseg0_of(phys_ptr)` returns
`phys | KSEG0_BASE`. No post-paging fixup pass.

## The `mmu_alloc` registry

Every `AllocatePages` we make funnels through `mmu_alloc(pages, kind, &phys)`
and gets recorded in a fixed-size registry of `(phys, pages, kind)`. Two
downstream consumers need it:

1. **`memmap_to_nt`** — UEFI's memory map describes generic
   `EfiLoaderData` / `EfiBootServicesCode` regions. NT wants finer
   classification (`LoaderSystemCode`, `LoaderHalCode`, `LoaderBootDriver`,
   `LoaderNlsData`, etc.). The registry overlays `PageKind` onto the UEFI
   map so we emit correct NT memory types.
2. **`build_page_tables`** — KSEG0 maps exactly the registered pages.
   Anything not in the registry is invisible in KSEG0, which is what keeps
   free pages at `ReferenceCount == 0` during the kernel's PDE walk.

`mmu_alloc_at` (vs `AllocateAnyPages`) is used for `/FIXED` images. NT's
`ntoskrnl.exe` is built without relocations; it MUST land at physical
`ImageBase & ~KSEG0_BASE` (= `0x00100000`).

## The KSEG0 trap

**Blanket-mapping all physical RAM into KSEG0 breaks MM init.**

`NTOS/MM/I386/INIT386.C:457-498` walks every valid PDE, then every valid
PTE within each PT, and for each PTE that points at a page `<=
MmHighestPhysicalPage` does:

```c
Pfn2 = MI_PFN_ELEMENT(PointerPte->u.Hard.PageFrameNumber);
...
Pfn2->ReferenceCount = 1;
```

Then a **second** descriptor walk (`:572-594`) adds `LoaderFree` /
`LoaderFirmwareTemporary` pages to the free list — but only if
`Pfn->ReferenceCount == 0`:

```c
case LoaderFree:
case LoaderLoadedProgram:
case LoaderFirmwareTemporary:
case LoaderOsloaderStack:
    Pfn1 = MI_PFN_ELEMENT(NextPhysicalPage);
    while (i != 0) {
        if (Pfn1->ReferenceCount == 0) {
            MiInsertPageInList(MmPageLocationList[FreePageList], ...);
        }
        ...
    }
```

Map all of RAM in KSEG0 → every page has RefCount=1 after the walk → zero
pages reach the free list → `MiRemoveAnyPage` hits an empty list head →
`STOP 0x4E (PFN_LIST_CORRUPT)` with `arg3 = MmAvailablePages = 0`.

The fix is in `build_page_tables`: iterate the allocation registry and map
**only** those phys ranges in KSEG0. Free RAM stays unmapped, its PFN
entries stay at RefCount=0, and the descriptor walk can add them. The
identity mapping is still a blanket 0..256 MB — it only needs to survive
the CR3 swap + `jmp` into KSEG0, and the kernel unmaps PDE[0..511] early.

## The NLS-contiguity trap

NT's `Phase1Initialization` assumes the three NLS blobs are contiguous:

```c
// NTOS/INIT/INIT.C:389-397
InitNlsTableBase = LoaderBlock->NlsData->AnsiCodePageData;
InitOemCodePageDataOffset = OemCodePageData - AnsiCodePageData;
InitUnicodeCaseTableDataOffset = UnicodeCaseTableData - AnsiCodePageData;

RtlInitNlsTables(
    AnsiCodePageData,                                      // base
    (PUCHAR)AnsiCodePageData + InitOemCodePageDataOffset,  // = OEM if contig
    (PUCHAR)AnsiCodePageData + InitUnicodeCaseTableDataOffset, // = Unicode
    &InitTableInfo);
```

Three separate `AllocatePages` calls give you three arbitrary, non-adjacent
phys ranges → `UnicodeCaseTableDataOffset` is a bogus delta → 3rd arg to
`RtlInitNlsTables` lands on unmapped system-PTE space →
`STOP 0x50 (PAGE_FAULT_IN_NONPAGED_AREA)`.

`main.c` probes the three NLS file sizes, page-aligns each slab, allocates
**one** block, reads each file into its slot, and hands
`(base, ansi_off, oem_off, uni_off)` to `loaderblock_set_nls`.

## The contiguous-NLS + single-arena discipline

Both traps above have the same root cause: the kernel assumes adjacency wherever we gave it pointers computed via offset arithmetic. Two rules:

- If the kernel does `base + offset`, the data must be in one allocation.
- If the kernel walks a struct and follows a pointer, that pointer must be
  in a range we've mapped into KSEG0 (= in the registry).

## The TSS-size trap (STOP 0x1E)

NT's `KTSS` is **~8364 bytes**, not the classic 104-byte TSS. Layout (from `NTOS/INC/I386.H`):

- `0x00..0x67` — standard 32-bit TSS header (104 B).
- `0x68..0x208B` — one `KIIO_ACCESS_MAP` (32 B direction map + 8196 B I/O permission bitmap).
- `0x208C..0x20AB` — `KINT_DIRECTION_MAP` (32 B).

`KiInitializeTSS` fills the I/O bitmap with `rep stos` of `0xFFFFFFFF` over `0x801` dwords starting at `TSS+0x88`. A 2-page (8192 B) TSS allocation overflows by ~170 B into whatever is phys-adjacent. In our case that was fastfat's PE headers, which then made `PsLoadedModule` scan bugcheck at `RtlImageNtHeader` returning NULL on the corrupted image. **Always allocate at least 3 pages for TSS** and set the GDT limit accordingly.

## The < 16 MiB trap (kernel-accessible data)

NT 3.5's `MmCreateProcessAddressSpace` (`NTOS/MM/PROCSUP.C:52,297`) copies only these PDE ranges from the idle process's PD to every new process's PD:

- **`CODE_START..CODE_END` = `0x80000000..0x80FFFFFF`** — the first 16 MiB of KSEG0.
- `MmNonPagedSystemStart..NON_PAGED_SYSTEM_END` — nonpaged system PTE/pool area.
- `MM_SYSTEM_CACHE_WORKING_SET..MmSystemCacheEnd` — system cache.

Anything the kernel accesses via KSEG0 at phys ≥ 16 MiB (= KSEG0 virt ≥ `0x81000000`) becomes **unmapped** after `SwapContext` switches CR3 to a new process. This first bites at `mov [ecx+0x1C], eax` in `SwapContext` writing `CR3` into the TSS — if the TSS is at phys > 16 MiB its KSEG0 alias dies on the context switch, triple-faulting immediately.

**Rule**: anything the kernel touches via KSEG0 at runtime (PD, PTs, TSS, PCR, GDT, IDT, idle stack, kernel image, HAL image, driver images) MUST live at phys < 16 MiB. `mmu_alloc_below(pages, kind, 0x01000000, &phys)` uses UEFI `AllocateMaxAddress` to enforce this. `mmu_alloc_reserved` + the `pe_stage` fallback path both use it.

Things the kernel only touches during Phase 0/1 init (LPB arena, NLS block, the PE file blobs we keep around as `LoaderFirmwareTemporary`) don't need to be low — `MmFreeLoaderBlock` runs before any user process spawns, and Phase 0/1 runs under the idle process with our full KSEG0 PD still in place.

## The NT-vs-UEFI struct-alignment trap

gnu-efi's ia32 headers declare `EFI_LBA` as `UINT64`. GCC's default 32-bit ABI aligns `UINT64` to 4 bytes; UEFI's ABI (and OVMF's compiled layout) aligns to 8. Result: reading `bio->Media->LastBlock` reads 4 bytes early, landing in `IoAlign` + first half of `LastBlock`. Symptom: `LastBlock.lo = 0, LastBlock.hi = <real value>`.

Compile with `-malign-double` so our `UINT64` alignment matches OVMF's. Required for `EFI_BLOCK_IO_MEDIA`, `LARGE_INTEGER`, and any UEFI struct containing a `UINT64`.

## The ARC name match (STOP 0x7B → success path)

`IopCreateArcNames` (`NTOS/IO/IOINIT.C:1355`) matches each detected disk against `LoaderBlock->ArcDiskInformation->DiskSignatures` using **all three** of:

- `diskBlock->Signature == driveLayout->Signature` — DWORD at MBR offset `0x1B8`.
- `(diskBlock->CheckSum + sum_of_first_128_dwords_of_MBR) == 0` — we store two's complement.
- `diskBlock->ValidPartitionTable == TRUE`.

All three must hold or no `\ArcName\multi(0)disk(0)rdisk(0)partition(1)` → `\Device\Harddisk0\Partition1` symlink gets created, and the boot volume can't be resolved. `main.c` reads sector 0 via `fs_boot_disk_read_sector0`, computes the checksum, and passes `(signature, negsum)` to `loaderblock_set_boot_disk`.

## The INT 13 drive-parameter blob (STOP 0x7B → Configuration Data)

atdisk's geometry init (`NTOS/DD/HARDDISK/I386/ATD_CONF.C:1278 UpdateGeometryFromBios`) opens `\Registry\Machine\Hardware\Description\System` and reads its `"Configuration Data"` value. The value is constructed by `CmpInitializeRegistryNode` (`NTOS/CONFIG/CMCONFIG.C:657`) from `ConfigurationData` pointer on the node — **which must point at a `CM_PARTIAL_RESOURCE_LIST`** (starting with `Version`), NOT a full `CM_FULL_RESOURCE_DESCRIPTOR`. The kernel prepends the `InterfaceType + BusNumber` header itself.

`CmResourceTypeDeviceSpecific` is enum position **5**, not `0x80`. Confusing it with the INT 13h drive-select value (also `0x80`) silently bakes a wrong `Type` field into the PartialDescriptor and the kernel rejects the blob.

CHS geometry is fabricated from the real disk size (queried via `fs_boot_disk_size` → BlockIo `LastBlock`): heads=16, sectors/track=63, `cyls = ceil(blocks / (16*63))`. atdisk derives `PartitionLength = cyl * heads * spt * 512` and uses that to bound reads.

## The CMOS drive-type trap

atdisk won't even probe the IDE controller unless CMOS byte `0x12` has the high nibble non-zero (indicates "drive 0 present"). Legacy BIOSes write this at POST; OVMF doesn't. We poke CMOS directly before `ExitBootServices`:

```c
// CMOS[0x12] = 0xF0  → drive 0 = extended type, drive 1 = none
// CMOS[0x19] = 47    → extended type value (arbitrary non-zero)
```

The actual value at `0x19` doesn't matter because atdisk's `IssueIdentify` queries the drive for real geometry — but byte `0x12` gating is a hard prerequisite.

## Debugging

### Reading NT bugcheck output

The bugcheck banner (`*** STOP: 0xNN ...`) from `KeBugCheckEx` + the module list is emitted on COM2 by the HAL. Both COM1 and COM2 are muxed to stdio in `boot.sh`, so scrollback has everything.

Common codes:

| Code | Meaning                               | Hints                                |
|------|---------------------------------------|--------------------------------------|
| 0x1E | `KMODE_EXCEPTION_NOT_HANDLED`         | arg1=exc_code, arg2=EIP, arg4=fault VA. Often flagged as `MM_NONPAGED_POOL_END` in some NT docs — it just means the kernel faulted and no handler caught it. Common cause: TSS-size overrun (see "TSS-size trap"). |
| 0x4E | `PFN_LIST_CORRUPT`                    | arg3=`MmAvailablePages` — 0 means no pages reached free list. See "KSEG0 trap". |
| 0x50 | `PAGE_FAULT_IN_NONPAGED_AREA`         | arg1=VA. Often NLS-offset bug. See "NLS-contiguity trap". |
| 0x6B | `PROCESS1_INITIALIZATION_FAILED`      | arg1=NTSTATUS from smss launch. `0xC000003A` = `STATUS_OBJECT_PATH_NOT_FOUND` → check `NtBootPathName` matches the on-disk layout. |
| 0x7B | `INACCESSIBLE_BOOT_DEVICE`            | arg1=addr of boot device path ptr, arg2 = NTSTATUS. `0xC0000034` = `STATUS_OBJECT_NAME_NOT_FOUND` → ARC name match failed (see "ARC name match"). Watch for intermediate fails: atdisk not seeing the disk (CMOS trap), `IoReadPartitionTable` returning `STATUS_INVALID_PARAMETER` (disk geometry zeroed — INT 13 blob trap), or the partition table itself missing (esp.img needs MBR). |
| _triple_ | Uncaught fault, QEMU `-no-reboot` bails. | Grab `qemu.log` with `-d int,cpu_reset` — it prints the last trap frame + faulting instruction. Common cause: KSEG0 access to phys ≥ 16 MiB after `SwapContext` (see "< 16 MiB trap"). |
| 0xC0000005 | (as arg1 of 0x1E) Access violation | arg2 has the EIP of the faulting instruction. |

### gdb stub workflow

`./debug.sh` launches QEMU paused (`-s -S`) and drives gdb in batch mode via `gdb.script`. Typical uses:

- Break at a known code point (`break *0x80112ff6` = PE entry thunk) to
  verify you got there.
- Break at `$KeBugCheckEx` (from `ntoskrnl.gdb` generated by `pe2gdb.py`)
  to catch every bugcheck. Inspect `[esp+0..20]` for the call site + five
  params.
- Break just before a faulting instruction and inspect registers to see
  which data was wrong. For the STOP 0x1E at `0x801b3b29` example, we
  broke at `0x801b3b20` in a loop, dumped `eax` (DllBase) + `edi` (LDR
  entry) on each iteration to identify which module had the bad headers.

### Symbol lookup

`pe2gdb.py` produces `ntoskrnl.gdb` and `hal.gdb` from each PE's export table. Only exported symbols are there; internal static functions (KiTrap, MiReserveSystemPtes, many MM helpers) are not. For those, disassemble around the address and identify the function by its call pattern:

- `mov eax, fs:0x0` / `mov fs:0x0, esp` prologue → SEH-guarded routine.
- `cmp WORD [x], 0x5a4d` + `cmp DWORD [x+e_lfanew], 0x4550` →
  `RtlImageNtHeader`.
- `mov edi, DWORD PTR ds:0xXXXXXXXX` where the constant is a list head →
  walking `PsLoadedModuleList` or similar.

The nearest-exported-symbol gap tells you roughly where you are but won't name the static. Don't fight it; use the code shape.

### Layout assumptions

Check these when a bugcheck points at LPB dereference:

- `LDR_DATA_TABLE_ENTRY` offsets must match NT 3.5 (`NT/PUBLIC/SDK/INC/NTLDR.H`).
- `LOADER_PARAMETER_BLOCK` must match `NTOS/INC/ARC.H:1596`. The `union
  { I386_LOADER_BLOCK I386; ... }` at the end is the same offset for all
  arches; we just define the I386 variant directly.
- `MEMORY_ALLOCATION_DESCRIPTOR` is 20 bytes (LIST_ENTRY + enum + 2×ULONG).
- `NT_MEMORY_TYPE` enum values are load-bearing: `LoaderSystemCode=9`,
  `LoaderHalCode=10`, `LoaderBootDriver=11`, `LoaderStartupPcrPage=17`,
  `LoaderRegistryData=19`, `LoaderMemoryData=20`, `LoaderNlsData=21`.

## Files

| File               | Role                                                |
|--------------------|-----------------------------------------------------|
| `main.c`           | `efi_main` orchestrator                             |
| `com1.[ch]`        | Raw COM1 serial (post-ExitBootServices survival)    |
| `fs.[ch]`          | ESP reads: `fs_read`, `fs_read_into`, `fs_file_size`, `fs_boot_disk_size`, `fs_boot_disk_read_sector0` |
| `mmu.[ch]`         | Page alloc registry, `mmu_alloc{,_at,_below}`, PD/PT/GDT/IDT build |
| `pe.[ch]`          | PE32 staging + import resolution                    |
| `memmap.[ch]`      | UEFI → NT memory descriptor translation             |
| `loaderblock.[ch]` | LPB arena + LDR entries                             |
| `nt.h`             | NT kernel structure layouts (no NT headers dragged in) |
| `handoff.S`        | Final asm: disable ints, switch stack, FPU init, call KiSystemStartup |
| `boot.sh`          | QEMU launcher, COM1+COM2 muxed, optional `GDB=1`    |
| `debug.sh`         | One-shot gdb session                                |
| `gdb.script`       | Scripted gdb commands for `debug.sh`                |
