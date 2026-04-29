#
# MicroNT MAKEFILE.INC for BASE/CLIENT (kernel32.dll).
#
# Diverges from the original NT 3.5 DAYTONA/MAKEFILE.INC, which assumed
# winerror.rc + msg00001.bin already existed in PRIVATE/WINDOWS/NLSMSG/
# from a prior nmake pass and just copied them across.  Our flatter
# layout has no DAYTONA leaf and no prior NLSMSG build step, so we
# invoke mc inline from the .mc source.  Result lands in BASE/CLIENT/
# directly, ready for base.rc to #include.
#
# Relative paths (we live at PRIVATE/WINDOWS/BASE/CLIENT):
#   ..\..\nlsmsg\        — sibling NLSMSG dir holding winerror.mc
#   ..\..\winnls\        — sibling WINNLS dir (referenced by base.rc)
#

winerror.rc msg00001.bin: ..\..\nlsmsg\winerror.mc
    mc -r . -h . ..\..\nlsmsg\winerror.mc

$(NTTARGETFILE0): msg00001.bin winerror.rc base.rc ..\..\winnls\winnls.rc
