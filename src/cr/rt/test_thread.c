/*
 * rt/test_thread.c -- tiny native entry points used by the selftest
 * harness to verify RtlCreateUserThread actually runs our code on a
 * new OS thread. Not linked from production code paths; kept
 * separate from k32_* / libc_* shims so the selftest dependency is
 * visible at file level.
 *
 * __attribute__((dllexport)) puts each symbol in the PE export table
 * so LuaJIT ffi.C (which goes through GetProcAddress -- the PE
 * export directory) can find it by name. Without this the EXE has
 * an empty export table and ffi.C returns 127 (ERROR_PROC_NOT_FOUND).
 * --kill-at on the linker strips the stdcall @N suffix so the
 * exported name is plain rt_thread_flag_set, not _rt_thread_flag_set@4.
 *
 * Stdcall with one PVOID arg matches PUSER_THREAD_START_ROUTINE, so
 * ntdll RtlUserThreadStart (the trampoline RtlCreateUserThread sets
 * up) calls into these cleanly and auto-calls NtTerminateThread when
 * we return.
 */

#define EXPORT __attribute__((dllexport)) __attribute__((stdcall))

/*
 * Writes 0x12345678 through its pointer argument. The test harness
 * allocates a ULONG, passes its address as the thread parameter,
 * then waits on the thread handle and reads the value back.
 */
EXPORT unsigned long rt_thread_flag_set(void *param)
{
    if (param) {
        *(volatile unsigned long *)param = 0x12345678UL;
    }
    return 0;
}

/*
 * Signals a given event handle and returns. Pairs with an
 * NtWaitForSingleObject in the parent so the thread test doesn't
 * need a polling loop. Thread parameter is the event HANDLE.
 */
unsigned long __attribute__((stdcall))
    NtSetEvent(void *EventHandle, long *PreviousState);

EXPORT unsigned long rt_thread_signal_event(void *param)
{
    if (param) {
        NtSetEvent(param, (long *)0);
    }
    return 0;
}
