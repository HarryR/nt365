/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

    pathmisc.c

Abstract:

    Win32 misceleneous path functions

Author:

    Mark Lucovsky (markl) 16-Oct-1990

Revision History:

--*/

#include "basedll.h"

BOOL
IsThisARootDirectory(
    HANDLE RootHandle,
    PUNICODE_STRING FileName OPTIONAL
    )
{
    PFILE_NAME_INFORMATION FileNameInfo;
    WCHAR Buffer[MAX_PATH+sizeof(FileNameInfo)];
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;
    BOOL rv;

    OBJECT_ATTRIBUTES Attributes;
    HANDLE LinkHandle;
    WCHAR LinkValueBuffer[2*MAX_PATH];
    UNICODE_STRING LinkValue;
    ULONG ReturnedLength;

    rv = FALSE;

    FileNameInfo = (PFILE_NAME_INFORMATION)Buffer;
    Status = NtQueryInformationFile(
                RootHandle,
                &IoStatusBlock,
                FileNameInfo,
                sizeof(Buffer),
                FileNameInformation
                );
    if ( NT_SUCCESS(Status) ) {
            if ( FileNameInfo->FileName[(FileNameInfo->FileNameLength>>1)-1] == (WCHAR)'\\' ) {
            rv = TRUE;
            }
        }

    if ( !rv ) {

        //
        // See if this is a dos substed drive (or) redirected net drive
        //

        if ( ARGUMENT_PRESENT(FileName) ) {

            FileName->Length = FileName->Length - sizeof((WCHAR)'\\');

            InitializeObjectAttributes( &Attributes,
                                        FileName,
                                        OBJ_CASE_INSENSITIVE,
                                        NULL,
                                        NULL
                                      );
            Status = NtOpenSymbolicLinkObject( &LinkHandle,
                                               SYMBOLIC_LINK_QUERY,
                                               &Attributes
                                             );
            FileName->Length = FileName->Length + sizeof((WCHAR)'\\');
            if ( NT_SUCCESS(Status) ) {

                //
                // Now query the link and see if there is a redirection
                //

                LinkValue.Buffer = LinkValueBuffer;
                LinkValue.Length = 0;
                LinkValue.MaximumLength = (USHORT)(sizeof(LinkValueBuffer));
                ReturnedLength = 0;
                Status = NtQuerySymbolicLinkObject( LinkHandle,
                                                    &LinkValue,
                                                    &ReturnedLength
                                                  );
                NtClose( LinkHandle );

                if ( NT_SUCCESS(Status) ) {
                    rv = TRUE;
                    }
                }

            }
        }
    return rv;
}


UINT
APIENTRY
GetSystemDirectoryA(
    LPSTR lpBuffer,
    UINT uSize
    )

/*++

Routine Description:

    ANSI thunk to GetSystemDirectoryW

--*/

{
    ANSI_STRING AnsiString;
    NTSTATUS Status;

    if ( uSize < BaseWindowsSystemDirectory.MaximumLength>>1 ) {
        return BaseWindowsSystemDirectory.MaximumLength>>1;
        }
    AnsiString.MaximumLength = (USHORT)(uSize);
    AnsiString.Buffer = lpBuffer;

    Status = BasepUnicodeStringTo8BitString(
                &AnsiString,
                &BaseWindowsSystemDirectory,
                FALSE
                );
    if ( !NT_SUCCESS(Status) ) {
        return 0;
        }
    return AnsiString.Length;
}

UINT
APIENTRY
GetSystemDirectoryW(
    LPWSTR lpBuffer,
    UINT uSize
    )

/*++

Routine Description:

    This function obtains the pathname of the Windows system
    subdirectory.  The system subdirectory contains such files as
    Windows libraries, drivers, and font files.

    The pathname retrieved by this function does not end with a
    backslash unless the system directory is the root directory.  For
    example, if the system directory is named WINDOWS\SYSTEM on drive
    C:, the pathname of the system subdirectory retrieved by this
    function is C:\WINDOWS\SYSTEM.

Arguments:

    lpBuffer - Points to the buffer that is to receive the
        null-terminated character string containing the pathname.

    uSize - Specifies the maximum size (in bytes) of the buffer.  This
        value should be set to at least MAX_PATH to allow sufficient room in
        the buffer for the pathname.

Return Value:

    The return value is the length of the string copied to lpBuffer, not
    including the terminating null character.  If the return value is
    greater than uSize, the return value is the size of the buffer
    required to hold the pathname.  The return value is zero if the
    function failed.

--*/

{
    if ( uSize*2 < BaseWindowsSystemDirectory.MaximumLength ) {
        return BaseWindowsSystemDirectory.MaximumLength/2;
        }
    RtlMoveMemory(
        lpBuffer,
        BaseWindowsSystemDirectory.Buffer,
        BaseWindowsSystemDirectory.Length
        );
    lpBuffer[(BaseWindowsSystemDirectory.Length>>1)] = UNICODE_NULL;
    return BaseWindowsSystemDirectory.Length/2;
}


UINT
APIENTRY
GetWindowsDirectoryA(
    LPSTR lpBuffer,
    UINT uSize
    )

/*++

Routine Description:

    ANSI thunk to GetWindowsDirectoryW

--*/

{
    ANSI_STRING AnsiString;
    NTSTATUS Status;

    if ( uSize < BaseWindowsDirectory.MaximumLength>>1 ) {
        return BaseWindowsDirectory.MaximumLength>>1;
        }
    AnsiString.MaximumLength = (USHORT)(uSize);
    AnsiString.Buffer = lpBuffer;

    Status = BasepUnicodeStringTo8BitString(
                &AnsiString,
                &BaseWindowsDirectory,
                FALSE
                );
    if ( !NT_SUCCESS(Status) ) {
        return 0;
        }
    return AnsiString.Length;
}

UINT
APIENTRY
GetWindowsDirectoryW(
    LPWSTR lpBuffer,
    UINT uSize
    )

/*++

Routine Description:

    This function obtains the pathname of the Windows directory.  The
    Windows directory contains such files as Windows applications,
    initialization files, and help files.

    The pathname retrieved by this function does not end with a
    backslash unless the Windows directory is the root directory.  For
    example, if the Windows directory is named WINDOWS on drive C:, the
    pathname of the Windows directory retrieved by this function is
    C:\WINDOWS If Windows was installed in the root directory of drive
    C:, the pathname retrieved by this function is C:\

Arguments:

    lpBuffer - Points to the buffer that is to receive the
        null-terminated character string containing the pathname.

    uSize - Specifies the maximum size (in bytes) of the buffer.  This
        value should be set to at least MAX_PATH to allow sufficient room in
        the buffer for the pathname.

Return Value:

    The return value is the length of the string copied to lpBuffer, not
    including the terminating null character.  If the return value is
    greater than uSize, the return value is the size of the buffer
    required to hold the pathname.  The return value is zero if the
    function failed.

--*/

{
    if ( uSize*2 < BaseWindowsDirectory.MaximumLength ) {
        return BaseWindowsDirectory.MaximumLength/2;
        }
    RtlMoveMemory(
        lpBuffer,
        BaseWindowsDirectory.Buffer,
        BaseWindowsDirectory.Length
        );
    lpBuffer[(BaseWindowsDirectory.Length>>1)] = UNICODE_NULL;
    return BaseWindowsDirectory.Length/2;
}


UINT
APIENTRY
GetSystemWindowsDirectoryW(
    LPWSTR lpBuffer,
    UINT uSize
    )

/*++

Routine Description:

    This function obtains the pathname of the system Windows directory.

    On a Terminal Server this is the shared Windows directory rather than
    the per-user one; MicroNT has no per-user redirection, so it is the
    same directory as GetWindowsDirectoryW.

Arguments:

    lpBuffer - Points to the buffer that is to receive the
        null-terminated character string containing the pathname.

    uSize - Specifies the maximum size (in wchars) of the buffer.  This
        value should be set to at least MAX_PATH to allow sufficient room in
        the buffer for the pathname.

Return Value:

    The return value is the length of the string copied to lpBuffer, not
    including the terminating null character.  If the return value is
    greater than uSize, the return value is the size of the buffer
    required to hold the pathname.  The return value is zero if the
    function failed.

--*/

{

    if ( uSize*2 < BaseWindowsDirectory.MaximumLength ) {
        return BaseWindowsDirectory.MaximumLength/2;
    }
    RtlCopyMemory(
        lpBuffer,
        BaseWindowsDirectory.Buffer,
        BaseWindowsDirectory.Length
        );
    lpBuffer[(BaseWindowsDirectory.Length>>1)] = UNICODE_NULL;
    return BaseWindowsDirectory.Length/2;
}



UINT
APIENTRY
GetDriveTypeA(
    LPCSTR lpRootPathName
    )

/*++

Routine Description:

    ANSI thunk to GetDriveTypeW

--*/

{
    PUNICODE_STRING Unicode;
    ANSI_STRING AnsiString;
    NTSTATUS Status;

    Unicode = &NtCurrentTeb()->StaticUnicodeString;
    RtlInitAnsiString(
        &AnsiString,
        ARGUMENT_PRESENT(lpRootPathName) ? lpRootPathName : "\\"
        );

    Status = Basep8BitStringToUnicodeString(Unicode,&AnsiString,FALSE);
    if ( !NT_SUCCESS(Status) ) {
        if ( Status == STATUS_BUFFER_OVERFLOW ) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            }
        else {
            BaseSetLastNTError(Status);
            }
        return 1;
        }
    return (GetDriveTypeW((LPCWSTR)Unicode->Buffer));
}

UINT
APIENTRY
GetDriveTypeW(
    LPCWSTR lpRootPathName
    )

/*++

Routine Description:

    This function determines whether a disk drive is removeable, fixed,
    remote, CD ROM, or a RAM disk.

    The return value is zero if the function cannot determine the drive
    type, or 1 if the specified root directory does not exist.

Arguments:

    lpRootPathName - An optional parameter, that if specified, supplies
        the root directory of the disk whose drive type is to be
        determined.  If this parameter is not specified, then the root
        of the current directory is used.

Return Value:

    The return value specifies the type of drive.  It can be one of the
    following values:

    DRIVE_UNKNOWN - The drive type can not be determined.

    DRIVE_NO_ROOT_DIR - The root directory does not exist.

    DRIVE_REMOVABLE - Disk can be removed from the drive.

    DRIVE_FIXED - Disk cannot be removed from the drive.

    DRIVE_REMOTE - Drive is a remote (network) drive.

    DRIVE_CDROM - Drive is a CD rom drive.

    DRIVE_RAMDISK - Drive is a RAM disk.

--*/

