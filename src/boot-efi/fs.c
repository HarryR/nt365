#include "fs.h"
#include "log.h"

/*
 * ESP file reading.
 *
 *   fs_init:
 *     1. HandleProtocol(ImageHandle, LoadedImage) -> loaded_image
 *     2. HandleProtocol(loaded_image->DeviceHandle, SimpleFileSystem) -> sfs
 *     3. sfs->OpenVolume() -> g_root
 *
 *   fs_read(path):
 *     1. g_root->Open(path, READ)
 *     2. file->GetInfo(FileInfo) -> size
 *     3. BS->AllocatePages(EfiLoaderData, ceil(size/4K)) -> phys
 *     4. file->Read(&size, phys)
 *     5. file->Close()
 *
 * Pages are EfiLoaderData so they survive ExitBootServices. They're
 * preserved via the memory map entry we'll emit in memmap.c.
 */

static EFI_FILE_PROTOCOL *g_root = NULL;

/* gnu-efi ia32 uses native stdcall for protocol fn ptrs — uefi_call_wrapper
 * is a no-op on ia32 but we use it for portability with x86_64 builds. */

static const EFI_GUID g_loaded_image_guid = LOADED_IMAGE_PROTOCOL;
static const EFI_GUID g_sfs_guid          = SIMPLE_FILE_SYSTEM_PROTOCOL;
static const EFI_GUID g_file_info_guid    = EFI_FILE_INFO_ID;
static const EFI_GUID g_block_io_guid     = BLOCK_IO_PROTOCOL;

EFI_STATUS fs_init(EFI_HANDLE ImageHandle) {
    EFI_LOADED_IMAGE_PROTOCOL        *loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *sfs;
    EFI_STATUS status;

    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               ImageHandle,
                               (EFI_GUID *)&g_loaded_image_guid,
                               (void **)&loaded_image);
    if (EFI_ERROR(status)) {
        BXLOG(L"HandleProtocol(LoadedImage) failed: 0x%lx", (UINT64)status);
        return status;
    }

    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               loaded_image->DeviceHandle,
                               (EFI_GUID *)&g_sfs_guid,
                               (void **)&sfs);
    if (EFI_ERROR(status)) {
        BXLOG(L"HandleProtocol(SimpleFileSystem) failed: 0x%lx", (UINT64)status);
        return status;
    }

    status = uefi_call_wrapper(sfs->OpenVolume, 2, sfs, &g_root);
    if (EFI_ERROR(status)) {
        BXLOG(L"OpenVolume failed: 0x%lx", (UINT64)status);
        return status;
    }

    return EFI_SUCCESS;
}

