#include "pe.h"
#include "log.h"

/*
 * PE32 structures (locally defined so we don't drag NT headers into the
 * UEFI freestanding build). Values taken straight from winnt.h.
 */

#define IMAGE_DOS_SIGNATURE  0x5A4D   /* "MZ" */
#define IMAGE_NT_SIGNATURE   0x00004550 /* "PE\0\0" */

#define IMAGE_FILE_MACHINE_I386  0x014C

#define IMAGE_DIRECTORY_ENTRY_EXPORT    0
#define IMAGE_DIRECTORY_ENTRY_IMPORT    1
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5

#define IMAGE_REL_BASED_ABSOLUTE 0
#define IMAGE_REL_BASED_HIGH     1
#define IMAGE_REL_BASED_LOW      2
#define IMAGE_REL_BASED_HIGHLOW  3
#define IMAGE_REL_BASED_HIGHADJ  4

#define IMAGE_ORDINAL_FLAG32  0x80000000u

typedef struct __attribute__((packed)) {
    UINT16 e_magic, e_cblp, e_cp, e_crlc;
    UINT16 e_cparhdr, e_minalloc, e_maxalloc, e_ss;
    UINT16 e_sp, e_csum, e_ip, e_cs, e_lfarlc, e_ovno;
    UINT16 e_res[4];
    UINT16 e_oemid, e_oeminfo;
    UINT16 e_res2[10];
    UINT32 e_lfanew;
} image_dos_header_t;

typedef struct __attribute__((packed)) {
    UINT16 Machine, NumberOfSections;
    UINT32 TimeDateStamp, PointerToSymbolTable, NumberOfSymbols;
    UINT16 SizeOfOptionalHeader, Characteristics;
} image_file_header_t;

typedef struct __attribute__((packed)) {
    UINT32 VirtualAddress;
    UINT32 Size;
} image_data_directory_t;

typedef struct __attribute__((packed)) {
    UINT16 Magic;                        /* 0x010B for PE32 */
    UINT8  MajorLinkerVersion, MinorLinkerVersion;
    UINT32 SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData;
    UINT32 AddressOfEntryPoint, BaseOfCode, BaseOfData;
    UINT32 ImageBase;
    UINT32 SectionAlignment, FileAlignment;
    UINT16 MajorOperatingSystemVersion, MinorOperatingSystemVersion;
    UINT16 MajorImageVersion, MinorImageVersion;
    UINT16 MajorSubsystemVersion, MinorSubsystemVersion;
    UINT32 Win32VersionValue;
    UINT32 SizeOfImage, SizeOfHeaders;
    UINT32 CheckSum;
    UINT16 Subsystem, DllCharacteristics;
    UINT32 SizeOfStackReserve, SizeOfStackCommit;
    UINT32 SizeOfHeapReserve, SizeOfHeapCommit;
    UINT32 LoaderFlags, NumberOfRvaAndSizes;
    image_data_directory_t DataDirectory[16];
} image_optional_header32_t;

typedef struct __attribute__((packed)) {
    UINT32                    Signature;
    image_file_header_t       FileHeader;
    image_optional_header32_t OptionalHeader;
} image_nt_headers32_t;

typedef struct __attribute__((packed)) {
    char   Name[8];
    UINT32 VirtualSize;
    UINT32 VirtualAddress;
    UINT32 SizeOfRawData;
    UINT32 PointerToRawData;
    UINT32 PointerToRelocations, PointerToLinenumbers;
    UINT16 NumberOfRelocations, NumberOfLinenumbers;
    UINT32 Characteristics;
} image_section_header_t;

typedef struct __attribute__((packed)) {
    UINT32 VirtualAddress;
    UINT32 SizeOfBlock;
    /* UINT16 entries[]; — SizeOfBlock-8 bytes of 16-bit (type<<12 | offset) */
} image_base_relocation_t;

typedef struct __attribute__((packed)) {
    UINT32 OriginalFirstThunk;   /* RVA to ILT (UINT32[]) */
    UINT32 TimeDateStamp, ForwarderChain;
    UINT32 Name;                  /* RVA to DLL name (ASCIZ) */
    UINT32 FirstThunk;            /* RVA to IAT (UINT32[], patched at load) */
} image_import_descriptor_t;

