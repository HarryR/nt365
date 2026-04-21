# MicroNT UEFI Loader

A from-scratch gnu-efi PE loader that brings up NT 3.5 (x86) under OVMF64 + QEMU. Follows the OSLOADER pattern: write KSEG0 pointers directly into NT structures — no post-paging fixup pass.

The loader itself is 64-bit (OVMF64 / `BOOTX64.EFI`); the NT kernel is 32-bit non-PAE. A transition stub (`handoff.S`) performs the formal long-mode exit (CR0.PG → EFER.LME → CR4.PAE → CR3 swap) before calling `KiSystemStartup`.

Targets: QEMU `-machine pc` (i440fx + PIIX3) with `OVMF_CODE_4M.fd` firmware. Works without modification on OVMF's 32-bit and 64-bit images, but we commit to 64-bit to keep a single code path. No effort spent on bare-metal portability; behavior of real UEFI implementations is not in scope.

## Build + run

```sh
make            # builds BOOTX64.EFI
./boot.sh       # runs under QEMU, COM1+COM2 muxed to stdio
GDB=1 ./boot.sh # same, but freezes CPU and listens on :1234 for gdb
TRACE=1 ./boot.sh # enables -d int,cpu_reset,in_asm to ./qemu.log
./debug.sh      # one-shot: starts QEMU paused, runs gdb.script, captures
```

**Never run qemu directly** — use `boot.sh`. It sets the OVMF pflash vars, muxes COM1 + COM2 to stdio, handles `GDB=1` and `TRACE=1` toggling, and keeps the logs in sensible places. Running QEMU by hand will diverge from what CI + GDB expect.

## High-level flow

`efi_main` (`main.c`) orchestrates:

1. **`com1_init`** — serial alive before anything else; every log line goes to COM1 so we can see what the loader did even after `ExitBootServices`.
2. **`fs_init`** — locate the ESP via `EFI_LOADED_IMAGE_PROTOCOL` on our own handle, then open the simple-file-system on that device.
3. **File reads** — `fs_read` pulls the kernel + HAL + boot drivers + SYSTEM hive into `AllocatePages`'d buffers, each tagged with a `PageKind`.
4. **NLS concat** — `fs_read_into` reads the three code-page files (`c_1252`, `c_437`, `l_intl`) into **one contiguous `PK_NLS` allocation**. Not cosmetic: NT's `Phase1Initialization` computes `UnicodeCaseTableDataOffset = UnicodeCaseTableData - AnsiCodePageData` and indexes from the base (`NTOS/INIT/INIT.C:392`). If the three pointers aren't into the same contiguous block, Phase 1 dereferences a bogus VA.
5. **`pe_stage`** — parse each PE, alloc at `ImageBase & ~KSEG0_BASE` (via `mmu_alloc_at` — required for `/FIXED` images like `ntoskrnl.exe` with no `.reloc`), copy headers + sections, apply base relocations.
6. **`pe_resolve_imports`** — two-pass so `ntoskrnl <-> hal` circular imports resolve.
7. **`mmu_alloc_reserved`** — PD, PCR, SUD, TSS, idle stack, GDT, IDT. All `< 16 MiB` (see "The < 16 MiB trap").
8. **`arena_init`** — reserve the shared arena for LPB and hwtree (< 16 MiB, `PK_MEMORY_DATA`).
9. **`hwtree_build`** — ARC hardware inventory: synthesize disk CHS + probe UARTs, emit `CONFIGURATION_COMPONENT_DATA` tree. Returns the root KSEG0 VA.
10. **`lpb_build` + `lpb_wire_modules`** — `LOADER_PARAMETER_BLOCK` + its lists/strings/LDR entries, ConfigurationRoot latched from hwtree.
11. **`mmu_register_image`** — register the UEFI-placed loader image and current stack in the identity-only registry so they're reachable across the mode drop but NOT mirrored into KSEG0.
12. **`mmu_alloc_pt_pool`** — size + allocate the exact-count PT pool based on registered PDE slots. Last UEFI allocation.
13. **`memmap_capture`** — captures the final UEFI memory map. MapKey valid until any further UEFI call.
14. **`lpb_link_memmap`** — pure arena writes, no UEFI calls. MapKey still valid.
15. **`ExitBootServices`** — point of no return. UEFI services are gone; we're still in long mode on UEFI's PML4.
16. **`mmu_build_and_activate`** — populate the 32-bit PD/PTs + GDT/IDT + TSS. No CR3 change yet — those are staged for the transition stub.
17. **`handoff`** (asm) — formal long-mode exit + transition into `KiSystemStartup`. See "The 64→32 mode drop".