{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES Obja;
    HANDLE Handle;
    UNICODE_STRING FileName;
    IO_STATUS_BLOCK IoStatusBlock;
    BOOLEAN TranslationStatus;
    PVOID FreeBuffer;
    DWORD ReturnValue;
    FILE_FS_DEVICE_INFORMATION DeviceInfo;
    WCHAR DefaultPath[2];
    LPCWSTR RootPathName;
    WCHAR wch;
    ULONG StaticDriveBit;

    if ( (DWORD)lpRootPathName == 0xffffffff ) {
        BaseStaticFixedDriveMask = 0;
        BaseStaticFloppyDriveMask = 0;
        return DRIVE_UNKNOWN;
        }

    DefaultPath[0] = (WCHAR)'\\';
    DefaultPath[1] = UNICODE_NULL;

    RootPathName = ARGUMENT_PRESENT(lpRootPathName) ? lpRootPathName : DefaultPath;

    //
    // Fast path, check for fixed drives that might be cached from
    // a previous call.
    //
    StaticDriveBit = 0;
    wch = RtlUpcaseUnicodeChar(*RootPathName);
    if ( wch >= (WCHAR)'A' && wch <= (WCHAR)'Z' ) {
        if ( RootPathName[1]==(WCHAR)':' &&
             RootPathName[2]==(WCHAR)'\\' &&
             RootPathName[3]==UNICODE_NULL ) {
            StaticDriveBit = 1 << wch - (WCHAR)'A';
            if ( BaseStaticFixedDriveMask & StaticDriveBit ) {
                return DRIVE_FIXED;
                }
            if ( BaseStaticFloppyDriveMask & StaticDriveBit ) {
                return DRIVE_REMOVABLE;
                }
            }
        }

    TranslationStatus = RtlDosPathNameToNtPathName_U(
                            RootPathName,
                            &FileName,
                            NULL,
                            NULL
                            );

    if ( !TranslationStatus ) {
        return DRIVE_NO_ROOT_DIR;
        }

    FreeBuffer = FileName.Buffer;

    //
    // Check to make sure a root was specified
    //

    if ( FileName.Buffer[(FileName.Length >> 1)-1] != '\\' ) {
        RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);
        return DRIVE_NO_ROOT_DIR;
        }

    FileName.Length -= 2;

    InitializeObjectAttributes(
        &Obja,
        &FileName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
        );

    //
    // Open the file
    //

    Status = NtOpenFile(
                &Handle,
                (ACCESS_MASK)FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                &Obja,
                &IoStatusBlock,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE
                );

    //
    //
    // substd drives are really directories, so if we are dealing with one
    // of them, bypass this
    //

    if ( Status == STATUS_FILE_IS_A_DIRECTORY ) {
        Status = NtOpenFile(
                    &Handle,
                    (ACCESS_MASK)FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                    &Obja,
                    &IoStatusBlock,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    FILE_SYNCHRONOUS_IO_NONALERT
                    );
        StaticDriveBit = 0;
        }
    else {

        //
        // check for substed drives another way just in case
        //

        FileName.Length = FileName.Length + sizeof((WCHAR)'\\');
        if (!IsThisARootDirectory(NULL,&FileName) ) {
            FileName.Length = FileName.Length - sizeof((WCHAR)'\\');
            if (NT_SUCCESS(Status)) {
                NtClose(Handle);
                }
            Status = NtOpenFile(
                        &Handle,
                        (ACCESS_MASK)FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &Obja,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT
                        );
            StaticDriveBit = 0;
            }
        }
    RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);
    if ( !NT_SUCCESS(Status) ) {
        return DRIVE_NO_ROOT_DIR;
        }

    //
    // Determine if this is a network or disk file system. If it
    // is a disk file system determine if this is removable or not
    //

    Status = NtQueryVolumeInformationFile(
                Handle,
                &IoStatusBlock,
                &DeviceInfo,
                sizeof(DeviceInfo),
                FileFsDeviceInformation
                );
    if ( !NT_SUCCESS(Status) ) {
        ReturnValue = DRIVE_UNKNOWN;
        }
    else if ( DeviceInfo.Characteristics & FILE_REMOTE_DEVICE ) {
        ReturnValue = DRIVE_REMOTE;
        }
    else {
        switch ( DeviceInfo.DeviceType ) {

            case FILE_DEVICE_NETWORK:
            case FILE_DEVICE_NETWORK_FILE_SYSTEM:
                ReturnValue = DRIVE_REMOTE;
                break;

            case FILE_DEVICE_CD_ROM:
            case FILE_DEVICE_CD_ROM_FILE_SYSTEM:
                ReturnValue = DRIVE_CDROM;
                break;

            case FILE_DEVICE_VIRTUAL_DISK:
                ReturnValue = DRIVE_RAMDISK;
                break;

            case FILE_DEVICE_DISK:
            case FILE_DEVICE_DISK_FILE_SYSTEM:

                if ( DeviceInfo.Characteristics & FILE_REMOVABLE_MEDIA ) {
                    if ( StaticDriveBit ) {
                        if ( !(DeviceInfo.Characteristics & FILE_VIRTUAL_VOLUME) ) {
                            BaseStaticFloppyDriveMask |= StaticDriveBit;
                            }
                        }
                    ReturnValue = DRIVE_REMOVABLE;
                    }
                else {
                    if ( StaticDriveBit ) {
                        if ( !(DeviceInfo.Characteristics & FILE_VIRTUAL_VOLUME) ) {
                            BaseStaticFixedDriveMask |= StaticDriveBit;
                            }
                        }
                    ReturnValue = DRIVE_FIXED;
                    }
                break;

            default:
                ReturnValue = DRIVE_UNKNOWN;
                break;
            }
        }
    NtClose(Handle);
    return ReturnValue;
}

DWORD
APIENTRY
SearchPathA(
    LPCSTR lpPath,
    LPCSTR lpFileName,
    LPCSTR lpExtension,
    DWORD nBufferLength,
    LPSTR lpBuffer,
    LPSTR *lpFilePart
    )

/*++

Routine Description:

    ANSI thunk to SearchPathW

--*/

{

    UNICODE_STRING xlpPath;
    PUNICODE_STRING Unicode;
    UNICODE_STRING xlpExtension;
    PWSTR xlpBuffer;
    DWORD ReturnValue;
    ANSI_STRING AnsiString;
    UNICODE_STRING UnicodeString;
    NTSTATUS Status;
    PWSTR FilePart;
    PWSTR *FilePartPtr;

    if ( ARGUMENT_PRESENT(lpFilePart) ) {
        FilePartPtr = &FilePart;
        }
    else {
        FilePartPtr = NULL;
        }

    Unicode = &NtCurrentTeb()->StaticUnicodeString;
    RtlInitAnsiString(&AnsiString,lpFileName);

    Status = Basep8BitStringToUnicodeString(Unicode,&AnsiString,FALSE);
    if ( !NT_SUCCESS(Status) ) {
        if ( Status == STATUS_BUFFER_OVERFLOW ) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            }
        else {
            BaseSetLastNTError(Status);
            }
        return 0;
        }

    if ( ARGUMENT_PRESENT(lpExtension) ) {
        RtlInitAnsiString(&AnsiString,lpExtension);
        Status = Basep8BitStringToUnicodeString(&xlpExtension,&AnsiString,TRUE);
        if ( !NT_SUCCESS(Status) ) {
            BaseSetLastNTError(Status);
            return 0;
            }
        }
    else {
        xlpExtension.Buffer = NULL;
        }

    if ( ARGUMENT_PRESENT(lpPath) ) {
        RtlInitAnsiString(&AnsiString,lpPath);
        Status = Basep8BitStringToUnicodeString(&xlpPath,&AnsiString,TRUE);
        if ( !NT_SUCCESS(Status) ) {
            if ( ARGUMENT_PRESENT(lpExtension) ) {
                RtlFreeUnicodeString(&xlpExtension);
                }
            BaseSetLastNTError(Status);
            return 0;
            }
        }
    else {
        xlpPath.Buffer = NULL;
        }

    xlpBuffer = RtlAllocateHeap(RtlProcessHeap(), 0,nBufferLength<<1);
    if ( !xlpBuffer ) {
        BaseSetLastNTError(STATUS_NO_MEMORY);
        goto bail0;
        }
    ReturnValue = SearchPathW(
                    xlpPath.Buffer,
                    Unicode->Buffer,
                    xlpExtension.Buffer,
                    nBufferLength,
                    xlpBuffer,
                    FilePartPtr
                    );
    if ( ReturnValue && ReturnValue <= nBufferLength ) {
        RtlInitUnicodeString(&UnicodeString,xlpBuffer);
        AnsiString.MaximumLength = (USHORT)(nBufferLength+1);
        AnsiString.Buffer = lpBuffer;
        Status = BasepUnicodeStringTo8BitString(&AnsiString,&UnicodeString,FALSE);
        if ( !NT_SUCCESS(Status) ) {
            BaseSetLastNTError(Status);
            ReturnValue = 0;
            }
        else {
            if ( ARGUMENT_PRESENT(lpFilePart) ) {
                if ( FilePart == NULL ) {
                    *lpFilePart = NULL;
                    }
                else {
                    *lpFilePart = (LPSTR)(FilePart - xlpBuffer);
                    *lpFilePart = *lpFilePart + (DWORD)lpBuffer;
                    }
                }
            }
        }

    RtlFreeHeap(RtlProcessHeap(), 0,xlpBuffer);
bail0:
    if ( ARGUMENT_PRESENT(lpExtension) ) {
        RtlFreeUnicodeString(&xlpExtension);
        }

    if ( ARGUMENT_PRESENT(lpPath) ) {
        RtlFreeUnicodeString(&xlpPath);
        }
    return ReturnValue;
}

DWORD
APIENTRY
SearchPathW(
    LPCWSTR lpPath,
    LPCWSTR lpFileName,
    LPCWSTR lpExtension,
    DWORD nBufferLength,
    LPWSTR lpBuffer,
    LPWSTR *lpFilePart
    )

/*++

Routine Description:

    This function is used to search for a file specifying a search path
    and a filename.  It returns with a fully qualified pathname of the
    found file.

    This function is used to locate a file using the specified path.  If
    the file is found, its fully qualified pathname is returned.  In
    addition to this, it calculates the address of the file name portion
    of the fully qualified pathname.

Arguments:

    lpPath - An optional parameter, that if specified, supplies the
        search path to be used when locating the file.  If this
        parameter is not specified, the default windows search path is
        used.  The default path is:

          - The current directory

          - The windows directory

          - The windows system directory

          - The directories listed in the path environment variable

    lpFileName - Supplies the file name of the file to search for.

    lpExtension - An optional parameter, that if specified, supplies an
        extension to be added to the filename when doing the search.
        The extension is only added if the specified filename does not
        end with an extension.

    nBufferLength - Supplies the length in characters of the buffer that
        is to receive the fully qualified path.

    lpBuffer - Returns the fully qualified pathname corresponding to the
        file that was found.

    lpFilePart - Returns the address of the last component of the fully
        qualified pathname.

Return Value:

    The return value is the length of the string copied to lpBuffer, not
    including the terminating null character.  If the return value is
    greater than nBufferLength, the return value is the size of the buffer
    required to hold the pathname.  The return value is zero if the
    function failed.


--*/

