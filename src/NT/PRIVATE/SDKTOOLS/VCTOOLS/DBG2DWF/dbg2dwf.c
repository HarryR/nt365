/*
 * dbg2dwf — convert NT 3.5 sidecar .DBG (CV4 / NB09) to a gdb-loadable
 * ELF carrying DWARF 2 line + minimal type info.
 *
 * MVP scope (this file):
 *   - Open .DBG via imagehlp's MapDebugInformation.
 *   - Walk CodeView OMF directory: sstModule, sstAlignSym, sstSrcModule.
 *   - Emit ET_EXEC ELF i386 with:
 *       .symtab/.strtab    — function publics (name → image VA)
 *       .debug_info        — one DW_TAG_compile_unit DIE per CV module
 *       .debug_abbrev      — single DW_TAG_compile_unit abbrev
 *       .debug_line        — DWARF 2 line program per module
 *       .debug_str         — strings used by DIE attributes
 *       .shstrtab          — section name strings
 *
 * Locals (S_BPREL32) and full types (LF_*) land in a later MVP.
 *
 * Usage:
 *     dbg2dwf <input.dbg|.exe> <output.elf>
 *
 * The output ELF is loaded via gdb's `add-symbol-file <elf>` (no offset
 * needed — symbol VAs are absolute, computed as PE imagebase + RVA).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <fcntl.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <windows.h>
#include <imagehlp.h>

#include "cvexefmt.h"
#include "cvinfo.h"

/* ----------------------------------------------------------------
 * Dynamic byte buffer.
 * ---------------------------------------------------------------- */

typedef struct {
    unsigned char *p;
    unsigned long  len;
    unsigned long  cap;
} Buf;

