!IF 0

Copyright (c) 1989  Microsoft Corporation

Module Name:

    sources.

Abstract:

    This file specifies the target component being built and the list of
    sources files needed to build that component.  Also specifies optional
    compiler switches and libraries that are unique for the component being
    built.


Author:

    Mark Lucovsky (markl) 18-Sep-1990

NOTE:   Commented description of this file is in \nt\bak\bin\sources.tpl

!ENDIF

MAJORCOMP=windows
MINORCOMP=base

TARGETNAME=kernel32
TARGETPATH=$(BASEDIR)\public\sdk\lib
TARGETTYPE=DYNLINK
# MicroNT: dropped ..\..\..\obj\*\conlib.lib — we don't lift WINCON/CLIENT.
# The console export surface is replaced by stdio.c (see SOURCES below).
# Original NT 3.5 ran the build from BASE/CLIENT/DAYTONA, where
# `..\..\obj` = BASE/obj and `..\..\..\obj` = WINDOWS/obj.  We're flat
# (no DAYTONA leaf) so paths shift one up: `..\obj` = BASE/obj and
# `..\..\obj` = WINDOWS/obj.
LINKLIBS=..\obj\*\baselib.lib \
       ..\..\obj\*\nlslib.lib \
       $(BASEDIR)\public\sdk\lib\*\ntdll.lib

DLLENTRY=BaseDllInitialize

# MicroNT: original NT 3.5 ran the build from BASE/CLIENT/DAYTONA, where
# `..` = CLIENT and `..\..\inc` = BASE/INC.  We're flat (no DAYTONA leaf)
# so paths shift one up.  `.` (implicit cwd) covers BASE/CLIENT itself.
INCLUDES=..\inc;..\..\inc

CONDITIONAL_INCLUDES=doswin32.h

# MicroNT: -D_NTSYSTEM_ flips NTSYSAPI from __declspec(dllimport) to
# nothing.  Two effects we need:
#   1. csrstub.c defines Csr* primitives locally — without _NTSYSTEM_,
#      the include of ntcsrdll.h sees dllimport on the declarations,
#      MSVC then auto-promotes our definitions to dllexport, and Csr*
#      leaks into kernel32's export table.  We don't want that.
#   2. process.c / thread.c / vdm.c / dllatom.c / debug.c / support.c
#      and nlslib's section.c / tables.c also call Csr*; without
#      _NTSYSTEM_ they emit `call [__imp__CsrFoo@N]` indirect-through-IAT,
#      and the linker can't find import thunks (Csr* aren't in any .def).
#      With _NTSYSTEM_ they emit direct `call _CsrFoo@N` and the
#      linker resolves to csrstub.obj's local definitions.
# Side effect: every other NTSYSAPI symbol (RtlInit*, NtCreateFile, ...)
# also loses dllimport hint, so the linker satisfies them via ntdll.lib's
# non-decorated pseudo-thunks (one extra jmp per call, negligible).
# ntdll itself uses _NTSYSTEM_ to define these same symbols; we play
# the same role for Csr*.
C_DEFINES=$(C_DEFINES) -D_KERNEL32_ -D_NTSYSTEM_

SYNCHRONIZE_BLOCK=1

#
# Setup for application developer to set breakpoints with windbg
#

# MicroNT: original NT 3.5 ran from BASE/CLIENT/DAYTONA, so
# `..\kernel32.prf` resolved up to BASE/CLIENT/kernel32.prf.  Flat
# layout — kernel32.prf is in cwd.
DLLORDER=kernel32.prf

NTDEBUGTYPE=coff

SOURCES= ..\base.rc    \
         ..\beep.c     \
         ..\compname.c \
         ..\curdir.c   \
         ..\datetime.c \
         ..\debug.c    \
         ..\dir.c      \
         ..\dllatom.c  \
         ..\dllini.c   \
         ..\dosdev.c   \
         ..\error.c    \
         ..\filefind.c \
         ..\filehops.c \
         ..\filemap.c  \
         ..\filemisc.c \
         ..\fileopcr.c \
         ..\gmem.c     \
         ..\handle.c   \
         ..\baseinit.c \
         ..\lcompat.c  \
         ..\lmem.c     \
         ..\mailslot.c \
         ..\message.c  \
         ..\module.c   \
         ..\namepipe.c \
         ..\pathmisc.c \
         ..\perfctr.c  \
         ..\pipe.c     \
         ..\process.c  \
         ..\support.c  \
         ..\synch.c    \
         ..\thread.c   \
         ..\ustubs.c   \
         ..\stdio.c    \
         ..\csrstub.c  \
         ..\comm.c     \
         ..\vdm.c      \
         ..\updres.c   \
         ..\blddcb.c   \
         ..\tapeapi.c  \
         ..\debugint.c

UMTYPE=console
UMAPPL=dh
UMTEST=
UMLIBS=
OPTIONAL_UMTEST=tatom*tprof*taskstrs*winperf*tbase*tprof*tfile*ttask*tmem

NTTARGETFILE0=obj\*\base.res

DLLDEF=obj\*\kernel32.def
