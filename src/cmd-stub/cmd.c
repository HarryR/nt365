/*
 * cmd-stub: a minimal cmd.exe replacement for driving NT 3.5-era NMAKE.EXE
 * under wibo. Implements only what NMAKE rule bodies actually emit.
 *
 * Supported:
 *   - External command exec via CreateProcessA (PATH-searched).
 *   - Redirection:  >, >>, <, 2>, 2>&1, >nul, 2>nul.
 *   - Pipes:  cmd1 | cmd2
 *   - Chains: cmd1 && cmd2 / cmd1 || cmd2 / cmd1 & cmd2
 *   - Builtins: echo, erase, del, rem.
 *
 * Written in K&R-compatible C89 so NT 3.5-era CL 8.50 can compile it.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE    16384
#define MAX_TOKENS  256

/* NT 3.5's WinAPI headers predate these constants. */
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD) -1)
#endif
#ifndef FILE_ATTRIBUTE_DIRECTORY
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#endif

static int g_verbose = 0;

/* ---------- Tokenizer: splits a line respecting "double quotes". ---------- */

typedef struct {
    char *argv[MAX_TOKENS];
    int   argc;
    char  buf[MAX_LINE];
} Tokens;

static void tokenize(const char *src, Tokens *t)
{
    size_t len;
    char *p;

    t->argc = 0;
    len = strlen(src);
    if (len >= MAX_LINE) len = MAX_LINE - 1;
    memcpy(t->buf, src, len);
    t->buf[len] = 0;

    p = t->buf;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (t->argc >= MAX_TOKENS - 1) break;
        if (*p == '"') {
            p++;
            t->argv[t->argc++] = p;
            while (*p && *p != '"') p++;
            if (*p) { *p = 0; p++; }
        } else {
            t->argv[t->argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) { *p = 0; p++; }
        }
    }
    t->argv[t->argc] = NULL;
}

/* ---------- Builtins ---------- */

static int do_echo(int argc, char **argv, HANDLE hout)
{
    DWORD w;
    int i;
    for (i = 1; i < argc; i++) {
        if (i > 1) WriteFile(hout, " ", 1, &w, NULL);
        WriteFile(hout, argv[i], (DWORD)strlen(argv[i]), &w, NULL);
    }
    WriteFile(hout, "\r\n", 2, &w, NULL);
    return 0;
}

static int do_erase(int argc, char **argv)
{
    int rc = 0;
    int i;
    for (i = 1; i < argc; i++) {
        if (!DeleteFileA(argv[i])) {
            DWORD err = GetLastError();
            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
                rc = 1;
        }
    }
    return rc;
}

/* Append the entire contents of file `src` to open handle `dst`. */
static int copy_append_file(const char *src, HANDLE dst)
{
    HANDLE fh;
    char buf[16384];
    DWORD r, w;
    fh = CreateFileA(src, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "cmd-stub: copy: cannot open %s: %lu\n",
                src, (unsigned long)GetLastError());
        return 1;
    }
    while (ReadFile(fh, buf, sizeof(buf), &r, NULL) && r > 0) {
        if (!WriteFile(dst, buf, r, &w, NULL) || w != r) {
            CloseHandle(fh);
            return 1;
        }
    }
    CloseHandle(fh);
    return 0;
}

/*
 * do_copy implements a minimal cmd.exe `copy` builtin.
 *
 * Supported forms:
 *   copy SRC DST                      simple file copy (DST written fresh)
 *   copy SRC1+SRC2+... DST            concatenate sources into DST
 *
 * The "+" form can appear either as a single token "a+b+c" or split across
 * tokens ("a" "+" "b+c"). Leading switches (/B /A /Y /V) are accepted and
 * ignored — NT 3.5 NMAKE rules don't rely on their semantics. The last
 * non-plus-containing token is treated as the destination.
 */