static void buf_init(Buf *b)
{
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_grow(Buf *b, unsigned long need)
{
    unsigned long cap;

    if (b->cap >= b->len + need) return;
    cap = b->cap ? b->cap : 256;
    while (cap < b->len + need) cap *= 2;
    b->p = (unsigned char *)realloc(b->p, cap);
    if (!b->p) {
        fprintf(stderr, "dbg2dwf: out of memory (need %lu)\n", cap);
        exit(2);
    }
    b->cap = cap;
}

static void buf_put(Buf *b, const void *data, unsigned long n)
{
    buf_grow(b, n);
    memcpy(b->p + b->len, data, n);
    b->len += n;
}

static void buf_u8(Buf *b, unsigned char v)   { buf_put(b, &v, 1); }
static void buf_u16(Buf *b, unsigned short v) { buf_put(b, &v, 2); }
static void buf_u32(Buf *b, unsigned long v)  { buf_put(b, &v, 4); }
static void buf_str(Buf *b, const char *s)    { buf_put(b, s, (unsigned long)(strlen(s) + 1)); }

static void buf_uleb(Buf *b, unsigned long v)
{
    unsigned char byte;

    do {
        byte = (unsigned char)(v & 0x7f);
        v >>= 7;
        if (v) byte |= 0x80;
        buf_u8(b, byte);
    } while (v);
}

static void buf_sleb(Buf *b, long v)
{
    int more = 1;
    unsigned char byte;
    int sign;

    while (more) {
        byte = (unsigned char)(v & 0x7f);
        v >>= 7;             /* arithmetic shift on signed long */
        sign = byte & 0x40;
        if ((v == 0 && !sign) || (v == -1L && sign)) {
            more = 0;
        } else {
            byte |= 0x80;
        }
        buf_u8(b, byte);
    }
}

/* Patch a 4-byte little-endian value already written at offset off. */
static void buf_patch_u32(Buf *b, unsigned long off, unsigned long v)
{
    b->p[off + 0] = (unsigned char)(v & 0xff);
    b->p[off + 1] = (unsigned char)((v >>  8) & 0xff);
    b->p[off + 2] = (unsigned char)((v >> 16) & 0xff);
    b->p[off + 3] = (unsigned char)((v >> 24) & 0xff);
}

/* ----------------------------------------------------------------
 * String table (deduplicated).  For .strtab and .debug_str.
 * ---------------------------------------------------------------- */

typedef struct {
    Buf data;
} Strtab;

static void strtab_init(Strtab *t)
{
    buf_init(&t->data);
    /* ELF + DWARF convention: index 0 is empty string. */
    buf_u8(&t->data, 0);
}

static unsigned long strtab_add(Strtab *t, const char *s)
{
    unsigned long off, len, i;

    /* Linear scan dedup; tables stay small for our scale. */
    len = (unsigned long)strlen(s);
    for (i = 0; i + len < t->data.len; ) {
        if (memcmp(t->data.p + i, s, len + 1) == 0) return i;
        while (i < t->data.len && t->data.p[i] != 0) i++;
        i++;  /* skip the NUL */
    }
    off = t->data.len;
    buf_put(&t->data, s, len + 1);
    return off;
}

/* Forward declarations for helpers defined further down so the
 * Module-management code can call them.  All of these live in the CV
 * parsing block — kept there because they're conceptually input-side
 * utilities, but referenced from the storage layer. */
static char *path_normalize_dup(const char *p);
static void  path_split(const char *path, char *dir, size_t dir_sz,
                        char *base, size_t base_sz);

/* ----------------------------------------------------------------
 * Module + per-module data extracted from the CV directory.
 * ---------------------------------------------------------------- */

/* A local variable / formal parameter / register-resident value
 * scoped under a Symbol that is_func.  off + dwarf_reg interpretation:
 *   dwarf_reg < 0           → BP-relative (off is the EBP offset)
 *   dwarf_reg >= 0, off==0  → plain register (DW_OP_regN)
 *   dwarf_reg >= 0, off!=0  → register + offset (DW_OP_bregN <off>)
 * is_param distinguishes formal parameters (CV BPRel positive offset
 * convention) from locals so DWARF can emit the right tag. */
typedef struct Local {
    char         *name;
    long          off;
    int           dwarf_reg;
    int           is_param;
    unsigned short typind;       /* CV type index (0 = unknown) */
} Local;

typedef struct Symbol {
    char        *name;
    unsigned long va;
    unsigned long size;
    int          is_func;
    Local        *locals;
    unsigned long n_locals;
    unsigned long cap_locals;
    unsigned short typind;       /* CV type index — for procs this is
                                  * a LF_PROCEDURE TI; we resolve to
                                  * the return type at emit time. */
    /* Frame-valid range, absolute VAs.  CV4 PROCSYM32.DbgStart marks
     * where the prologue is done; DbgEnd marks where the epilogue
     * begins.  BP-relative param/local locations are only meaningful
     * in [body_start, body_end) — outside, EBP is either the caller's
     * value (entry) or already restored (epilogue), and reading
     * [ebp+8] yields garbage.  Emitting DW_AT_location as a location
     * list scoped to this range makes gdb show <optimized out> in the
     * prologue instead of dereferencing a stale EBP. */
    unsigned long body_start;
    unsigned long body_end;
    /* True when the function sets up a real EBP frame.  Derived from
     * CV4's CV_PROCFLAGS.CV_PFLAG_FPO (bit 0) — NB: CV4 polarity is
     * 1 = FPO active (frame pointer omitted), so has_fp = !(flag&1).
     * For FPO funcs the BP-rel offsets in CV are runtime-invalid:
     * EBP holds the caller's value (often 0 at boot) and reading
     * [ebp+N] yields garbage.  We emit an empty location list so gdb
     * shows <optimized out> instead.  Proper esp-rel tracking needs
     * FPO_DATA from .debug$F + .debug_frame CFI. */
    int           has_fp;
} Symbol;

/* === Type table === */
typedef struct CvTypeEntry {
    unsigned short leaf;
    const unsigned char *payload;
    unsigned long  payload_len;
    unsigned long  dwarf_off;     /* abs offset in .debug_info, 0 = pending */
} CvTypeEntry;

static CvTypeEntry  *types     = NULL;
static unsigned long n_types   = 0;
static const unsigned long ti_min = 0x1000;   /* CV_FIRST_NONPRIM */

/* Primitive TI → DWARF offset, populated by emit_primitives_into_cu.
 * Sized to cover the CV primitive TI range (low 12 bits). */
static unsigned long prim_off[0x1000];
static unsigned long void_off  = 0;   /* fallback for unknown types */

typedef struct LineRow {
    unsigned long va;
    unsigned long line;
    unsigned long file_id;   /* index into the module's file_names array */
    unsigned long seq_id;    /* CV4 per-(file,seg) line block index — rows
                                with different seq_ids must NOT share a
                                DWARF line-program sequence, since the code
                                ranges they describe are non-contiguous and
                                gdb otherwise stretches the last line of
                                one block across the gap to the next. */
} LineRow;

typedef struct Module {
    char         *name;          /* obj file path (sstModule's library name) */
    unsigned long low_va;        /* min function VA */
    unsigned long high_va;       /* max (function VA + size) */

    char         **files;        /* source file names (1-based index per CV) */
    unsigned long  n_files;
    unsigned long  cap_files;

    LineRow       *lines;
    unsigned long  n_lines;
    unsigned long  cap_lines;

    Symbol        *funcs;        /* per-CU function symbols */
    unsigned long  n_funcs;
    unsigned long  cap_funcs;

    /* set by emitter: */
    unsigned long  line_program_off;  /* offset into .debug_line */
    unsigned long  cu_info_off;       /* offset of this CU in .debug_info,
                                         used by .debug_aranges to point at
                                         the right CU.  0 if this Module
                                         contributed no CU (n_lines==0 &&
                                         n_funcs==0). */
    int            cu_emitted;        /* set when emit_debug_info_cu actually
                                         wrote a CU for this module. */
} Module;

static Module *mods = NULL;
static unsigned long n_mods = 0;
static unsigned long cap_mods = 0;

static Symbol *syms = NULL;
static unsigned long n_syms = 0;
static unsigned long cap_syms = 0;

/* Segment map: seg-1 → image-relative VA of segment's start. */
static unsigned long *seg_rva = NULL;
static unsigned short n_segs = 0;

static unsigned long g_image_base = 0;

static Module *new_module(void)
{
    Module *m;

    if (n_mods + 1 > cap_mods) {
        cap_mods = cap_mods ? cap_mods * 2 : 16;
        mods = (Module *)realloc(mods, cap_mods * sizeof(Module));
        if (!mods) { fputs("dbg2dwf: oom mods\n", stderr); exit(2); }
    }
    m = &mods[n_mods++];
    memset(m, 0, sizeof(*m));
    return m;
}

static unsigned long mod_add_file(Module *m, const char *fname)
{
    unsigned long i;
    char *norm;

    norm = path_normalize_dup(fname);
    for (i = 0; i < m->n_files; i++) {
        if (strcmp(m->files[i], norm) == 0) {
            free(norm);
            return i + 1;
        }
    }
    if (m->n_files + 1 > m->cap_files) {
        m->cap_files = m->cap_files ? m->cap_files * 2 : 4;
        m->files = (char **)realloc(m->files, m->cap_files * sizeof(char *));
        if (!m->files) { fputs("dbg2dwf: oom files\n", stderr); exit(2); }
    }
    m->files[m->n_files++] = norm;   /* takes ownership */
    return m->n_files;  /* 1-based */
}

static void mod_add_line(Module *m, unsigned long va, unsigned long line,
                         unsigned long file_id, unsigned long seq_id)
{
    LineRow *r;

    if (m->n_lines + 1 > m->cap_lines) {
        m->cap_lines = m->cap_lines ? m->cap_lines * 2 : 64;
        m->lines = (LineRow *)realloc(m->lines, m->cap_lines * sizeof(LineRow));
        if (!m->lines) { fputs("dbg2dwf: oom lines\n", stderr); exit(2); }
    }
    r = &m->lines[m->n_lines++];
    r->va      = va;
    r->line    = line;
    r->file_id = file_id;
    r->seq_id  = seq_id;
}

static void add_symbol(const char *name, unsigned long va, unsigned long size,
                       int is_func)
{
    Symbol *s;
    char *dup;

    if (n_syms + 1 > cap_syms) {
        cap_syms = cap_syms ? cap_syms * 2 : 64;
        syms = (Symbol *)realloc(syms, cap_syms * sizeof(Symbol));
        if (!syms) { fputs("dbg2dwf: oom syms\n", stderr); exit(2); }
    }
    s = &syms[n_syms++];
    dup = (char *)malloc(strlen(name) + 1);
    if (!dup) { fputs("dbg2dwf: oom sym\n", stderr); exit(2); }
    strcpy(dup, name);
    s->name    = dup;
    s->va      = va;
    s->size    = size;
    s->is_func = is_func;
}

/* Same as add_symbol but also attaches the function to a module so the
 * emitter can produce DW_TAG_subprogram DIEs as children of the CU.
 * Returns the index of the newly-added function within m->funcs, so
 * the caller can attach locals to it without keeping a pointer that
 * could dangle across realloc. */
static unsigned long mod_add_func(Module *m, const char *name,
                                  unsigned long va, unsigned long size,
                                  unsigned short typind,
                                  unsigned long body_start,
                                  unsigned long body_end,
                                  int has_fp)
{
    Symbol *s;
    char *dup;

    add_symbol(name, va, size, 1);
    if (m->n_funcs + 1 > m->cap_funcs) {
        m->cap_funcs = m->cap_funcs ? m->cap_funcs * 2 : 8;
        m->funcs = (Symbol *)realloc(m->funcs, m->cap_funcs * sizeof(Symbol));
        if (!m->funcs) { fputs("dbg2dwf: oom funcs\n", stderr); exit(2); }
    }
    s = &m->funcs[m->n_funcs++];
    dup = (char *)malloc(strlen(name) + 1);
    if (!dup) { fputs("dbg2dwf: oom func\n", stderr); exit(2); }
    strcpy(dup, name);
    s->name        = dup;
    s->va          = va;
    s->size        = size;
    s->is_func     = 1;
    s->locals      = NULL;
    s->n_locals    = 0;
    s->cap_locals  = 0;
    s->typind      = typind;
    s->body_start  = body_start;
    s->body_end    = body_end;
    s->has_fp      = has_fp;
    if (va < m->low_va)         m->low_va  = va;
    if (va + size > m->high_va) m->high_va = va + size;
    return m->n_funcs - 1;
}

/* Append a Local to a function's locals[].  func is identified by
 * (Module*, index) rather than a raw pointer so realloc on funcs[] in
 * a sibling call can't dangle the reference (in practice it can't
 * happen between S_LPROC32 and S_END for the same proc, but cheap
 * insurance). */
static void func_add_local(Module *m, unsigned long fidx,
                           const char *name, long off,
                           int dwarf_reg, int is_param,
                           unsigned short typind)
{
    Symbol *fn;
    Local  *l;
    char   *dup;

    if (fidx >= m->n_funcs) return;
    fn = &m->funcs[fidx];
    if (fn->n_locals + 1 > fn->cap_locals) {
        fn->cap_locals = fn->cap_locals ? fn->cap_locals * 2 : 4;
        fn->locals = (Local *)realloc(fn->locals,
                                      fn->cap_locals * sizeof(Local));
        if (!fn->locals) { fputs("dbg2dwf: oom locals\n", stderr); exit(2); }
    }
    l = &fn->locals[fn->n_locals++];
    dup = (char *)malloc(strlen(name) + 1);
    if (!dup) { fputs("dbg2dwf: oom local\n", stderr); exit(2); }
    strcpy(dup, name);
    l->name      = dup;
    l->off       = off;
    l->dwarf_reg = dwarf_reg;
    l->is_param  = is_param;
    l->typind    = typind;
}

/* CV register number → DWARF i386 register number.  Maps the 32-bit
 * GP regs only (CV 17..24 → DWARF 0..7).  Returns -1 for anything
 * we don't model in MVP3 (16-bit, FP stack, segment, MMX). */
/* Map a CV4 register code to a DWARF register number.
 *
 * IMPORTANT: we emit x86-64 DWARF register numbers, not i386, even
 * though the kernel is 32-bit code in an EM_386 ELF.  Reason: our
 * debugger is gdb attached to qemu-system-x86_64's gdbstub, which
 * advertises the target as i386:x86-64 (long-mode CPU running 32-bit
 * code).  gdb uses the *target* architecture's DWARF reg map for
 * expression evaluation regardless of the objfile's e_machine, so
 * i386 numbers (EBP=5) get interpreted as x86-64 (5=rdi) and locations
 * fail with "Cannot access memory at address 0x8" (rdi=0 + fbreg+8).
 *
 * CV4 x86 reg codes: 17=EAX, 18=ECX, 19=EDX, 20=EBX, 21=ESP, 22=EBP,
 *                    23=ESI, 24=EDI.
 * x86-64 DWARF nums: 0=rax, 1=rdx, 2=rcx, 3=rbx, 4=rsi, 5=rdi,
 *                    6=rbp, 7=rsp. */
static int cv_to_dwarf_reg(unsigned short cv_reg)
{
    static const signed char map[] = {
        0,  /* 17 EAX → rax (0) */
        2,  /* 18 ECX → rcx (2) */
        1,  /* 19 EDX → rdx (1) */
        3,  /* 20 EBX → rbx (3) */
        7,  /* 21 ESP → rsp (7) */
        6,  /* 22 EBP → rbp (6) */
        4,  /* 23 ESI → rsi (4) */
        5,  /* 24 EDI → rdi (5) */
    };
    if (cv_reg >= 17 && cv_reg <= 24) return map[cv_reg - 17];
    return -1;
}

/* ----------------------------------------------------------------
 * CodeView parsing: walk the OMF directory and extract modules,
 * symbols, and line rows.
 *
 * The CodeView blob layout (NB09 / cvpacked C8):
 *     magic        ("NB09" or similar — 8 bytes)
 *     lfoDir       (4 bytes — offset of OMFDirHeader from blob start)
 *     ...
 *     [at lfoDir]  OMFDirHeader { cbDirHeader, cbDirEntry, cDir,
 *                                 lfoNextDir, flags }
 *     [next]       OMFDirEntry[cDir] { SubSection, iMod, lfo, cb }
 *
 * Each entry's data lives at blob_base + lfo for cb bytes.
 * ---------------------------------------------------------------- */

/* Record-length-prefixed string ("ST"-style: 1 byte length + bytes). */
static const char *st_to_cstr(const unsigned char *st, char *buf, size_t bufsz)
{
    unsigned long n = st[0];
    if (n + 1 > bufsz) n = (unsigned long)(bufsz - 1);
    memcpy(buf, st + 1, n);
    buf[n] = '\0';
    return buf;
}

/* Strip cdecl underscore prefix and `@N` stdcall byte-count suffix.
 * `_KeBugCheckEx@20` → `KeBugCheckEx`.  Modifies buf in place. */
static void demangle(char *buf)
{
    char *at;
    size_t n;

    n = strlen(buf);
    if (n > 0 && buf[0] == '_') {
        memmove(buf, buf + 1, n);
        n--;
    }
    at = strchr(buf, '@');
    if (at) *at = '\0';
}

/* Normalize a Windows-style path to POSIX so Linux gdb can resolve it
 * against the actual filesystem.  Strips a drive-letter prefix
 * (`Z:\foo` → `\foo`) and converts every backslash to forward-slash.
 * Returns a malloc'd copy. */
static char *path_normalize_dup(const char *p)
{
    char  *out, *o;
    size_t n;

    if (p[0] && p[1] == ':') p += 2;     /* drop drive letter */
    n = strlen(p);
    out = (char *)malloc(n + 1);
    if (!out) { fputs("dbg2dwf: oom path\n", stderr); exit(2); }
    for (o = out; *p; p++, o++) *o = (*p == '\\') ? '/' : *p;
    *o = '\0';
    return out;
}

/* Split a POSIX path into directory and basename, both written into
 * caller-supplied buffers.  No trailing slash on the directory part.
 * If no slash present, dir is empty. */
static void path_split(const char *path, char *dir, size_t dir_sz,
                       char *base, size_t base_sz)
{
    const char *s = strrchr(path, '/');
    size_t n;

    if (!s) {
        dir[0] = '\0';
        strncpy(base, path, base_sz - 1);
        base[base_sz - 1] = '\0';
        return;
    }
    n = (size_t)(s - path);
    if (n >= dir_sz) n = dir_sz - 1;
    memcpy(dir, path, n);
    dir[n] = '\0';
    strncpy(base, s + 1, base_sz - 1);
    base[base_sz - 1] = '\0';
}

/* sstSegMap parser: array of OMFSegMapDesc preceded by header.
 * We only need each desc's seg index → start RVA mapping. */
typedef struct {
    unsigned short cSeg;
    unsigned short cSegLog;
} SegMapHdr;

typedef struct {
    unsigned short flags;
    unsigned short ovl;
    unsigned short group;
    unsigned short frame;
    unsigned short iSegName;
    unsigned short iClassName;
    unsigned long  offset;
    unsigned long  cbSeg;
} SegMapDesc;

static void parse_segmap(const unsigned char *p, unsigned long cb)
{
    SegMapHdr h;
    unsigned long i;
    SegMapDesc d;

    if (cb < sizeof(h)) return;
    memcpy(&h, p, sizeof(h));
    p += sizeof(h);
    n_segs = h.cSeg;
    seg_rva = (unsigned long *)calloc(n_segs, sizeof(unsigned long));
    if (!seg_rva) { fputs("dbg2dwf: oom segmap\n", stderr); exit(2); }
    for (i = 0; i < n_segs && (i + 1) * sizeof(d) <= cb - sizeof(h); i++) {
        memcpy(&d, p + i * sizeof(d), sizeof(d));
        seg_rva[i] = d.offset;
    }
}

static unsigned long seg_off_to_va(unsigned short seg, unsigned long off)
{
    if (seg == 0 || seg > n_segs) return g_image_base + off;
    return g_image_base + seg_rva[seg - 1] + off;
}

/* sstModule: per-module header.
 *     ovlNumber    ushort
 *     iLib         ushort
 *     cSeg         ushort
 *     style[2]     char[2]   (e.g. "CV")
 *     SegInfo[cSeg] { Seg, pad, offset, cbSeg }   each 12 bytes
 *     ModName      ST (length-prefixed)
 */
static void parse_module(const unsigned char *p, unsigned long cb,
                         unsigned short imod)
{
    unsigned short cSeg;
    unsigned long  hdr_size;
    char          name_buf[512];
    Module        *m;

    (void)imod;

    if (cb < 8) return;
    cSeg = *(unsigned short *)(p + 4);
    hdr_size = 8 + (unsigned long)cSeg * 12;
    if (cb < hdr_size) return;
    m = new_module();
    st_to_cstr(p + hdr_size, name_buf, sizeof(name_buf));
    m->name = path_normalize_dup(name_buf);
    m->low_va  = (unsigned long)-1;
    m->high_va = 0;
}

/* sstSrcModule: per-module line/file table.
 *     cFile, cSeg     2 bytes each
 *     baseSrcFile[cFile]    (4 bytes each — offsets into this subsection)
 *     SegOff[cSeg] { start, end }    8 bytes each
 *     seg[cSeg]              2 bytes each — segment indices
 *     [per-file blocks at baseSrcFile[i]]
 *
 * Per-file block:
 *     cSeg, pad        2 bytes each
 *     baseSrcLn[cSeg]  4 bytes each
 *     SegOff[cSeg]     8 bytes each
 *     name             ST
 *     [per-seg line blocks at baseSrcLn[j]]
 *
 * Per-seg line block:
 *     Seg, cPair       2 bytes each
 *     offset[cPair]    4 bytes each
 *     linenumber[cPair] 2 bytes each
 */
static void parse_srcmodule(const unsigned char *base, unsigned long cb,
                            unsigned short imod)
{
    Module        *m = NULL;
    unsigned short cFile, cSeg;
    unsigned long  i, j, k;
    const unsigned char *p;
    char           name_buf[512];
    unsigned long  seq_id = 0;

    /* Find module by index — modules registered in order they appear in
     * the directory; here we trust the iMod numbering matches our slot. */
    if (imod == 0 || imod > n_mods) return;
    m = &mods[imod - 1];

    if (cb < 4) return;
    cFile = *(unsigned short *)(base + 0);
    cSeg  = *(unsigned short *)(base + 2);

    for (i = 0; i < cFile; i++) {
        unsigned long ofile = *(unsigned long *)(base + 4 + i * 4);
        unsigned short fcSeg, fpad;
        unsigned char  cbName;
        unsigned long  fid;

        if (ofile + 4 > cb) continue;
        p = base + ofile;
        fcSeg = *(unsigned short *)(p + 0);
        fpad  = *(unsigned short *)(p + 2);
        (void)fpad;

        /* Skip baseSrcLn[fcSeg] + SegOff[fcSeg] to reach the name. */
        cbName = *(p + 4 + fcSeg * 4 + fcSeg * 8);
        st_to_cstr(p + 4 + fcSeg * 4 + fcSeg * 8, name_buf,
                   sizeof(name_buf));
        fid = mod_add_file(m, name_buf);

        /* Walk per-seg line blocks.  Each (file, seg) block describes
         * one contiguous code region — give it its own seq_id so the
         * DWARF emitter inserts an end_sequence between distinct
         * regions (handle.obj contributes to several non-contiguous
         * segments; without sequence breaks the last line of one block
         * "sticks" up to the first line of the next). */
        for (j = 0; j < fcSeg; j++) {
            unsigned long oseg = *(unsigned long *)(p + 4 + j * 4);
            const unsigned char *q;
            unsigned short qSeg, qPair;
            const unsigned long  *offs;
            const unsigned short *lns;

            if (oseg + 4 > cb) continue;
            q     = base + oseg;
            qSeg  = *(unsigned short *)(q + 0);
            qPair = *(unsigned short *)(q + 2);
            offs  = (const unsigned long  *)(q + 4);
            lns   = (const unsigned short *)(q + 4 + qPair * 4);

            seq_id++;
            for (k = 0; k < qPair; k++) {
                unsigned long va;

                va = seg_off_to_va(qSeg, offs[k]);
                mod_add_line(m, va, lns[k], fid, seq_id);
                if (va < m->low_va)  m->low_va  = va;
                if (va > m->high_va) m->high_va = va;
            }
        }
    }
}

/* sstAlignSym (or sstSymbols for C7): module's own symbol stream.
 * First DWORD is signature (1 = C7).  Records follow:
 *     reclen  ushort
 *     rectyp  ushort
 *     ...     reclen-2 bytes of payload
 */
static void parse_alignsym(const unsigned char *base, unsigned long cb,
                           unsigned short imod)
{
    unsigned long  off;
    unsigned long  sig;
    char           name_buf[512];
    Module        *m = NULL;
    /* Current procedure scope for nested S_BPREL32 / S_REGREL32 / etc.
     * cur_func = (m->funcs index) + 1 — 0 means "not inside a proc".
     * scope_depth counts S_LPROC*, S_BLOCK*, S_WITH* opens minus
     * S_END closes.  We attach locals to the enclosing proc as long
     * as scope_depth > 0; lexical-block nesting (DWARF DW_TAG_lexical_block)
     * is left for a later iteration — for MVP3 all locals are children
     * of the proc DIE.  */
    unsigned long  cur_func = 0;
    int            scope_depth = 0;

    if (imod != 0 && imod <= n_mods) m = &mods[imod - 1];

    if (cb < 4) return;
    sig = *(unsigned long *)base;
    off = (sig == 1L) ? 4 : 0;

    while (off + 4 <= cb) {
        unsigned short reclen, rectyp;
        const unsigned char *rec;

        reclen = *(unsigned short *)(base + off);
        rectyp = *(unsigned short *)(base + off + 2);
        if (reclen == 0 || off + 2 + reclen > cb) break;
        rec = base + off + 4;  /* payload after reclen + rectyp */

        switch (rectyp) {
        case S_GPROC32:
        case S_LPROC32: {
            /* PROCSYM32 layout (after reclen+rectyp), pack(1):
             *   pParent  (4)  @0
             *   pEnd     (4)  @4
             *   pNext    (4)  @8
             *   len      (4)  @12
             *   DbgStart (4)  @16
             *   DbgEnd   (4)  @20
             *   off      (4)  @24
             *   seg      (2)  @28
             *   typind   (2)  @30 (CV_typ_t = ushort in CV4)
             *   flags    (1)  @32 (CV_PROCFLAGS = 1 byte bitfield union)
             *   name     (ST) @33 */
            unsigned long  flen   = *(unsigned long *)(rec + 12);
            unsigned long  dbg_s  = *(unsigned long *)(rec + 16);
            unsigned long  dbg_e  = *(unsigned long *)(rec + 20);
            unsigned long  fofs   = *(unsigned long *)(rec + 24);
            unsigned short fseg   = *(unsigned short *)(rec + 28);
            unsigned short ftypind = *(unsigned short *)(rec + 30);
            unsigned char  flags   = rec[32];          /* CV_PROCFLAGS */
            /* CV4's CV_PFLAG_FPO = bit 0, set when FPO is in use.
             * (Later CV versions inverted this to CV_PFLAG_NOFPO; see
             * LANGAPI/INCLUDE/CVINFO.H — the CV4 NT 3.5 polarity is
             * 1 = "frame pointer omitted".) */
            int            has_fp  = (flags & 0x01) == 0;
            const unsigned char *st = rec + 33;
            unsigned long  va;

            st_to_cstr(st, name_buf, sizeof(name_buf));
            demangle(name_buf);
            va = seg_off_to_va(fseg, fofs);
            if (m) {
                cur_func = mod_add_func(m, name_buf, va, flen, ftypind,
                                        va + dbg_s, va + dbg_e,
                                        has_fp) + 1;
                scope_depth = 1;
            } else {
                add_symbol(name_buf, va, flen, 1);
            }
            break;
        }

        case S_BLOCK32:
        case S_WITH32:
            /* Lexical-scope opener; for MVP3 we track depth but don't
             * emit DW_TAG_lexical_block — locals nested in blocks are
             * just appended to the enclosing proc's children. */
            if (scope_depth > 0) scope_depth++;
            break;

        case S_END:
            if (scope_depth > 0) scope_depth--;
            if (scope_depth == 0) cur_func = 0;
            break;

        case S_BPREL32: {
            long           off_bp  = *(long *)(rec + 0);
            unsigned short ti      = *(unsigned short *)(rec + 4);
            st_to_cstr(rec + 6, name_buf, sizeof(name_buf));
            if (m && cur_func) {
                func_add_local(m, cur_func - 1, name_buf, off_bp,
                               -1, off_bp > 0, ti);
            }
            break;
        }

        case S_REGREL32: {
            long           off_r  = *(long *)(rec + 0);
            unsigned short ti     = *(unsigned short *)(rec + 4);
            unsigned short cv_r   = *(unsigned short *)(rec + 6);
            int            dr     = cv_to_dwarf_reg(cv_r);
            st_to_cstr(rec + 8, name_buf, sizeof(name_buf));
            if (m && cur_func && dr >= 0) {
                func_add_local(m, cur_func - 1, name_buf, off_r, dr, 0, ti);
            }
            break;
        }

        case S_REGISTER: {
            unsigned short ti     = *(unsigned short *)(rec + 0);
            unsigned short cv_r   = *(unsigned short *)(rec + 2);
            int            dr     = cv_to_dwarf_reg(cv_r);
            st_to_cstr(rec + 4, name_buf, sizeof(name_buf));
            if (m && cur_func && dr >= 0) {
                func_add_local(m, cur_func - 1, name_buf, 0, dr, 0, ti);
            }
            break;
        }

        case S_PUB32: {
            /* PUBSYM32 layout (after rectyp):
             *   off (4), seg (2), typind (2), name (ST) */
            unsigned long  pofs = *(unsigned long *)(rec + 0);
            unsigned short pseg = *(unsigned short *)(rec + 4);
            const unsigned char *st = rec + 8;
            unsigned long va;
            unsigned long k;
            int dup = 0;

            st_to_cstr(st, name_buf, sizeof(name_buf));
            demangle(name_buf);
            va = seg_off_to_va(pseg, pofs);
            /* Skip if a per-module S_LPROC32/S_GPROC32 already added a
             * symbol at this VA (mod_add_func feeds the global syms[]
             * table too).  Otherwise gdb sees two ELF symbols at the
             * same address — `b <name>` reports "(2 locations)" and
             * the CU lookup picks the size-zero S_PUB32 stub, losing
             * line/source association. */
            for (k = 0; k < n_syms; k++) {
                if (syms[k].va == va && syms[k].is_func) { dup = 1; break; }
            }
            if (!dup) add_symbol(name_buf, va, 0, 1);
            break;
        }

        default:
            break;
        }

        off += 2 + reclen;
    }
}

/* sstGlobalTypes parser (NB09 cvpacked C8.0 format).  Layout:
 *   [4 bytes] OMFTypeFlags
 *   [4 bytes] cnt — number of type records
 *   [cnt × 4 bytes] offset table — per-record offset from records_base
 *   [type records] each: reclen (2) + leaf (2) + payload (reclen-2)
 *
 * TI numbering: types[0] = TI 0x1000 (CV_FIRST_NONPRIM). */
static void parse_global_types(const unsigned char *p, unsigned long cb)
{
    unsigned long          flags, cnt;
    const unsigned long   *offsets;
    const unsigned char   *records_base;
    unsigned long          records_len;
    unsigned long          i;

    if (cb < 8) return;
    flags = *(unsigned long *)p;
    cnt   = *(unsigned long *)(p + 4);
    (void)flags;
    if (cb < 8 + cnt * 4) return;

    offsets       = (const unsigned long *)(p + 8);
    records_base  = p + 8 + cnt * 4;
    records_len   = cb - (8 + cnt * 4);

    n_types = cnt;
    types   = (CvTypeEntry *)calloc(n_types ? n_types : 1, sizeof(CvTypeEntry));
    if (!types) { fputs("dbg2dwf: oom types\n", stderr); exit(2); }

    for (i = 0; i < cnt; i++) {
        unsigned long  off = offsets[i];
        unsigned short reclen, leaf;
        if (off + 4 > records_len) continue;
        reclen = *(unsigned short *)(records_base + off);
        leaf   = *(unsigned short *)(records_base + off + 2);
        if (reclen < 2 || off + 2 + reclen > records_len) continue;
        types[i].leaf        = leaf;
        types[i].payload     = records_base + off + 4;
        types[i].payload_len = (unsigned long)reclen - 2;
        types[i].dwarf_off   = 0;
    }
}

/* Walk one OMFDirEntry: dispatch by SubSection. */
static void process_dir_entry(const unsigned char *cv_base, unsigned long cv_cb,
                              const OMFDirEntry *de)
{
    const unsigned char *p;
    unsigned long cb;

    if (de->lfo + de->cb > cv_cb) return;
    p  = cv_base + de->lfo;
    cb = de->cb;

    switch (de->SubSection) {
    case sstModule:
        parse_module(p, cb, de->iMod);
        break;
    case sstAlignSym:
    case sstSymbols:
        parse_alignsym(p, cb, de->iMod);
        break;
    case sstSrcModule:
        parse_srcmodule(p, cb, de->iMod);
        break;
    case sstSegMap:
        /* Skipped — sstSegMap describes CV logical segments
         * (offset-within-physical-frame), not RVAs.  We get the real
         * seg-→-RVA mapping from the .DBG's IMAGE_SECTION_HEADER table
         * already populated by load_dbg_codeview(). */
        break;
    case sstGlobalPub:
        /* Global publics blob — preceded by an OMFSymHash header:
         *   ushort symhash    (offset 0)
         *   ushort addrhash   (offset 2)
         *   ulong  cbSymbol   (offset 4)   ← length of the symbol stream
         *   ulong  cbHSym     (offset 8)
         *   ulong  cbHAddr    (offset 12)
         * Symbols follow at offset 16; hash tables follow them but
         * we don't need those. */
        if (cb > 16) {
            unsigned long cb_sym = *(unsigned long *)(p + 4);
            if (cb_sym <= cb - 16) parse_alignsym(p + 16, cb_sym, 0);
        }
        break;
    case sstGlobalTypes:
        parse_global_types(p, cb);
        break;
    default:
        break;
    }
}

static void walk_codeview(const unsigned char *cv_base, unsigned long cv_cb)
{
    unsigned long lfo_dir;
    OMFDirHeader  hdr;
    unsigned long i;

    /* The blob layout is "<sig>" (4 bytes), lfoDir (4 bytes), then data,
     * with the directory at lfoDir.  Some images have additional bytes
     * before lfoDir; for our cvpacked NB09 case, lfoDir lives at offset 4. */
    if (cv_cb < 8) return;
    lfo_dir = *(unsigned long *)(cv_base + 4);
    if (lfo_dir + sizeof(hdr) > cv_cb) return;
    memcpy(&hdr, cv_base + lfo_dir, sizeof(hdr));
    if (hdr.cDir == 0 ||
        lfo_dir + sizeof(hdr) + (unsigned long)hdr.cDir * sizeof(OMFDirEntry)
            > cv_cb) {
        return;
    }

    /* First pass: segmap + modules so they're indexed before we walk
     * symbols / lines that reference them. */
    for (i = 0; i < hdr.cDir; i++) {
        OMFDirEntry de;
        memcpy(&de, cv_base + lfo_dir + sizeof(hdr) + i * sizeof(de),
               sizeof(de));
        if (de.SubSection == sstSegMap || de.SubSection == sstModule) {
            process_dir_entry(cv_base, cv_cb, &de);
        }
    }
    /* Second pass: everything else. */
    for (i = 0; i < hdr.cDir; i++) {
        OMFDirEntry de;
        memcpy(&de, cv_base + lfo_dir + sizeof(hdr) + i * sizeof(de),
               sizeof(de));
        if (de.SubSection != sstSegMap && de.SubSection != sstModule) {
            process_dir_entry(cv_base, cv_cb, &de);
        }
    }
}

/* ----------------------------------------------------------------
 * DWARF emit.
 * ---------------------------------------------------------------- */

#define DW_TAG_array_type        0x01
#define DW_TAG_enumeration_type  0x04
#define DW_TAG_formal_parameter  0x05
#define DW_TAG_member            0x0d
#define DW_TAG_pointer_type      0x0f
#define DW_TAG_compile_unit      0x11
#define DW_TAG_structure_type    0x13
#define DW_TAG_subroutine_type   0x15
#define DW_TAG_typedef           0x16
#define DW_TAG_union_type        0x17
#define DW_TAG_subrange_type     0x21
#define DW_TAG_base_type         0x24
#define DW_TAG_const_type        0x26
#define DW_TAG_enumerator        0x28
#define DW_TAG_subprogram        0x2e
#define DW_TAG_variable          0x34
#define DW_TAG_volatile_type     0x35
#define DW_TAG_unspecified_type  0x3b

#define DW_AT_location           0x02
#define DW_AT_name               0x03
#define DW_AT_byte_size          0x0b
#define DW_AT_stmt_list          0x10
#define DW_AT_low_pc             0x11
#define DW_AT_high_pc            0x12
#define DW_AT_language           0x13
#define DW_AT_const_value        0x1c
#define DW_AT_comp_dir           0x1b
#define DW_AT_upper_bound        0x2f
#define DW_AT_producer           0x25
#define DW_AT_prototyped         0x27
#define DW_AT_data_member_location 0x38
#define DW_AT_external           0x3f
#define DW_AT_frame_base         0x40
#define DW_AT_encoding           0x3e
#define DW_AT_type               0x49

#define DW_OP_reg5               0x55  /* unused — kept for reference */
#define DW_OP_reg6               0x56  /* x86-64 RBP (== i386 EBP low half) */
#define DW_OP_breg6              0x76
#define DW_OP_breg7              0x77
#define DW_OP_deref_size         0x94
#define DW_OP_fbreg              0x91

/* DWARF Call Frame Information opcodes (DWARF 2 + DWARF 3 extensions). */
#define DW_CFA_advance_loc       0x40       /* high 2 bits = opcode, low 6 = delta */
#define DW_CFA_offset            0x80       /* high 2 = opcode, low 6 = reg */
#define DW_CFA_nop               0x00
#define DW_CFA_advance_loc1      0x02
#define DW_CFA_advance_loc2      0x03
#define DW_CFA_advance_loc4      0x04
#define DW_CFA_def_cfa           0x0c
#define DW_CFA_def_cfa_expression 0x0f
#define DW_CFA_expression        0x10
#define DW_CFA_val_expression    0x16

#define DW_ATE_address           0x01
#define DW_ATE_boolean           0x02
#define DW_ATE_float             0x04
#define DW_ATE_signed            0x05
#define DW_ATE_signed_char       0x06
#define DW_ATE_unsigned          0x07
#define DW_ATE_unsigned_char     0x08

#define DW_FORM_addr         0x01
#define DW_FORM_data2        0x05
#define DW_FORM_data4        0x06
#define DW_FORM_string       0x08
#define DW_FORM_block1       0x0a
#define DW_FORM_data1        0x0b
#define DW_FORM_flag         0x0c
#define DW_FORM_sdata        0x0d
#define DW_FORM_strp         0x0e
#define DW_FORM_udata        0x0f
#define DW_FORM_ref_addr     0x10
#define DW_FORM_ref4         0x13

#define DW_LNS_copy              1
#define DW_LNS_advance_pc        2
#define DW_LNS_advance_line      3
#define DW_LNS_set_file          4
#define DW_LNS_const_add_pc      8
#define DW_LNS_fixed_advance_pc  9

#define DW_LNE_end_sequence      1
#define DW_LNE_set_address       2

#define DW_LANG_C89              0x0001

/* Sort line rows primarily by seq_id so the DWARF emitter sees rows
 * grouped per CV4 (file,seg) block, and secondarily by VA within a
 * group so the state-machine sequence is monotonic (gdb requires that
 * within a sequence).  qsort callback must be __cdecl regardless of
 * /Gz default. */
static int __cdecl cmp_linerow(const void *a, const void *b)
{
    const LineRow *la = (const LineRow *)a;
    const LineRow *lb = (const LineRow *)b;
    if (la->seq_id < lb->seq_id) return -1;
    if (la->seq_id > lb->seq_id) return 1;
    if (la->va < lb->va) return -1;
    if (la->va > lb->va) return 1;
    return 0;
}

static void emit_debug_line_for_module(Buf *line_buf, Module *m)
{
    unsigned long unit_start, len_off, header_start, header_len_off;
    unsigned long header_payload, prog_start;
    unsigned long unit_len, header_len;
    unsigned long i;
    unsigned long cur_va, cur_line, cur_file;
    int           is_first;
    static const unsigned char std_op_lengths[] = {
        0, /* DW_LNS_copy */
        1, /* DW_LNS_advance_pc */
        1, /* DW_LNS_advance_line */
        1, /* DW_LNS_set_file */
        1, /* DW_LNS_set_column */
        0, /* DW_LNS_negate_stmt */
        0, /* DW_LNS_set_basic_block */
        0, /* DW_LNS_const_add_pc */
        1, /* DW_LNS_fixed_advance_pc — 1 fixed-2-byte arg, but ULEB count
              field still says "1 operand" */
    };

    if (m->n_lines == 0) {
        m->line_program_off = line_buf->len;
        return;
    }
    qsort(m->lines, m->n_lines, sizeof(LineRow), cmp_linerow);
    m->line_program_off = line_buf->len;

    /* unit_length placeholder. */
    unit_start  = line_buf->len;
    len_off     = unit_start;
    buf_u32(line_buf, 0);
    /* version */
    buf_u16(line_buf, 2);
    /* header_length placeholder. */
    header_start   = line_buf->len;
    header_len_off = line_buf->len;
    buf_u32(line_buf, 0);
    header_payload = line_buf->len;

    /* minimum_instruction_length, default_is_stmt, line_base, line_range,
     * opcode_base. */
    buf_u8(line_buf, 1);    /* min_inst_len: 1 byte (i386 variable-length) */
    buf_u8(line_buf, 1);    /* default_is_stmt: yes */
    buf_u8(line_buf, 1);    /* line_base = 1 (signed) */
    buf_u8(line_buf, 1);    /* line_range = 1 — disables special opcodes,
                               we explicitly emit advance_pc + advance_line */
    buf_u8(line_buf, 10);   /* opcode_base: 10 standard ops + 0-th reserved */
    /* standard_opcode_lengths[opcode_base - 1] = 9 entries. */
    buf_put(line_buf, std_op_lengths, sizeof(std_op_lengths));

    /* include_directories: collect unique parent dirs across all of
     * this module's files, then emit them.  file_names then references
     * dirs by 1-based index (0 = "current dir", which gdb fills in
     * from DW_AT_comp_dir). */
    {
        char         **dirs;
        unsigned long *file_dir_idx;
        unsigned long  n_dirs = 0;
        unsigned long  j;
        char           dir_buf[1024];
        char           base_buf[512];

        dirs         = (char **)calloc(m->n_files ? m->n_files : 1,
                                       sizeof(char *));
        file_dir_idx = (unsigned long *)calloc(m->n_files ? m->n_files : 1,
                                               sizeof(unsigned long));

        for (i = 0; i < m->n_files; i++) {
            path_split(m->files[i], dir_buf, sizeof(dir_buf),
                       base_buf, sizeof(base_buf));
            if (dir_buf[0] == '\0') {
                file_dir_idx[i] = 0;
                continue;
            }
            for (j = 0; j < n_dirs; j++) {
                if (strcmp(dirs[j], dir_buf) == 0) break;
            }
            if (j == n_dirs) {
                dirs[j] = (char *)malloc(strlen(dir_buf) + 1);
                if (!dirs[j]) { fputs("oom dirs\n", stderr); exit(2); }
                strcpy(dirs[j], dir_buf);
                n_dirs++;
            }
            file_dir_idx[i] = j + 1;     /* 1-based */
        }

        for (j = 0; j < n_dirs; j++) buf_str(line_buf, dirs[j]);
        buf_u8(line_buf, 0);  /* include_directories terminator */

        for (i = 0; i < m->n_files; i++) {
            path_split(m->files[i], dir_buf, sizeof(dir_buf),
                       base_buf, sizeof(base_buf));
            buf_str (line_buf, base_buf);
            buf_uleb(line_buf, file_dir_idx[i]);
            buf_uleb(line_buf, 0);  /* mtime */
            buf_uleb(line_buf, 0);  /* length */
        }
        buf_u8(line_buf, 0);  /* file_names terminator */

        for (j = 0; j < n_dirs; j++) free(dirs[j]);
        free(dirs);
        free(file_dir_idx);
    }

    /* Now we know the header length — patch it. */
    header_len = line_buf->len - header_payload;
    buf_patch_u32(line_buf, header_len_off, header_len);

    /* ----- Line program ----- */
    prog_start = line_buf->len;
    cur_va     = 0;
    cur_line   = 1;
    cur_file   = 1;
    is_first   = 1;

    {
        unsigned long cur_seq = 0;
        for (i = 0; i < m->n_lines; i++) {
            LineRow *r = &m->lines[i];

            /* New CV4 (file,seg) block — close any open sequence and
             * start a fresh one rooted at this row's VA.  Without this
             * the last line of one block sticks across the gap to the
             * next block (handle.obj contributes to several disjoint
             * regions; gdb otherwise reports the wrong file at any
             * address inside the gap). */
            if (is_first || r->seq_id != cur_seq) {
                if (!is_first) {
                    buf_u8 (line_buf, 0);
                    buf_uleb(line_buf, 1);
                    buf_u8 (line_buf, DW_LNE_end_sequence);
                }
                buf_u8 (line_buf, 0);             /* DW_LNS_extended_op */
                buf_uleb(line_buf, 1 + 4);        /* size of payload */
                buf_u8 (line_buf, DW_LNE_set_address);
                buf_u32(line_buf, r->va);
                cur_va   = r->va;
                cur_line = 1;
                cur_file = 1;
                cur_seq  = r->seq_id;
                is_first = 0;
            } else if (r->va > cur_va) {
                buf_u8 (line_buf, DW_LNS_advance_pc);
                buf_uleb(line_buf, r->va - cur_va);
                cur_va = r->va;
            }
            if (r->file_id != cur_file) {
                buf_u8 (line_buf, DW_LNS_set_file);
                buf_uleb(line_buf, r->file_id);
                cur_file = r->file_id;
            }
            if ((long)r->line != (long)cur_line) {
                buf_u8 (line_buf, DW_LNS_advance_line);
                buf_sleb(line_buf, (long)r->line - (long)cur_line);
                cur_line = r->line;
            }
            buf_u8(line_buf, DW_LNS_copy);
        }
    }

    /* End sequence. */
    buf_u8 (line_buf, 0);
    buf_uleb(line_buf, 1);
    buf_u8 (line_buf, DW_LNE_end_sequence);

    (void)prog_start;

    /* Patch unit_length: bytes from after the length field to end. */
    unit_len = line_buf->len - (unit_start + 4);
    buf_patch_u32(line_buf, len_off, unit_len);
}

/* Abbrev codes (single shared table — DWARF lets multiple CUs reuse it):
 *   1  source CU compile_unit       7  pointer_type
 *   2  subprogram (frame_base+type) 8  structure_type (children)
 *   3  formal_parameter (named)     9  member
 *   4  variable                     10 union_type (children)
 *   5  types-only compile_unit      11 array_type (children)
 *   6  base_type                    12 subrange_type
 *   13 enumeration_type (children)  14 enumerator
 *   15 const_type                   16 volatile_type
 *   17 subroutine_type (children)   18 unspecified_type
 *   19 formal_parameter (anon)      20 typedef
 */
static void emit_debug_abbrev(Buf *abbrev)
{
    /* 1: DW_TAG_compile_unit (source) — deliberately NO DW_AT_low_pc /
     * DW_AT_high_pc on the CU.  When the CU advertises a single PC range
     * gdb (gdb/dwarf2/read.c:scan_partial_symbols, SET_ADDRMAP gating)
     * skips building per-subprogram addrmap entries and falls back to
     * "smallest CU range wins" lookup.  Module code is non-contiguous
     * (paged + non-paged + init), so each CU's (min,max) envelope
     * overlaps several others — gdb picks the wrong CU and the line
     * lookup fails ("No line number information for address ...",
     * "Line N is at <addr> but contains no code").  Without CU PC info
     * gdb walks subprograms instead and builds an exact addrmap. */
    buf_uleb(abbrev, 1);
    buf_uleb(abbrev, DW_TAG_compile_unit);
    buf_u8  (abbrev, 1);
    buf_uleb(abbrev, DW_AT_name);       buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_comp_dir);   buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_language);   buf_uleb(abbrev, DW_FORM_data2);
    buf_uleb(abbrev, DW_AT_stmt_list);  buf_uleb(abbrev, DW_FORM_data4);
    buf_uleb(abbrev, DW_AT_producer);   buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 2: DW_TAG_subprogram */
    buf_uleb(abbrev, 2);
    buf_uleb(abbrev, DW_TAG_subprogram);
    buf_u8  (abbrev, 1);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_low_pc);      buf_uleb(abbrev, DW_FORM_addr);
    buf_uleb(abbrev, DW_AT_high_pc);     buf_uleb(abbrev, DW_FORM_addr);
    buf_uleb(abbrev, DW_AT_external);    buf_uleb(abbrev, DW_FORM_flag);
    buf_uleb(abbrev, DW_AT_frame_base);  buf_uleb(abbrev, DW_FORM_block1);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 3: DW_TAG_formal_parameter (named).  DW_AT_location uses
     * DW_FORM_data4 = offset into .debug_loc (a location list scoped
     * to [body_start, body_end) — the PC range where EBP is a valid
     * frame pointer per CV4's PROCSYM32.DbgStart/DbgEnd). */
    buf_uleb(abbrev, 3);
    buf_uleb(abbrev, DW_TAG_formal_parameter);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_location);    buf_uleb(abbrev, DW_FORM_data4);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 4: DW_TAG_variable */
    buf_uleb(abbrev, 4);
    buf_uleb(abbrev, DW_TAG_variable);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_location);    buf_uleb(abbrev, DW_FORM_data4);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 5: DW_TAG_compile_unit (types-only) */
    buf_uleb(abbrev, 5);
    buf_uleb(abbrev, DW_TAG_compile_unit);
    buf_u8  (abbrev, 1);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_language);    buf_uleb(abbrev, DW_FORM_data2);
    buf_uleb(abbrev, DW_AT_producer);    buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 6: DW_TAG_base_type */
    buf_uleb(abbrev, 6);
    buf_uleb(abbrev, DW_TAG_base_type);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_byte_size);   buf_uleb(abbrev, DW_FORM_data1);
    buf_uleb(abbrev, DW_AT_encoding);    buf_uleb(abbrev, DW_FORM_data1);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 7: DW_TAG_pointer_type */
    buf_uleb(abbrev, 7);
    buf_uleb(abbrev, DW_TAG_pointer_type);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_byte_size);   buf_uleb(abbrev, DW_FORM_data1);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 8: DW_TAG_structure_type */
    buf_uleb(abbrev, 8);
    buf_uleb(abbrev, DW_TAG_structure_type);
    buf_u8  (abbrev, 1);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_byte_size);   buf_uleb(abbrev, DW_FORM_udata);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 9: DW_TAG_member */
    buf_uleb(abbrev, 9);
    buf_uleb(abbrev, DW_TAG_member);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_data_member_location); buf_uleb(abbrev, DW_FORM_udata);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 10: DW_TAG_union_type */
    buf_uleb(abbrev, 10);
    buf_uleb(abbrev, DW_TAG_union_type);
    buf_u8  (abbrev, 1);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_byte_size);   buf_uleb(abbrev, DW_FORM_udata);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 11: DW_TAG_array_type */
    buf_uleb(abbrev, 11);
    buf_uleb(abbrev, DW_TAG_array_type);
    buf_u8  (abbrev, 1);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 12: DW_TAG_subrange_type */
    buf_uleb(abbrev, 12);
    buf_uleb(abbrev, DW_TAG_subrange_type);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, DW_AT_upper_bound); buf_uleb(abbrev, DW_FORM_udata);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 13: DW_TAG_enumeration_type */
    buf_uleb(abbrev, 13);
    buf_uleb(abbrev, DW_TAG_enumeration_type);
    buf_u8  (abbrev, 1);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_byte_size);   buf_uleb(abbrev, DW_FORM_udata);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 14: DW_TAG_enumerator */
    buf_uleb(abbrev, 14);
    buf_uleb(abbrev, DW_TAG_enumerator);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_const_value); buf_uleb(abbrev, DW_FORM_sdata);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 15: DW_TAG_const_type */
    buf_uleb(abbrev, 15);
    buf_uleb(abbrev, DW_TAG_const_type);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 16: DW_TAG_volatile_type */
    buf_uleb(abbrev, 16);
    buf_uleb(abbrev, DW_TAG_volatile_type);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 17: DW_TAG_subroutine_type */
    buf_uleb(abbrev, 17);
    buf_uleb(abbrev, DW_TAG_subroutine_type);
    buf_u8  (abbrev, 1);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 18: DW_TAG_unspecified_type */
    buf_uleb(abbrev, 18);
    buf_uleb(abbrev, DW_TAG_unspecified_type);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 19: DW_TAG_formal_parameter (anonymous — for proc types) */
    buf_uleb(abbrev, 19);
    buf_uleb(abbrev, DW_TAG_formal_parameter);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* 20: DW_TAG_typedef */
    buf_uleb(abbrev, 20);
    buf_uleb(abbrev, DW_TAG_typedef);
    buf_u8  (abbrev, 0);
    buf_uleb(abbrev, DW_AT_name);        buf_uleb(abbrev, DW_FORM_strp);
    buf_uleb(abbrev, DW_AT_type);        buf_uleb(abbrev, DW_FORM_ref_addr);
    buf_uleb(abbrev, 0);  buf_uleb(abbrev, 0);

    /* End of abbrev table. */
    buf_uleb(abbrev, 0);
}