{

    LPWSTR ComputedFileName;
    ULONG ExtensionLength;
    ULONG PathLength;
    ULONG FileLength;
    LPCWSTR p;
    LPWSTR p1;
    LPWSTR AllocatedPath;
    ULONG ComputedLength;
    UNICODE_STRING Scratch;
    UNICODE_STRING AllocatedPathString;
    NTSTATUS Status;
    RTL_PATH_TYPE PathType;
    WCHAR wch;

    //
    // if the file name is not a relative name, then
    // check to see if the file exists.
    //

    nBufferLength *= 2;
    PathType = RtlDetermineDosPathNameType_U(lpFileName);
    if ( PathType == RtlPathTypeRelative ) {

        //
        // for .\ and ..\ names don't search path
        //

        if ( lpFileName[0] == (WCHAR)'.') {
            if ( lpFileName[1] == (WCHAR)'\\' || lpFileName[1] == (WCHAR)'/') {
                PathType = RtlPathTypeUnknown;
                }
            else {
                if ( lpFileName[1] == (WCHAR)'.'  &&
                    (lpFileName[2] == (WCHAR)'\\' || lpFileName[2] == (WCHAR)'/') ) {
                    PathType = RtlPathTypeUnknown;
                    }
                }
            }
        }

    if ( PathType != RtlPathTypeRelative ) {
        if (RtlDoesFileExists_U(lpFileName) ) {
            ExtensionLength = RtlGetFullPathName_U(
                                lpFileName,
                                nBufferLength,
                                lpBuffer,
                                lpFilePart
                                );
            return ExtensionLength/2;
            }
        else {

            //
            // if the name does not exist, then see if adding the extension
            // helps any
            //

            if ( ARGUMENT_PRESENT(lpExtension) ) {
                RtlInitUnicodeString(&Scratch,lpExtension);
                ExtensionLength = Scratch.Length;
                RtlInitUnicodeString(&Scratch,lpFileName);
                AllocatedPathString.Length = (USHORT)ExtensionLength + Scratch.Length;
                AllocatedPathString.MaximumLength = AllocatedPathString.Length + (USHORT)sizeof(UNICODE_NULL);
                AllocatedPathString.Buffer = RtlAllocateHeap(
                                                RtlProcessHeap(),
                                                0,
                                                AllocatedPathString.MaximumLength
                                                );
                if ( !AllocatedPathString.Buffer ) {
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return 0;
                    }
                RtlCopyMemory(AllocatedPathString.Buffer,lpFileName,Scratch.Length);
                RtlCopyMemory(AllocatedPathString.Buffer+(Scratch.Length>>1),lpExtension,ExtensionLength+sizeof(UNICODE_NULL));
                if (RtlDoesFileExists_U(AllocatedPathString.Buffer) ) {
                    ExtensionLength = RtlGetFullPathName_U(
                                        AllocatedPathString.Buffer,
                                        nBufferLength,
                                        lpBuffer,
                                        lpFilePart
                                        );
                    ExtensionLength /= 2;
                    }
                else {
                    SetLastError(ERROR_FILE_NOT_FOUND);
                    ExtensionLength = 0;
                    }
                RtlFreeHeap(RtlProcessHeap(), 0, AllocatedPathString.Buffer);
                return ExtensionLength;
                }
            else {
                SetLastError(ERROR_INVALID_PARAMETER);
                return 0;
                }
            }
        }

    //
    // Determine if the file name contains an extension
    //

    ExtensionLength = 1;
    p = lpFileName;
    while (*p) {
        if ( *p == (WCHAR)'.' ) {
            ExtensionLength = 0;
            break;
            }
        p++;
        }

    //
    // If no extension was found, then determine the extension length
    // that should be used to search for the file
    //

    if ( ExtensionLength ) {
        if ( ARGUMENT_PRESENT(lpExtension) ) {
            RtlInitUnicodeString(&Scratch,lpExtension);
            ExtensionLength = Scratch.Length;
            }
        else {
            ExtensionLength = 0;
            }
        }
    else {
        ExtensionLength = 0;
        }

    //
    // If the lpPath parameter is not specified, then use the
    // default windows search.
    //

    if ( !ARGUMENT_PRESENT(lpPath) ) {
        AllocatedPath = BaseComputeProcessDllPath(NULL, GetEnvironmentStringsW());
        RtlInitUnicodeString(&Scratch,AllocatedPath);
        PathLength = Scratch.Length;
        lpPath = AllocatedPath;
        }
    else {
        RtlInitUnicodeString(&Scratch,lpPath);
        PathLength = Scratch.Length;
        AllocatedPath = NULL;
        }

    //
    // Compute the file name length and the path length;
    //

    RtlInitUnicodeString(&Scratch,lpFileName);
    FileLength = Scratch.Length;

    //
    // trim trailing spaces, and then check for a real filelength
    // if length is 0 (NULL, "", or " ") passed in then abort the
    // search
    //

    if ( FileLength ) {
        wch = Scratch.Buffer[(FileLength>>1) - 1];
        while ( FileLength && wch == (WCHAR)' ' ) {
            FileLength -= sizeof(WCHAR);
            wch = Scratch.Buffer[(FileLength>>1) - 1];
            }
        if ( !FileLength ) {
            if ( AllocatedPath ) {
                RtlFreeHeap(RtlProcessHeap(), 0, AllocatedPath);
                }
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
            }
        else {
            FileLength = Scratch.Length;
            }
        }
    else {
        if ( AllocatedPath ) {
            RtlFreeHeap(RtlProcessHeap(), 0, AllocatedPath);
            }
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
        }

    ComputedLength = PathLength + FileLength + ExtensionLength + 3*sizeof(UNICODE_NULL);
    ComputedFileName = RtlAllocateHeap(
                            RtlProcessHeap(), 0,
                            ComputedLength
                            );
    if ( !ComputedFileName ) {
        if ( AllocatedPath ) {
            RtlFreeHeap(RtlProcessHeap(), 0, AllocatedPath);
            }
        BaseSetLastNTError(STATUS_NO_MEMORY);
        return 0;
        }

    //
    // find ; 's in path and copy path component to computed file name
    //

    do {
        p1 = ComputedFileName;
        while (*lpPath) {
            if (*lpPath == (WCHAR)';') {
                lpPath++;
                break;
                }

            *p1++ = *lpPath++;
            }

        if (p1 != ComputedFileName &&
            p1 [ -1 ] != (WCHAR)'\\' ) {
            *p1++ = (WCHAR)'\\';
            }

        if (*lpPath == UNICODE_NULL) {
            lpPath = NULL;
            }
        RtlMoveMemory(p1,lpFileName,FileLength);
        if ( ExtensionLength ) {
            RtlMoveMemory((PUCHAR)p1+FileLength,lpExtension,ExtensionLength+sizeof(UNICODE_NULL));
            }
        else {
            *(LPWSTR)(((PUCHAR)p1+FileLength)) = UNICODE_NULL;
            }
        if (RtlDoesFileExists_U(ComputedFileName) ) {
            ExtensionLength = RtlGetFullPathName_U(
                                ComputedFileName,
                                nBufferLength,
                                lpBuffer,
                                lpFilePart
                                );
            if ( AllocatedPath ) {
                RtlFreeHeap(RtlProcessHeap(), 0, AllocatedPath);
                }
            RtlFreeHeap(RtlProcessHeap(), 0, ComputedFileName);
            return ExtensionLength/2;
            }
        }
    while ( lpPath );

    if ( AllocatedPath ) {
        RtlFreeHeap(RtlProcessHeap(), 0, AllocatedPath);
        }
    RtlFreeHeap(RtlProcessHeap(), 0, ComputedFileName);
    SetLastError(ERROR_FILE_NOT_FOUND);
    return 0;
}


DWORD
APIENTRY
GetTempPathA(
    DWORD nBufferLength,
    LPSTR lpBuffer
    )

/*++

Routine Description:

    ANSI thunk to GetTempPathW

--*/

{
    ANSI_STRING AnsiString;
    UNICODE_STRING UnicodeString;
    NTSTATUS Status;

    UnicodeString.MaximumLength = (USHORT)((nBufferLength<<1)+sizeof(UNICODE_NULL));
    UnicodeString.Buffer = RtlAllocateHeap(
                                RtlProcessHeap(), 0,
                                UnicodeString.MaximumLength
                                );
    if ( !UnicodeString.Buffer ) {
        BaseSetLastNTError(STATUS_NO_MEMORY);
        return 0;
        }
    UnicodeString.Length = (USHORT)GetTempPathW(
                                        (DWORD)(UnicodeString.MaximumLength-sizeof(UNICODE_NULL))/2,
                                        UnicodeString.Buffer
                                        )*2;
    if ( UnicodeString.Length > (USHORT)(UnicodeString.MaximumLength-sizeof(UNICODE_NULL)) ) {
        RtlFreeHeap(RtlProcessHeap(), 0,UnicodeString.Buffer);
        return UnicodeString.Length>>1;
        }
    AnsiString.Buffer = lpBuffer;
    AnsiString.MaximumLength = (USHORT)(nBufferLength+1);
    Status = BasepUnicodeStringTo8BitString(&AnsiString,&UnicodeString,FALSE);
    RtlFreeHeap(RtlProcessHeap(), 0,UnicodeString.Buffer);
    if ( !NT_SUCCESS(Status) ) {
        BaseSetLastNTError(Status);
        return 0;
        }
    return AnsiString.Length;
}

DWORD
APIENTRY
GetTempPathW(
    DWORD nBufferLength,
    LPWSTR lpBuffer
    )

/*++

Routine Description:

    This function is used to return the pathname of the directory that
    should be used to create temporary files.

Arguments:

    nBufferLength - Supplies the length in bytes of the buffer that is
        to receive the temporary file path.

    lpBuffer - Returns the pathname of the directory that should be used
        to create temporary files in.

Return Value:

    The return value is the length of the string copied to lpBuffer, not
    including the terminating null character.  If the return value is
    greater than nSize, the return value is the size of the buffer
    required to hold the pathname.  The return value is zero if the
    function failed.

--*/

