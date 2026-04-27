/*
 * lua_main.c — DLL entry + LuajitMain wrapper for lua.dll.
 *
 * lua.dll bundles three things:
 *
 *   1.  The LuaJIT VM (entire libluajit.a — the public Lua C API:
 *       lua_*, luaL_*, luaJIT_*).
 *   2.  The librt internals (libc_*, k32_* shims that LuaJIT calls into,
 *       since we have no kernel32.dll on MicroNT).  Internal — *not*
 *       part of lua.dll's contract; consumers should reach the Win32-
 *       shaped surface only via documented kernel32 names if/when we
 *       ever ship a real kernel32 DLL.
 *   3.  The C escape hatches: cr_thread (`_cr_thread_*`).  Exported,
 *       part of the contract — every Lua-running process gets the same
 *       environment regardless of which EXE loaded the VM.
 *
 * This file provides the DLL's entry-point glue:
 *
 *   - DllMain — runs ntshim_init() on PROCESS_ATTACH so this DLL's
 *     own libc state (heap / clock / stdio) is live before any
 *     consumer calls into us.
 *   - LuajitMain — the entry run.exe (and any future host EXE) calls
 *     to drive the LuaJIT interpreter.  Forwards argc/argv into the
 *     standalone luajit.c's main().  Renaming would have meant
 *     patching the LuaJIT submodule; we simply call through.
 */

#include <windows.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

extern void  ntshim_init(void);
extern int   main(int argc, char **argv);
extern int   luaopen_nt__thread(lua_State *L);

/*
 * luaL_openlibs wrapper — opens the standard LuaJIT libs via the real
 * implementation (resolved through ld --wrap), then registers our
 * native modules into package.preload so chunks pick them up via
 *
 *   local _t = require('nt._thread')
 *
 * not via ffi.C symbol lookup.  The nt._thread name is internal
 * plumbing under the public nt.thread Lua module; the underscore
 * marks it "don't reach for this directly."
 */
extern void __real_luaL_openlibs(lua_State *L);

void __wrap_luaL_openlibs(lua_State *L)
{
    __real_luaL_openlibs(L);

    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, luaopen_nt__thread);
    lua_setfield(L, -2, "nt._thread");
    lua_pop(L, 2);
}

__declspec(dllexport)
int LuajitMain(int argc, char **argv)
{
    return main(argc, argv);
}

__attribute__((used))
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst;
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        ntshim_init();
    }
    return TRUE;
}