typedef struct __attribute__((packed)) {
    UINT32 Characteristics, TimeDateStamp;
    UINT16 MajorVersion, MinorVersion;
    UINT32 Name;
    UINT32 Base;
    UINT32 NumberOfFunctions;
    UINT32 NumberOfNames;
    UINT32 AddressOfFunctions;   /* RVA to UINT32[NumberOfFunctions] */
    UINT32 AddressOfNames;       /* RVA to UINT32[NumberOfNames] */
    UINT32 AddressOfNameOrdinals;/* RVA to UINT16[NumberOfNames] */
} image_export_directory_t;

/*----------------------------------------------------------------------------
 * Helpers
 *---------------------------------------------------------------------------*/

static int ascii_icmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int ascii_cmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return *a - *b;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void *memcpy_u8(void *dst, const void *src, UINTN n) {
    UINT8 *d = dst;
    const UINT8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

static void memzero(void *dst, UINTN n) {
    UINT8 *d = dst;
    while (n--) *d++ = 0;
}

/*----------------------------------------------------------------------------
 * pe_stage
 *---------------------------------------------------------------------------*/

EFI_STATUS pe_stage(const void *blob, UINTN blob_size,
                    PageKind kind, const char *name,
                    pe_image_t *out) {
    const UINT8 *file = blob;
    const image_dos_header_t    *dos;
    const image_nt_headers32_t  *nt;
    const image_section_header_t *sec;
    const image_optional_header32_t *opt;
    UINTN n_sections, pages, i;
    EFI_PHYSICAL_ADDRESS phys = 0;
    UINT8 *dest;
    EFI_STATUS status;
    INT32 delta;

    if (blob_size < sizeof(image_dos_header_t)) return EFI_INVALID_PARAMETER;
    dos = (const image_dos_header_t *)file;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        BXLOG(L"bad MZ in %a", name);
        return EFI_INVALID_PARAMETER;
    }
    if (dos->e_lfanew + sizeof(*nt) > blob_size) return EFI_INVALID_PARAMETER;
    nt = (const image_nt_headers32_t *)(file + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        BXLOG(L"bad PE in %a", name);
        return EFI_INVALID_PARAMETER;
    }
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        BXLOG(L"not i386 in %a", name);
        return EFI_INVALID_PARAMETER;
    }

    opt = &nt->OptionalHeader;
    n_sections = nt->FileHeader.NumberOfSections;
    sec = (const image_section_header_t *)
          ((const UINT8 *)opt + nt->FileHeader.SizeOfOptionalHeader);

    pages = (opt->SizeOfImage + 0xFFF) >> 12;

    /* Prefer loading at ImageBase_phys = ImageBase & ~KSEG0_BASE. This
     * is REQUIRED for /FIXED images (no .reloc) — ntoskrnl.exe is built
     * without relocations and cannot be rebased. For images with
     * relocations, we still try the preferred address first so no
     * rebasing is needed; fall back to AnyPages if the preferred range
     * is occupied AND there are relocations to fix things up. */
    {
        UINT32 preferred_phys = opt->ImageBase & ~KSEG0_BASE;
        int    has_relocs     = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size != 0;

        status = mmu_alloc_at(pages, kind, preferred_phys, &phys);
        if (EFI_ERROR(status)) {
            if (!has_relocs) {
                BXLOG(L"/FIXED image %a can't be placed at preferred phys 0x%x (status 0x%lx) — fatal",
                      name, preferred_phys, (UINT64)status);
                return status;
            }
            BXLOG(L"preferred phys busy, falling back for %a", name);
            /* Force below 16 MiB: driver/kernel images must live at phys
             * < 16 MiB so their KSEG0 aliases survive a process switch
             * (only PDE[512..515] get copied by MmCreateProcessAddressSpace
             * per NTOS/MM/PROCSUP.C:52,297). */
            status = mmu_alloc_below(pages, kind, 0x01000000, &phys);
            if (EFI_ERROR(status)) return status;
        }
    }

    dest = (UINT8 *)(UINTN)phys;
    memzero(dest, pages << 12);

    /* Copy PE headers so the kernel can introspect if needed. */
    memcpy_u8(dest, file, opt->SizeOfHeaders);

    /* Copy sections. */
    for (i = 0; i < n_sections; i++) {
        UINT32 vsize = sec[i].VirtualSize;
        UINT32 rsize = sec[i].SizeOfRawData;
        UINT32 copy  = rsize < vsize ? rsize : vsize;
        if (sec[i].PointerToRawData && rsize) {
            memcpy_u8(dest + sec[i].VirtualAddress,
                      file + sec[i].PointerToRawData, copy);
        }
        /* Remainder is already zero from memzero above. */
    }

    /* Virtual base = KSEG0|phys — this is where the image "lives" once
     * paging is on. Relocations rebase absolute addresses to this base. */
    out->phys_mapped   = phys;
    out->image_base_va = (UINT32)(KSEG0_BASE | (UINT32)phys);
    out->size_of_image = opt->SizeOfImage;
    out->entry_rva     = opt->AddressOfEntryPoint;
    out->name          = name;

    /* Apply base relocations. Delta = actual - header's ImageBase. */
    delta = (INT32)(out->image_base_va - opt->ImageBase);
    if (delta != 0 && opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
        UINT32 rva  = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
        UINT32 left = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
        const image_base_relocation_t *rb =
            (const image_base_relocation_t *)(dest + rva);
        while (left >= sizeof(*rb) && rb->SizeOfBlock >= sizeof(*rb)) {
            UINT32 n = (rb->SizeOfBlock - sizeof(*rb)) / 2;
            const UINT16 *entries = (const UINT16 *)(rb + 1);
            UINT32 block_va = rb->VirtualAddress;
            UINT32 k;
            for (k = 0; k < n; k++) {
                UINT16 type   = entries[k] >> 12;
                UINT16 offset = entries[k] & 0xFFF;
                UINT32 *patch = (UINT32 *)(dest + block_va + offset);
                switch (type) {
                case IMAGE_REL_BASED_ABSOLUTE: break;   /* pad */
                case IMAGE_REL_BASED_HIGHLOW:  *patch += (UINT32)delta; break;
                default:
                    BXLOG(L"unhandled reloc type in %a", name);
                    break;
                }
            }
            left -= rb->SizeOfBlock;
            rb = (const image_base_relocation_t *)((const UINT8 *)rb + rb->SizeOfBlock);
        }
    }

    BXLOG(L"staged %a at phys 0x%lx (base=0x%x size=0x%x entry=0x%x)",
          name, (UINT64)phys,
          out->image_base_va, opt->SizeOfImage,
          out->image_base_va + out->entry_rva);
    return EFI_SUCCESS;
}