## Memory layout at handoff

```
Virtual                      Physical (QEMU default 128 MB)
┌─────────────────────────┐
│ 0x00000000..0x00FFFFFF  │  Identity map — ONLY for registered pages
│                         │  (mmu_alloc'd + loader image + UEFI stack).
│                         │  Torn down when NT switches CR3 to a new
│                         │  process PD (MmCreateProcessAddressSpace
│                         │  doesn't copy PDE[0..511]).
│                         │
│ 0x80000000..0x80FFFFFF  │  First 16 MiB of KSEG0 — mirrors phys 0..16 MiB
│                         │  for registered pages only. PDE[512..515]
│                         │  specifically is the range NT 3.5 copies into
│                         │  every new process PD, so everything the
│                         │  kernel touches post-process-switch lives
│                         │  here. Enforced via mmu_alloc_below(.., 0x1000000).
│                         │
│ 0x81000000..0xBFFFFFFF  │  KSEG0 of phys ≥ 16 MiB — ONLY the LPB arena,
│                         │  NLS block, and Phase-0-only scratch. Kernel
│                         │  touches none of this post-MmFreeLoaderBlock.
│                         │
│ 0xC0000000..0xC03FFFFF  │  Self-map (PDE[768] = PD).
│ 0xFFC00000..0xFFFFFFFF  │  HAL PT (PDE[1023]). PCR at 0xFFDFF000, SUD at
│                         │  0xFFDF0000.
└─────────────────────────┘
```

Identity + KSEG0 mirror use **separate PT pages** so kernel edits on KSEG0 PTs don't bleed into the identity view during teardown. Both are populated only for registered phys ranges — **never blanket**. See "The KSEG0 trap" for why.

All NT structures the kernel reads go into a single arena (`arena.c`) whose phys base we register as `PK_MEMORY_DATA`. Pointers are KSEG0-relative at write time — `kseg0_of(phys_ptr)` returns `phys | KSEG0_BASE`. No post-paging fixup pass.

## The `mmu_alloc` registry

Every `AllocatePages` we make funnels through `mmu_alloc(pages, kind, &phys)` (or its `_at` / `_below` variants) and gets recorded in a fixed-size registry of `(phys, pages, kind)`. Three downstream consumers need it:

1. **`memmap_to_nt`** — UEFI's memory map describes generic `EfiLoaderData` / `EfiBootServicesCode` regions. NT wants finer classification (`LoaderSystemCode`, `LoaderHalCode`, `LoaderBootDriver`, `LoaderNlsData`, etc.). The registry overlays `PageKind` onto the UEFI map so we emit correct NT memory types.
2. **`build_page_tables`** — identity map + KSEG0 mirror cover exactly the registered pages. Anything not in the registry is invisible, which is what keeps free pages at `ReferenceCount == 0` during the kernel's PDE walk.
3. **`mmu_alloc_pt_pool`** — counts unique PDE slots across both registries and allocates an exact-sized PT pool. Replaces the old "blanket-allocate 256 MB worth of PTs" approach.

A second, identity-only registry (`mmu_register_image`) covers phys ranges we didn't allocate but must still identity-map (the UEFI-placed loader image, our current stack). These are present in the identity map only — they are NOT mirrored into KSEG0 and NOT overlaid onto memmap (the coarse UEFI→NT type stands).

`mmu_alloc_at` (vs `AllocateAnyPages`) is used for `/FIXED` images. NT's `ntoskrnl.exe` is built without relocations; it MUST land at physical `ImageBase & ~KSEG0_BASE` (= `0x00100000`).

`mmu_alloc_below` uses UEFI `AllocateMaxAddress` to pin allocations below a given phys cap. Used for everything the kernel must reach post-process-switch (see "The < 16 MiB trap").

## The KSEG0 trap

**Blanket-mapping all physical RAM into KSEG0 breaks MM init.**

`NTOS/MM/I386/INIT386.C:457-498` walks every valid PDE, then every valid PTE within each PT, and for each PTE that points at a page `<= MmHighestPhysicalPage` does:

```c
Pfn2 = MI_PFN_ELEMENT(PointerPte->u.Hard.PageFrameNumber);
...
Pfn2->ReferenceCount = 1;
```