static int do_copy(int argc, char **argv)
{
    int i, n_srcs = 0;
    const char *srcs[64];
    const char *dst = NULL;
    char joined[MAX_LINE];
    char dst_resolved[MAX_LINE];
    size_t jlen = 0;
    HANDLE dsth;
    int rc = 0;
    char *tok;
    size_t tok_len;
    int have_plus_anywhere = 0;

    /* First pass: join all non-switch args into one string, separated by
     * single spaces. This lets us handle both "a+b dst" (1 src arg) and
     * "a + b dst" (3 src args) uniformly. */
    joined[0] = 0;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '/') continue;   /* ignore /B /A /Y /V etc. */
        tok_len = strlen(argv[i]);
        if (jlen + tok_len + 2 >= sizeof(joined)) break;
        if (jlen > 0) joined[jlen++] = ' ';
        memcpy(joined + jlen, argv[i], tok_len);
        jlen += tok_len;
        joined[jlen] = 0;
    }

    /* If there's any "+" in the joined string it's a concatenation. The
     * destination is everything after the last " " (space) that follows
     * the "+" section. We split by whitespace but merge tokens connected
     * by "+" into one source-group. */
    for (i = 0; joined[i]; i++) {
        if (joined[i] == '+') { have_plus_anywhere = 1; break; }
    }

    /* Parse: walk tokens, detect if "+"-joined with next one. */
    {
        char *p = joined;
        char *last = NULL;
        while (*p) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            tok = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) { *p = 0; p++; }
            last = tok;
            if (n_srcs < (int)(sizeof(srcs) / sizeof(srcs[0]))) srcs[n_srcs++] = tok;
        }
        /* If concatenation mode: destination is the last token; all others
         * (each possibly containing "+"-joined source lists) are sources. */
        if (have_plus_anywhere && n_srcs >= 2) {
            dst = last;
            n_srcs--;
        } else if (n_srcs == 2) {
            dst = srcs[1];
            n_srcs = 1;
        } else if (n_srcs == 1) {
            /* Single arg, no destination: copy to current directory. */
            dst = ".";
        } else {
            fprintf(stderr, "cmd-stub: copy: not enough arguments\n");
            return 1;
        }
    }

    if (!dst || !*dst) {
        fprintf(stderr, "cmd-stub: copy: missing destination\n");
        return 1;
    }

    /* If the destination is a directory (or "."), cmd.exe interprets it as
     * "copy into this directory, keeping the source's filename". Append the
     * basename of the first source to dst. */
    {
        DWORD attrs;
        int is_dir;
        attrs = GetFileAttributesA(dst);
        is_dir = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
        if (!is_dir && strcmp(dst, ".") == 0) is_dir = 1;
        if (is_dir && n_srcs >= 1) {
            const char *first = srcs[0];
            const char *plus = strchr(first, '+');
            size_t first_len = plus ? (size_t)(plus - first) : strlen(first);
            const char *base = first;
            size_t i2;
            size_t baselen, dstlen;
            int needs_sep;
            for (i2 = 0; i2 < first_len; i2++) {
                if (first[i2] == '/' || first[i2] == '\\') base = first + i2 + 1;
            }
            baselen = first_len - (size_t)(base - first);
            dstlen = strlen(dst);
            needs_sep = dstlen > 0 && dst[dstlen - 1] != '/' && dst[dstlen - 1] != '\\';
            if (dstlen + (needs_sep ? 1 : 0) + baselen >= sizeof(dst_resolved)) {
                fprintf(stderr, "cmd-stub: copy: path too long\n");
                return 1;
            }
            memcpy(dst_resolved, dst, dstlen);
            if (needs_sep) dst_resolved[dstlen++] = '\\';
            memcpy(dst_resolved + dstlen, base, baselen);
            dst_resolved[dstlen + baselen] = 0;
            dst = dst_resolved;
        }
    }

    dsth = CreateFileA(dst, GENERIC_WRITE, FILE_SHARE_READ,
                       NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (dsth == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "cmd-stub: copy: cannot create %s: %lu\n",
                dst, (unsigned long)GetLastError());
        return 1;
    }

    /* Each srcs[i] may itself contain "+"-joined names like "a+b+c". */
    for (i = 0; i < n_srcs && rc == 0; i++) {
        char group[MAX_LINE];
        size_t glen = strlen(srcs[i]);
        char *sub, *next;
        if (glen >= sizeof(group)) { rc = 1; break; }
        memcpy(group, srcs[i], glen + 1);
        sub = group;
        while (sub && *sub) {
            next = strchr(sub, '+');
            if (next) { *next = 0; next++; }
            if (*sub) {
                if (copy_append_file(sub, dsth) != 0) { rc = 1; break; }
            }
            sub = next;
        }
    }

    CloseHandle(dsth);
    return rc;
}

/* ---------- External command execution ---------- */

static void rejoin_argv(int argc, char **argv, char *out, size_t outsz)
{
    size_t used = 0;
    int needs_quote, i;
    size_t alen;
    for (i = 0; i < argc; i++) {
        if (i > 0 && used + 1 < outsz) out[used++] = ' ';
        needs_quote = (strchr(argv[i], ' ') != NULL)
                      || (strchr(argv[i], '\t') != NULL)
                      || argv[i][0] == 0;
        if (needs_quote && used + 1 < outsz) out[used++] = '"';
        alen = strlen(argv[i]);
        if (used + alen < outsz) { memcpy(out + used, argv[i], alen); used += alen; }
        if (needs_quote && used + 1 < outsz) out[used++] = '"';
    }
    if (used < outsz) out[used] = 0; else out[outsz - 1] = 0;
}