/*----------------------------------------------------------------------------
 * pe_resolve_imports
 *---------------------------------------------------------------------------*/

static const pe_image_t *find_module(const pe_image_t *mods, UINTN n,
                                     const char *name) {
    UINTN i;
    for (i = 0; i < n; i++)
        if (ascii_icmp(mods[i].name, name) == 0) return &mods[i];
    return 0;
}

static UINT32 resolve_export_byname(const pe_image_t *m, const char *name) {
    UINT8 *base = (UINT8 *)(UINTN)m->phys_mapped;
    const image_dos_header_t   *dos = (const image_dos_header_t *)base;
    const image_nt_headers32_t *nt  =
        (const image_nt_headers32_t *)(base + dos->e_lfanew);
    const image_data_directory_t *edir_dd =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const image_export_directory_t *edir;
    const UINT32 *names, *funcs;
    const UINT16 *ordinals;
    UINT32 i;

    if (!edir_dd->VirtualAddress || !edir_dd->Size) return 0;
    edir = (const image_export_directory_t *)(base + edir_dd->VirtualAddress);
    names    = (const UINT32 *)(base + edir->AddressOfNames);
    funcs    = (const UINT32 *)(base + edir->AddressOfFunctions);
    ordinals = (const UINT16 *)(base + edir->AddressOfNameOrdinals);

    for (i = 0; i < edir->NumberOfNames; i++) {
        const char *ename = (const char *)(base + names[i]);
        if (ascii_cmp(ename, name) == 0) {
            UINT32 rva = funcs[ordinals[i]];
            /* Forwarder check: RVA inside export table range → forwarder string. */
            if (rva >= edir_dd->VirtualAddress &&
                rva <  edir_dd->VirtualAddress + edir_dd->Size) {
                BXLOG(L"forwarder not handled for %a", name);
                return 0;
            }
            return m->image_base_va + rva;
        }
    }
    return 0;
}