/* Encode a SLEB128 into out, returning the byte count. */
static int encode_sleb_inplace(long v, unsigned char *out)
{
    int n = 0;
    int more = 1;
    unsigned char byte;
    int sign;

    while (more) {
        byte = (unsigned char)(v & 0x7f);
        v >>= 7;
        sign = byte & 0x40;
        if ((v == 0 && !sign) || (v == -1L && sign)) more = 0;
        else byte |= 0x80;
        out[n++] = byte;
    }
    return n;
}

/* Build a DWARF location expression for a local: returns byte count
 * written into out (max ~16 bytes for our cases). */
static int encode_location(const Local *l, unsigned char *out)
{
    int n = 0;

    if (l->dwarf_reg < 0) {
        /* BP-relative: DW_OP_fbreg <SLEB>.  Frame_base on the parent
         * subprogram DIE is DW_OP_reg6 (x86-64 RBP, whose low half is
         * the i386 EBP we want), so DW_OP_fbreg <off> = RBP+off,
         * matching CV's BPRel convention. */
        out[n++] = DW_OP_fbreg;
        n += encode_sleb_inplace(l->off, out + n);
    } else if (l->off == 0) {
        /* Plain register: DW_OP_reg<N> = 0x50 + N (for N in 0..31). */
        out[n++] = (unsigned char)(0x50 + l->dwarf_reg);
    } else {
        /* Reg-relative: DW_OP_breg<N> <SLEB> = 0x70 + N then offset. */
        out[n++] = (unsigned char)(0x70 + l->dwarf_reg);
        n += encode_sleb_inplace(l->off, out + n);
    }
    return n;
}

