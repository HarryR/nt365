-- ntosbe layer: msvc
--
-- The MSVC NT 3.5 toolchain, staged under \SystemRoot\pkg\msvc20\.
-- pkg/ is the convention for optional bundles; the toolchain isn't part
-- of the OS surface.  The loader still finds kernel32.dll / ntdll.dll
-- via System32 (standard NT search order), so the toolchain EXEs don't
-- need to be on the loader path.
--
-- Files only — no drivers, no SYSTEM-hive services.  Needed by the
-- selfhost profile (test.msvc spawns these EXEs; test.ntosbe drives
-- NMAKE in-OS).

local M = {}

M.name = "msvc"
M.description = "MSVC NT 3.5 toolchain (ML/MC/RC/LINK/CL/NMAKE) + cmd.exe"

function M.files(paths)
    -- CL chain (CL/CL386/C1/C1XX/C2) is the full driver; RC pulls the
    -- RCDLL chain; LINK auto-invokes CVPACK on every CV-debug link;
    -- splitsym + dbg2dwf are the debug-info post-build pass.  CRTDLL is
    -- the CRT used by NMAKE / LINK / CVTRES — it lives in PUBLIC/SDK/LIB
    -- on the host tree, not OAK/BIN, so its source path differs.
    local msvc = paths.nt .. "/PUBLIC/OAK/BIN/I386"
    return {
        { dest = "pkg/msvc20/ML.EXE",       src = msvc .. "/ML.EXE"       },
        { dest = "pkg/msvc20/ML.ERR",       src = msvc .. "/ML.ERR"       },
        { dest = "pkg/msvc20/MC.EXE",       src = msvc .. "/MC.EXE"       },
        { dest = "pkg/msvc20/CVTRES.EXE",   src = msvc .. "/CVTRES.EXE"   },
        { dest = "pkg/msvc20/cvtres.err",   src = msvc .. "/cvtres.err"   },
        { dest = "pkg/msvc20/RC.EXE",       src = msvc .. "/RC.EXE"       },
        { dest = "pkg/msvc20/RCDLL.DLL",    src = msvc .. "/RCDLL.DLL"    },
        { dest = "pkg/msvc20/RCPP.EXE",     src = msvc .. "/RCPP.EXE"     },
        { dest = "pkg/msvc20/RCPP.ERR",     src = msvc .. "/RCPP.ERR"     },
        { dest = "pkg/msvc20/LINK.EXE",     src = msvc .. "/LINK.EXE"     },
        { dest = "pkg/msvc20/LINK.ERR",     src = msvc .. "/LINK.ERR"     },
        { dest = "pkg/msvc20/CVPACK.EXE",   src = msvc .. "/CVPACK.EXE"   },
        { dest = "pkg/msvc20/cvpack.err",   src = msvc .. "/cvpack.err"   },
        { dest = "pkg/msvc20/CL.EXE",       src = msvc .. "/CL.EXE"       },
        { dest = "pkg/msvc20/CL386.EXE",    src = msvc .. "/CL386.EXE"    },
        { dest = "pkg/msvc20/CL.ERR",       src = msvc .. "/CL.ERR"       },
        { dest = "pkg/msvc20/C1.EXE",       src = msvc .. "/C1.EXE"       },
        { dest = "pkg/msvc20/C1.ERR",       src = msvc .. "/C1.ERR"       },
        { dest = "pkg/msvc20/C1XX.EXE",     src = msvc .. "/C1XX.EXE"     },
        { dest = "pkg/msvc20/C2.EXE",       src = msvc .. "/C2.EXE"       },
        { dest = "pkg/msvc20/CL32.MSG",     src = msvc .. "/CL32.MSG"     },
        { dest = "pkg/msvc20/MSVCRT20.DLL", src = msvc .. "/MSVCRT20.DLL" },
        { dest = "pkg/msvc20/DBI.DLL",      src = msvc .. "/DBI.DLL"      },
        { dest = "pkg/msvc20/NMAKE.EXE",    src = msvc .. "/NMAKE.EXE"    },
        { dest = "pkg/msvc20/NMAKE.ERR",    src = msvc .. "/NMAKE.ERR"    },
        { dest = "pkg/msvc20/CRTDLL.DLL",   src = paths.sdk_lib .. "/CRTDLL.DLL" },
        { dest = "pkg/msvc20/SPLITSYM.EXE", src = msvc .. "/SPLITSYM.EXE" },
        { dest = "pkg/msvc20/DBG2DWF.EXE",  src = msvc .. "/DBG2DWF.EXE"  },
        { dest = "pkg/msvc20/IMAGEHLP.DLL", src = msvc .. "/IMAGEHLP.DLL" },
        -- NT 3.5 cmd.exe (in-tree at WINDOWS/CMD/).  NMAKE shells inline
        -- commands through COMSPEC; tchain's NT_ENV points COMSPEC at
        -- this exact path.  Real cmd.exe handles `if exist`, `for`,
        -- `set`, `%VAR%` expansion that a cmd-stub can't.
        { dest = "pkg/msvc20/cmd.exe",
          src  = paths.nt .. "/PRIVATE/WINDOWS/CMD/obj/i386/cmd.exe" },
    }
end

return M
