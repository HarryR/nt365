/*
 * libc_stdio.c — FILE handle management, fopen/fread/fwrite/fclose,
 * and the byte-level helpers (fputs/fputc/fgetc/fgets/…).
 *
 * Stdin is a real file handle set by ntshim_init; stdout/stderr route
 * through DbgPrint (FFLAG_CONSOLE) → COM1. fopen paths are NT-namespace
 * (e.g. \SystemRoot\lua\nt\dll\fs.lua) — no DOS translation.
 */

#include "libc_internal.h"

/* ntdll file surface */
extern NTSTATUS NTAPI NtCreateFile (HANDLE *FileHandle, ULONG DesiredAccess,
                                    POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
                                    PLARGE_INTEGER AllocationSize,
                                    ULONG FileAttributes,
                                    ULONG ShareAccess,
                                    ULONG CreateDisposition,
                                    ULONG CreateOptions,
                                    PVOID EaBuffer, ULONG EaLength);
extern NTSTATUS NTAPI NtReadFile   (HANDLE, HANDLE Event, PVOID ApcRoutine,
                                    PVOID ApcContext, PIO_STATUS_BLOCK,
                                    PVOID Buffer, ULONG Length,
                                    PLARGE_INTEGER ByteOffset, PULONG Key);
extern NTSTATUS NTAPI NtWriteFile  (HANDLE, HANDLE Event, PVOID ApcRoutine,
                                    PVOID ApcContext, PIO_STATUS_BLOCK,
                                    PVOID Buffer, ULONG Length,
                                    PLARGE_INTEGER ByteOffset, PULONG Key);
extern NTSTATUS NTAPI NtClose      (HANDLE);

struct FILE _libc_stdin  = { 0, FFLAG_READ  | FFLAG_STATIC,                 {0,0} };
struct FILE _libc_stdout = { 0, FFLAG_WRITE | FFLAG_CONSOLE | FFLAG_STATIC, {0,0} };
struct FILE _libc_stderr = { 0, FFLAG_WRITE | FFLAG_CONSOLE | FFLAG_STATIC, {0,0} };

FILE *stdin  = &_libc_stdin;
FILE *stdout = &_libc_stdout;
FILE *stderr = &_libc_stderr;

size_t fwrite(const void *ptr, size_t size, size_t n, FILE *fp)
{
    IO_STATUS_BLOCK iosb;
    NTSTATUS st;
    ULONG total = (ULONG)(size * n);

    if (fp->flags & FFLAG_CONSOLE) {
        /* DbgPrint's internal buffer is 512 bytes; chunk. Use %.*s so
         * format-string injection from printed data can't hurt us. */
        const char *p = (const char *)ptr;
        ULONG left = total;
        while (left) {
            ULONG chunk = left > 480 ? 480 : left;
            DbgPrint("%.*s", (int)chunk, p);
            p    += chunk;
            left -= chunk;
        }
        return n;
    }

    if (!(fp->flags & FFLAG_WRITE) || !fp->handle) { fp->flags |= FFLAG_ERR; return 0; }
    st = NtWriteFile(fp->handle, 0, 0, 0, &iosb, (PVOID)ptr, total, &fp->pos, 0);
    if (!NT_SUCCESS(st)) { fp->flags |= FFLAG_ERR; return 0; }
    fp->pos.LowPart += (ULONG)iosb.Information;   /* no 64-bit wraps yet */
    return iosb.Information / (size ? size : 1);
}

size_t fread(void *ptr, size_t size, size_t n, FILE *fp)
{
    IO_STATUS_BLOCK iosb;
    NTSTATUS st;
    ULONG total = (ULONG)(size * n);

    if (!(fp->flags & FFLAG_READ) || !fp->handle) { fp->flags |= FFLAG_ERR; return 0; }
    st = NtReadFile(fp->handle, 0, 0, 0, &iosb, ptr, total, &fp->pos, 0);
    if (!NT_SUCCESS(st)) {
        /* STATUS_END_OF_FILE is 0xC0000011 — normal EOF, not an error. */
        if ((unsigned)st == 0xC0000011) fp->flags |= FFLAG_EOF;
        else                            fp->flags |= FFLAG_ERR;
        return 0;
    }
    fp->pos.LowPart += (ULONG)iosb.Information;
    return iosb.Information / (size ? size : 1);
}

int fputs(const char *s, FILE *fp)
{
    size_t n = strlen(s);
    return (int)fwrite(s, 1, n, fp);
}

int fputc(int c, FILE *fp)
{
    unsigned char b = (unsigned char)c;
    fwrite(&b, 1, 1, fp);
    return c;
}

int puts(const char *s)    { fputs(s, stdout); fputc('\n', stdout); return 0; }
int putchar(int c)         { return fputc(c, stdout); }
int feof (FILE *fp)        { return (fp->flags & FFLAG_EOF) ? 1 : 0; }
int ferror(FILE *fp)       { return (fp->flags & FFLAG_ERR) ? 1 : 0; }
int fflush(FILE *fp)       { (void)fp; return 0; }       /* unbuffered */