EFI_STATUS fs_read(const CHAR16 *path, PageKind kind,
                   void **out_buf, UINTN *out_size) {
    EFI_FILE_PROTOCOL    *file;
    EFI_FILE_INFO        *info = NULL;
    EFI_PHYSICAL_ADDRESS  phys;
    UINTN                 info_size = 0;
    UINTN                 size, pages;
    EFI_STATUS            status;

    if (!g_root) {
        BXLOG(L"read before init");
        return EFI_NOT_READY;
    }

    status = uefi_call_wrapper(g_root->Open, 5, g_root, &file,
                               (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        BXLOG(L"open %s failed: 0x%lx", path, (UINT64)status);
        return status;
    }

    /* Two-step GetInfo: first with NULL buffer to learn the required size. */
    status = uefi_call_wrapper(file->GetInfo, 4, file,
                               (EFI_GUID *)&g_file_info_guid,
                               &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL) {
        BXLOG(L"GetInfo sizing failed for %s", path);
        goto close_err;
    }
    status = uefi_call_wrapper(BS->AllocatePool, 3,
                               EfiLoaderData, info_size, (void **)&info);
    if (EFI_ERROR(status)) goto close_err;
    status = uefi_call_wrapper(file->GetInfo, 4, file,
                               (EFI_GUID *)&g_file_info_guid,
                               &info_size, info);
    if (EFI_ERROR(status)) {
        BXLOG(L"GetInfo failed for %s", path);
        goto free_info_err;
    }
    size = (UINTN)info->FileSize;

    pages = (size + EFI_PAGE_SIZE - 1) >> EFI_PAGE_SHIFT;
    status = mmu_alloc(pages, kind, &phys);
    if (EFI_ERROR(status)) {
        BXLOG(L"mmu_alloc failed for %s", path);
        goto free_info_err;
    }

    status = uefi_call_wrapper(file->Read, 3, file, &size, (void *)(UINTN)phys);
    if (EFI_ERROR(status)) {
        BXLOG(L"Read failed for %s", path);
        uefi_call_wrapper(BS->FreePages, 2, phys, pages);
        goto free_info_err;
    }

    uefi_call_wrapper(BS->FreePool, 1, info);
    uefi_call_wrapper(file->Close, 1, file);

    BXLOG(L"%s -> 0x%lx (%lu bytes)", path, (UINT64)phys, (UINT64)size);

    *out_buf  = (void *)(UINTN)phys;
    *out_size = size;
    return EFI_SUCCESS;

free_info_err:
    if (info) uefi_call_wrapper(BS->FreePool, 1, info);
close_err:
    uefi_call_wrapper(file->Close, 1, file);
    return status;
}

/* Walk BlockIo handles and return the first whole-disk (non-partition,
 * media-present) one, picking the one with the most blocks. Separated
 * from fs_boot_disk_size so multiple callers can re-locate the disk. */
static EFI_BLOCK_IO_PROTOCOL *find_whole_disk_bio(void) {
    EFI_HANDLE *handles = NULL;
    UINTN       n_handles = 0;
    EFI_BLOCK_IO_PROTOCOL *chosen = NULL;
    UINT64                 chosen_blocks = 0;
    if (uefi_call_wrapper(BS->LocateHandleBuffer, 5,
                          ByProtocol, (EFI_GUID *)&g_block_io_guid,
                          NULL, &n_handles, &handles) != EFI_SUCCESS) return 0;
    for (UINTN i = 0; i < n_handles; i++) {
        EFI_BLOCK_IO_PROTOCOL *bio;
        if (uefi_call_wrapper(BS->HandleProtocol, 3,
                              handles[i], (EFI_GUID *)&g_block_io_guid,
                              (void **)&bio) != EFI_SUCCESS) continue;
        if (!bio->Media) continue;
        if (bio->Media->LogicalPartition) continue;
        if (!bio->Media->MediaPresent) continue;
        UINT64 blocks = bio->Media->LastBlock + 1;
        if (blocks > chosen_blocks) { chosen = bio; chosen_blocks = blocks; }
    }
    uefi_call_wrapper(BS->FreePool, 1, handles);
    return chosen;
}

EFI_STATUS fs_boot_disk_read_sector0(void *out, UINTN out_size) {
    EFI_BLOCK_IO_PROTOCOL *bio = find_whole_disk_bio();
    if (!bio) return EFI_NOT_FOUND;
    if (out_size < bio->Media->BlockSize) return EFI_BUFFER_TOO_SMALL;
    return uefi_call_wrapper(bio->ReadBlocks, 5,
                             bio, bio->Media->MediaId, 0,
                             (UINTN)bio->Media->BlockSize, out);
}

EFI_STATUS fs_boot_disk_size(UINT64 *out_blocks, UINT32 *out_block_size) {
    EFI_BLOCK_IO_PROTOCOL *chosen = find_whole_disk_bio();
    if (!chosen) {
        BXLOG(L"no whole-disk BlockIo found");
        return EFI_NOT_FOUND;
    }

    *out_blocks     = chosen->Media->LastBlock + 1;
    *out_block_size = chosen->Media->BlockSize;

    {
        /* Total in MiB. Force 64-bit multiply + shift so we don't wrap on
         * ia32 when a large block count meets 512 B sectors. */
        UINT64 total = (UINT64)*out_blocks * (UINT64)*out_block_size;
        BXLOG(L"boot disk: %lu x %u bytes = %lu MiB",
              (UINT64)*out_blocks, *out_block_size, total >> 20);
    }
    return EFI_SUCCESS;
}

EFI_STATUS fs_file_size(const CHAR16 *path, UINTN *out_size) {
    EFI_FILE_PROTOCOL *file;
    EFI_FILE_INFO     *info = NULL;
    UINTN              info_size = 0;
    EFI_STATUS         status;

    if (!g_root) return EFI_NOT_READY;

    status = uefi_call_wrapper(g_root->Open, 5, g_root, &file,
                               (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(file->GetInfo, 4, file,
                               (EFI_GUID *)&g_file_info_guid,
                               &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL) goto done;

    status = uefi_call_wrapper(BS->AllocatePool, 3,
                               EfiLoaderData, info_size, (void **)&info);
    if (EFI_ERROR(status)) goto done;

    status = uefi_call_wrapper(file->GetInfo, 4, file,
                               (EFI_GUID *)&g_file_info_guid,
                               &info_size, info);
    if (!EFI_ERROR(status)) *out_size = (UINTN)info->FileSize;

    uefi_call_wrapper(BS->FreePool, 1, info);
done:
    uefi_call_wrapper(file->Close, 1, file);
    return status;
}

EFI_STATUS fs_read_into(const CHAR16 *path, void *buf, UINTN buf_size,
                        UINTN *out_size) {
    EFI_FILE_PROTOCOL *file;
    EFI_FILE_INFO     *info = NULL;
    UINTN              info_size = 0;
    UINTN              size;
    EFI_STATUS         status;

    if (!g_root) return EFI_NOT_READY;

    status = uefi_call_wrapper(g_root->Open, 5, g_root, &file,
                               (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        BXLOG(L"open %s failed: 0x%lx", path, (UINT64)status);
        return status;
    }

    status = uefi_call_wrapper(file->GetInfo, 4, file,
                               (EFI_GUID *)&g_file_info_guid,
                               &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL) goto close_err2;

    status = uefi_call_wrapper(BS->AllocatePool, 3,
                               EfiLoaderData, info_size, (void **)&info);
    if (EFI_ERROR(status)) goto close_err2;

    status = uefi_call_wrapper(file->GetInfo, 4, file,
                               (EFI_GUID *)&g_file_info_guid,
                               &info_size, info);
    if (EFI_ERROR(status)) goto free_info2;

    size = (UINTN)info->FileSize;
    if (size > buf_size) {
        BXLOG(L"%s buffer too small", path);
        status = EFI_BUFFER_TOO_SMALL;
        goto free_info2;
    }

    status = uefi_call_wrapper(file->Read, 3, file, &size, buf);
    if (EFI_ERROR(status)) {
        BXLOG(L"read %s failed: 0x%lx", path, (UINT64)status);
        goto free_info2;
    }

    *out_size = size;
    BXLOG(L"%s -> %lu bytes", path, (UINT64)size);

free_info2:
    uefi_call_wrapper(BS->FreePool, 1, info);
close_err2:
    uefi_call_wrapper(file->Close, 1, file);
    return status;
}