static int run_external(int argc, char **argv, HANDLE hin, HANDLE hout, HANDLE herr)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    static char cmdline[MAX_LINE];
    DWORD ec;
    DWORD err;
    HANDLE saved_in, saved_out, saved_err;
    int swapped_in = 0, swapped_out = 0, swapped_err = 0;
    BOOL ok;

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    /*
     * lpStartupInfo is ignored by some runners (notably wibo) — setting
     * STARTF_USESTDHANDLES doesn't make the child inherit our redirection
     * handles. Work around by swapping our own process's stdin/stdout/
     * stderr via SetStdHandle before spawning; the child inherits whatever
     * our process has at that point. Save the originals so we can restore
     * after the child exits, keeping cmd's own stdio intact for the next
     * command in the line.
     */
    saved_in  = GetStdHandle(STD_INPUT_HANDLE);
    saved_out = GetStdHandle(STD_OUTPUT_HANDLE);
    saved_err = GetStdHandle(STD_ERROR_HANDLE);
    if (hin  != saved_in)  { SetStdHandle(STD_INPUT_HANDLE,  hin);  swapped_in  = 1; }
    if (hout != saved_out) { SetStdHandle(STD_OUTPUT_HANDLE, hout); swapped_out = 1; }
    if (herr != saved_err) { SetStdHandle(STD_ERROR_HANDLE,  herr); swapped_err = 1; }

    rejoin_argv(argc, argv, cmdline, sizeof(cmdline));
    if (g_verbose) fprintf(stderr, "cmd-stub: exec [%s]\n", cmdline);

    ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    err = ok ? 0 : GetLastError();

    /* Restore immediately — the child has already forked by this point. */
    if (swapped_in)  SetStdHandle(STD_INPUT_HANDLE,  saved_in);
    if (swapped_out) SetStdHandle(STD_OUTPUT_HANDLE, saved_out);
    if (swapped_err) SetStdHandle(STD_ERROR_HANDLE,  saved_err);

    if (!ok) {
        fprintf(stderr, "cmd-stub: CreateProcess failed (%lu): %s\n",
                (unsigned long)err, cmdline);
        return (int)err;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    return (int)ec;
}

/* ---------- Redirection ---------- */

typedef struct {
    char *stdin_path;
    char *stdout_path;
    int   stdout_append;
    char *stderr_path;
    int   stderr_to_out;
    int   stdout_nul;
    int   stderr_nul;
} Redir;

static void extract_redirs(Tokens *t, Redir *r)
{
    int out = 0;
    int i;
    char *a;
    char *p;

    memset(r, 0, sizeof(*r));
    for (i = 0; i < t->argc; i++) {
        a = t->argv[i];
        if (a[0] == '>' && a[1] == '>') {
            r->stdout_append = 1;
            r->stdout_path = a[2] ? a + 2 : (i + 1 < t->argc ? t->argv[++i] : NULL);
        } else if (a[0] == '>') {
            p = a[1] ? a + 1 : (i + 1 < t->argc ? t->argv[++i] : NULL);
            if (p && (_stricmp(p, "nul") == 0)) r->stdout_nul = 1;
            else r->stdout_path = p;
        } else if (a[0] == '<') {
            r->stdin_path = a[1] ? a + 1 : (i + 1 < t->argc ? t->argv[++i] : NULL);
        } else if (a[0] == '2' && a[1] == '>') {
            if (a[2] == '&' && a[3] == '1') { r->stderr_to_out = 1; continue; }
            p = a[2] ? a + 2 : (i + 1 < t->argc ? t->argv[++i] : NULL);
            if (p && (_stricmp(p, "nul") == 0)) r->stderr_nul = 1;
            else r->stderr_path = p;
        } else {
            t->argv[out++] = a;
        }
    }
    t->argc = out;
    t->argv[out] = NULL;
}

static HANDLE open_out(const char *path, int append)
{
    return CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                       append ? OPEN_ALWAYS : CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}
static HANDLE open_in(const char *path)
{
    return CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}