/* Pick a CU "primary source" path: the first source file the line
 * table mentions if any, falling back to the .obj name.  gdb uses
 * DW_AT_name + DW_AT_comp_dir to identify the source file when the
 * user types `b file.c:N`. */
static const char *cu_source_name(Module *m)
{
    if (m->n_files > 0) return m->files[0];
    return m->name;
}

/* Read a CV numeric leaf at p, returning its value and bytes consumed.
 * Numeric leaves: short < LF_NUMERIC (0x8000) is the literal value;
 * otherwise leaf = LF_CHAR/SHORT/USHORT/LONG/ULONG with payload. */
static unsigned long extract_numeric(const unsigned char *p,
                                     unsigned long *out_size,
                                     unsigned long *out_consumed)
{
    unsigned short leaf = *(unsigned short *)p;
    if (leaf < 0x8000) {
        *out_size = leaf;
        *out_consumed = 2;
        return leaf;
    }
    switch (leaf) {
        case 0x8000:  /* LF_CHAR */
            *out_size = (unsigned long)(long)*(signed char *)(p + 2);
            *out_consumed = 3;
            break;
        case 0x8001:  /* LF_SHORT */
            *out_size = (unsigned long)(long)*(signed short *)(p + 2);
            *out_consumed = 4;
            break;
        case 0x8002:  /* LF_USHORT */
            *out_size = *(unsigned short *)(p + 2);
            *out_consumed = 4;
            break;
        case 0x8003:  /* LF_LONG */
            *out_size = (unsigned long)*(long *)(p + 2);
            *out_consumed = 6;
            break;
        case 0x8004:  /* LF_ULONG */
            *out_size = *(unsigned long *)(p + 2);
            *out_consumed = 6;
            break;
        default:
            *out_size = 0;
            *out_consumed = 2;
            break;
    }
    return *out_size;
}

