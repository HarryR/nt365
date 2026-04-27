/*
 * cr_thread.c — C escape-hatch for spawning an OS thread that runs Lua
 * code in its own fresh lua_State.
 *
 * Why this is C, not Lua:
 *
 *   The thread entry passed to RtlCreateUserThread must be a real
 *   native function. An FFI callback (ffi.cast on a Lua function)
 *   re-enters the originating lua_State on invocation, which would
 *   corrupt the parent's VM since lua_State is single-threaded.
 *   Bringing up a fresh state on a new thread therefore requires
 *   native code — this file.
 *
 *   This is the architectural rule for cr's top-level C files: each
 *   one must justify itself with a "Lua + FFI cannot express this
 *   safely" reason. New additions need the same justification or they
 *   belong in lua/ as raw FFI.
 *
 * Lifetime model:
 *
 *   Everything the spawned thread reads or writes — the chunk source,
 *   the payload, the output buffer, the done event, and the
 *   CR_THREAD struct itself — is allocated on the process heap (the
 *   one PEB->ProcessHeap that NT 3.5's RtlAllocateHeap is internally
 *   thread-safe on). Nothing comes from Lua's GC.
 *
 *   Ownership is a 2-count refcount: parent and thread each hold one
 *   ref at spawn. Either party drops their ref when done with ctx
 *   (parent in _cr_thread_close, thread at end of _entry). Whoever
 *   atomically decrements to zero is the last toucher and runs
 *   _free_all. Neither party ever blocks the other; in particular
 *   _cr_thread_close NEVER calls NtTerminateThread (which would leak
 *   loader/heap critical sections held by the thread) and NEVER waits.
 *
 *   Crash safety: if the chunk takes the thread down before _entry's
 *   exit path runs (uncaught native exception, chunk-self-
 *   NtTerminateThread, stack overflow, ...), the thread's _release
 *   never fires and ctx would leak forever. To prevent this,
 *   _cr_thread_close polls thread_handle; if signaled and the
 *   thread_released CAS-flag is still 0, the parent claims the
 *   thread's reference and drops it on the thread's behalf. The flag
 *   is set in _entry's exit path before _release, so a normal exit
 *   loses the CAS race against the parent (intended).
 *
 *   Status reporting: ctx->status is initialised to STATUS_CRASH (3)
 *   at spawn. _entry's exit path overwrites it with 0 (chunk returned
 *   ok), 1 (chunk raised a Lua error), or 2 (couldn't even create a
 *   lua_State). If the parent reads 3 it knows the thread terminated
 *   without running its exit path — i.e. crashed.
 *
 * TLS:
 *
 *   Chunks have no TLS surface — TlsAlloc/TlsFree are not exposed via
 *   the cr Lua API. lua_State is per-thread by construction; nothing
 *   inside cr_thread or the standard nt.* modules calls TlsAlloc.
 *   Defensive auto-cleanup machinery (per-spawn bitmap, hooks into
 *   librt's TlsAlloc) was removed once we confirmed there is no
 *   consumer to track. If a future cr feature needs per-spawn TLS
 *   tracking, an opt-in cr.tls_alloc wrapper is the right shape;
 *   don't reintroduce the always-on hooks.
 */

#include "nt.h"          /* rt/nt.h via -Irt: NTSTATUS, HANDLE, BOOLEAN, ... */
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

/* dllexport puts these in the PE export table; used keeps LTO from
 * dropping them since callers are the dynamic loader / chunks via
 * ffi.C, not visible C call sites. */
#define EXPORT __declspec(dllexport) __attribute__((used))

/* ---- ntdll bits we need (kept local — see rt/nt.h policy) ---------- */

extern PVOID   NTAPI RtlAllocateHeap(HANDLE Heap, ULONG Flags, SIZE_T Size);
extern BOOLEAN NTAPI RtlFreeHeap    (HANDLE Heap, ULONG Flags, PVOID Mem);

extern NTSTATUS NTAPI NtCreateEvent (HANDLE *EventHandle, ULONG DesiredAccess,
                                     POBJECT_ATTRIBUTES Oa, int EventType,
                                     BOOLEAN InitialState);
extern NTSTATUS NTAPI NtSetEvent    (HANDLE EventHandle, LONG *PreviousState);
extern NTSTATUS NTAPI NtClose       (HANDLE Object);
extern NTSTATUS NTAPI NtWaitForSingleObject(HANDLE Object, BOOLEAN Alertable,
                                            PLARGE_INTEGER Timeout);