static HANDLE open_nul(void)
{
    /* Wibo does not map Windows' "NUL" device; open the host's /dev/null
     * directly via its POSIX path, which wibo's path resolver passes
     * through to the underlying filesystem. */
    HANDLE h = CreateFileA("/dev/null", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) return h;
    /* Fall back to the Windows name for native Wine/real-NT environments. */
    return CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* ---------- Single command (no pipes, no chains) ---------- */

static int run_simple(char *cmdstr, HANDLE hin, HANDLE hout, HANDLE herr)
{
    Tokens t;
    Redir r;
    HANDLE fh_in, fh_out, fh_err;
    HANDLE to_close_in, to_close_out, to_close_err;
    int rc;
    const char *cmd;

    tokenize(cmdstr, &t);
    if (t.argc == 0) return 0;
    extract_redirs(&t, &r);
    if (t.argc == 0) return 0;

    fh_in = hin; fh_out = hout; fh_err = herr;
    to_close_in = NULL; to_close_out = NULL; to_close_err = NULL;

    if (r.stdin_path) {
        fh_in = open_in(r.stdin_path);
        if (fh_in == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "cmd-stub: open %s: %lu\n",
                    r.stdin_path, (unsigned long)GetLastError());
            return 1;
        }
        to_close_in = fh_in;
    }
    if (r.stdout_nul) {
        fh_out = open_nul();
        if (fh_out == INVALID_HANDLE_VALUE) fh_out = hout;
        else to_close_out = fh_out;
    } else if (r.stdout_path) {
        fh_out = open_out(r.stdout_path, r.stdout_append);
        if (fh_out == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "cmd-stub: open %s: %lu\n",
                    r.stdout_path, (unsigned long)GetLastError());
            return 1;
        }
        if (r.stdout_append) SetFilePointer(fh_out, 0, NULL, FILE_END);
        to_close_out = fh_out;
    }
    if (r.stderr_to_out) {
        fh_err = fh_out;
    } else if (r.stderr_nul) {
        fh_err = open_nul();
        if (fh_err == INVALID_HANDLE_VALUE) fh_err = herr;
        else to_close_err = fh_err;
    } else if (r.stderr_path) {
        fh_err = open_out(r.stderr_path, 0);
        if (fh_err == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "cmd-stub: open %s: %lu\n",
                    r.stderr_path, (unsigned long)GetLastError());
            return 1;
        }
        to_close_err = fh_err;
    }

    cmd = t.argv[0];
    if (_stricmp(cmd, "echo") == 0) {
        rc = do_echo(t.argc, t.argv, fh_out);
    } else if (_stricmp(cmd, "erase") == 0 || _stricmp(cmd, "del") == 0) {
        rc = do_erase(t.argc, t.argv);
    } else if (_stricmp(cmd, "copy") == 0) {
        rc = do_copy(t.argc, t.argv);
    } else if (_stricmp(cmd, "ren") == 0 || _stricmp(cmd, "rename") == 0) {
        /* cmd.exe's ren takes src dst; destination may be a bare leaf name
         * (to rename in src's directory). We resolve that here. */
        if (t.argc != 3) {
            fprintf(stderr, "cmd-stub: ren: expected <src> <dst>\n");
            rc = 1;
        } else {
            const char *src = t.argv[1];
            const char *dst = t.argv[2];
            char dst_buf[MAX_LINE];
            /* If dst has no path separator, prepend src's directory. */
            if (!strchr(dst, '/') && !strchr(dst, '\\')) {
                const char *slash = strrchr(src, '/');
                const char *bslash = strrchr(src, '\\');
                const char *sep = (slash > bslash) ? slash : bslash;
                if (sep) {
                    size_t dirlen = (size_t)(sep - src + 1);
                    if (dirlen + strlen(dst) < sizeof(dst_buf)) {
                        memcpy(dst_buf, src, dirlen);
                        strcpy(dst_buf + dirlen, dst);
                        dst = dst_buf;
                    }
                }
            }
            if (!MoveFileA(src, dst)) {
                fprintf(stderr, "cmd-stub: ren %s -> %s failed: %lu\n",
                        src, dst, (unsigned long)GetLastError());
                rc = 1;
            } else {
                rc = 0;
            }
        }
    } else if (_stricmp(cmd, "rem") == 0) {
        rc = 0;
    } else {
        rc = run_external(t.argc, t.argv, fh_in, fh_out, fh_err);
    }

    if (to_close_in)  CloseHandle(to_close_in);
    if (to_close_out) CloseHandle(to_close_out);
    if (to_close_err) CloseHandle(to_close_err);
    return rc;
}

/* ---------- Line splitting helpers ---------- */

static int split_unquoted(char *s, const char *sep, char **seg, int max)
{
    int seplen = (int)strlen(sep);
    int n = 0;
    int in_q = 0;
    if (n < max) seg[n++] = s;
    while (*s) {
        if (*s == '"') in_q = !in_q;
        if (!in_q && strncmp(s, sep, seplen) == 0) {
            *s = 0;
            s += seplen;
            if (n < max) seg[n++] = s;
            continue;
        }
        s++;
    }
    return n;
}