static UINT32 resolve_export_byord(const pe_image_t *m, UINT32 ordinal) {
    UINT8 *base = (UINT8 *)(UINTN)m->phys_mapped;
    const image_dos_header_t   *dos = (const image_dos_header_t *)base;
    const image_nt_headers32_t *nt  =
        (const image_nt_headers32_t *)(base + dos->e_lfanew);
    const image_data_directory_t *edir_dd =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const image_export_directory_t *edir;
    UINT32 idx;

    if (!edir_dd->VirtualAddress || !edir_dd->Size) return 0;
    edir = (const image_export_directory_t *)(base + edir_dd->VirtualAddress);
    if (ordinal < edir->Base) return 0;
    idx = ordinal - edir->Base;
    if (idx >= edir->NumberOfFunctions) return 0;
    {
        const UINT32 *funcs = (const UINT32 *)(base + edir->AddressOfFunctions);
        UINT32 rva = funcs[idx];
        if (rva >= edir_dd->VirtualAddress &&
            rva <  edir_dd->VirtualAddress + edir_dd->Size) {
            BXLOG(L"forwarder (ord) not handled");
            return 0;
        }
        return m->image_base_va + rva;
    }
}

EFI_STATUS pe_resolve_imports(pe_image_t *img,
                              const pe_image_t *modules, UINTN n_modules) {
    UINT8 *base = (UINT8 *)(UINTN)img->phys_mapped;
    const image_dos_header_t   *dos = (const image_dos_header_t *)base;
    const image_nt_headers32_t *nt  =
        (const image_nt_headers32_t *)(base + dos->e_lfanew);
    const image_data_directory_t *idir_dd =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const image_import_descriptor_t *iid;
    UINTN failed = 0;

    if (!idir_dd->VirtualAddress || !idir_dd->Size) {
        BXLOG(L"%a has no imports", img->name);
        return EFI_SUCCESS;
    }

    for (iid = (const image_import_descriptor_t *)(base + idir_dd->VirtualAddress);
         iid->Name;
         iid++) {
        const char *dll = (const char *)(base + iid->Name);
        const pe_image_t *mod = find_module(modules, n_modules, dll);
        UINT32 *iat;
        const UINT32 *ilt;

        if (!mod) {
            BXLOG(L"%a imports from missing module: %a", img->name, dll);
            failed++;
            continue;
        }
        iat = (UINT32 *)(base + iid->FirstThunk);
        ilt = (const UINT32 *)(base + (iid->OriginalFirstThunk
                                       ? iid->OriginalFirstThunk
                                       : iid->FirstThunk));
        for (; *ilt; ilt++, iat++) {
            UINT32 resolved;
            if (*ilt & IMAGE_ORDINAL_FLAG32) {
                resolved = resolve_export_byord(mod, *ilt & 0xFFFF);
                if (!resolved) {
                    BXLOG(L"unresolved ordinal import %u from %a into %a",
                          (UINT32)(*ilt & 0xFFFF), dll, img->name);
                    failed++;
                    continue;
                }
            } else {
                /* By name: ILT points at IMAGE_IMPORT_BY_NAME { Hint (u16), Name[] } */
                const char *iname = (const char *)(base + *ilt + 2);
                resolved = resolve_export_byname(mod, iname);
                if (!resolved) {
                    BXLOG(L"unresolved %a from %a into %a",
                          iname, dll, img->name);
                    failed++;
                    continue;
                }
            }
            *iat = resolved;
        }
    }

    if (failed) {
        BXLOG(L"%a: %u unresolved imports", img->name, (UINT32)failed);
    }
    return failed ? EFI_NOT_FOUND : EFI_SUCCESS;
}