extern NTSTATUS NTAPI NtTerminateThread(HANDLE Thread, NTSTATUS ExitStatus);

extern NTSTATUS NTAPI RtlCreateUserThread(HANDLE Process,
                                          PVOID  ThreadSecurityDescriptor,
                                          BOOLEAN CreateSuspended,
                                          ULONG ZeroBits,
                                          ULONG MaximumStackSize,
                                          ULONG CommittedStackSize,
                                          PVOID StartAddress,
                                          PVOID StartParameter,
                                          HANDLE *Thread,
                                          PVOID  ClientId);

/* PEB->ProcessHeap captured by libc_init.c (rt/libc_heap.c uses it for
 * malloc). We borrow the same heap so allocations made here can be
 * compared / freed against the same handle malloc uses. */
extern HANDLE _libc_heap;

#define HEAP                _libc_heap
#define EVENT_ALL_ACCESS    0x1F0003
#define NotificationEvent   0
#define STATUS_TIMEOUT      ((NTSTATUS)0x00000102L)
#define INFINITE            ((ULONG)0xFFFFFFFF)

#define DEFAULT_OUT_CAP     (4 * 1024)
#define DEFAULT_STACK_RES   (256 * 1024)
#define DEFAULT_STACK_COM   ( 16 * 1024)

/* status values stamped into ctx->status. STATUS_CRASH is the spawn-
 * time default; _entry's exit path overwrites with one of the others
 * before signalling done_event. If parent reads CRASH, the chunk took
 * the thread down before the exit path could run. */
#define STATUS_OK           0
#define STATUS_LUA_ERROR    1
#define STATUS_PANIC        2     /* couldn't create lua_State */
#define STATUS_CRASH        3     /* thread died before exit path */

/* ---- internal structure (opaque to Lua) ---------------------------- */

struct CR_THREAD {
    HANDLE thread_handle;
    HANDLE done_event;       /* NotificationEvent: set when result ready */
    char  *chunk;     ULONG chunk_len;
    char  *payload;   ULONG payload_len;
    char  *out_buf;   ULONG out_cap;   ULONG out_len;
    int    status;           /* one of STATUS_* */
    LONG   refcount;         /* parent + thread = 2 at spawn */
    volatile UCHAR thread_released;  /* CAS-flag: thread has called _release */
};

/* ---- helpers -------------------------------------------------------- */

/* Free every kernel handle and heap block in ctx, then ctx itself.
 * Caller must have proven nobody else will dereference t. */
static void _free_all(struct CR_THREAD *t)
{
    if (t->thread_handle) NtClose(t->thread_handle);
    if (t->done_event)    NtClose(t->done_event);
    if (t->chunk)   RtlFreeHeap(HEAP, 0, t->chunk);
    if (t->payload) RtlFreeHeap(HEAP, 0, t->payload);
    if (t->out_buf) RtlFreeHeap(HEAP, 0, t->out_buf);
    RtlFreeHeap(HEAP, 0, t);
}

/* Drop one reference. Whoever decrements to zero is by definition the
 * last toucher (the other party's decrement-and-step-away has already
 * happened) and is safe to free. Atomic via gcc's lock-prefixed builtin. */
static void _release(struct CR_THREAD *t)
{
    if (__sync_sub_and_fetch(&t->refcount, 1) == 0) _free_all(t);
}

static void _copy_result(struct CR_THREAD *t, lua_State *L, int lua_status)
{
    size_t      len;
    const char *s = lua_tolstring(L, -1, &len);
    ULONG       i;

    if (s == 0) { s = ""; len = 0; }
    if (len > t->out_cap) len = t->out_cap;
    for (i = 0; i < (ULONG)len; i++) t->out_buf[i] = s[i];
    t->out_len = (ULONG)len;
    t->status  = (lua_status == 0) ? STATUS_OK : STATUS_LUA_ERROR;
}

/* ---- thread entry --------------------------------------------------- */

/* Native-NT thread entries are contractually one-way: `RtlInitializeContext`
 * (rtl/i386/context.c) sets up the stack with a parameter at [esp+4] and
 * a "return address" slot at [esp+0] that it never writes. If the entry
 * function returns, `ret` pops 0 into EIP and the thread crashes with
 * STATUS_ACCESS_VIOLATION. Win32's kernel32 wraps entries in
 * BaseThreadStartThunk → ExitThread; native ntdll callers (us) must call
 * NtTerminateThread ourselves before returning. */