{

    DWORD Length;
    BOOLEAN AddTrailingSlash;
    PUNICODE_STRING Unicode;
    UNICODE_STRING EnvironmentValue;
    NTSTATUS Status;
    LPWSTR Name;
    ULONG Position;

    nBufferLength *= 2;
    Unicode = &NtCurrentTeb()->StaticUnicodeString;
    EnvironmentValue = *Unicode;

    AddTrailingSlash = FALSE;

    Status = RtlQueryEnvironmentVariable_U(NULL,&BaseTmpVariableName,&EnvironmentValue);
    if ( !NT_SUCCESS(Status) ) {
        Status = RtlQueryEnvironmentVariable_U(NULL,&BaseTempVariableName,&EnvironmentValue);
        }

    if ( NT_SUCCESS(Status) ) {
        Name = EnvironmentValue.Buffer;
        if ( Name[(EnvironmentValue.Length>>1)-1] != (WCHAR)'\\' ) {
            AddTrailingSlash = TRUE;
            }
        }
    else {
        Name = BaseDotVariableName.Buffer;
        AddTrailingSlash = TRUE;
        }

    Length = RtlGetFullPathName_U(
                Name,
                nBufferLength,
                lpBuffer,
                NULL
                );
    Position = Length>>1;

    //
    // Make sure there is room for a trailing back slash
    //

    if ( Length && Length < nBufferLength ) {
        if ( lpBuffer[Position-1] != '\\' ) {
            if ( Length+sizeof((WCHAR)'\\') < nBufferLength ) {
                lpBuffer[Position] = (WCHAR)'\\';
                lpBuffer[Position+1] = UNICODE_NULL;
                return (Length+sizeof((WCHAR)'\\'))/2;
                }
            else {
                return (Length+sizeof((WCHAR)'\\')+sizeof(UNICODE_NULL))/2;
                }
            }
        else {
            return Length/2;
            }
        }
    else {
        if ( AddTrailingSlash ) {
            Length += sizeof((WCHAR)'\\');
            }
        return Length/2;
        }
}

UINT
APIENTRY
GetTempFileNameA(
    LPCSTR lpPathName,
    LPCSTR lpPrefixString,
    UINT uUnique,
    LPSTR lpTempFileName
    )

/*++

Routine Description:

    ANSI thunk to GetTempFileNameW

--*/

{
    PUNICODE_STRING Unicode;
    UNICODE_STRING UnicodePrefix;
    ANSI_STRING AnsiString;
    NTSTATUS Status;
    UINT ReturnValue;
    UNICODE_STRING UnicodeResult;

    Unicode = &NtCurrentTeb()->StaticUnicodeString;

    RtlInitAnsiString(&AnsiString,lpPathName);
    Status = Basep8BitStringToUnicodeString(Unicode,&AnsiString,FALSE);
    if ( !NT_SUCCESS(Status) ) {
        if ( Status == STATUS_BUFFER_OVERFLOW ) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            }
        else {
            BaseSetLastNTError(Status);
            }
        return 0;
        }

    RtlInitAnsiString(&AnsiString,lpPrefixString);
    Status = Basep8BitStringToUnicodeString(&UnicodePrefix,&AnsiString,TRUE);
    if ( !NT_SUCCESS(Status) ) {
        BaseSetLastNTError(Status);
        return 0;
        }
    UnicodeResult.MaximumLength = (USHORT)((MAX_PATH<<1)+sizeof(UNICODE_NULL));
    UnicodeResult.Buffer = RtlAllocateHeap(RtlProcessHeap(), 0,UnicodeResult.MaximumLength);
    if ( !UnicodeResult.Buffer ) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        RtlFreeUnicodeString(&UnicodePrefix);
        return 0;
        }

    ReturnValue = GetTempFileNameW(
                    Unicode->Buffer,
                    UnicodePrefix.Buffer,
                    uUnique,
                    UnicodeResult.Buffer
                    );
    if ( ReturnValue ) {
        RtlInitUnicodeString(&UnicodeResult,UnicodeResult.Buffer);
        AnsiString.Buffer = lpTempFileName;
        AnsiString.MaximumLength = MAX_PATH+1;
        Status = BasepUnicodeStringTo8BitString(&AnsiString,&UnicodeResult,FALSE);
        if ( !NT_SUCCESS(Status) ) {
            BaseSetLastNTError(Status);
            ReturnValue = 0;
            }
        }
    RtlFreeUnicodeString(&UnicodePrefix);
    RtlFreeHeap(RtlProcessHeap(), 0,UnicodeResult.Buffer);

    return ReturnValue;
}

UINT
APIENTRY
GetTempFileNameW(
    LPCWSTR lpPathName,
    LPCWSTR lpPrefixString,
    UINT uUnique,
    LPWSTR lpTempFileName
    )

/*++

Routine Description:

    This function creates a temporary filename of the following form:

        drive:\path\prefixuuuu.tmp

    In this syntax line, drive:\path\ is the path specified by the
    lpPathName parameter; prefix is all the letters (up to the first
    three) of the string pointed to by the lpPrefixString parameter; and
    uuuu is the hexadecimal value of the number specified by the
    uUnique parameter.

    To avoid problems resulting from converting OEM character an string
    to an ANSI string, an application should call the CreateFile
    function to create the temporary file.

    If the uUnique parameter is zero, GetTempFileName attempts to form a
    unique number based on the current system time.  If a file with the
    resulting filename exists, the number is increased by one and the
    test for existence is repeated.  This continues until a unique
    filename is found; GetTempFileName then creates a file by that name
    and closes it.  No attempt is made to create and open the file when
    uUnique is nonzero.

Arguments:

    lpPathName - Specifies the null terminated pathname of the directory
        to create the temporary file within.

    lpPrefixString - Points to a null-terminated character string to be
        used as the temporary filename prefix.  This string must consist
        of characters in the OEM-defined character set.

    uUnique - Specifies an unsigned integer.

    lpTempFileName - Points to the buffer that is to receive the
        temporary filename.  This string consists of characters in the
        OEM-defined character set.  This buffer should be at least MAX_PATH
        characters in length to allow sufficient room for the pathname.

Return Value:

    The return value specifies a unique numeric value used in the
    temporary filename.  If a nonzero value was given for the uUnique
    parameter, the return value specifies this same number.

--*/

{
    BASE_API_MSG m;
    PBASE_GETTEMPFILE_MSG a = (PBASE_GETTEMPFILE_MSG)&m.u.GetTempFile;
    LPWSTR p,savedp;
    ULONG Length;
    HANDLE FileHandle;
    ULONG PassCount;
    DWORD LastError;
    UNICODE_STRING UnicodePath, UnicodePrefix;
    CHAR UniqueAsAnsi[8];
    CHAR *c;
    ULONG i;


    PassCount = 0;
    RtlInitUnicodeString(&UnicodePath,lpPathName);
    Length = UnicodePath.Length;

    RtlMoveMemory(lpTempFileName,lpPathName,UnicodePath.MaximumLength);
    if ( lpTempFileName[(Length>>1)-1] != (WCHAR)'\\' ) {
        lpTempFileName[Length>>1] = (WCHAR)'\\';
        Length += sizeof(UNICODE_NULL);
        }

    lpTempFileName[(Length>>1)-1] = UNICODE_NULL;
    i = GetFileAttributesW(lpTempFileName);
    if ( (i == 0xffffffff) ||
         !(i & FILE_ATTRIBUTE_DIRECTORY) ) {
        SetLastError(ERROR_DIRECTORY);
        return FALSE;
        }
    lpTempFileName[(Length>>1)-1] = (WCHAR)'\\';

    RtlInitUnicodeString(&UnicodePrefix,lpPrefixString);
    if ( UnicodePrefix.Length > (USHORT)6 ) {
        UnicodePrefix.Length = (USHORT)6;
        }
    p = &lpTempFileName[Length>>1];
    Length = UnicodePrefix.Length;
    RtlMoveMemory(p,lpPrefixString,Length);
    p += (Length>>1);
    savedp = p;
    //
    // If uUnique is not specified, then get one
    //

try_again:
    p = savedp;
    if ( !uUnique ) {
        //
        // MicroNT: csrss-free.  Original kernel32 LPC'd basesrv
        // (BasepGetTempFile) for a system-wide monotonically increasing
        // 16-bit counter so concurrent GetTempFileName() callers across
        // processes wouldn't collide on the suffix.  We have one
        // process for now; an in-process atomic counter seeded from
        // GetTickCount gives the same uniqueness property within
        // this kernel32 instance.  When real cross-process uniqueness
        // matters (e.g. parallel CL runs), promote to a section-backed
        // counter under \BaseNamedObjects.
        //
        static volatile LONG BasepTempFileSeq = 0;
        if ( BasepTempFileSeq == 0 ) {
            BasepTempFileSeq = GetTickCount() & 0xFFFF;
            }
        a->uUnique = (UINT)(InterlockedIncrement( &BasepTempFileSeq ) & 0xFFFF);
        }
    else {
        a->uUnique = uUnique;
        }

    //
    // Convert the unique value to a 4 byte character string
    //

    RtlIntegerToChar ((ULONG) a->uUnique,16,5,UniqueAsAnsi);
    c = UniqueAsAnsi;
    for(i=0;i<4;i++){
        *p = RtlAnsiCharToUnicodeChar(&c);
        if ( *p == UNICODE_NULL ) {
            break;
            }
        p++;
        }
    RtlMoveMemory(p,BaseDotTmpSuffixName.Buffer,BaseDotTmpSuffixName.MaximumLength);

    if ( !uUnique ) {
        FileHandle = CreateFileW(
                        lpTempFileName,
                        GENERIC_READ,
                        0,
                        NULL,
                        CREATE_NEW,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL
                        );
        //
        // If the create worked, then we are ok. Just close the file.
        // Otherwise, try again.
        //

        if ( FileHandle != INVALID_HANDLE_VALUE ) {
            NtClose(FileHandle);
            }
        else {
            LastError = GetLastError();
            switch (LastError) {
                case ERROR_WRITE_PROTECT         :
                case ERROR_FILE_NOT_FOUND        :
                case ERROR_BAD_PATHNAME          :
                case ERROR_INVALID_NAME          :
                case ERROR_PATH_NOT_FOUND        :
                case ERROR_ACCESS_DENIED         :
                case ERROR_NETWORK_ACCESS_DENIED :
                case ERROR_DISK_CORRUPT          :
                case ERROR_FILE_CORRUPT          :
                    return 0;
                }

            PassCount++;
            if ( PassCount & 0xffff0000 ) {
                return 0;
                }
            goto try_again;
            }
        }
    return a->uUnique;
}

BOOL
APIENTRY
GetDiskFreeSpaceA(
    LPCSTR lpRootPathName,
    LPDWORD lpSectorsPerCluster,
    LPDWORD lpBytesPerSector,
    LPDWORD lpNumberOfFreeClusters,
    LPDWORD lpTotalNumberOfClusters
    )

/*++

Routine Description:

    ANSI thunk to GetDiskFreeSpaceW

--*/