/* Resolve a CV TI to its absolute .debug_info offset.  Returns
 * void_off as a fallback for primitives we don't model, unknown TIs,
 * or types whose DIE hasn't been emitted yet (forward refs). */
static unsigned long type_offset_for_ti(unsigned long ti)
{
    unsigned long off;
    if (ti == 0) return void_off;
    if (ti < ti_min) {
        off = prim_off[ti & 0xfff];
        return off ? off : void_off;
    }
    if (ti - ti_min < n_types) {
        off = types[ti - ti_min].dwarf_off;
        return off ? off : void_off;
    }
    return void_off;
}

/* Forward-ref patch list.  When a DW_FORM_ref_addr to a type whose
 * DIE hasn't been emitted yet, we write a placeholder and remember
 * (file_offset, ti) so apply_type_patches can rewrite it after all
 * type DIEs are emitted. */
typedef struct {
    unsigned long off;
    unsigned long ti;
} TypeRefPatch;

static TypeRefPatch  *patches     = NULL;
static unsigned long  n_patches   = 0;
static unsigned long  cap_patches = 0;

static void buf_type_ref(Buf *info, unsigned long ti)
{
    unsigned long off;

    if (ti >= ti_min && ti - ti_min < n_types &&
        types[ti - ti_min].dwarf_off == 0) {
        /* Forward ref — record patch and emit placeholder. */
        if (n_patches + 1 > cap_patches) {
            cap_patches = cap_patches ? cap_patches * 2 : 64;
            patches = (TypeRefPatch *)realloc(patches,
                       cap_patches * sizeof(TypeRefPatch));
            if (!patches) { fputs("oom patches\n", stderr); exit(2); }
        }
        patches[n_patches].off = info->len;
        patches[n_patches].ti  = ti;
        n_patches++;
        buf_u32(info, 0);
        return;
    }
    off = type_offset_for_ti(ti);
    buf_u32(info, off);
}

static void apply_type_patches(Buf *info)
{
    unsigned long i;
    for (i = 0; i < n_patches; i++) {
        unsigned long off = type_offset_for_ti(patches[i].ti);
        if (off == 0) off = void_off;
        buf_patch_u32(info, patches[i].off, off);
    }
}

/* Emit a single DW_TAG_base_type DIE, returning its abs offset. */
static unsigned long emit_base_type(Buf *info, Strtab *dstr,
                                    const char *name,
                                    unsigned char byte_size,
                                    unsigned char encoding)
{
    unsigned long here = info->len;
    buf_uleb(info, 6);  /* base_type abbrev */
    buf_u32(info, strtab_add(dstr, name));
    buf_u8 (info, byte_size);
    buf_u8 (info, encoding);
    return here;
}

/* Emit a DW_TAG_pointer_type pointing at an already-emitted target. */
static unsigned long emit_pointer_to(Buf *info, unsigned long target_off)
{
    unsigned long here = info->len;
    buf_uleb(info, 7);
    buf_u8 (info, 4);
    buf_u32(info, target_off);
    return here;
}

/* Walk an LF_FIELDLIST referenced by fl_ti, emitting DW_TAG_member /
 * DW_TAG_enumerator children for each entry.  Pads (0xf0..0xff) and
 * unknown leaves are skipped. */
static void emit_field_members(Buf *info, Strtab *dstr,
                               unsigned long fl_ti, int as_enumerators)
{
    CvTypeEntry *e;
    const unsigned char *p, *end;
    char name_buf[256];

    if (fl_ti < ti_min || fl_ti - ti_min >= n_types) return;
    e = &types[fl_ti - ti_min];
    if (e->leaf != 0x0204 /* LF_FIELDLIST */) return;

    p   = e->payload;
    end = p + e->payload_len;

    while (p < end) {
        /* Pad bytes (0xf0..0xff) align next sub-record to 4 bytes. */
        while (p < end && (*p & 0xf0) == 0xf0) p++;
        if (p + 2 > end) break;

        {
            unsigned short leaf = *(unsigned short *)p;
            p += 2;

            if (leaf == 0x0406 /* LF_MEMBER */) {
                /* lfMember after leaf: index(2), attr(2), numeric (offset),
                 * name (ST). */
                unsigned short attr;
                unsigned short idx;
                unsigned long  off_val, consumed;
                unsigned char  nlen;

                if (p + 4 > end) break;
                idx  = *(unsigned short *)p; p += 2;
                attr = *(unsigned short *)p; (void)attr; p += 2;
                if (p + 2 > end) break;
                extract_numeric(p, &off_val, &consumed);
                p += consumed;
                if (p >= end) break;
                nlen = *p; p++;
                if (p + nlen > end) break;
                memcpy(name_buf, p, nlen);
                name_buf[nlen] = '\0';
                p += nlen;

                if (!as_enumerators) {
                    buf_uleb(info, 9);  /* DW_TAG_member */
                    buf_u32(info, strtab_add(dstr, name_buf));
                    buf_uleb(info, off_val);
                    buf_type_ref(info, (idx));
                }
            } else if (leaf == 0x0403 /* LF_ENUMERATE */) {
                /* attr (2), value (numeric leaf), name (ST) */
                unsigned short attr;
                unsigned long  val, consumed;
                unsigned char  nlen;

                if (p + 2 > end) break;
                attr = *(unsigned short *)p; (void)attr; p += 2;
                if (p + 2 > end) break;
                extract_numeric(p, &val, &consumed);
                p += consumed;
                if (p >= end) break;
                nlen = *p; p++;
                if (p + nlen > end) break;
                memcpy(name_buf, p, nlen);
                name_buf[nlen] = '\0';
                p += nlen;

                if (as_enumerators) {
                    buf_uleb(info, 14);  /* DW_TAG_enumerator */
                    buf_u32(info, strtab_add(dstr, name_buf));
                    buf_sleb(info, (long)val);
                }
            } else {
                /* LF_BCLASS, LF_VBCLASS, LF_METHOD, LF_NESTTYPE, etc.
                 * — skip; we don't model C++ inheritance / methods /
                 * static members for MVP4.  Without knowing the leaf's
                 * size we have to bail out of the rest of the list. */
                break;
            }
        }
    }
}

/* Emit one user-defined type's DIE.  Sets types[idx].dwarf_off to the
 * resulting absolute offset.  Recursive only via type_offset_for_ti(),
 * which never re-enters emit (it just reads previously-set offsets). */
static void emit_one_user_type(Buf *info, Strtab *dstr, unsigned long idx)
{
    CvTypeEntry *e = &types[idx];
    const unsigned char *p = e->payload;
    unsigned long  consumed;
    char name_buf[256];

    e->dwarf_off = info->len;

    switch (e->leaf) {

    case 0x0001: {  /* LF_MODIFIER */
        /* type (2), attr (2): bit 0=const, bit 1=volatile */
        unsigned short utype = *(unsigned short *)(p + 0);
        unsigned short attr  = *(unsigned short *)(p + 2);
        unsigned long  off_underlying = type_offset_for_ti(utype);
        if (attr & 0x1) {
            buf_uleb(info, 15);  /* const_type */
            if (attr & 0x2) {
                /* nest volatile under const */
                unsigned long volatile_off = info->len + 5;
                buf_u32(info, volatile_off);
                buf_uleb(info, 16);  /* volatile_type */
                buf_u32(info, off_underlying);
            } else {
                buf_u32(info, off_underlying);
            }
        } else if (attr & 0x2) {
            buf_uleb(info, 16);
            buf_u32(info, off_underlying);
        } else {
            buf_uleb(info, 20);  /* typedef (no qualifier) */
            buf_u32(info, strtab_add(dstr, ""));
            buf_u32(info, off_underlying);
        }
        break;
    }

    case 0x0002: {  /* LF_POINTER */
        /* lfPointer after leaf: attr(2 — bit fields), utype(2),
         * optional pmem fields.  attr's bits give pointer-mode etc.;
         * we don't model member pointers, so just emit a generic
         * DW_TAG_pointer_type to utype. */
        unsigned short utype;
        if (e->payload_len < 4) { e->dwarf_off = void_off; return; }
        utype = *(unsigned short *)(p + 2);
        buf_uleb(info, 7);
        buf_u8 (info, 4);
        buf_type_ref(info, (utype));
        break;
    }

    case 0x0003: {  /* LF_ARRAY */
        /* elemtype (2), idxtype (2), data: numeric (size), name (ST) */
        unsigned short elemtype, idxtype;
        unsigned long  total_size;

        elemtype = *(unsigned short *)(p + 0);
        idxtype  = *(unsigned short *)(p + 2);
        (void)idxtype;
        if (e->payload_len < 6) { e->dwarf_off = void_off; return; }
        extract_numeric(p + 4, &total_size, &consumed);

        e->dwarf_off = info->len;
        buf_uleb(info, 11);
        buf_type_ref(info, (elemtype));

        buf_uleb(info, 12);
        buf_type_ref(info, (elemtype));   /* subrange index type */
        buf_uleb(info, total_size > 0 ? total_size - 1 : 0);

        buf_uleb(info, 0);  /* end array_type children */
        return;
    }

    case 0x0004:    /* LF_CLASS — treat as struct */
    case 0x0005: {  /* LF_STRUCTURE */
        /* lfStructure layout (CV4 pack(1)):
         *   count(2), field(2), property(2), derived(2), vshape(2),
         *   data: numeric (size), name (ST). */
        unsigned short field_ti;
        unsigned long  size;
        unsigned char  nlen;

        if (e->payload_len < 12) { e->dwarf_off = void_off; return; }
        field_ti = *(unsigned short *)(p + 2);
        extract_numeric(p + 10, &size, &consumed);
        nlen = *(p + 10 + consumed);
        if (nlen >= sizeof(name_buf)) nlen = sizeof(name_buf) - 1;
        memcpy(name_buf, p + 10 + consumed + 1, nlen);
        name_buf[nlen] = '\0';

        buf_uleb(info, 8);
        buf_u32(info, strtab_add(dstr, name_buf));
        buf_uleb(info, size);
        emit_field_members(info, dstr, field_ti, 0);
        buf_uleb(info, 0);
        return;
    }

    case 0x0006: {  /* LF_UNION */
        /* lfUnion: count(2), field(2), property(2), data: numeric+name */
        unsigned short field_ti;
        unsigned long  size;
        unsigned char  nlen;

        if (e->payload_len < 8) { e->dwarf_off = void_off; return; }
        field_ti = *(unsigned short *)(p + 2);
        extract_numeric(p + 6, &size, &consumed);
        nlen = *(p + 6 + consumed);
        if (nlen >= sizeof(name_buf)) nlen = sizeof(name_buf) - 1;
        memcpy(name_buf, p + 6 + consumed + 1, nlen);
        name_buf[nlen] = '\0';

        buf_uleb(info, 10);
        buf_u32(info, strtab_add(dstr, name_buf));
        buf_uleb(info, size);
        emit_field_members(info, dstr, field_ti, 0);
        buf_uleb(info, 0);
        return;
    }

    case 0x0007: {  /* LF_ENUM */
        /* lfEnum: count(2), utype(2), field(2), property(2), name (ST) */
        unsigned short utype, field_ti;
        unsigned char  nlen;

        if (e->payload_len < 9) { e->dwarf_off = void_off; return; }
        utype    = *(unsigned short *)(p + 2);
        field_ti = *(unsigned short *)(p + 4);
        nlen = *(p + 8);
        if (nlen >= sizeof(name_buf)) nlen = sizeof(name_buf) - 1;
        memcpy(name_buf, p + 9, nlen);
        name_buf[nlen] = '\0';

        buf_uleb(info, 13);
        buf_u32(info, strtab_add(dstr, name_buf));
        buf_uleb(info, 4);   /* assume int-sized */
        buf_type_ref(info, (utype));
        emit_field_members(info, dstr, field_ti, 1);
        buf_uleb(info, 0);
        return;
    }

    case 0x0008: {  /* LF_PROCEDURE */
        /* rvtype (2), calltype (1), reserved (1), parmcount (2),
         * arglist (2) */
        unsigned short rvtype = *(unsigned short *)(p + 0);
        buf_uleb(info, 17);
        buf_type_ref(info, (rvtype));
        /* Arg list expansion deferred — emit no children and close. */
        buf_uleb(info, 0);
        return;
    }

    case 0x0204:  /* LF_FIELDLIST — referenced inline by struct/union */
    case 0x0201:  /* LF_ARGLIST — referenced inline by procedure */
    default:
        /* Untyped placeholder so consumers get a valid ref. */
        e->dwarf_off = void_off;
        return;
    }
}

/* Emit the types-only CU at the start of .debug_info.  Returns nothing;
 * side effects: types[].dwarf_off and prim_off[] populated; void_off
 * set so downstream lookups have a sensible fallback. */