static ULONG NTAPI _entry(PVOID raw)
{
    struct CR_THREAD *t = (struct CR_THREAD *)raw;
    lua_State *L = luaL_newstate();
    if (L == 0) {
        t->status  = STATUS_PANIC;
        t->out_len = 0;
    } else {
        int rc;
        luaL_openlibs(L);

        /* Expose payload as the global PAYLOAD. Always a string,
         * possibly empty. The chunk reads it like any other global. */
        lua_pushlstring(L, t->payload, (size_t)t->payload_len);
        lua_setglobal(L, "PAYLOAD");

        rc = luaL_loadbuffer(L, t->chunk, (size_t)t->chunk_len, "=thread");
        if (rc == 0) rc = lua_pcall(L, 0, 1, 0);

        /* On both ok and error, top of stack is a string we marshal
         * back (return value, or pcall error message). _copy_result
         * overwrites STATUS_CRASH with STATUS_OK or STATUS_LUA_ERROR.
         * If we never reach this line — chunk did NtTerminateThread,
         * unhandled native exception, stack overflow — status stays at
         * STATUS_CRASH and the parent sees it. */
        _copy_result(t, L, rc);
        lua_close(L);
    }
    NtSetEvent(t->done_event, 0);

    /* Mark the thread as having reached its exit path BEFORE releasing.
     * Parent's close polls thread_handle and CAS-claims this flag if
     * the thread terminated without setting it (= crashed). The order
     * (set flag → _release) means a normal exit always loses the CAS
     * race against a parent that polls between the two. Acceptable —
     * the loser just doesn't perform the redundant claim. */
    __sync_lock_test_and_set(&t->thread_released, 1);
    _release(t);

    NtTerminateThread((HANDLE)(long)-2 /* NtCurrentThread */, 0);
    return 0;                 /* unreachable */
}

/* ---- exported API --------------------------------------------------- */

EXPORT struct CR_THREAD *_cr_thread_spawn(const char *chunk,   ULONG chunk_len,
                                          const char *payload, ULONG payload_len)
{
    struct CR_THREAD *t;
    NTSTATUS st;
    HANDLE   h;
    ULONG    i;

    t = (struct CR_THREAD *)RtlAllocateHeap(HEAP, HEAP_ZERO_MEMORY, sizeof(*t));
    if (t == 0) return 0;

    t->chunk_len   = chunk_len;
    t->payload_len = payload_len;
    t->out_cap     = DEFAULT_OUT_CAP;
    t->refcount    = 2;       /* parent + thread */
    t->status      = STATUS_CRASH;   /* sentinel: overwritten on normal exit */

    /* Always allocate at least 1 byte so a zero-length chunk/payload
     * still gets a valid (distinct) pointer that _free_all can release. */
    t->chunk   = (char *)RtlAllocateHeap(HEAP, 0, chunk_len   ? chunk_len   : 1);
    t->payload = (char *)RtlAllocateHeap(HEAP, 0, payload_len ? payload_len : 1);
    t->out_buf = (char *)RtlAllocateHeap(HEAP, 0, t->out_cap);
    if (!t->chunk || !t->payload || !t->out_buf) {
        _free_all(t);
        return 0;
    }
    for (i = 0; i < chunk_len;   i++) t->chunk  [i] = chunk  [i];
    for (i = 0; i < payload_len; i++) t->payload[i] = payload[i];

    st = NtCreateEvent(&t->done_event, EVENT_ALL_ACCESS, 0,
                       NotificationEvent, 0);
    if (st < 0) { t->done_event = 0; _free_all(t); return 0; }

    st = RtlCreateUserThread(
            (HANDLE)(long)-1,        /* current process pseudo-handle */
            0,                        /* no security descriptor */
            0,                        /* not suspended */
            0,                        /* zero bits */
            DEFAULT_STACK_RES,
            DEFAULT_STACK_COM,
            (PVOID)_entry,
            (PVOID)t,
            &h,
            0);                       /* don't care about ClientId */
    if (st < 0) { _free_all(t); return 0; }

    t->thread_handle = h;
    return t;
}

/* Borrowed thread handle, valid until _cr_thread_close. */
EXPORT HANDLE _cr_thread_handle(struct CR_THREAD *t)
{
    return t ? t->thread_handle : 0;
}