{
    PUNICODE_STRING Unicode;
    ANSI_STRING AnsiString;
    NTSTATUS Status;

    Unicode = &NtCurrentTeb()->StaticUnicodeString;
    RtlInitAnsiString(
        &AnsiString,
        ARGUMENT_PRESENT(lpRootPathName) ? lpRootPathName : "\\"
        );
    Status = Basep8BitStringToUnicodeString(Unicode,&AnsiString,FALSE);
    if ( !NT_SUCCESS(Status) ) {
        if ( Status == STATUS_BUFFER_OVERFLOW ) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            }
        else {
            BaseSetLastNTError(Status);
            }
        return FALSE;
        }
    return ( GetDiskFreeSpaceW(
                (LPCWSTR)Unicode->Buffer,
                lpSectorsPerCluster,
                lpBytesPerSector,
                lpNumberOfFreeClusters,
                lpTotalNumberOfClusters
                )
            );
}

BOOL
APIENTRY
GetDiskFreeSpaceW(
    LPCWSTR lpRootPathName,
    LPDWORD lpSectorsPerCluster,
    LPDWORD lpBytesPerSector,
    LPDWORD lpNumberOfFreeClusters,
    LPDWORD lpTotalNumberOfClusters
    )

/*++

Routine Description:

    The free space on a disk and the size parameters can be returned
    using GetDiskFreeSpace.

Arguments:

    lpRootPathName - An optional parameter, that if specified, supplies
        the root directory of the disk whose free space is to be
        returned for.  If this parameter is not specified, then the root
        of the current directory is used.

    lpSectorsPerCluster - Returns the number of sectors per cluster
        where a cluster is the allocation granularity on the disk.

    lpBytesPerSector - Returns the number of bytes per sector.

    lpNumberOfFreeClusters - Returns the total number of free clusters
        on the disk.

    lpTotalNumberOfClusters - Returns the total number of clusters on
        the disk.

Return Value:

    TRUE - The operation was successful.

    FALSE/NULL - The operation failed. Extended error status is available
        using GetLastError.

--*/

{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES Obja;
    HANDLE Handle;
    UNICODE_STRING FileName;
    IO_STATUS_BLOCK IoStatusBlock;
    BOOLEAN TranslationStatus;
    PVOID FreeBuffer;
    FILE_FS_SIZE_INFORMATION SizeInfo;
    WCHAR DefaultPath[2];

    DefaultPath[0] = (WCHAR)'\\';
    DefaultPath[1] = UNICODE_NULL;

    TranslationStatus = RtlDosPathNameToNtPathName_U(
                            ARGUMENT_PRESENT(lpRootPathName) ? lpRootPathName : DefaultPath,
                            &FileName,
                            NULL,
                            NULL
                            );

    if ( !TranslationStatus ) {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
        }

    FreeBuffer = FileName.Buffer;

    //
    // Check to make sure a root was specified
    //

    if ( FileName.Buffer[(FileName.Length >> 1)-1] != (WCHAR)'\\' ) {
        RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);
        BaseSetLastNTError(STATUS_OBJECT_NAME_INVALID);
        return FALSE;
        }

    InitializeObjectAttributes(
        &Obja,
        &FileName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
        );

    //
    // Open the file
    //

    Status = NtOpenFile(
                &Handle,
                (ACCESS_MASK)FILE_LIST_DIRECTORY | SYNCHRONIZE,
                &Obja,
                &IoStatusBlock,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                FILE_SYNCHRONOUS_IO_NONALERT | FILE_DIRECTORY_FILE
                );
    if ( !NT_SUCCESS(Status) ) {
        BaseSetLastNTError(Status);
        RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);
        return FALSE;
        }

    if ( !IsThisARootDirectory(Handle,&FileName) ) {
        NtClose(Handle);
        SetLastError(ERROR_DIR_NOT_ROOT);
        RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);
        return FALSE;
        }
    RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);

    //
    // Determine the size parameters of the volume.
    //

    Status = NtQueryVolumeInformationFile(
                Handle,
                &IoStatusBlock,
                &SizeInfo,
                sizeof(SizeInfo),
                FileFsSizeInformation
                );
    NtClose(Handle);
    if ( !NT_SUCCESS(Status) ) {
        BaseSetLastNTError(Status);
        return FALSE;
        }

    //
    // Deal with 64 bit sizes
    //

    if ( SizeInfo.TotalAllocationUnits.HighPart ) {
        SizeInfo.TotalAllocationUnits.LowPart = (ULONG)-1;
        }
    if ( SizeInfo.AvailableAllocationUnits.HighPart ) {
        SizeInfo.AvailableAllocationUnits.LowPart = (ULONG)-1;
        }

    *lpSectorsPerCluster = SizeInfo.SectorsPerAllocationUnit;
    *lpBytesPerSector = SizeInfo.BytesPerSector;
    *lpNumberOfFreeClusters =  SizeInfo.AvailableAllocationUnits.LowPart;
    *lpTotalNumberOfClusters = SizeInfo.TotalAllocationUnits.LowPart;

    return TRUE;
}

BOOL
APIENTRY
GetVolumeInformationA(
    LPCSTR lpRootPathName,
    LPSTR lpVolumeNameBuffer,
    DWORD nVolumeNameSize,
    LPDWORD lpVolumeSerialNumber,
    LPDWORD lpMaximumComponentLength,
    LPDWORD lpFileSystemFlags,
    LPSTR lpFileSystemNameBuffer,
    DWORD nFileSystemNameSize
    )

/*++

Routine Description:

    ANSI thunk to GetVolumeInformationW

--*/

{
    PUNICODE_STRING Unicode;
    ANSI_STRING AnsiString;
    NTSTATUS Status;
    UNICODE_STRING UnicodeVolumeName;
    UNICODE_STRING UnicodeFileSystemName;
    ANSI_STRING AnsiVolumeName;
    ANSI_STRING AnsiFileSystemName;
    BOOL ReturnValue;

    Unicode = &NtCurrentTeb()->StaticUnicodeString;
    RtlInitAnsiString(
        &AnsiString,
        ARGUMENT_PRESENT(lpRootPathName) ? lpRootPathName : "\\"
        );

    Status = Basep8BitStringToUnicodeString(Unicode,&AnsiString,FALSE);
    if ( !NT_SUCCESS(Status) ) {
        if ( Status == STATUS_BUFFER_OVERFLOW ) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            }
        else {
            BaseSetLastNTError(Status);
            }
        return FALSE;
        }

    UnicodeVolumeName.Buffer = NULL;
    UnicodeFileSystemName.Buffer = NULL;
    UnicodeVolumeName.MaximumLength = 0;
    UnicodeFileSystemName.MaximumLength = 0;
    AnsiVolumeName.Buffer = lpVolumeNameBuffer;
    AnsiVolumeName.MaximumLength = (USHORT)(nVolumeNameSize+1);
    AnsiFileSystemName.Buffer = lpFileSystemNameBuffer;
    AnsiFileSystemName.MaximumLength = (USHORT)(nFileSystemNameSize+1);

    try {
        if ( ARGUMENT_PRESENT(lpVolumeNameBuffer) ) {
            UnicodeVolumeName.MaximumLength = AnsiVolumeName.MaximumLength << 1;
            UnicodeVolumeName.Buffer = RtlAllocateHeap(
                                            RtlProcessHeap(), 0,
                                            UnicodeVolumeName.MaximumLength
                                            );

            if ( !UnicodeVolumeName.Buffer ) {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
                }
            }

        if ( ARGUMENT_PRESENT(lpFileSystemNameBuffer) ) {
            UnicodeFileSystemName.MaximumLength = AnsiFileSystemName.MaximumLength << 1;
            UnicodeFileSystemName.Buffer = RtlAllocateHeap(
                                                RtlProcessHeap(), 0,
                                                UnicodeFileSystemName.MaximumLength
                                                );

            if ( !UnicodeFileSystemName.Buffer ) {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
                }
            }

        ReturnValue = GetVolumeInformationW(
                            (LPCWSTR)Unicode->Buffer,
                            UnicodeVolumeName.Buffer,
                            nVolumeNameSize,
                            lpVolumeSerialNumber,
                            lpMaximumComponentLength,
                            lpFileSystemFlags,
                            UnicodeFileSystemName.Buffer,
                            nFileSystemNameSize
                            );

        if ( ReturnValue ) {

            if ( ARGUMENT_PRESENT(lpVolumeNameBuffer) ) {
                RtlInitUnicodeString(
                    &UnicodeVolumeName,
                    UnicodeVolumeName.Buffer
                    );

                Status = BasepUnicodeStringTo8BitString(
                            &AnsiVolumeName,
                            &UnicodeVolumeName,
                            FALSE
                            );

                if ( !NT_SUCCESS(Status) ) {
                    BaseSetLastNTError(Status);
                    return FALSE;
                    }
                }

            if ( ARGUMENT_PRESENT(lpFileSystemNameBuffer) ) {
                RtlInitUnicodeString(
                    &UnicodeFileSystemName,
                    UnicodeFileSystemName.Buffer
                    );

                Status = BasepUnicodeStringTo8BitString(
                            &AnsiFileSystemName,
                            &UnicodeFileSystemName,
                            FALSE
                            );

                if ( !NT_SUCCESS(Status) ) {
                    BaseSetLastNTError(Status);
                    return FALSE;
                    }
                }
            }
        }
    finally {
        if ( UnicodeVolumeName.Buffer ) {
            RtlFreeHeap(RtlProcessHeap(), 0,UnicodeVolumeName.Buffer);
            }
        if ( UnicodeFileSystemName.Buffer ) {
            RtlFreeHeap(RtlProcessHeap(), 0,UnicodeFileSystemName.Buffer);
            }
        }

    return ReturnValue;
}

BOOL
APIENTRY
GetVolumeInformationW(
    LPCWSTR lpRootPathName,
    LPWSTR lpVolumeNameBuffer,
    DWORD nVolumeNameSize,
    LPDWORD lpVolumeSerialNumber,
    LPDWORD lpMaximumComponentLength,
    LPDWORD lpFileSystemFlags,
    LPWSTR lpFileSystemNameBuffer,
    DWORD nFileSystemNameSize
    )

/*++

Routine Description:

    This function returns information about the file system whose root
    directory is specified.

Arguments:

    lpRootPathName - An optional parameter, that if specified, supplies
        the root directory of the file system that information is to be
        returned about.  If this parameter is not specified, then the
        root of the current directory is used.

    lpVolumeNameBuffer - An optional parameter that if specified returns
        the name of the specified volume.

    nVolumeNameSize - Supplies the length of the volume name buffer.
        This parameter is ignored if the volume name buffer is not
        supplied.

    lpVolumeSerialNumber - An optional parameter that if specified
        points to a DWORD.  The DWORD contains the 32-bit of the volume
        serial number.

    lpMaximumComponentLength - An optional parameter that if specified
        returns the maximum length of a filename component supported by
        the specified file system.  A filename component is that portion
        of a filename between pathname seperators.

    lpFileSystemFlags - An optional parameter that if specified returns
        flags associated with the specified file system.

        lpFileSystemFlags Flags:

            FS_CASE_IS_PRESERVED - Indicates that the case of file names
                is preserved when the name is placed on disk.

            FS_CASE_SENSITIVE - Indicates that the file system supports
                case sensitive file name lookup.

            FS_UNICODE_STORED_ON_DISK - Indicates that the file system
                supports unicode in file names as they appear on disk.

    lpFileSystemNameBuffer - An optional parameter that if specified returns
        the name for the specified file system (e.g. FAT, HPFS...).

    nFileSystemNameSize - Supplies the length of the file system name
        buffer.  This parameter is ignored if the file system name
        buffer is not supplied.

Return Value:

    TRUE - The operation was successful.

    FALSE/NULL - The operation failed. Extended error status is available
        using GetLastError.

--*/