Then a **second** descriptor walk (`:572-594`) adds `LoaderFree` / `LoaderFirmwareTemporary` pages to the free list — but only if `Pfn->ReferenceCount == 0`:

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

Map all of RAM in KSEG0 → every page has RefCount=1 after the walk → zero pages reach the free list → `MiRemoveAnyPage` hits an empty list head → `STOP 0x4E (PFN_LIST_CORRUPT)` with `arg3 = MmAvailablePages = 0`.

The fix is `build_page_tables`: iterate the allocation registry and map **only** those phys ranges in KSEG0. Free RAM stays unmapped, its PFN entries stay at RefCount=0, and the descriptor walk can add them.

## The 64→32 mode drop

`handoff.S` runs in long mode on UEFI's PML4 after `ExitBootServices`. It must hand control to 32-bit `KiSystemStartup` in our own PD. The stub steps are:

1. Stash the six args (kernel entry, LPB KSEG0, stack top KSEG0, PD phys, GDT phys, IDT phys) into a `trans_block` data area resolved via RIP-relative LEA. All subsequent accesses go through `%rbx`/`%ebx` + offset — no absolute symbol references, which would require R_X86_64_32 relocations gnu-efi's PIE linker refuses to emit.
2. `LGDT` with the 10-byte long-mode form (phys base). UEFI's PML4 identity-maps phys, so the far-return below can fetch the CS descriptor.
3. Far-return to 32-bit compat CS (`KGDT_R0_CODE = 0x08`).
4. Reload data segments with 32-bit `KGDT_R0_DATA = 0x10`.
5. `CR0.PG = 0` — disable paging. Now unpaged; next instruction fetch continues at the same linear addr because UEFI identity-mapped the stub.
6. `EFER.LME = 0` — exit long mode.
7. Normalise `CR4` to `0x640` (`OSFXSR | OSXMMEXCPT | MCE`). OVMF64 leaves extra bits set (PAE, PGE, PSE) that NT 3.5's MM doesn't expect.
8. `CR3 = TR_PD` — load our NT page directory.
9. `CR0.PG = 1` — re-enable paging, now 32-bit non-PAE.
10. `LGDT` AGAIN with the 6-byte 32-bit form + **KSEG0** base. See "The GDTR/IDTR survival trap".
11. `LIDT` with KSEG0 base (same reason).
12. `FS = KGDT_R0_PCR` (`0x30`) — selector base = `0xFFDFF000` (from the GDT).
13. `LTR KGDT_TSS` (`0x28`) — install 32-bit TSS.
14. Switch ESP to KSEG0 idle-stack top.
15. `clts` / `fninit` / `fnclex` — FPU fresh state.
16. Push LPB; `call [kernel_entry]`. Never returns.

The code path between `CR0.PG=0` and `CR0.PG=1` is identity-mapped in UEFI's PML4 (RAM) AND in our NT PD (via `mmu_register_image` of the loader image), so the instruction stream survives the CR3 swap without discontinuity.

## The GDTR/IDTR survival trap (KSEG0 re-LGDT/LIDT)

After the CR3 swap the CPU holds a GDTR/IDTR base that references the descriptor tables. On every trap or segment load the CPU re-walks the page tables via the current CR3 to fetch the descriptor. If that base isn't reachable in the current CR3, the fetch faults — and the fault handler's own IDT fetch faults too → `#DF` → `#DF` TSS walks also fail → **triple fault**.

The bind is `MmCreateProcessAddressSpace` (`NTOS/MM/PROCSUP.C:297-305`). When the kernel builds a new process PD it copies only:

- **`CODE_START..CODE_END` = `0x80000000..0x80FFFFFF`** (KSEG0 first 16 MiB).
- `MmNonPagedSystemStart..NON_PAGED_SYSTEM_END` (non-paged PTE/pool).
- `MM_SYSTEM_CACHE_WORKING_SET..MmSystemCacheEnd` (system cache).

**Not copied: PDE[0..511] (the entire identity map).** The moment the kernel switches CR3 to the new process PD, any descriptor referenced by its phys linear address becomes unreachable.