static void emit_types_cu(Buf *info, Strtab *dstr)
{
    unsigned long unit_start, len_off, unit_len;
    unsigned long i;

    /* Header: ref_addr offsets are absolute within .debug_info, so we
     * record offsets directly. */
    unit_start = info->len;
    len_off    = unit_start;
    buf_u32(info, 0);
    buf_u16(info, 2);
    buf_u32(info, 0);
    buf_u8 (info, 4);

    /* Types CU root DIE. */
    buf_uleb(info, 5);
    buf_u32(info, strtab_add(dstr, "<microNT-types>"));
    buf_u16(info, DW_LANG_C89);
    buf_u32(info, strtab_add(dstr, "MicroNT dbg2dwf"));

    /* Primitive base_types — the small set we can name; any other CV
     * primitive falls back to void_off via type_offset_for_ti. */
    void_off                = emit_base_type(info, dstr, "void",          1, 0);
    prim_off[0x03]          = void_off;
    prim_off[0x10]          = emit_base_type(info, dstr, "char",          1, DW_ATE_signed_char);
    prim_off[0x11]          = emit_base_type(info, dstr, "short",         2, DW_ATE_signed);
    prim_off[0x12]          = emit_base_type(info, dstr, "long",          4, DW_ATE_signed);
    prim_off[0x13]          = emit_base_type(info, dstr, "long long",     8, DW_ATE_signed);
    prim_off[0x20]          = emit_base_type(info, dstr, "unsigned char", 1, DW_ATE_unsigned_char);
    prim_off[0x21]          = emit_base_type(info, dstr, "unsigned short",2, DW_ATE_unsigned);
    prim_off[0x22]          = emit_base_type(info, dstr, "unsigned long", 4, DW_ATE_unsigned);
    prim_off[0x23]          = emit_base_type(info, dstr, "unsigned long long", 8, DW_ATE_unsigned);
    prim_off[0x40]          = emit_base_type(info, dstr, "float",         4, DW_ATE_float);
    prim_off[0x41]          = emit_base_type(info, dstr, "double",        8, DW_ATE_float);
    prim_off[0x70]          = emit_base_type(info, dstr, "char",          1, DW_ATE_signed_char);
    prim_off[0x71]          = emit_base_type(info, dstr, "wchar_t",       2, DW_ATE_unsigned);
    prim_off[0x72]          = emit_base_type(info, dstr, "short",         2, DW_ATE_signed);
    prim_off[0x73]          = emit_base_type(info, dstr, "unsigned short",2, DW_ATE_unsigned);
    prim_off[0x74]          = emit_base_type(info, dstr, "int",           4, DW_ATE_signed);
    prim_off[0x75]          = emit_base_type(info, dstr, "unsigned int",  4, DW_ATE_unsigned);

    /* 32-bit pointer variants (CV TI 0x04xx).  We emit them for the
     * primitives above; other base TIs fall back to void *. */
    {
        static const unsigned short ptr_bases[] = {
            0x03, 0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23,
            0x40, 0x41, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75
        };
        unsigned long n = sizeof(ptr_bases) / sizeof(ptr_bases[0]);
        unsigned long k;
        for (k = 0; k < n; k++) {
            unsigned short b = ptr_bases[k];
            unsigned long  base_off = prim_off[b];
            if (base_off == 0) base_off = void_off;
            prim_off[0x400 | b] = emit_pointer_to(info, base_off);
        }
    }

    /* User-defined types in TI order. */
    for (i = 0; i < n_types; i++) {
        emit_one_user_type(info, dstr, i);
    }

    /* Patch every DW_FORM_ref_addr that pointed at a not-yet-emitted
     * TI when first written.  After this loop, every type ref resolves
     * to a real DIE offset (or void_off for unmodelled cases). */
    apply_type_patches(info);

    /* End of types CU children. */
    buf_uleb(info, 0);

    unit_len = info->len - (unit_start + 4);
    buf_patch_u32(info, len_off, unit_len);
}

/* True if syms[i]'s VA already shows up in some module's funcs[].
 * Used to dedup globals against per-module subprograms when emitting
 * the synthetic globals CU. */
static int sym_va_in_module_funcs(unsigned long va)
{
    unsigned long i, j;
    for (i = 0; i < n_mods; i++) {
        Module *m = &mods[i];
        for (j = 0; j < m->n_funcs; j++) {
            if (m->funcs[j].va == va) return 1;
        }
    }
    return 0;
}

/* Emit a synthetic CU containing DW_TAG_subprogram entries for every
 * global-pub symbol that doesn't already appear in a module's funcs[].
 * Without this, exported functions whose CV records were collapsed to
 * S_PUB32 by cvpack (no per-module sstSymbols entry) only land in
 * .symtab — gdb's `b <name>` looks in DWARF subprograms first and
 * reports "Function not defined".  No locals / types attached; we
 * just need a name → VA binding gdb can resolve.  Functions with
 * size==0 (typical for S_PUB32) get high_pc = va+1 as a placeholder. */
static void emit_globals_cu(Buf *info, Strtab *dstr)
{
    unsigned long unit_start, len_off, unit_len;
    unsigned long i, n_emit = 0;

    for (i = 0; i < n_syms; i++) {
        if (syms[i].is_func && !sym_va_in_module_funcs(syms[i].va)) n_emit++;
    }
    if (n_emit == 0) return;

    unit_start = info->len;
    len_off    = unit_start;
    buf_u32(info, 0);
    buf_u16(info, 2);
    buf_u32(info, 0);
    buf_u8 (info, 4);

    /* Reuse types-only-CU abbrev (5): name + language + producer. */
    buf_uleb(info, 5);
    buf_u32(info, strtab_add(dstr, "<microNT-globals>"));
    buf_u16(info, DW_LANG_C89);
    buf_u32(info, strtab_add(dstr, "MicroNT dbg2dwf"));

    for (i = 0; i < n_syms; i++) {
        Symbol *s = &syms[i];
        unsigned long high;
        if (!s->is_func) continue;
        if (sym_va_in_module_funcs(s->va)) continue;
        high = s->size > 0 ? s->va + s->size : s->va + 1;
        buf_uleb(info, 2);                           /* subprogram */
        buf_u32(info, strtab_add(dstr, s->name));
        buf_u32(info, s->va);
        buf_u32(info, high);
        buf_u8 (info, 1);                            /* external */
        buf_u8 (info, 1);                            /* frame_base block len */
        buf_u8 (info, DW_OP_reg6);                   /* x86-64 RBP (== EBP) */
        buf_u32(info, void_off);                     /* DW_AT_type = void */
        buf_uleb(info, 0);                           /* no children */
    }

    buf_uleb(info, 0);                               /* end CU children */

    unit_len = info->len - (unit_start + 4);
    buf_patch_u32(info, len_off, unit_len);
}

/* Resolve PROCSYM32.typind → return type's DWARF offset.  PROCSYM32
 * carries a TI of an LF_PROCEDURE record whose first field is the
 * actual return type.  If we can't resolve, default to void. */
static unsigned long resolve_proc_return_off(unsigned short ti)
{
    if (ti < ti_min) return type_offset_for_ti(ti);
    if (ti - ti_min >= n_types) return void_off;
    {
        CvTypeEntry *e = &types[ti - ti_min];
        if (e->leaf == 0x0008 /* LF_PROCEDURE */ && e->payload_len >= 2) {
            unsigned short rvtype = *(unsigned short *)e->payload;
            return type_offset_for_ti(rvtype);
        }
    }
    return type_offset_for_ti(ti);
}

static void emit_debug_info_cu(Buf *info, Buf *loc, Strtab *dstr, Module *m)
{
    unsigned long unit_start, len_off, unit_len;
    unsigned long i;
    const char    *cu_path;
    char           cu_dir[1024];
    char           cu_base[512];

    /* Skip CUs that contribute nothing — they're library refs (e.g.
     * ntoskrnl.exe entries) with no source/line/symbol content.  Empty
     * CUs confuse gdb's symbol search. */
    if (m->n_lines == 0 && m->n_funcs == 0) return;

    cu_path  = cu_source_name(m);
    path_split(cu_path, cu_dir, sizeof(cu_dir), cu_base, sizeof(cu_base));

    unit_start = info->len;
    m->cu_info_off = unit_start;
    m->cu_emitted  = 1;
    len_off    = unit_start;
    buf_u32(info, 0);                          /* unit_length placeholder */
    buf_u16(info, 2);                          /* version */
    buf_u32(info, 0);                          /* debug_abbrev_offset */
    buf_u8 (info, 4);                          /* address_size */

    /* === DW_TAG_compile_unit DIE === */
    buf_uleb(info, 1);
    buf_u32(info, strtab_add(dstr, cu_base));        /* DW_AT_name (basename) */
    buf_u32(info, strtab_add(dstr, cu_dir));         /* DW_AT_comp_dir */
    buf_u16(info, DW_LANG_C89);
    buf_u32(info, m->line_program_off);              /* DW_AT_stmt_list */
    buf_u32(info, strtab_add(dstr, "MicroNT dbg2dwf"));

    /* === DW_TAG_subprogram children === */
    for (i = 0; i < m->n_funcs; i++) {
        Symbol       *fn = &m->funcs[i];
        unsigned long j;

        buf_uleb(info, 2);                              /* abbrev 2 */
        buf_u32(info, strtab_add(dstr, fn->name));
        buf_u32(info, fn->va);
        buf_u32(info, fn->va + fn->size);
        buf_u8 (info, 1);
        /* DW_AT_frame_base = DW_OP_reg6 — x86-64 RBP.  See cv_to_dwarf_reg
         * comment: gdb attached to qemu-system-x86_64 uses x86-64 DWARF
         * register numbers regardless of objfile e_machine, so EBP
         * (i386 reg 5 = x86-64 reg 6 = RBP) must be encoded as reg6. */
        buf_u8 (info, 1);
        buf_u8 (info, DW_OP_reg6);
        /* DW_AT_type — return type, dereferenced from the proc TI. */
        buf_u32(info, resolve_proc_return_off(fn->typind));

        /* Per-local children.  DW_AT_location is a DW_FORM_data4
         * offset into .debug_loc — a list of one entry scoped to
         * [body_start, body_end), terminated by (0,0).  Outside that
         * range gdb shows <optimized out> instead of evaluating
         * [ebp+N] against an EBP that isn't a valid frame pointer
         * (caller's value during prologue, restored during epilogue).
         * Falls back to function range if CV gave us no DbgStart/End. */
        for (j = 0; j < fn->n_locals; j++) {
            Local         *l = &fn->locals[j];
            unsigned char  loc_buf[16];
            int            loc_len;
            unsigned long  list_off = loc->len;
            unsigned long  range_lo;
            unsigned long  range_hi;
            if (fn->body_end > fn->body_start) {
                range_lo = fn->body_start;
                range_hi = fn->body_end;
            } else {
                range_lo = fn->va;
                range_hi = fn->va + fn->size;
            }

            buf_uleb(info, l->is_param ? 3 : 4);
            buf_u32(info, strtab_add(dstr, l->name));
            buf_u32(info, list_off);                  /* DW_AT_location → list offset */
            buf_type_ref(info, (l->typind));

            /* For FPO functions the BP-rel offset CV emits is wrong at
             * runtime (no real EBP frame) — emit just the (0,0)
             * terminator so gdb shows <optimized out> instead of
             * dereferencing a stale EBP.  Tracking the correct
             * esp-rel location needs FPO_DATA + .debug_frame CFI. */
            if (fn->has_fp) {
                loc_len = encode_location(l, loc_buf);
                /* Location list entry: (start, end, length, expression). */
                buf_u32(loc, range_lo);
                buf_u32(loc, range_hi);
                buf_u16(loc, (unsigned short)loc_len);
                buf_put(loc, loc_buf, (unsigned long)loc_len);
            }
            /* Terminator. */
            buf_u32(loc, 0);
            buf_u32(loc, 0);
        }
        /* End of subprogram's children list. */
        buf_uleb(info, 0);
    }

    /* End of CU's children list. */
    buf_uleb(info, 0);

    unit_len = info->len - (unit_start + 4);
    buf_patch_u32(info, len_off, unit_len);
}

/* Emit .debug_frame Call Frame Information so gdb's stack walker can
 * unwind 32-bit kernel frames despite running in x86-64 long-mode
 * gdbstub.  Without this, gdb reads 8 bytes per stack slot (long-mode
 * default) and bt past frame 0 shows concatenated dwords as "addresses"
 * (`0x801b9f67fca13f4c` etc.).
 *
 * Strategy:
 *   - One CIE establishing default rules at function entry: CFA=RSP+4,
 *     saved RA at CFA-4 (32-bit return address pushed by CALL).
 *   - One FDE per non-FPO function covering [func_va, func_va+size).
 *     At body_start (after `push ebp; mov ebp, esp`):
 *       CFA               = RBP + 8       (def_cfa_expression)
 *       saved RBP value   = *(uint32_t*)(RBP+0)   (val_expression w/ deref_size 4)
 *       saved RA  value   = *(uint32_t*)(RBP+4)   (val_expression w/ deref_size 4)
 *     The val_expression form is required (not plain CFA-offset rules)
 *     because gdb in x86-64 mode would otherwise read 8-byte values for
 *     RBP (reg 6) and RIP (reg 16); deref_size 4 forces a 4-byte read
 *     and zero-extends, matching the 32-bit kernel ABI.
 *
 *   - FPO functions get no FDE — gdb falls back to its prologue
 *     analyser, which is no worse than what we have now and avoids
 *     having to parse .debug$F FPO_DATA + synthesise CFA from ESP. */