{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES Obja;
    HANDLE Handle;
    UNICODE_STRING FileName;
    IO_STATUS_BLOCK IoStatusBlock;
    BOOLEAN TranslationStatus;
    PVOID FreeBuffer;
    PFILE_FS_ATTRIBUTE_INFORMATION AttributeInfo;
    PFILE_FS_VOLUME_INFORMATION VolumeInfo;
    ULONG AttributeInfoLength;
    ULONG VolumeInfoLength;
    WCHAR DefaultPath[2];
    BOOL rv;

    rv = FALSE;
    DefaultPath[0] = (WCHAR)'\\';
    DefaultPath[1] = UNICODE_NULL;

    nVolumeNameSize *= 2;
    nFileSystemNameSize *= 2;

    TranslationStatus = RtlDosPathNameToNtPathName_U(
                            ARGUMENT_PRESENT(lpRootPathName) ? lpRootPathName : DefaultPath,
                            &FileName,
                            NULL,
                            NULL
                            );

    if ( !TranslationStatus ) {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
        }

    FreeBuffer = FileName.Buffer;

    //
    // Check to make sure a root was specified
    //

    if ( FileName.Buffer[(FileName.Length >> 1)-1] != '\\' ) {
        RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);
        BaseSetLastNTError(STATUS_OBJECT_NAME_INVALID);
        return FALSE;
        }

    InitializeObjectAttributes(
        &Obja,
        &FileName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
        );

    AttributeInfo = NULL;
    VolumeInfo = NULL;

    //
    // Open the file
    //

    Status = NtOpenFile(
                &Handle,
                (ACCESS_MASK)FILE_LIST_DIRECTORY | SYNCHRONIZE,
                &Obja,
                &IoStatusBlock,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                FILE_SYNCHRONOUS_IO_NONALERT | FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT
                );
    if ( !NT_SUCCESS(Status) ) {
        BaseSetLastNTError(Status);
        RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);
        return FALSE;
        }

    if ( !IsThisARootDirectory(Handle,&FileName) ) {
        NtClose(Handle);
        SetLastError(ERROR_DIR_NOT_ROOT);
        RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);
        return FALSE;
        }
    RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);

    if ( ARGUMENT_PRESENT(lpVolumeNameBuffer) ||
         ARGUMENT_PRESENT(lpVolumeSerialNumber) ) {
        if ( ARGUMENT_PRESENT(lpVolumeNameBuffer) ) {
            VolumeInfoLength = sizeof(*VolumeInfo)+nVolumeNameSize;
            }
        else {
            VolumeInfoLength = sizeof(*VolumeInfo)+MAX_PATH;
            }
        VolumeInfo = RtlAllocateHeap(RtlProcessHeap(), 0,VolumeInfoLength);
        }

    if ( ARGUMENT_PRESENT(lpFileSystemNameBuffer) ||
         ARGUMENT_PRESENT(lpMaximumComponentLength) ||
         ARGUMENT_PRESENT(lpFileSystemFlags) ) {
        if ( ARGUMENT_PRESENT(lpFileSystemNameBuffer) ) {
            AttributeInfoLength = sizeof(*AttributeInfo) + nFileSystemNameSize;
            }
        else {
            AttributeInfoLength = sizeof(*AttributeInfo) + MAX_PATH;
            }
        AttributeInfo = RtlAllocateHeap(RtlProcessHeap(), 0,AttributeInfoLength);
        }

    try {
        if ( VolumeInfo ) {
            Status = NtQueryVolumeInformationFile(
                        Handle,
                        &IoStatusBlock,
                        VolumeInfo,
                        VolumeInfoLength,
                        FileFsVolumeInformation
                        );
            if ( !NT_SUCCESS(Status) ) {
                BaseSetLastNTError(Status);
                rv = FALSE;
                goto finally_exit;
                }
            }

        if ( AttributeInfo ) {
            Status = NtQueryVolumeInformationFile(
                        Handle,
                        &IoStatusBlock,
                        AttributeInfo,
                        AttributeInfoLength,
                        FileFsAttributeInformation
                        );
            if ( !NT_SUCCESS(Status) ) {
                BaseSetLastNTError(Status);
                rv = FALSE;
                goto finally_exit;
                }
            }
        try {
            if ( VolumeInfo ) {

                //
                // BUGBUG DeleteMe NT is still not unicode, so this
                // code accounts for that
                //
                if ( ARGUMENT_PRESENT(lpVolumeNameBuffer) ) {
                    if ( VolumeInfo->VolumeLabelLength >= nVolumeNameSize ) {
                        SetLastError(ERROR_BAD_LENGTH);
                        rv = FALSE;
                        goto finally_exit;
                        }
                    else {
                        RtlMoveMemory( lpVolumeNameBuffer,
                                       VolumeInfo->VolumeLabel,
                                       VolumeInfo->VolumeLabelLength );

                        *(lpVolumeNameBuffer + (VolumeInfo->VolumeLabelLength >> 1)) = UNICODE_NULL;
                        }
                    }
                }

            if ( ARGUMENT_PRESENT(lpVolumeSerialNumber) ) {
                *lpVolumeSerialNumber = VolumeInfo->VolumeSerialNumber;
                }

            if ( ARGUMENT_PRESENT(lpFileSystemNameBuffer) ) {

                //
                // BUGBUG DeleteMe NT is still not unicode, so this
                // code accounts for that
                //

                if ( AttributeInfo->FileSystemNameLength >= nFileSystemNameSize ) {
                    SetLastError(ERROR_BAD_LENGTH);
                    rv = FALSE;
                    goto finally_exit;
                    }
                else {
                    RtlMoveMemory( lpFileSystemNameBuffer,
                                   AttributeInfo->FileSystemName,
                                   AttributeInfo->FileSystemNameLength );

                    *(lpFileSystemNameBuffer + (AttributeInfo->FileSystemNameLength >> 1)) = UNICODE_NULL;
                    }
                }

            if ( ARGUMENT_PRESENT(lpMaximumComponentLength) ) {
                *lpMaximumComponentLength = AttributeInfo->MaximumComponentNameLength;
                }

            if ( ARGUMENT_PRESENT(lpFileSystemFlags) ) {
                *lpFileSystemFlags = AttributeInfo->FileSystemAttributes;
                }
            }
        except (EXCEPTION_EXECUTE_HANDLER) {
            BaseSetLastNTError(STATUS_ACCESS_VIOLATION);
            return FALSE;
            }
        rv = TRUE;
finally_exit:;
        }
    finally {
        NtClose(Handle);
        if ( VolumeInfo ) {
            RtlFreeHeap(RtlProcessHeap(), 0,VolumeInfo);
            }
        if ( AttributeInfo ) {
            RtlFreeHeap(RtlProcessHeap(), 0,AttributeInfo);
            }
        }
    return rv;
}

DWORD
APIENTRY
GetLogicalDriveStringsA(
    DWORD nBufferLength,
    LPSTR lpBuffer
    )
{
    DWORD DriveType;
    ANSI_STRING RootName;
    int i;
    PUCHAR Dst;
    DWORD BytesLeft;
    DWORD BytesNeeded;
    BOOL WeFailed;

    BytesNeeded = 0;
    BytesLeft = nBufferLength;
    Dst = (PUCHAR)lpBuffer;
    WeFailed = FALSE;

    RtlInitAnsiString(&RootName,"A:\\");
    for ( i=0; i<26; i++ ) {
        RootName.Buffer[0] = (CHAR)((CHAR)i+'A');
        DriveType = GetDriveTypeA(RootName.Buffer);
        if ( DriveType == 0 || DriveType == 1 ) {
            continue;
            }
        BytesNeeded += RootName.MaximumLength;
        if ( BytesNeeded < (USHORT)BytesLeft ) {
            RtlMoveMemory(Dst,RootName.Buffer,RootName.MaximumLength);
            Dst += RootName.MaximumLength;
            *Dst = '\0';
            }
        else {
            WeFailed = TRUE;
            }
        }

    if ( WeFailed ) {
        BytesNeeded++;
        }
    //
    // Need to handle network uses;
    //

    return( BytesNeeded );
}

DWORD
APIENTRY
GetLogicalDriveStringsW(
    DWORD nBufferLength,
    LPWSTR lpBuffer
    )
{
    DWORD DriveType;
    UNICODE_STRING RootName;
    NTSTATUS Status;
    int i;
    PUCHAR Dst;
    DWORD BytesLeft;
    DWORD BytesNeeded;
    BOOL WeFailed;

    nBufferLength = nBufferLength*2;
    BytesNeeded = 0;
    BytesLeft = nBufferLength;
    Dst = (PUCHAR)lpBuffer;
    WeFailed = FALSE;

    RtlInitUnicodeString(&RootName,L"A:\\");

    for ( i=0; i<26; i++ ) {
        RootName.Buffer[0] = (WCHAR)((CHAR)i+'A');
        DriveType = GetDriveTypeW(RootName.Buffer);
        if ( DriveType == 0 || DriveType == 1 ) {
            continue;
            }
        BytesNeeded += RootName.MaximumLength;
        if ( BytesNeeded < (USHORT)BytesLeft ) {
            RtlMoveMemory(Dst,RootName.Buffer,RootName.MaximumLength);
            Dst += RootName.MaximumLength;
            *(PWSTR)Dst = UNICODE_NULL;
            }
        else {
            WeFailed = TRUE;
            }
        }

    if ( WeFailed ) {
        BytesNeeded += 2;
        }

    //
    // Need to handle network uses;
    //

    return( BytesNeeded/2 );
}

