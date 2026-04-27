/*
 * run.c — thin trampoline.  rt/entry.c parses argv from the PEB and
 * calls into the main() below; we forward straight into LuajitMain.
 *
 * No Lua state, no Lua C API, no LuaJIT VM in this binary.  All of
 * that lives in lua.dll, which Ldr maps into our process (and any
 * other Lua-using process) with shared R/X pages — NT's section-
 * object aliasing gives us CoW for free.
 *
 * LuajitMain is statically imported (dllimport) from lua.dll; the
 * kernel-side image loader resolves it at process startup, runs
 * lua.dll's DllMain (which calls ntshim_init for the DLL's own libc
 * state), and by the time main() runs the import is bound and the
 * VM is ready.
 *
 * For Ldr to find lua.dll, the process's DllPath must include the
 * directory containing it.  See cr/Makefile + mkhive.py for how that's
 * arranged on the initial-process side.
 */

__declspec(dllimport) int LuajitMain(int argc, char **argv);

int main(int argc, char **argv)
{
    return LuajitMain(argc, argv);
}