/* fgetc / fgets — byte-at-a-time; good enough until Lua's io.read appears. */
int fgetc(FILE *fp)
{
    unsigned char b;
    return fread(&b, 1, 1, fp) == 1 ? (int)b : EOF;
}

char *fgets(char *buf, int n, FILE *fp)
{
    int i, c;
    if (n <= 0) return 0;
    for (i = 0; i < n - 1; ) {
        c = fgetc(fp);
        if (c == EOF) { if (i == 0) return 0; else break; }
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    buf[i] = 0;
    return buf;
}

/* ---------- fopen / fclose ------------------------------------------- */
/* Paths are NT-namespace form, fed straight to NtCreateFile with no DOS
 * translation. Only "r", "rb", "w", "wb", "a", "ab" supported. */

#define FILE_GENERIC_READ            0x00120089
#define FILE_GENERIC_WRITE           0x00120116
#define FILE_SHARE_READ              0x00000001
#define FILE_SHARE_WRITE             0x00000002
#define FILE_OPEN                    0x00000001
#define FILE_OVERWRITE_IF            0x00000005
#define FILE_OPEN_IF                 0x00000003
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020
#define FILE_NON_DIRECTORY_FILE      0x00000040

FILE *fopen(const char *path, const char *mode)
{
    UNICODE_STRING    ntpath;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK   iosb;
    HANDLE            h;
    ULONG             access, disp, share, create_opts;
    ULONG             fflags;
    NTSTATUS          st;
    unsigned short    wpath[512];
    size_t            i, plen;
    FILE             *fp;

    access = 0; disp = 0; fflags = 0;
    switch (*mode) {
    case 'r': access = FILE_GENERIC_READ;  disp = FILE_OPEN;         fflags = FFLAG_READ;  break;
    case 'w': access = FILE_GENERIC_WRITE; disp = FILE_OVERWRITE_IF; fflags = FFLAG_WRITE; break;
    case 'a': access = FILE_GENERIC_WRITE; disp = FILE_OPEN_IF;      fflags = FFLAG_WRITE | FFLAG_APPEND; break;
    default:  return 0;
    }
    share       = FILE_SHARE_READ | FILE_SHARE_WRITE;
    create_opts = FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE;

    plen = strlen(path);
    if (plen == 0 || plen >= sizeof(wpath) / 2) return 0;
    for (i = 0; i < plen; i++) wpath[i] = (unsigned short)(unsigned char)path[i];
    wpath[plen] = 0;

    ntpath.Buffer        = (PWSTR)wpath;
    ntpath.Length        = (USHORT)(plen * sizeof(unsigned short));
    ntpath.MaximumLength = (USHORT)((plen + 1) * sizeof(unsigned short));

    oa.Length                   = sizeof(oa);
    oa.RootDirectory            = 0;
    oa.ObjectName               = &ntpath;
    oa.Attributes               = OBJ_CASE_INSENSITIVE;
    oa.SecurityDescriptor       = 0;
    oa.SecurityQualityOfService = 0;

    st = NtCreateFile(&h, access, &oa, &iosb, 0, 0x80 /* FILE_ATTRIBUTE_NORMAL */,
                      share, disp, create_opts, 0, 0);
    if (!NT_SUCCESS(st)) return 0;

    fp = (FILE *)malloc(sizeof(*fp));
    if (!fp) { NtClose(h); return 0; }
    fp->handle      = h;
    fp->flags       = fflags;
    fp->pos.LowPart = fp->pos.HighPart = 0;
    return fp;
}

int fclose(FILE *fp)
{
    if (!fp) return EOF;
    if (fp->flags & FFLAG_STATIC) return 0;
    if (fp->handle) NtClose(fp->handle);
    free(fp);
    return 0;
}

int fseek(FILE *fp, long off, int whence)
{
    /* Only SEEK_SET + SEEK_CUR for now. SEEK_END wants NtQueryInformationFile. */
    switch (whence) {
    case SEEK_SET: fp->pos.LowPart = (ULONG)off; fp->pos.HighPart = off < 0 ? -1 : 0; return 0;
    case SEEK_CUR: fp->pos.LowPart += (ULONG)off; return 0;
    default:       fp->flags |= FFLAG_ERR; return -1;
    }
}

long ftell(FILE *fp) { return (long)fp->pos.LowPart; }

/* ---------- mingw stdio glue ----------------------------------------- */
/* mingw's <stdio.h> expands stdin/stdout/stderr to __acrt_iob_func(N).
 * Return our static FILE*s — index is 0/1/2, no tags. */
FILE *__acrt_iob_func(unsigned index)
{
    switch (index) {
    case 0: return &_libc_stdin;
    case 1: return &_libc_stdout;
    case 2: return &_libc_stderr;
    default: return 0;
    }
}