/* Non-blocking poll: has the thread terminated yet?
 *
 * Returns 1 = thread terminated (handle signaled, _entry done or
 * crashed), 0 = still running, -1 = error or invalid ctx.
 *
 * NEVER blocks. The C side deliberately exposes no blocking-wait
 * primitive: any actual waiting is the Lua side's job, which lets a
 * future reactor intercept and replace blocking shapes with coroutine
 * yields without touching this file. Lua-side `nt.thread:wait` is
 * implemented over `nt.dll.ke.NtWaitForSingleObject` on the borrowed
 * thread handle. */
EXPORT int _cr_thread_done(struct CR_THREAD *t)
{
    LARGE_INTEGER zero;
    NTSTATUS st;
    if (!t || !t->thread_handle) return -1;
    zero.LowPart = 0; zero.HighPart = 0;
    st = NtWaitForSingleObject(t->thread_handle, 0, &zero);
    if (st == 0)              return 1;
    if (st == STATUS_TIMEOUT) return 0;
    return -1;
}

/* Caller must confirm via _cr_thread_done (or wait on the thread
 * handle Lua-side) that the thread has terminated before reading a
 * result. *out_buf and *out_len are written; pointer remains valid
 * until _cr_thread_close. Returns thread status:
 *   0 = ok (chunk returned a string),
 *   1 = lua error (out_buf carries the error message),
 *   2 = panic (couldn't create lua_State; out_buf empty),
 *   3 = crash (thread terminated before its exit path ran). */
EXPORT int _cr_thread_result(struct CR_THREAD *t,
                             const char **out_buf, ULONG *out_len)
{
    if (!t) return -1;
    if (out_buf) *out_buf = t->out_buf;
    if (out_len) *out_len = t->out_len;
    return t->status;
}

/* Drop the parent's reference. Returns immediately, never blocks.
 *
 * Three cases:
 *
 *   1. Thread still running: drop parent's ref. Thread keeps its ref
 *      and will run cleanup when it eventually exits. ctx survives.
 *
 *   2. Thread exited normally: thread already _release'd before
 *      terminating. Parent's _release brings refcount to zero,
 *      triggering _free_all (TLS cleanup + handle close + heap free).
 *
 *   3. Thread crashed (terminated without running its exit path —
 *      uncaught native exception, chunk-self-NtTerminateThread,
 *      stack overflow): thread's _release was never called, so
 *      refcount is still 2. Without intervention, parent's drop
 *      would leave refcount at 1 and ctx (including the leaked
 *      TLS index bitmap) would live until process exit.
 *
 *      We detect this by polling thread_handle for signaled state
 *      and CAS-claiming the thread_released flag. If we win the CAS
 *      (flag was 0 → thread didn't reach its exit), we _release on
 *      the thread's behalf so the refcount can reach zero and
 *      _free_all reclaims the TLS indices.
 *
 * Crucially this does NOT call NtTerminateThread. Killing a thread
 * that holds a kernel critical section (e.g. mid-LdrLoadDll, mid-
 * heap-allocation) leaves that section locked forever and deadlocks
 * the rest of the process. Cooperative cancellation (signal an event
 * the chunk waits on) is the caller's responsibility. */
EXPORT void _cr_thread_close(struct CR_THREAD *t)
{
    LARGE_INTEGER zero;

    if (!t) return;

    /* Crash detection: thread_handle becomes signaled on ANY thread
     * exit, normal or abnormal. If signaled and the thread didn't
     * mark itself released (CAS-claim wins), it crashed — release on
     * its behalf so refcount can reach zero and TLS cleanup runs. */
    if (t->thread_handle) {
        zero.LowPart = 0; zero.HighPart = 0;
        if (NtWaitForSingleObject(t->thread_handle, 0, &zero) == 0 &&
            __sync_lock_test_and_set(&t->thread_released, 1) == 0)
        {
            _release(t);   /* extra decrement on the dead thread's behalf */
        }
    }

    _release(t);           /* parent's own decrement */
}

/* ---- native Lua module ---------------------------------------------
 *
 * Registered as `nt._thread` via package.preload (see lua_main.c's
 * luaL_openlibs wrapper) — internal plumbing under the public nt.thread
 * surface, the leading underscore signals "don't reach for this
 * directly".  Going through ffi.C would fall back to LuaJIT's
 * default-lib chain (EXE / DLL / CRT), which does carry these symbols
 * for our build but the native registration is more honest: cr_thread's
 * shape is Lua-call-stack, not C-ABI.
 *
 * The C-side `_cr_thread_*` exports remain in lua.dll's PE export
 * table for now — harmless, in case something else (a debugger
 * extension, a future C consumer) wants the raw entries — but Lua
 * code only ever reaches them via this module.
 *
 * Userdata layout: a single pointer to CR_THREAD; __gc closes the ctx.
 */

