/*
 * ESP file reading.
 *
 * Uses EFI_LOADED_IMAGE_PROTOCOL on our own ImageHandle to find the
 * device we were loaded from, then EFI_SIMPLE_FILE_SYSTEM_PROTOCOL to
 * open files by path. Allocates pages via AllocatePages(LoaderData) and
 * owns them past ExitBootServices — the kernel will see these pages
 * through memory descriptors (type LoaderSystemCode / LoaderHalCode /
 * etc. depending on the caller's intent).
 */
#ifndef _BOOT_EFI_FS_H_
#define _BOOT_EFI_FS_H_

#include <efi.h>
#include <efilib.h>
#include "mmu.h"

EFI_STATUS fs_init(EFI_HANDLE ImageHandle);

/*
 * Read `path` into a freshly-AllocatePages'd buffer via mmu_alloc, tagging
 * the allocation with `kind` so memmap.c can emit the correct NT memory
 * type. Path uses backslashes, e.g. L"\\System32\\ntoskrnl.exe".
 * On success: *out_buf points at page-aligned image; *out_size is byte size.
 * Caller does NOT free — the pages become part of the handed-off image set.
 */
EFI_STATUS fs_read(const CHAR16 *path, PageKind kind,
                   void **out_buf, UINTN *out_size);

/*
 * Read `path` into a caller-provided buffer of at least `buf_size` bytes.
 * Does NOT allocate or register anything — the buffer must already exist
 * (e.g. a slab inside a larger contiguous allocation). On success,
 * *out_size is the actual byte count read.
 */
EFI_STATUS fs_read_into(const CHAR16 *path, void *buf, UINTN buf_size,
                        UINTN *out_size);

/* Return the byte size of `path` without reading its contents. */
EFI_STATUS fs_file_size(const CHAR16 *path, UINTN *out_size);

/* Locate the whole-disk (non-partition) BlockIo and return
 *   *out_blocks = Media->LastBlock + 1
 *   *out_block_size = Media->BlockSize
 * Used by loaderblock.c to synthesize CHS for atdisk's INT13 params. */
EFI_STATUS fs_boot_disk_size(UINT64 *out_blocks, UINT32 *out_block_size);

/* Read sector 0 (MBR) from the whole-disk BlockIo into `out`. `out_size`
 * must be >= the disk's block size. Used to compute the ARC disk
 * signature and checksum the kernel matches against. */
EFI_STATUS fs_boot_disk_read_sector0(void *out, UINTN out_size);

/* A single directory entry returned by fs_listdir. */
typedef struct {
    CHAR16 name[64];   /* entry filename, NUL-terminated */
    int    is_dir;     /* nonzero if the entry is a subdirectory */
} fs_dirent;

/*
 * Enumerate the directory `path`, filling out[0..*out_count-1] with up
 * to `max` entries ("." and ".." excluded).  UEFI returns entries in
 * directory order — the caller sorts if order matters.  Used to walk
 * the \Boot\ boot-driver tree on the ESP.
 */
EFI_STATUS fs_listdir(const CHAR16 *path, fs_dirent *out,
                      UINTN max, UINTN *out_count);

#endif