BOOL
WINAPI
SetVolumeLabelA(
    LPCSTR lpRootPathName,
    LPCSTR lpVolumeName
    )
{
    PUNICODE_STRING Unicode;
    ANSI_STRING AnsiString;
    NTSTATUS Status;
    UNICODE_STRING UnicodeVolumeName;
    BOOL ReturnValue;

    Unicode = &NtCurrentTeb()->StaticUnicodeString;
    RtlInitAnsiString(
        &AnsiString,
        ARGUMENT_PRESENT(lpRootPathName) ? lpRootPathName : "\\"
        );

    Status = RtlAnsiStringToUnicodeString(Unicode,&AnsiString,FALSE);
    if ( !NT_SUCCESS(Status) ) {
        if ( Status == STATUS_BUFFER_OVERFLOW ) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            }
        else {
            BaseSetLastNTError(Status);
            }
        return FALSE;
        }

    if ( ARGUMENT_PRESENT(lpVolumeName) ) {
        RtlInitAnsiString(&AnsiString,lpVolumeName);
        Status = RtlAnsiStringToUnicodeString(&UnicodeVolumeName,&AnsiString,TRUE);
        if ( !NT_SUCCESS(Status) ) {
            BaseSetLastNTError(Status);
            return FALSE;
            }

        }
    else {
        UnicodeVolumeName.Buffer = NULL;
        }
    ReturnValue = SetVolumeLabelW((LPCWSTR)Unicode->Buffer,(LPCWSTR)UnicodeVolumeName.Buffer);

    if ( UnicodeVolumeName.Buffer ) {
        RtlFreeUnicodeString(&UnicodeVolumeName);
        }

    return ReturnValue;
}

BOOL
WINAPI
SetVolumeLabelW(
    LPCWSTR lpRootPathName,
    LPCWSTR lpVolumeName
    )
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES Obja;
    HANDLE Handle;
    UNICODE_STRING FileName;
    UNICODE_STRING LabelName;
    IO_STATUS_BLOCK IoStatusBlock;
    BOOLEAN TranslationStatus;
    PVOID FreeBuffer;
    PFILE_FS_LABEL_INFORMATION LabelInformation;
    ULONG LabelInfoLength;
    WCHAR DefaultPath[2];
    BOOL rv;

    rv = FALSE;
    DefaultPath[0] = (WCHAR)'\\';
    DefaultPath[1] = UNICODE_NULL;

    if ( ARGUMENT_PRESENT(lpVolumeName) ) {
        RtlInitUnicodeString(&LabelName,lpVolumeName);
        }
    else {
        LabelName.Length = 0;
        LabelName.MaximumLength = 0;
        LabelName.Buffer = NULL;
        }

    TranslationStatus = RtlDosPathNameToNtPathName_U(
                            ARGUMENT_PRESENT(lpRootPathName) ? lpRootPathName : DefaultPath,
                            &FileName,
                            NULL,
                            NULL
                            );

    if ( !TranslationStatus ) {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
        }

    FreeBuffer = FileName.Buffer;

    //
    // Check to make sure a root was specified
    //

    if ( FileName.Buffer[(FileName.Length >> 1)-1] != '\\' ) {
        RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);
        BaseSetLastNTError(STATUS_OBJECT_NAME_INVALID);
        return FALSE;
        }

    InitializeObjectAttributes(
        &Obja,
        &FileName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
        );

    //
    // Open the file
    //

    Status = NtOpenFile(
                &Handle,
                (ACCESS_MASK)FILE_WRITE_DATA | SYNCHRONIZE,
                &Obja,
                &IoStatusBlock,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                FILE_SYNCHRONOUS_IO_NONALERT
                );
    RtlFreeHeap(RtlProcessHeap(), 0,FreeBuffer);
    if ( !NT_SUCCESS(Status) ) {
        BaseSetLastNTError(Status);
        return FALSE;
        }

    if ( !IsThisARootDirectory(Handle,NULL) ) {
        NtClose(Handle);
        SetLastError(ERROR_DIR_NOT_ROOT);
        return FALSE;
        }

    //
    // Set the volume label
    //

    LabelInformation = NULL;

    try {

        rv = TRUE;

        //
        // the label info buffer contains a single wchar that is the basis of
        // the label name. Subtract this out so the info length is the length
        // of the label and the structure (not including the extra wchar)
        //

        if ( LabelName.Length ) {
            LabelInfoLength = sizeof(*LabelInformation) + LabelName.Length - sizeof(WCHAR);
            }
        else {
            LabelInfoLength = sizeof(*LabelInformation);
            }

        LabelInformation = RtlAllocateHeap(RtlProcessHeap(), 0, LabelInfoLength);
        if ( LabelInformation ) {
            RtlCopyMemory(
                LabelInformation->VolumeLabel,
                LabelName.Buffer,
                LabelName.Length
                );
            LabelInformation->VolumeLabelLength = LabelName.Length;
            Status = NtSetVolumeInformationFile(
                        Handle,
                        &IoStatusBlock,
                        (PVOID) LabelInformation,
                        LabelInfoLength,
                        FileFsLabelInformation
                        );
            if ( !NT_SUCCESS(Status) ) {
                rv = FALSE;
                BaseSetLastNTError(Status);
                }
            }
        else {
            rv = FALSE;
            BaseSetLastNTError(STATUS_NO_MEMORY);
            }
        }
    finally {
        NtClose(Handle);
        if ( LabelInformation ) {
            RtlFreeHeap(RtlProcessHeap(), 0,LabelInformation);
            }
        }
    return rv;
}

/*++

  GetLongPathNameW and its path-name helpers, ported verbatim from the
  Server 2003 RTM shell-path code (base/win32/client/vdm.c).  In stock NT
  these lived alongside the (stripped) VDM support; the helpers are pure
  path-string logic with no VDM dependency.  Ordered so each is defined
  before its first use.  Only change from the original: the GetLongPathNameW
  heap allocations drop the VDM debug heap tag.

--*/

/**
    This function determines if the given name is a valid short name.
    This function only does "obvious" testing since there are not precise
    ways to cover all the file systems(each file system has its own
    file name domain(for example, FAT allows all extended chars and space char
    while NTFS **may** not).
    The main purpose is to help the caller decide if a long to short name
    conversion is necessary. When in doubt, this function simply tells the
    caller that the given name is NOT a short name so that caller would
    do whatever it takes to convert the name.
    This function applies strict rules in deciding if the given name
    is a valid short name. For example, a name containing any extended chars
    is treated as invalid; a name with embedded space chars is also treated
    as invalid.
    A name is a valid short name if ALL the following conditions are met:
    (1). total length <= 13.
    (2). 0 < base name length <= 8.
    (3). extention name length <= 3.
    (4). only one '.' is allowed and must not be the first char.
    (5). every char must be legal defined by the IllegalMask array.

    null path, "." and ".." are treated valid.

    Input: LPCWSTR Name -  points to the name to be checked. It does not
                           have to be NULL terminated.

           int Length - Length of the name, not including teminated NULL char.

    output: TRUE - if the given name is a short file name.
            FALSE - if the given name is not a short file name
**/

// bit set -> char is illegal
DWORD   IllegalMask[] =

{
    // code 0x00 - 0x1F --> all illegal
    0xFFFFFFFF,
    // code 0x20 - 0x3f --> 0x20,0x22,0x2A-0x2C,0x2F and 0x3A-0x3F are illegal
    0xFC009C05,
    // code 0x40 - 0x5F --> 0x5B-0x5D are illegal
    0x38000000,
    // code 0x60 - 0x7F --> 0x7C is illegal
    0x10000000
};

BOOL
IsShortName_U(
    LPCWSTR Name,
    int     Length
    )
{
    int Index;
    BOOL ExtensionFound;
    DWORD      dwStatus;
    UNICODE_STRING UnicodeName;
    ANSI_STRING AnsiString;
    UCHAR      AnsiBuffer[MAX_PATH];
    UCHAR      Char;

    ASSERT(Name);

    // total length must less than 13(8.3 = 8 + 1 + 3 = 12)
    if (Length > 12)
        return FALSE;
    //  "" or "." or ".."
    if (!Length)
        return TRUE;
    if (L'.' == *Name)
    {
        // "." or ".."
        if (1 == Length || (2 == Length && L'.' == Name[1]))
            return TRUE;
        else
            // '.' can not be the first char(base name length is 0)
            return FALSE;
    }

    UnicodeName.Buffer = (LPWSTR)Name;
    UnicodeName.Length =
    UnicodeName.MaximumLength = (USHORT)(Length * sizeof(WCHAR));

    AnsiString.Buffer = AnsiBuffer;
    AnsiString.Length = 0;
    AnsiString.MaximumLength = MAX_PATH; // make a dangerous assumption

    dwStatus = BasepUnicodeStringTo8BitString(&AnsiString,
                                              &UnicodeName,
                                              FALSE);
    if (! NT_SUCCESS(dwStatus)) {
         return(FALSE);
    }

    // all trivial cases are tested, now we have to walk through the name
    ExtensionFound = FALSE;
    for (Index = 0; Index < AnsiString.Length; Index++)
    {
        Char = AnsiString.Buffer[Index];

        // Skip over and Dbcs characters
        if (IsDBCSLeadByte(Char)) {
            //
            //  1) if we're looking at base part ( !ExtensionPresent ) and the 8th byte
            //     is in the dbcs leading byte range, it's error ( Index == 7 ). If the
            //     length of base part is more than 8 ( Index > 7 ), it's definitely error.
            //
            //  2) if the last byte ( Index == DbcsName.Length - 1 ) is in the dbcs leading
            //     byte range, it's error
            //
            if ((!ExtensionFound && (Index >= 7)) ||
                (Index == AnsiString.Length - 1)) {
                return FALSE;
            }
            Index += 1;
            continue;
        }

        // make sure the char is legal
        if (Char > 0x7F || IllegalMask[Char / 32] & (1 << (Char % 32)))
            return FALSE;

        if ('.' == Char)
        {
            // (1) can have only one '.'
            // (2) can not have more than 3 chars following.
            if (ExtensionFound || Length - (Index + 1) > 3)
            {
                return FALSE;
            }
            ExtensionFound = TRUE;
        }
        // base length > 8 chars
        if (Index >= 8 && !ExtensionFound)
            return FALSE;
    }
    return TRUE;

}
/**
    This function determines if the given name is a valid long name.
    This function only does "obvious" testing since there are not precise
    ways to cover all the file systems(each file system has its own
    file name domain(for example, FAT allows all extended chars and space char
    while NTFS **may** not)
    This function helps the caller to determine if a short to long name
    conversion is necessary. When in doubt, this function simply tells the
    caller that the given name is NOT a long name so that caller would
    do whatever it takes to convert the name.
    A name is a valid long name if one of the following conditions is met:
    (1). total length >= 13.
    (2). 0 == base name length ||  base name length > 8.
    (3). extention name length > 3.
    (4). '.' is the first char.
    (5). muitlple '.'


    null path, "." and ".." are treat as valid long name.

    Input: LPCWSTR Name -  points to the name to be checked. It does not
                           have to be NULL terminated.

           int Length - Length of the name, not including teminated NULL char.

    output: TRUE - if the given name is a long file name.
            FALSE - if the given name is not a long file name
**/


