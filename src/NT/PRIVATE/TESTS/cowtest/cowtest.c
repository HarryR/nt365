/*++

Module Name:

    cowtest.c

Abstract:

    Native NT test program for copy-on-write on image section pages.

    Creates a section from a DLL file on disk, maps it with write-copy
    protection, reads a page, writes to it (should trigger COW), and
    verifies the result. Prints diagnostics via DbgPrint.

    Linked as IMAGE_SUBSYSTEM_NATIVE — runs under the micront profile
    with no Win32 subsystem dependency.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>

void NtProcessStartup(PPEB Peb)
{
    NTSTATUS Status;
    HANDLE FileHandle, SectionHandle;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING FileName;
    IO_STATUS_BLOCK IoStatusBlock;
    PVOID BaseAddress;
    ULONG ViewSize;
    ULONG OldProtect;
    ULONG TestValue;

    DbgPrint("COWTEST: starting\n");

    //
    // Open a DLL file on disk to use as the image section source.
    // We use ntdll.dll since it's always present.
    //

    RtlInitUnicodeString(&FileName,
        L"\\DosDevices\\C:\\System32\\ntdll.dll");

    InitializeObjectAttributes(&ObjectAttributes,
                               &FileName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenFile(&FileHandle,
                        FILE_READ_DATA | FILE_EXECUTE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        0);

    if (!NT_SUCCESS(Status)) {
        DbgPrint("COWTEST: NtOpenFile(ntdll.dll) failed %08lx\n", Status);
        goto Exit;
    }
    DbgPrint("COWTEST: opened ntdll.dll\n");

    //
    // Create an image section from the file.
    //

    Status = NtCreateSection(&SectionHandle,
                             SECTION_ALL_ACCESS,
                             NULL,
                             NULL,
                             PAGE_EXECUTE,
                             SEC_IMAGE,
                             FileHandle);

    NtClose(FileHandle);

    if (!NT_SUCCESS(Status)) {
        DbgPrint("COWTEST: NtCreateSection(SEC_IMAGE) failed %08lx\n", Status);
        goto Exit;
    }
    DbgPrint("COWTEST: created image section\n");

    //
    // Map the section with write-copy protection.
    //

    BaseAddress = NULL;
    ViewSize = 0;

    Status = NtMapViewOfSection(SectionHandle,
                                NtCurrentProcess(),
                                &BaseAddress,
                                0,
                                0,
                                NULL,
                                &ViewSize,
                                ViewShare,
                                0,
                                PAGE_WRITECOPY);

    if (!NT_SUCCESS(Status)) {
        DbgPrint("COWTEST: NtMapViewOfSection(PAGE_WRITECOPY) failed %08lx\n", Status);
        NtClose(SectionHandle);
        goto Exit;
    }
    DbgPrint("COWTEST: mapped at %p size %08lx\n", BaseAddress, ViewSize);

    //
    // Read from the mapped section — should work.
    //

    __try {
        TestValue = *(volatile ULONG *)((PUCHAR)BaseAddress + 0x1000);
        DbgPrint("COWTEST: read OK, value at +0x1000 = %08lx\n", TestValue);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DbgPrint("COWTEST: READ FAILED — exception %08lx\n",
                 GetExceptionCode());
    }

    //
    // Write to the mapped section — should trigger COW.
    //

    DbgPrint("COWTEST: attempting write to +0x1000 (should trigger COW)\n");

    __try {
        *(volatile ULONG *)((PUCHAR)BaseAddress + 0x1000) = 0xDEADBEEF;
        DbgPrint("COWTEST: WRITE SUCCEEDED (COW worked)\n");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DbgPrint("COWTEST: WRITE FAILED — exception %08lx (COW BROKEN)\n",
                 GetExceptionCode());
    }

    //
    // Verify the write stuck.
    //

    __try {
        TestValue = *(volatile ULONG *)((PUCHAR)BaseAddress + 0x1000);
        if (TestValue == 0xDEADBEEF) {
            DbgPrint("COWTEST: verify OK — read back 0xDEADBEEF\n");
        } else {
            DbgPrint("COWTEST: verify MISMATCH — read back %08lx\n", TestValue);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DbgPrint("COWTEST: verify READ FAILED — exception %08lx\n",
                 GetExceptionCode());
    }

    //
    // Also test with NtProtectVirtualMemory to explicitly set write-copy.
    //

    {
        PVOID ProtectAddress = (PVOID)((ULONG)BaseAddress + 0x2000);
        ULONG ProtectSize = 0x1000;

        Status = NtProtectVirtualMemory(NtCurrentProcess(),
                                        &ProtectAddress,
                                        &ProtectSize,
                                        PAGE_WRITECOPY,
                                        &OldProtect);
        DbgPrint("COWTEST: NtProtectVirtualMemory(PAGE_WRITECOPY) = %08lx old=%08lx\n",
                 Status, OldProtect);

        if (NT_SUCCESS(Status)) {
            __try {
                *(volatile ULONG *)((PUCHAR)BaseAddress + 0x2000) = 0xCAFEBABE;
                DbgPrint("COWTEST: explicit COW write SUCCEEDED\n");
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                DbgPrint("COWTEST: explicit COW write FAILED — %08lx\n",
                         GetExceptionCode());
            }
        }
    }

    NtUnmapViewOfSection(NtCurrentProcess(), BaseAddress);
    NtClose(SectionHandle);

    DbgPrint("COWTEST: done — spinning forever\n");

Exit:
    // Don't terminate — smss raises a hard error if InitialCommand exits.
    // Spin so the process stays alive and serial output can flush.
    {
        LARGE_INTEGER delay;
        delay.LowPart = 0;
        delay.HighPart = 0x80000000;  // wait forever
        for (;;) {
            NtDelayExecution(FALSE, &delay);
        }
    }
}