static void emit_debug_frame(Buf *frame)
{
    unsigned long cie_start, cie_len_off, cie_len;
    unsigned long cie_off;
    unsigned long i, j;
    /* Pre-built CFA expressions (kept short — the deref_size variant
     * encodes "read 4 bytes at this address" so gdb honours the 32-bit
     * stack slot regardless of long-mode interpretation). */
    static const unsigned char expr_cfa_rbp_plus_8[]   = { DW_OP_breg6, 0x08 };
    static const unsigned char expr_val_rbp[]          = { DW_OP_breg6, 0x00,
                                                           DW_OP_deref_size, 0x04 };
    static const unsigned char expr_val_ra[]           = { DW_OP_breg6, 0x04,
                                                           DW_OP_deref_size, 0x04 };
    /* CIE-default RA expression: at function entry RBP isn't a frame
     * pointer yet, but the return address is at the top of stack.
     * Read 4 bytes from RSP+0 — same trick as the body rules. */
    static const unsigned char expr_val_ra_entry[]     = { DW_OP_breg7, 0x00,
                                                           DW_OP_deref_size, 0x04 };
    /* x86-64 DWARF register numbers we reference: RBP=6, RA=16. */
    enum { REG_RSP = 7, REG_RBP = 6, REG_RA = 16 };

    /* ---------- CIE ---------- */
    cie_off     = frame->len;
    cie_start   = frame->len;
    cie_len_off = frame->len;
    buf_u32(frame, 0);                          /* unit_length placeholder */
    buf_u32(frame, 0xFFFFFFFFUL);               /* CIE_id (.debug_frame marker) */
    buf_u8 (frame, 1);                          /* version (DWARF 2 CFI) */
    buf_u8 (frame, 0);                          /* augmentation: empty NUL */
    buf_uleb(frame, 1);                         /* code_alignment_factor */
    buf_sleb(frame, -4);                        /* data_alignment_factor (4-byte slots) */
    buf_u8 (frame, REG_RA);                     /* return_address_register (DWARF 2: u8) */
    /* Initial instructions — at function entry: CFA = RSP+4, RA value
     * = *(uint32_t*)(RSP).  Use val_expression so gdb reads 4 bytes
     * even in long-mode interpretation; plain DW_CFA_offset would let
     * gdb default to 8 bytes and produce a concatenated dword as
     * "saved rip" the moment a breakpoint stops at the function entry
     * before the prologue runs. */
    buf_u8 (frame, DW_CFA_def_cfa);
    buf_uleb(frame, REG_RSP);
    buf_uleb(frame, 4);
    buf_u8 (frame, DW_CFA_val_expression);
    buf_uleb(frame, REG_RA);
    buf_uleb(frame, sizeof(expr_val_ra_entry));
    buf_put(frame, expr_val_ra_entry, sizeof(expr_val_ra_entry));
    /* Pad to 4-byte boundary so unit_length is the real on-disk size. */
    while ((frame->len - cie_start) % 4) buf_u8(frame, DW_CFA_nop);
    cie_len = frame->len - cie_start - 4;
    buf_patch_u32(frame, cie_len_off, cie_len);

    /* ---------- One FDE per non-FPO function ---------- */
    for (i = 0; i < n_mods; i++) {
        Module *m = &mods[i];
        for (j = 0; j < m->n_funcs; j++) {
            Symbol       *fn = &m->funcs[j];
            unsigned long fde_start, fde_len_off, fde_len;
            unsigned long delta;

            if (!fn->has_fp) continue;          /* FPO — skip */
            if (fn->body_end <= fn->body_start) continue;

            fde_start   = frame->len;
            fde_len_off = frame->len;
            buf_u32(frame, 0);                              /* unit_length placeholder */
            buf_u32(frame, cie_off);                        /* CIE_pointer */
            buf_u32(frame, fn->va);                         /* initial_location */
            buf_u32(frame, fn->size ? fn->size : 1);        /* address_range */

            /* Advance to body_start, then install body rules. */
            delta = fn->body_start - fn->va;
            if (delta == 0) {
                /* nothing — already at start */
            } else if (delta < 64) {
                buf_u8(frame, (unsigned char)(DW_CFA_advance_loc | delta));
            } else if (delta <= 0xff) {
                buf_u8(frame, DW_CFA_advance_loc1);
                buf_u8(frame, (unsigned char)delta);
            } else if (delta <= 0xffff) {
                buf_u8(frame, DW_CFA_advance_loc2);
                buf_u16(frame, (unsigned short)delta);
            } else {
                buf_u8(frame, DW_CFA_advance_loc4);
                buf_u32(frame, delta);
            }
            /* CFA = RBP + 8 (via expression — the expression's result IS
             * the address, no implicit deref). */
            buf_u8 (frame, DW_CFA_def_cfa_expression);
            buf_uleb(frame, sizeof(expr_cfa_rbp_plus_8));
            buf_put(frame, expr_cfa_rbp_plus_8, sizeof(expr_cfa_rbp_plus_8));
            /* RBP value = *(uint32_t*)(RBP+0). */
            buf_u8 (frame, DW_CFA_val_expression);
            buf_uleb(frame, REG_RBP);
            buf_uleb(frame, sizeof(expr_val_rbp));
            buf_put(frame, expr_val_rbp, sizeof(expr_val_rbp));
            /* RA value  = *(uint32_t*)(RBP+4). */
            buf_u8 (frame, DW_CFA_val_expression);
            buf_uleb(frame, REG_RA);
            buf_uleb(frame, sizeof(expr_val_ra));
            buf_put(frame, expr_val_ra, sizeof(expr_val_ra));

            /* Pad to 4-byte boundary. */
            while ((frame->len - fde_start) % 4) buf_u8(frame, DW_CFA_nop);
            fde_len = frame->len - fde_start - 4;
            buf_patch_u32(frame, fde_len_off, fde_len);
        }
    }
}

/* Emit .debug_aranges with one entry per emitted CU.  Each entry lists
 * the precise (start, length) ranges of the CU's functions, *not* the
 * (min, max) envelope — modules are non-contiguous (paged + non-paged
 * + init blocks) and using the envelope means several CUs claim each
 * other's gaps, so gdb's CU lookup picks whichever is first in
 * .debug_info, which is almost always wrong.  Aranges with tight
 * ranges fixes `info line *<addr>` to land on the right CU. */
static void emit_debug_aranges(Buf *aranges)
{
    unsigned long i, j;

    for (i = 0; i < n_mods; i++) {
        Module        *m = &mods[i];
        unsigned long  unit_start, len_off, payload_start, header_len;
        unsigned long  unit_len, pad;

        if (!m->cu_emitted || m->n_funcs == 0) continue;

        unit_start = aranges->len;
        len_off    = unit_start;
        buf_u32(aranges, 0);                    /* unit_length placeholder */
        buf_u16(aranges, 2);                    /* version */
        buf_u32(aranges, m->cu_info_off);       /* debug_info_offset */
        buf_u8 (aranges, 4);                    /* address_size */
        buf_u8 (aranges, 0);                    /* segment_size */

        /* The DWARF spec requires the first tuple to be aligned to
         * (2 * address_size) bytes from the start of the CU header,
         * i.e. on an 8-byte boundary here.  Pad with zeros. */
        header_len    = aranges->len - unit_start;
        pad           = (8 - (header_len % 8)) % 8;
        for (j = 0; j < pad; j++) buf_u8(aranges, 0);
        payload_start = aranges->len;

        for (j = 0; j < m->n_funcs; j++) {
            Symbol *fn = &m->funcs[j];
            buf_u32(aranges, fn->va);
            buf_u32(aranges, fn->size ? fn->size : 1);
        }
        /* Terminator (0,0). */
        buf_u32(aranges, 0);
        buf_u32(aranges, 0);

        unit_len = aranges->len - (unit_start + 4);
        buf_patch_u32(aranges, len_off, unit_len);
        (void)payload_start;
    }
}

/* ----------------------------------------------------------------
 * ELF emit (32-bit, little-endian, i386, ET_EXEC).
 *
 * Why ET_EXEC and not ET_REL: gdb treats relocatable objects as
 * relocate-on-load, applying base offsets to every section/symbol —
 * which mangles our absolute kernel addresses (0x80100000+ becomes
 * 0x10000+something).  ET_EXEC tells gdb the addresses are already
 * final, so `info line *0x801b9f3b` and `info symbol 0x801b9f3b`
 * resolve directly against the .text sh_addr range.
 * ---------------------------------------------------------------- */

#define ELFCLASS32   1
#define ELFDATA2LSB  1
#define EV_CURRENT   1
#define ET_EXEC      2
#define EM_386       3

#define SHN_UNDEF    0
#define SHN_ABS      0xfff1

#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_NOBITS   8

#define SHF_ALLOC      0x2
#define SHF_EXECINSTR  0x4

#define STB_LOCAL    0
#define STB_GLOBAL   1
#define STT_NOTYPE   0
#define STT_FUNC     2

typedef struct {
    unsigned char e_ident[16];
    unsigned short e_type;
    unsigned short e_machine;
    unsigned long  e_version;
    unsigned long  e_entry;
    unsigned long  e_phoff;
    unsigned long  e_shoff;
    unsigned long  e_flags;
    unsigned short e_ehsize;
    unsigned short e_phentsize;
    unsigned short e_phnum;
    unsigned short e_shentsize;
    unsigned short e_shnum;
    unsigned short e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    unsigned long  sh_name;
    unsigned long  sh_type;
    unsigned long  sh_flags;
    unsigned long  sh_addr;
    unsigned long  sh_offset;
    unsigned long  sh_size;
    unsigned long  sh_link;
    unsigned long  sh_info;
    unsigned long  sh_addralign;
    unsigned long  sh_entsize;
} Elf32_Shdr;

typedef struct {
    unsigned long  p_type;
    unsigned long  p_offset;
    unsigned long  p_vaddr;
    unsigned long  p_paddr;
    unsigned long  p_filesz;
    unsigned long  p_memsz;
    unsigned long  p_flags;
    unsigned long  p_align;
} Elf32_Phdr;

#define PT_LOAD      1
#define PF_X         1
#define PF_R         4

typedef struct {
    unsigned long  st_name;
    unsigned long  st_value;
    unsigned long  st_size;
    unsigned char  st_info;
    unsigned char  st_other;
    unsigned short st_shndx;
} Elf32_Sym;

#define ELF_ST_INFO(b, t)  (((b) << 4) + ((t) & 0xf))

/* ----------------------------------------------------------------
 * Pull it all together.
 * ---------------------------------------------------------------- */