BOOL
IsLongName_U(
    LPCWSTR Name,
    int Length
    )
{
    int Index;
    BOOL ExtensionFound;
    // (1) NULL path
    // (2) total length > 12
    // (3) . is the first char (cover "." and "..")
    if (!Length || Length > 12 || L'.' == *Name)
        return TRUE;
    ExtensionFound = FALSE;
    for (Index = 0; Index < Length; Index++)
    {
        if (L'.' == Name[Index])
        {
            // multiple . or extension longer than 3
            if (ExtensionFound || Length - (Index + 1) > 3)
                return TRUE;
            ExtensionFound = TRUE;
        }
        // base length longer than 8
        if (Index >= 8 && !ExtensionFound)
            return TRUE;
    }
    return FALSE;
}
/**
    Search for SFN(Short File Name) or LFN(Long File Name) in the
    given path depends on FindLFN.

    Input: LPWSTR Path
                The given path name. Does not have to be fully qualified.
                However, path type separaters are not allowed.
           LPWSTR* ppFirst
                To return the pointer points to the first char
                of the name found.
           LPWSTR* ppLast
                To return the pointer points the char right after
                the last char of the name found.
           BOOL FindLFN
                TRUE to search for LFN, otherwise, search for SFN

    Output:
            TRUE
                if the target file name type is found, ppFirst and
                ppLast are filled with pointers.
            FALSE
                if the target file name type not found.

    Remark: "\\." and "\\.." are special cases. When encountered, they
            are ignored and the function continue to search


**/
BOOL
FindLFNorSFN_U(
    LPWSTR  Path,
    LPWSTR* ppFirst,
    LPWSTR* ppLast,
    BOOL    FindLFN
    )
{
    LPWSTR pFirst, pLast;
    BOOL TargetFound;

    ASSERT(Path);

    pFirst = Path;

    TargetFound = FALSE;

    while(TRUE) {
        //skip over leading path separator
        // it is legal to have multiple path separators in between
        // name such as "foobar\\\\\\multiplepathchar"
        while (*pFirst != UNICODE_NULL  && (*pFirst == L'\\' || *pFirst == L'/'))
            pFirst++;
        if (*pFirst == UNICODE_NULL)
            break;
        pLast = pFirst + 1;
        while (*pLast != UNICODE_NULL && *pLast != L'\\' && *pLast != L'/')
            pLast++;
        if (FindLFN)
            TargetFound = !IsShortName_U(pFirst, (int)(pLast - pFirst));
        else
            TargetFound = !IsLongName_U(pFirst, (int)(pLast - pFirst));
        if (TargetFound) {
            if(ppFirst && ppLast) {
                *ppFirst = pFirst;
                // pLast point to the last char of the path/file name
                *ppLast = pLast;
                }
            break;
            }
        if (*pLast == UNICODE_NULL)
            break;
        pFirst = pLast + 1;
        }
    return TargetFound;
}
LPCWSTR
SkipPathTypeIndicator_U(
    LPCWSTR Path
    )
{
    RTL_PATH_TYPE   RtlPathType;
    LPCWSTR         pFirst;
    DWORD           Count;

    RtlPathType = RtlDetermineDosPathNameType_U(Path);
    switch (RtlPathType) {
        // form: "\\server_name\share_name\rest_of_the_path"
        case RtlPathTypeUncAbsolute:
        case RtlPathTypeLocalDevice:
            pFirst = Path + 2;
            Count = 2;
            // guard for UNICODE_NULL is necessary because
            // RtlDetermineDosPathNameType_U doesn't really
            // verify an UNC name.
            while (Count && *pFirst != UNICODE_NULL) {
                if (*pFirst == L'\\' || *pFirst == L'/')
                    Count--;
                pFirst++;
                }
            break;

        // form: "\\."
        case RtlPathTypeRootLocalDevice:
            pFirst = NULL;
            break;

        // form: "D:\rest_of_the_path"
        case RtlPathTypeDriveAbsolute:
            pFirst = Path + 3;
            break;

        // form: "D:rest_of_the_path"
        case RtlPathTypeDriveRelative:
            pFirst = Path + 2;
            break;

        // form: "\rest_of_the_path"
        case RtlPathTypeRooted:
            pFirst = Path + 1;
            break;

        // form: "rest_of_the_path"
        case RtlPathTypeRelative:
            pFirst = Path;
            break;

        default:
            pFirst = NULL;
            break;
        }
    return pFirst;
}
DWORD
APIENTRY
GetLongPathNameW(
    IN  LPCWSTR lpszShortPath,
    IN  LPWSTR  lpszLongPath,
    IN  DWORD   cchBuffer
)
{

    LPCWSTR pcs;
    DWORD ReturnLen, Length;
    LPWSTR pSrc, pSrcCopy, pFirst, pLast, Buffer, pDst;
    WCHAR   wch;
    HANDLE          FindHandle;
    WIN32_FIND_DATAW        FindData;
    UINT PrevErrorMode;

    if (!ARGUMENT_PRESENT(lpszShortPath)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
        }
    //
    // override the error mode since we will be touching the media.
    // This is to prevent file system's pop-up when the given path does not
    // exist or the media is not available.
    // we are doing this because we can not depend on the caller's current
    // error mode. NOTE: the old error mode must be restored.
    PrevErrorMode = SetErrorMode(SEM_NOOPENFILEERRORBOX | SEM_FAILCRITICALERRORS);

    try {

        Buffer = NULL;
        pSrcCopy = NULL;
        // first make sure the given path exist.
        //
        if (0xFFFFFFFF == GetFileAttributesW(lpszShortPath))
        {
            // last error has been set by GetFileAttributes
            ReturnLen = 0;
            goto glnTryExit;
        }
        pcs = SkipPathTypeIndicator_U(lpszShortPath);
        if (!pcs || *pcs == UNICODE_NULL || !FindLFNorSFN_U((LPWSTR)pcs, &pFirst, &pLast, FALSE))
            {
            // The path is ok and does not need conversion at all.
            // Check if we need to do copy
            ReturnLen = wcslen(lpszShortPath);
            if (cchBuffer > ReturnLen && ARGUMENT_PRESENT(lpszLongPath))
                {
                if (lpszLongPath != lpszShortPath)
                    RtlMoveMemory(lpszLongPath, lpszShortPath,
                                      (ReturnLen + 1)* sizeof(WCHAR)
                                      );
                }
            else {
                // No buffer or buffer too small, the return size
                // has to count the terminated NULL char
                ReturnLen++;
                }
            goto glnTryExit;
            }


        // conversions  are necessary, make a local copy of the string
        // because we have to party on it.

        ASSERT(!pSrcCopy);

        Length = wcslen(lpszShortPath) + 1;
        pSrcCopy = RtlAllocateHeap(RtlProcessHeap(), 0,
                                   Length * sizeof(WCHAR)
                                   );
        if (!pSrcCopy) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            goto glnTryExit;
            }
        RtlMoveMemory(pSrcCopy, lpszShortPath, Length * sizeof(WCHAR));
        // pFirst points to the first char of the very first SFN in the path
        // pLast points to the char right after the last char of the very
        // first SFN in the path. *pLast could be UNICODE_NULL
        pFirst = pSrcCopy + (pFirst - lpszShortPath);
        pLast = pSrcCopy + (pLast - lpszShortPath);
        //
        // We allow lpszShortPath be overlapped with lpszLongPath so
        // allocate a local buffer if necessary:
        // (1) the caller does provide a legitimate buffer and
        // (2) the buffer overlaps with lpszShortName

        pDst = lpszLongPath;
        if (cchBuffer && ARGUMENT_PRESENT(lpszLongPath) &&
            (lpszLongPath >= lpszShortPath && lpszLongPath < lpszShortPath + Length ||
             lpszLongPath < lpszShortPath && lpszLongPath + cchBuffer >= lpszShortPath))
            {
            ASSERT(!Buffer);

            Buffer = RtlAllocateHeap(RtlProcessHeap(), 0,
                                           cchBuffer * sizeof(WCHAR));
            if (!Buffer){
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                goto glnTryExit;
                }
            pDst = Buffer;
            }

        pSrc = pSrcCopy;
        ReturnLen = 0;
        do {
            // there are three pointers involve in the conversion loop:
            // pSrc, pFirst and pLast. Their relationship
            // is:
            //
            // "c:\long~1.1\\foo.bar\\long~2.2\\bar"
            //  ^          ^          ^       ^
            //  |          |          |       |
            //  |          pSrc       pFirst  pLast
            //  pSrcCopy
            //
            // pSrcCopy always points to the very first char of the entire
            // path.
            //
            // chars between pSrc(included) and pFirst(not included)
            // do not need conversion so we simply copy them.
            // chars between pFirst(included) and pLast(not included)
            // need conversion.
            //
            Length = (ULONG)(pFirst - pSrc);
            ReturnLen += Length;
            if (Length && cchBuffer > ReturnLen && ARGUMENT_PRESENT(lpszShortPath))
                {
                RtlMoveMemory(pDst, pSrc, Length * sizeof(WCHAR));
                pDst += Length;
                }
            // now try to convert the name, chars between pFirst and (pLast - 1)
            wch = *pLast;
            *pLast = UNICODE_NULL;
            FindHandle = FindFirstFileW(pSrcCopy, &FindData);
            *pLast = wch;
            if (FindHandle != INVALID_HANDLE_VALUE){
                FindClose(FindHandle);
                // if no long name, copy the original name
                // starts with pFirst(included) and ends with pLast(excluded)
                if (!(Length = wcslen(FindData.cFileName)))
                    Length = (ULONG)(pLast - pFirst);
                else
                    pFirst = FindData.cFileName;
                ReturnLen += Length;
                if (cchBuffer > ReturnLen && ARGUMENT_PRESENT(lpszLongPath))
                    {
                    RtlMoveMemory(pDst, pFirst, Length * sizeof(WCHAR));
                    pDst += Length;
                    }
                }
            else {
                // invalid path, reset the length, mark the error and
                // bail out of the loop. We will be copying the source
                // to destination later.
                //
                ReturnLen = 0;
                break;
                }
            pSrc = pLast;
            if (*pSrc == UNICODE_NULL)
                break;
            } while (FindLFNorSFN_U(pSrc, &pFirst, &pLast, FALSE));

        if (ReturnLen) {
            //copy the rest of the path from pSrc. This may only contain
            //a single NULL char
            Length = wcslen(pSrc);
            ReturnLen += Length;
            if (cchBuffer > ReturnLen && ARGUMENT_PRESENT(lpszLongPath))
                {
                RtlMoveMemory(pDst, pSrc, (Length + 1) * sizeof(WCHAR));
                if (Buffer)
                    RtlMoveMemory(lpszLongPath, Buffer, (ReturnLen + 1) * sizeof(WCHAR));
                }
            else
                ReturnLen++;
            }

glnTryExit:
        ;
        }
        finally {
            if (pSrcCopy)
                RtlFreeHeap(RtlProcessHeap(), 0, pSrcCopy);
            if (Buffer)
                RtlFreeHeap(RtlProcessHeap(), 0, Buffer);
            }

    // restore error mode.
    SetErrorMode(PrevErrorMode);
    return ReturnLen;
}