Fix: the pre-drop `LGDT` uses phys (required — UEFI's PML4 only identity-maps phys, and we need the far-return to fetch the CS descriptor). Post-CR3-swap we **re-`LGDT`** with the KSEG0 alias (`phys | 0x80000000`) of the same GDT page, and `LIDT` with the KSEG0 alias of the IDT page. The GDT/IDT phys pages live `< 16 MiB` (via `mmu_alloc_below`), so their KSEG0 aliases fall in PDE[512..515] — the copied range. GDTR/IDTR survive every process-PD swap forever.

Symptoms when you forget this: the kernel runs through HAL init, Kd init, MM Phase 0, down to the first paged-pool demand-zero fault, then triple-faults on the #PF. `qemu.log` with `TRACE=1` shows `check_exception old: 0xe new 0xe` → `check_exception old: 0x8 new 0xe` → `Triple fault`, with `IDT= 00XXXXXX` (phys — the giveaway).

## The struct-packing trap (64-bit loader ↔ 32-bit kernel)

The loader builds in x86_64 long mode; the kernel reads the same structs in 32-bit mode. Under `-m64`, `LIST_ENTRY` (two pointers) is 16 bytes and `PVOID` is 8; under `-m32` they're 8 and 4. Leaving the NT structs with native C pointer types shifts every field by 4+ bytes and the kernel reads garbage.

`nt.h` declares all pointer-equivalent fields as `UINT32` — the "wire-format pointer" type, holds a KSEG0 VA (always `0x80000000..0xFFFFFFFF`, always fits in 32 bits). `LIST_ENTRY` is redefined as `NT_LIST_ENTRY` (two `UINT32` Flink/Blink). `lpb.c` and `hwtree.c` cast pointers into these fields via `kseg0_of(phys_ptr) = (UINT32)((UINTN)ptr | KSEG0_BASE)`. Reviewed structures include `LOADER_PARAMETER_BLOCK`, `MEMORY_ALLOCATION_DESCRIPTOR`, `LDR_DATA_TABLE_ENTRY`, `BOOT_DRIVER_LIST_ENTRY`, `CONFIGURATION_COMPONENT_DATA`, `ARC_DISK_SIGNATURE`, `NLS_DATA_BLOCK`.

Symptom when you forget this: HAL Phase 0 prints fine (no struct deref), then `MmInitializeMemoryLimits` walks `LoaderBlock->MemoryDescriptorListHead` with wrong field offsets, reads garbage `MemoryType`, and dies somewhere in PFN DB setup with wildly varying fault addresses run-to-run.

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

Three separate `AllocatePages` calls give you three arbitrary, non-adjacent phys ranges → `UnicodeCaseTableDataOffset` is a bogus delta → 3rd arg to `RtlInitNlsTables` lands on unmapped system-PTE space → `STOP 0x50 (PAGE_FAULT_IN_NONPAGED_AREA)`.

`main.c` probes the three NLS file sizes, page-aligns each slab, allocates **one** block, reads each file into its slot, and hands `(base, ansi_off, oem_off, uni_off)` to `lpb_set_nls`.

## The contiguous-NLS + single-arena discipline

Both contiguity traps have the same root cause: the kernel assumes adjacency wherever we gave it pointers computed via offset arithmetic. Two rules:

- If the kernel does `base + offset`, the data must be in one allocation.
- If the kernel walks a struct and follows a pointer, that pointer must be in a range we've mapped into KSEG0 (= in the registry).

## The TSS-size trap (STOP 0x1E)

NT's `KTSS` is **~8364 bytes**, not the classic 104-byte TSS. Layout (from `NTOS/INC/I386.H`):

- `0x00..0x67` — standard 32-bit TSS header (104 B).
- `0x68..0x208B` — one `KIIO_ACCESS_MAP` (32 B direction map + 8196 B I/O permission bitmap).
- `0x208C..0x20AB` — `KINT_DIRECTION_MAP` (32 B).

`KiInitializeTSS` fills the I/O bitmap with `rep stos` of `0xFFFFFFFF` over `0x801` dwords starting at `TSS+0x88`. A 2-page (8192 B) TSS allocation overflows by ~170 B into whatever is phys-adjacent. In our case that was fastfat's PE headers, which then made `PsLoadedModule` scan bugcheck at `RtlImageNtHeader` returning NULL on the corrupted image. **Always allocate at least 3 pages for TSS** and set the GDT limit accordingly.

## The < 16 MiB trap (kernel-accessible data)

`MmCreateProcessAddressSpace` copies PDE[512..515] of KSEG0 into every process PD (see "The GDTR/IDTR survival trap" for the full PDE list). Anything the kernel accesses via KSEG0 at phys ≥ 16 MiB (= KSEG0 virt ≥ `0x81000000`) becomes **unmapped** after `SwapContext` switches CR3.

**Rule**: anything the kernel touches via KSEG0 at runtime (PD, PTs, TSS, PCR, GDT, IDT, idle stack, kernel image, HAL image, driver images) MUST live at phys < 16 MiB. `mmu_alloc_below(pages, kind, 0x01000000, &phys)` uses UEFI `AllocateMaxAddress` to enforce this. `mmu_alloc_reserved` + the `pe_stage` fallback path both use it.

Things the kernel only touches during Phase 0/1 init (LPB arena, NLS block, the PE file blobs we keep around as `LoaderFirmwareTemporary`) don't need to be low — `MmFreeLoaderBlock` runs before any user process spawns, and Phase 0/1 runs under the idle process with our full KSEG0 PD still in place. That said, `mmu_alloc_below` is the default for the LPB arena too, because UEFI on larger guests tends to place `EfiLoaderData` allocations high, and a high arena means KSEG0 access to the LPB dies the moment a driver's init routine runs under a new process.

## The ARC name match (STOP 0x7B → success path)

`IopCreateArcNames` (`NTOS/IO/IOINIT.C:1355`) matches each detected disk against `LoaderBlock->ArcDiskInformation->DiskSignatures` using **all three** of:

- `diskBlock->Signature == driveLayout->Signature` — DWORD at MBR offset `0x1B8`.
- `(diskBlock->CheckSum + sum_of_first_128_dwords_of_MBR) == 0` — we store two's complement.
- `diskBlock->ValidPartitionTable == TRUE`.

All three must hold or no `\ArcName\multi(0)disk(0)rdisk(0)partition(1)` → `\Device\Harddisk0\Partition1` symlink gets created, and the boot volume can't be resolved. `main.c` reads sector 0 via `fs_boot_disk_read_sector0`, computes the checksum, and passes `(signature, negsum)` to `lpb_set_boot_disk`.

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

## The NT-vs-UEFI struct-alignment trap

gnu-efi's ia32 headers declare `EFI_LBA` as `UINT64`. GCC's default 32-bit ABI aligns `UINT64` to 4 bytes; UEFI's ABI (and OVMF's compiled layout) aligns to 8. Result: reading `bio->Media->LastBlock` reads 4 bytes early, landing in `IoAlign` + first half of `LastBlock`. Symptom: `LastBlock.lo = 0, LastBlock.hi = <real value>`.