#define CR_THREAD_MT "nt._thread"

struct cr_thread_ud {
    struct CR_THREAD *t;
};

static struct cr_thread_ud *_check_ud(lua_State *L, int idx)
{
    return (struct cr_thread_ud *)luaL_checkudata(L, idx, CR_THREAD_MT);
}

static int l_spawn(lua_State *L)
{
    size_t      chunk_len, payload_len;
    const char *chunk   = luaL_checklstring(L, 1, &chunk_len);
    const char *payload = luaL_optlstring (L, 2, "", &payload_len);
    struct CR_THREAD     *t;
    struct cr_thread_ud  *ud;

    t = _cr_thread_spawn(chunk,   (ULONG)chunk_len,
                         payload, (ULONG)payload_len);
    if (t == 0) {
        lua_pushnil(L);
        lua_pushliteral(L, "cr_thread.spawn: heap or thread create failed");
        return 2;
    }

    ud = (struct cr_thread_ud *)lua_newuserdata(L, sizeof(*ud));
    ud->t = t;
    luaL_getmetatable(L, CR_THREAD_MT);
    lua_setmetatable(L, -2);
    return 1;
}

/* :handle() — return the borrowed thread HANDLE as a lightuserdata.
 * Lifetime of the underlying kernel handle is bound to the ctx; caller
 * must not NtClose it.  Pair with nt.dll.handle.borrow(...) to wrap. */
static int l_handle(lua_State *L)
{
    struct cr_thread_ud *ud = _check_ud(L, 1);
    if (ud->t == 0) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, _cr_thread_handle(ud->t));
    return 1;
}

/* :done() — non-blocking poll. Returns 1 / 0 / -1 (matches the C
 * shape; Lua side typically converts to a boolean). */
static int l_done(lua_State *L)
{
    struct cr_thread_ud *ud = _check_ud(L, 1);
    lua_pushinteger(L, _cr_thread_done(ud->t));
    return 1;
}

/* :result() — returns (status, output_string).  status is one of
 * STATUS_OK / STATUS_LUA_ERROR / STATUS_PANIC / STATUS_CRASH (0..3).
 * Lua side maps these to "ok"/"error"/"panic"/"crash". */
static int l_result(lua_State *L)
{
    struct cr_thread_ud *ud = _check_ud(L, 1);
    const char *buf = "";
    ULONG       len = 0;
    int status;

    if (ud->t == 0) {
        lua_pushinteger(L, -1);
        lua_pushliteral(L, "");
        return 2;
    }
    status = _cr_thread_result(ud->t, &buf, &len);
    lua_pushinteger(L, status);
    lua_pushlstring(L, buf, (size_t)len);
    return 2;
}

/* :close() / __gc — drop parent's reference. Idempotent: both paths
 * may fire (manual close + GC) and only the first does work. */
static int l_close(lua_State *L)
{
    struct cr_thread_ud *ud = _check_ud(L, 1);
    if (ud->t != 0) {
        _cr_thread_close(ud->t);
        ud->t = 0;
    }
    return 0;
}

static const luaL_Reg cr_thread_methods[] = {
    { "handle", l_handle },
    { "done",   l_done   },
    { "result", l_result },
    { "close",  l_close  },
    { 0, 0 }
};

static const luaL_Reg cr_thread_module[] = {
    { "spawn", l_spawn },
    { 0, 0 }
};

/* Lua's require-by-file convention encodes a `.` in the module name
 * as `__` in the C symbol: nt._thread → luaopen_nt__thread.  We use
 * preload (set up in lua_main.c) so the symbol name doesn't actually
 * matter for resolution, but matching the convention keeps things
 * unsurprising for anyone grepping. */
EXPORT int luaopen_nt__thread(lua_State *L)
{
    /* Metatable for the userdata.  __index = self so methods resolve. */
    luaL_newmetatable(L, CR_THREAD_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_close);
    lua_setfield(L, -2, "__gc");
    luaL_register(L, 0, cr_thread_methods);
    lua_pop(L, 1);

    /* Module table — exposes spawn(); methods are reached via :method
     * on the userdata returned by spawn. */
    lua_newtable(L);
    luaL_register(L, 0, cr_thread_module);
    return 1;
}