/* ---------- Pipelines ---------- */

static int run_pipeline(char *line, HANDLE hin_outer, HANDLE hout_outer, HANDLE herr)
{
    char *segs[32];
    int n = 0;
    int in_q = 0;
    char *p;
    HANDLE prev_in;
    HANDLE to_close;
    int last_rc = 0;
    int i;
    SECURITY_ATTRIBUTES sa;
    HANDLE pipe_r, pipe_w, stage_out;
    int rc;

    if (n < 32) segs[n++] = line;
    for (p = line; *p; p++) {
        if (*p == '"') in_q = !in_q;
        if (!in_q && *p == '|' && p[1] != '|') {
            *p = 0;
            if (n < 32) segs[n++] = p + 1;
        }
    }
    if (n == 1) return run_simple(segs[0], hin_outer, hout_outer, herr);

    prev_in = hin_outer;
    to_close = NULL;
    for (i = 0; i < n; i++) {
        pipe_r = NULL; pipe_w = NULL;
        stage_out = (i == n - 1) ? hout_outer : NULL;
        if (i != n - 1) {
            sa.nLength = sizeof(sa);
            sa.lpSecurityDescriptor = NULL;
            sa.bInheritHandle = TRUE;
            if (!CreatePipe(&pipe_r, &pipe_w, &sa, 0)) {
                fprintf(stderr, "cmd-stub: CreatePipe failed\n");
                return 1;
            }
            stage_out = pipe_w;
        }
        rc = run_simple(segs[i], prev_in, stage_out, herr);
        if (to_close) { CloseHandle(to_close); to_close = NULL; }
        if (pipe_w) CloseHandle(pipe_w);
        prev_in = pipe_r;
        to_close = pipe_r;
        last_rc = rc;
    }
    if (to_close && to_close != hin_outer) CloseHandle(to_close);
    return last_rc;
}

/* ---------- Conditional chains (&&, ||, &) ---------- */

static int run_line(char *line)
{
    char *end;
    char *and_segs[32];
    char *or_segs[16];
    char *amp_segs[16];
    int n_and, n_or, n_amp;
    int i, j, k;
    int last_rc = 0;
    int or_rc, amp_rc;
    HANDLE hin, hout, herr;

    while (*line == ' ' || *line == '\t') line++;
    end = line + strlen(line);
    while (end > line && (end[-1] == ' ' || end[-1] == '\t'
                          || end[-1] == '\r' || end[-1] == '\n'))
        *--end = 0;
    if (!*line) return 0;

    hin  = GetStdHandle(STD_INPUT_HANDLE);
    hout = GetStdHandle(STD_OUTPUT_HANDLE);
    herr = GetStdHandle(STD_ERROR_HANDLE);

    n_and = split_unquoted(line, "&&", and_segs, 32);
    for (i = 0; i < n_and; i++) {
        n_or = split_unquoted(and_segs[i], "||", or_segs, 16);
        or_rc = 0;
        for (j = 0; j < n_or; j++) {
            n_amp = split_unquoted(or_segs[j], "&", amp_segs, 16);
            amp_rc = 0;
            for (k = 0; k < n_amp; k++) {
                amp_rc = run_pipeline(amp_segs[k], hin, hout, herr);
            }
            or_rc = amp_rc;
            if (or_rc == 0) break;
        }
        last_rc = or_rc;
        if (last_rc != 0) break;
    }
    return last_rc;
}

/* ---------- Entry point ---------- */

int main(void)
{
    const char *raw;
    const char *p;
    static char line[MAX_LINE];
    size_t n, lenq;

    raw = GetCommandLineA();
    if (!raw) return 2;

    p = raw;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') p++;
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    while (*p == ' ' || *p == '\t') p++;

    if (*p != '/' || (p[1] != 'C' && p[1] != 'c')) {
        fprintf(stderr, "cmd-stub: only '/C <command>' invocation is supported\n");
        return 2;
    }
    p += 2;
    while (*p == ' ' || *p == '\t') p++;

    if (getenv("CMDSTUB_VERBOSE")) g_verbose = 1;

    n = strlen(p);
    if (n >= MAX_LINE) n = MAX_LINE - 1;
    memcpy(line, p, n);
    line[n] = 0;

    lenq = strlen(line);
    if (line[0] == '"' && lenq >= 2 && line[lenq - 1] == '"') {
        line[lenq - 1] = 0;
        memmove(line, line + 1, lenq - 1);
    }

    return run_line(line);
}