(Historical — resolved by switching to the x86_64 build where `UINT64` natively aligns to 8. Kept here as context for anyone resurrecting the 32-bit loader path.)

## Debugging

### Reading NT bugcheck output

The bugcheck banner (`*** STOP: 0xNN ...`) from `KeBugCheckEx` + the module list is emitted on COM2 by the HAL. Both COM1 and COM2 are muxed to stdio in `boot.sh`, so scrollback has everything.

Common codes:

| Code | Meaning                               | Hints                                |
|------|---------------------------------------|--------------------------------------|
| 0x1E | `KMODE_EXCEPTION_NOT_HANDLED`         | arg1=exc_code, arg2=EIP, arg4=fault VA. Common cause: TSS-size overrun (see "TSS-size trap"). |
| 0x4E | `PFN_LIST_CORRUPT`                    | arg3=`MmAvailablePages` — 0 means no pages reached free list. See "KSEG0 trap". |
| 0x50 | `PAGE_FAULT_IN_NONPAGED_AREA`         | arg1=VA. Often NLS-offset bug. See "NLS-contiguity trap". |
| 0x6B | `PROCESS1_INITIALIZATION_FAILED`      | arg1=NTSTATUS from smss launch. `0xC000003A` = `STATUS_OBJECT_PATH_NOT_FOUND` → check `NtBootPathName` matches the on-disk layout. |
| 0x7B | `INACCESSIBLE_BOOT_DEVICE`            | arg1=addr of boot device path ptr, arg2 = NTSTATUS. `0xC0000034` = `STATUS_OBJECT_NAME_NOT_FOUND` → ARC name match failed (see "ARC name match"). Watch for intermediate fails: atdisk not seeing the disk (CMOS trap), `IoReadPartitionTable` returning `STATUS_INVALID_PARAMETER` (disk geometry zeroed — INT 13 blob trap), or the partition table itself missing (esp.img needs MBR). |
| _triple_ | Uncaught fault, QEMU `-no-reboot` bails. | Grab `qemu.log` with `TRACE=1` — it prints the last trap frame + faulting instruction + `check_exception` chain. `IDT=` line shows whether GDTR/IDTR survived the CR3 swap (phys base = you forgot the KSEG0 re-LGDT/LIDT). |
| 0xC0000005 | (as arg1 of 0x1E) Access violation | arg2 has the EIP of the faulting instruction. |