static void emit_elf(const char *out_path)
{
    Buf debug_line, debug_info, debug_abbrev, debug_aranges, debug_loc;
    Buf debug_frame, symtab_buf;
    Strtab strtab;     /* .strtab — symbol names */
    Strtab debug_str;  /* .debug_str — DIE strings */
    Strtab shstrtab;   /* .shstrtab — section names */
    unsigned long i;
    unsigned long sh_off[16], sh_size[16];
    int ish_null, ish_text, ish_symtab, ish_strtab;
    int ish_dl, ish_di, ish_da, ish_ds, ish_ar, ish_lo, ish_fr, ish_sh;
    int n_sections;
    Elf32_Ehdr eh;
    Elf32_Shdr sh;
    Elf32_Sym  sym;
    FILE *fp;
    unsigned long off, hdr_off, shdr_off;

    buf_init(&debug_line);
    buf_init(&debug_info);
    buf_init(&debug_abbrev);
    buf_init(&debug_aranges);
    buf_init(&debug_loc);
    buf_init(&debug_frame);
    buf_init(&symtab_buf);
    strtab_init(&strtab);
    strtab_init(&debug_str);
    strtab_init(&shstrtab);

    /* Per-CU line programs first — emit_debug_info_cu needs the offsets. */
    for (i = 0; i < n_mods; i++) {
        emit_debug_line_for_module(&debug_line, &mods[i]);
    }
    /* One shared abbrev table. */
    emit_debug_abbrev(&debug_abbrev);
    /* Types CU first — its absolute offsets become the ref_addr targets
     * for DW_AT_type fields in the per-source CUs that follow. */
    emit_types_cu(&debug_info, &debug_str);
    /* Per-source CUs. */
    for (i = 0; i < n_mods; i++) {
        emit_debug_info_cu(&debug_info, &debug_loc, &debug_str, &mods[i]);
    }
    /* Synthetic CU for global publics (S_PUB32 only, no per-module
     * sstSymbols entry).  Gives gdb a DW_TAG_subprogram to resolve
     * `b <name>` against — without it, exported kernel symbols like
     * Phase1Initialization would only live in .symtab. */
    emit_globals_cu(&debug_info, &debug_str);
    /* .debug_aranges — must come AFTER per-CU emit so cu_info_off is set. */
    emit_debug_aranges(&debug_aranges);
    /* .debug_frame — CFI for 32-bit unwinding under x86-64 gdbstub. */
    emit_debug_frame(&debug_frame);

    /* .symtab — index 0 is null sym (ELF spec). */
    sym.st_name  = 0;
    sym.st_value = 0;
    sym.st_size  = 0;
    sym.st_info  = ELF_ST_INFO(STB_LOCAL, STT_NOTYPE);
    sym.st_other = 0;
    sym.st_shndx = SHN_UNDEF;
    buf_put(&symtab_buf, &sym, sizeof(sym));

    for (i = 0; i < n_syms; i++) {
        Symbol *s = &syms[i];
        sym.st_name  = strtab_add(&strtab, s->name);
        sym.st_value = s->va;
        sym.st_size  = s->size;
        sym.st_info  = ELF_ST_INFO(STB_GLOBAL, s->is_func ? STT_FUNC : STT_NOTYPE);
        sym.st_other = 0;
        /* Bind functions to the .text section (index 1) so gdb's
         * `info symbol <va>` reverse lookup recognises them as code.
         * SHN_ABS made names available for forward lookup
         * (`info functions`) but not for "what's at this address". */
        sym.st_shndx = s->is_func ? 1 /* ish_text */ : SHN_ABS;
        buf_put(&symtab_buf, &sym, sizeof(sym));
    }

    /* Section name registration order = section ordering in file. */
    ish_null   = 0;                    /* SHT_NULL */
    ish_text   = 1;                    /* .text (PROGBITS, alloc/exec, no
                                          file bytes — see Shdr below) */
    ish_symtab = 2;
    ish_strtab = 3;
    ish_dl     = 4;
    ish_di     = 5;
    ish_da     = 6;
    ish_ds     = 7;
    ish_ar     = 8;
    ish_lo     = 9;
    ish_fr     = 10;
    ish_sh     = 11;
    n_sections = 12;

    (void)strtab_add(&shstrtab, "");
    {
        unsigned long n_text   = strtab_add(&shstrtab, ".text");
        unsigned long n_symtab = strtab_add(&shstrtab, ".symtab");
        unsigned long n_strtab = strtab_add(&shstrtab, ".strtab");
        unsigned long n_dl     = strtab_add(&shstrtab, ".debug_line");
        unsigned long n_di     = strtab_add(&shstrtab, ".debug_info");
        unsigned long n_da     = strtab_add(&shstrtab, ".debug_abbrev");
        unsigned long n_ds     = strtab_add(&shstrtab, ".debug_str");
        unsigned long n_ar     = strtab_add(&shstrtab, ".debug_aranges");
        unsigned long n_lo     = strtab_add(&shstrtab, ".debug_loc");
        unsigned long n_fr     = strtab_add(&shstrtab, ".debug_frame");
        unsigned long n_sh     = strtab_add(&shstrtab, ".shstrtab");

        /* ---------- File layout ---------- */
        memset(&eh, 0, sizeof(eh));
        eh.e_ident[0] = 0x7f;
        eh.e_ident[1] = 'E';
        eh.e_ident[2] = 'L';
        eh.e_ident[3] = 'F';
        eh.e_ident[4] = ELFCLASS32;
        eh.e_ident[5] = ELFDATA2LSB;
        eh.e_ident[6] = EV_CURRENT;
        eh.e_type      = ET_EXEC;
        eh.e_machine   = EM_386;
        eh.e_version   = EV_CURRENT;
        eh.e_ehsize    = sizeof(eh);
        eh.e_phentsize = sizeof(Elf32_Phdr);
        eh.e_phnum     = 1;       /* one PT_LOAD covering .text */
        eh.e_phoff     = sizeof(eh);
        eh.e_shentsize = sizeof(Elf32_Shdr);
        eh.e_shnum     = (unsigned short)n_sections;
        eh.e_shstrndx  = (unsigned short)ish_sh;

        /* Layout file: ehdr, phdr, then section data in order, then shdr. */
        off = sizeof(eh) + sizeof(Elf32_Phdr);
        sh_off[ish_null]   = 0; sh_size[ish_null]   = 0;
        /* .text has no file bytes (sh_offset stays 0, see Shdr below). */
        sh_off[ish_text]   = 0; sh_size[ish_text]   = 0;
        sh_off[ish_symtab] = off; sh_size[ish_symtab] = symtab_buf.len;
        off += symtab_buf.len;
        sh_off[ish_strtab] = off; sh_size[ish_strtab] = strtab.data.len;
        off += strtab.data.len;
        sh_off[ish_dl]     = off; sh_size[ish_dl]     = debug_line.len;
        off += debug_line.len;
        sh_off[ish_di]     = off; sh_size[ish_di]     = debug_info.len;
        off += debug_info.len;
        sh_off[ish_da]     = off; sh_size[ish_da]     = debug_abbrev.len;
        off += debug_abbrev.len;
        sh_off[ish_ds]     = off; sh_size[ish_ds]     = debug_str.data.len;
        off += debug_str.data.len;
        sh_off[ish_ar]     = off; sh_size[ish_ar]     = debug_aranges.len;
        off += debug_aranges.len;
        sh_off[ish_lo]     = off; sh_size[ish_lo]     = debug_loc.len;
        off += debug_loc.len;
        sh_off[ish_fr]     = off; sh_size[ish_fr]     = debug_frame.len;
        off += debug_frame.len;
        sh_off[ish_sh]     = off; sh_size[ish_sh]     = shstrtab.data.len;
        off += shstrtab.data.len;

        eh.e_shoff = off;

        fp = fopen(out_path, "wb");
        if (!fp) {
            fprintf(stderr, "dbg2dwf: cannot open %s for write\n", out_path);
            exit(2);
        }
        fwrite(&eh, 1, sizeof(eh), fp);

        /* Single PT_LOAD spanning .text — addresses match what we put
         * in the .text Shdr below.  ET_EXEC requires at least one
         * program header for gdb to honour absolute sh_addr / st_value
         * without relocation. */
        {
            Elf32_Phdr ph;
            unsigned long lo = (unsigned long)-1, hi = 0;
            unsigned long s;
            for (s = 0; s < n_syms; s++) {
                unsigned long va  = syms[s].va;
                unsigned long end = va + (syms[s].size ? syms[s].size : 1);
                if (va < lo) lo = va;
                if (end > hi) hi = end;
            }
            memset(&ph, 0, sizeof(ph));
            ph.p_type   = PT_LOAD;
            ph.p_offset = 0;
            ph.p_vaddr  = (lo == (unsigned long)-1) ? 0 : lo;
            ph.p_paddr  = ph.p_vaddr;
            ph.p_filesz = 0;       /* no file bytes; .text is unbacked */
            ph.p_memsz  = (lo == (unsigned long)-1) ? 0 : (hi - lo);
            ph.p_flags  = PF_R | PF_X;
            ph.p_align  = 1;
            fwrite(&ph, 1, sizeof(ph), fp);
        }

        fwrite(symtab_buf.p,    1, symtab_buf.len,    fp);
        fwrite(strtab.data.p,   1, strtab.data.len,   fp);
        fwrite(debug_line.p,    1, debug_line.len,    fp);
        fwrite(debug_info.p,    1, debug_info.len,    fp);
        fwrite(debug_abbrev.p,  1, debug_abbrev.len,  fp);
        fwrite(debug_str.data.p,1, debug_str.data.len,fp);
        fwrite(debug_aranges.p, 1, debug_aranges.len, fp);
        fwrite(debug_loc.p,     1, debug_loc.len,     fp);
        fwrite(debug_frame.p,   1, debug_frame.len,   fp);
        fwrite(shstrtab.data.p, 1, shstrtab.data.len, fp);

        /* Now the section header table. */
        shdr_off = ftell(fp);
        (void)shdr_off;
        hdr_off = 0;
        (void)hdr_off;

        /* SHT_NULL — index 0 */
        memset(&sh, 0, sizeof(sh));
        fwrite(&sh, 1, sizeof(sh), fp);

        /* .text — declared SHT_PROGBITS (not NOBITS) so gdb's reverse
         * line-lookup ("`info line *<addr>`") accepts addresses inside
         * it as "real code".  NOBITS is treated as BSS-equivalent and
         * gdb prints "but contains no code" for any address there.
         * sh_offset=0 with sh_size>0 is technically a malformed
         * PROGBITS, but gdb tolerates it (it never reads the bytes
         * because we have no file backing — it just consults sh_addr
         * and sh_size for the address-in-section check). */
        {
            unsigned long lo = (unsigned long)-1, hi = 0;
            unsigned long s;
            for (s = 0; s < n_syms; s++) {
                unsigned long va  = syms[s].va;
                unsigned long end = va + (syms[s].size ? syms[s].size : 1);
                if (va < lo) lo = va;
                if (end > hi) hi = end;
            }
            memset(&sh, 0, sizeof(sh));
            sh.sh_name = n_text;
            sh.sh_type = SHT_PROGBITS;
            sh.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
            sh.sh_addr  = (lo == (unsigned long)-1) ? 0 : lo;
            sh.sh_offset = 0;
            sh.sh_size = (lo == (unsigned long)-1) ? 0 : (hi - lo);
            sh.sh_addralign = 1;
            fwrite(&sh, 1, sizeof(sh), fp);
        }

        /* .symtab */
        memset(&sh, 0, sizeof(sh));
        sh.sh_name = n_symtab;
        sh.sh_type = SHT_SYMTAB;
        sh.sh_offset = sh_off[ish_symtab];
        sh.sh_size   = sh_size[ish_symtab];
        sh.sh_link   = ish_strtab;
        sh.sh_info   = 1;          /* index of first non-local symbol */
        sh.sh_addralign = 4;
        sh.sh_entsize  = sizeof(Elf32_Sym);
        fwrite(&sh, 1, sizeof(sh), fp);

        /* .strtab */
        memset(&sh, 0, sizeof(sh));
        sh.sh_name = n_strtab;
        sh.sh_type = SHT_STRTAB;
        sh.sh_offset = sh_off[ish_strtab];
        sh.sh_size   = sh_size[ish_strtab];
        sh.sh_addralign = 1;
        fwrite(&sh, 1, sizeof(sh), fp);

        /* .debug_line */
        memset(&sh, 0, sizeof(sh));
        sh.sh_name = n_dl;
        sh.sh_type = SHT_PROGBITS;
        sh.sh_offset = sh_off[ish_dl];
        sh.sh_size   = sh_size[ish_dl];
        sh.sh_addralign = 1;
        fwrite(&sh, 1, sizeof(sh), fp);

        /* .debug_info */
        memset(&sh, 0, sizeof(sh));
        sh.sh_name = n_di;
        sh.sh_type = SHT_PROGBITS;
        sh.sh_offset = sh_off[ish_di];
        sh.sh_size   = sh_size[ish_di];
        sh.sh_addralign = 1;
        fwrite(&sh, 1, sizeof(sh), fp);

        /* .debug_abbrev */
        memset(&sh, 0, sizeof(sh));
        sh.sh_name = n_da;
        sh.sh_type = SHT_PROGBITS;
        sh.sh_offset = sh_off[ish_da];
        sh.sh_size   = sh_size[ish_da];
        sh.sh_addralign = 1;
        fwrite(&sh, 1, sizeof(sh), fp);

        /* .debug_str */
        memset(&sh, 0, sizeof(sh));
        sh.sh_name = n_ds;
        sh.sh_type = SHT_PROGBITS;
        sh.sh_offset = sh_off[ish_ds];
        sh.sh_size   = sh_size[ish_ds];
        sh.sh_addralign = 1;
        fwrite(&sh, 1, sizeof(sh), fp);

        /* .debug_aranges */
        memset(&sh, 0, sizeof(sh));
        sh.sh_name = n_ar;
        sh.sh_type = SHT_PROGBITS;
        sh.sh_offset = sh_off[ish_ar];
        sh.sh_size   = sh_size[ish_ar];
        sh.sh_addralign = 1;
        fwrite(&sh, 1, sizeof(sh), fp);

        /* .debug_loc */
        memset(&sh, 0, sizeof(sh));
        sh.sh_name = n_lo;
        sh.sh_type = SHT_PROGBITS;
        sh.sh_offset = sh_off[ish_lo];
        sh.sh_size   = sh_size[ish_lo];
        sh.sh_addralign = 1;
        fwrite(&sh, 1, sizeof(sh), fp);

        /* .debug_frame */
        memset(&sh, 0, sizeof(sh));
        sh.sh_name = n_fr;
        sh.sh_type = SHT_PROGBITS;
        sh.sh_offset = sh_off[ish_fr];
        sh.sh_size   = sh_size[ish_fr];
        sh.sh_addralign = 4;
        fwrite(&sh, 1, sizeof(sh), fp);

        /* .shstrtab */
        memset(&sh, 0, sizeof(sh));
        sh.sh_name = n_sh;
        sh.sh_type = SHT_STRTAB;
        sh.sh_offset = sh_off[ish_sh];
        sh.sh_size   = sh_size[ish_sh];
        sh.sh_addralign = 1;
        fwrite(&sh, 1, sizeof(sh), fp);

        /* Re-write ehdr to update e_shoff (we wrote it as 0 initially —
         * actually we computed the right value before fopen). */
        fseek(fp, 0, SEEK_SET);
        fwrite(&eh, 1, sizeof(eh), fp);

        fclose(fp);
    }
}

/* ----------------------------------------------------------------
 * main.
 * ---------------------------------------------------------------- */

/* Open a .DBG (IMAGE_SEPARATE_DEBUG_HEADER) file, find the CodeView
 * IMAGE_DEBUG_DIRECTORY entry, and return a malloc'd copy of the CV
 * blob.  Also fills in ImageBase and the section-→-RVA table the way
 * MapDebugInformation would have if it accepted .DBG files. */
static unsigned char *
load_dbg_codeview(const char *path,
                  unsigned long *out_cb,
                  unsigned long *out_image_base)
{
    FILE *fp;
    long  fsize;
    unsigned char *blob;
    IMAGE_SEPARATE_DEBUG_HEADER *hdr;
    PIMAGE_SECTION_HEADER sections;
    IMAGE_DEBUG_DIRECTORY *dirs;
    unsigned long n_dirs, i;
    unsigned long cv_off, cv_size;
    unsigned char *cv;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "dbg2dwf: cannot open %s\n", path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    blob = (unsigned char *)malloc(fsize);
    if (!blob) { fclose(fp); return NULL; }
    if ((long)fread(blob, 1, fsize, fp) != fsize) {
        fclose(fp); free(blob); return NULL;
    }
    fclose(fp);

    if ((unsigned long)fsize < sizeof(*hdr)) {
        fprintf(stderr, "dbg2dwf: file too small\n");
        free(blob); return NULL;
    }
    hdr = (IMAGE_SEPARATE_DEBUG_HEADER *)blob;
    if (hdr->Signature != IMAGE_SEPARATE_DEBUG_SIGNATURE) {
        fprintf(stderr, "dbg2dwf: bad .DBG signature 0x%lx\n",
                (unsigned long)hdr->Signature);
        free(blob); return NULL;
    }
    *out_image_base = hdr->ImageBase;

    /* Sections immediately follow the header. */
    sections = (PIMAGE_SECTION_HEADER)(hdr + 1);
    n_segs   = (unsigned short)hdr->NumberOfSections;
    seg_rva  = (unsigned long *)calloc(n_segs ? n_segs : 1,
                                       sizeof(unsigned long));
    for (i = 0; i < n_segs; i++) {
        seg_rva[i] = sections[i].VirtualAddress;
    }

    /* Debug directories live after sections + ExportedNames. */
    dirs = (IMAGE_DEBUG_DIRECTORY *)((unsigned char *)(sections + n_segs)
                                    + hdr->ExportedNamesSize);
    n_dirs = hdr->DebugDirectorySize / sizeof(*dirs);

    cv_off = 0; cv_size = 0;
    for (i = 0; i < n_dirs; i++) {
        if (dirs[i].Type == IMAGE_DEBUG_TYPE_CODEVIEW) {
            cv_off  = dirs[i].PointerToRawData;
            cv_size = dirs[i].SizeOfData;
            break;
        }
    }
    if (cv_size == 0 || cv_off + cv_size > (unsigned long)fsize) {
        fprintf(stderr, "dbg2dwf: no CodeView debug data in .DBG\n");
        free(blob); return NULL;
    }
    cv = (unsigned char *)malloc(cv_size);
    if (!cv) { free(blob); return NULL; }
    memcpy(cv, blob + cv_off, cv_size);
    free(blob);
    *out_cb = cv_size;
    return cv;
}

int __cdecl main(int argc, char *argv[])
{
    const char *in_path, *out_path;
    unsigned long n_funcs;
    unsigned long i;
    unsigned char *cv;
    unsigned long cv_cb;
    int verbose = 0;
    int argi;

    /* Force unbuffered stdio so wibo doesn't lose progress on a crash. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    in_path = NULL; out_path = NULL;
    for (argi = 1; argi < argc; argi++) {
        if (argv[argi][0] == '-') {
            if (!strcmp(argv[argi], "-v") || !strcmp(argv[argi], "--verbose"))
                verbose = 1;
            else {
                fprintf(stderr, "dbg2dwf: unknown option '%s'\n", argv[argi]);
                return 1;
            }
        } else if (!in_path) in_path = argv[argi];
        else if (!out_path) out_path = argv[argi];
        else { fputs("dbg2dwf: too many positional args\n", stderr); return 1; }
    }
    if (!in_path || !out_path) {
        fputs("Usage: dbg2dwf [-v] <input.dbg|.exe> <output.elf>\n", stderr);
        return 1;
    }

    cv = load_dbg_codeview(in_path, &cv_cb, &g_image_base);
    if (!cv) return 1;

    walk_codeview(cv, cv_cb);

    n_funcs = 0;
    for (i = 0; i < n_syms; i++) if (syms[i].is_func) n_funcs++;

    fprintf(stderr,
            "dbg2dwf: image_base=0x%lx mods=%lu syms=%lu (%lu fn)"
            " segs=%u types=%lu\n",
            g_image_base, n_mods, n_syms, n_funcs, (unsigned)n_segs, n_types);
    if (verbose) {
        for (i = 0; i < n_mods; i++) {
            fprintf(stderr,
                    "  mod[%lu] %s files=%lu lines=%lu va=[0x%lx,0x%lx]\n",
                    i, mods[i].name, mods[i].n_files, mods[i].n_lines,
                    mods[i].low_va, mods[i].high_va);
        }
    }

    emit_elf(out_path);
    fprintf(stderr, "dbg2dwf: wrote %s\n", out_path);

    free(cv);
    return 0;
}