### gdb stub workflow

`./debug.sh` launches QEMU paused (`-s -S`) and drives gdb in batch mode via `gdb.script`. Typical uses:

- Break at a known code point (`break *0x80112ff6` = PE entry thunk) to verify you got there.
- Break at `$KeBugCheckEx` (from `ntoskrnl.gdb` generated by `pe2gdb.py`) to catch every bugcheck. Inspect `[esp+0..20]` for the call site + five params.
- Break just before a faulting instruction and inspect registers to see which data was wrong.

### Symbol lookup

`pe2gdb.py` produces `ntoskrnl.gdb` and `hal.gdb` from each PE's export table. Only exported symbols are there; internal static functions (KiTrap, MiReserveSystemPtes, many MM helpers) are not. For those, disassemble around the address and identify the function by its call pattern:

- `mov eax, fs:0x0` / `mov fs:0x0, esp` prologue → SEH-guarded routine.
- `cmp WORD [x], 0x5a4d` + `cmp DWORD [x+e_lfanew], 0x4550` → `RtlImageNtHeader`.
- `mov edi, DWORD PTR ds:0xXXXXXXXX` where the constant is a list head → walking `PsLoadedModuleList` or similar.

The nearest-exported-symbol gap tells you roughly where you are but won't name the static. Don't fight it; use the code shape.

### Layout assumptions

Check these when a bugcheck points at LPB dereference:

- `LDR_DATA_TABLE_ENTRY` offsets must match NT 3.5 (`NT/PUBLIC/SDK/INC/NTLDR.H`).
- `LOADER_PARAMETER_BLOCK` must match `NTOS/INC/ARC.H:1596`. The `union { I386_LOADER_BLOCK I386; ... }` at the end is the same offset for all arches; we just define the I386 variant directly.
- `MEMORY_ALLOCATION_DESCRIPTOR` is 20 bytes (LIST_ENTRY + enum + 2×ULONG).
- `NT_MEMORY_TYPE` enum values are load-bearing: `LoaderSystemCode=9`, `LoaderHalCode=10`, `LoaderBootDriver=11`, `LoaderStartupPcrPage=17`, `LoaderRegistryData=19`, `LoaderMemoryData=20`, `LoaderNlsData=21`.
- All pointer-width fields in `nt.h` must be `UINT32`. If you find one declared `PVOID` or `PLIST_ENTRY`, it's the struct-packing trap.

## Files

| File               | Role                                                |
|--------------------|-----------------------------------------------------|
| `main.c`           | `efi_main` orchestrator                             |
| `com1.[ch]`        | Raw COM1 serial (post-ExitBootServices survival)    |
| `fs.[ch]`          | ESP reads: `fs_read`, `fs_read_into`, `fs_file_size`, `fs_boot_disk_size`, `fs_boot_disk_read_sector0` |
| `mmu.[ch]`         | Page alloc registry, `mmu_alloc{,_at,_below,_register_image,_alloc_pt_pool}`, PD/PT/GDT/IDT build |
| `pe.[ch]`          | PE staging + import resolution                      |
| `memmap.[ch]`      | UEFI → NT memory descriptor translation             |
| `arena.[ch]`       | Bump allocator + ASCII/UTF-16 string dup, singleton shared by lpb + hwtree |
| `uart.[ch]`        | Scratch-register UART presence probe                |
| `hwtree.[ch]`      | ARC `CONFIGURATION_COMPONENT_DATA` tree — disk CHS, ISA bus + SerialControllers |
| `lpb.[ch]`         | LOADER_PARAMETER_BLOCK struct + LDR entries + memory descriptor list + boot driver list |
| `nt.h`             | NT kernel structure layouts in UINT32-wire form (no NT headers dragged in) |
| `handoff.S`        | 64→32 mode drop + final segment/TSS/IDT install + `KiSystemStartup` tail call |
| `boot.sh`          | QEMU launcher, COM1+COM2 muxed, `GDB=1` / `TRACE=1` / `MEM=` toggles |
| `debug.sh`         | One-shot gdb session                                |
| `gdb.init`         | Scripted gdb commands for `debug.sh`                |
