-- nt.nls — boot-time publisher for the \NLS\ named-section namespace.
--
-- The MS NT 3.5 nlslib (kernel32's NLS half — MultiByteToWideChar,
-- WideCharToMultiByte, LCMapStringW, GetCPInfo, ...) opens these by
-- name at every process start: \NLS\NlsSectionUnicode, NlsSectionLocale,
-- NlsSectionCType, NlsSectionLANG_INTL, NlsSectionCP<id>. In stock NT,
-- basesrv (running inside csrss) creates the sections and stashes a
-- handle per section in csrss so the named objects survive past the
-- creating process's exit.
--
-- MicroNT has no csrss. This module replicates basesrv's publish step
-- using OBJ_PERMANENT on NtCreateSection: the named object outlives
-- the publisher process by virtue of the namespace flag, so subsequent
-- processes' nlslib does NtOpenSection on the name and gets a working
-- mapping with no further bootstrapping.
--
-- Divergence from BASESRV/SRVNLS.C:169 (BaseSrvNlsPreserveSection):
-- basesrv uses OBJ_OPENIF | OBJ_CASE_INSENSITIVE on create and keeps a
-- handle in csrss. We use OBJ_PERMANENT instead so we don't need a
-- live publisher process. Net effect identical for nlslib clients.
--
-- Caller must hold SeCreatePermanentPrivilege when invoking publish().
-- Use se.with_privileges({"SeCreatePermanentPrivilege"}, nls.publish)
-- for the textbook one-liner.

local bit = require('bit')

local fs  = require('nt.dll.fs')
local mm  = require('nt.dll.mm')
local ob  = require('nt.dll.ob')
local oa  = require('nt.dll.oa')

local NLS_DIR = "\\NLS"
local SYS32   = "\\SystemRoot\\System32\\"

-- Sortkey + SortTbls are static read-only weight tables, same shape
-- as the others.  The per-locale RW section that nlslib materializes
-- in WINNLS/TABLES.C:GetSortkeyFileInfo is a separate concern — it
-- only kicks in for locales that have *exceptions* to the default
-- weights (Czech, Slovak, traditional Spanish, …).  For LANG_INTL
-- (US English, the default) and most Latin-script locales, nlslib
-- falls through to the read-only default sortkey we publish here.
-- The runtime RW path falls back to STATUS_PORT_DISCONNECTED in
-- MicroNT — fine until someone wants exception-locale collation.
local SECTIONS = {
    { sec = "NlsSectionUnicode",   file = "unicode.nls"  },
    { sec = "NlsSectionLocale",    file = "locale.nls"   },
    { sec = "NlsSectionCType",     file = "ctype.nls"    },
    { sec = "NlsSectionLANG_INTL", file = "l_intl.nls"   },
    { sec = "NlsSectionCP1252",    file = "c_1252.nls"   },
    { sec = "NlsSectionCP437",     file = "c_437.nls"    },
    { sec = "NlsSectionSortkey",   file = "sortkey.nls"  },
    { sec = "NlsSectionSortTbls",  file = "sorttbls.nls" },
}

-- Attribute set for both the \NLS directory and every section under it.
-- OBJ_PERMANENT detaches namespace lifetime from this handle's death;
-- OBJ_OPENIF turns a re-publish into a no-op (idempotent boot) instead
-- of STATUS_OBJECT_NAME_COLLISION.
local PERMANENT_NAMED = {
    oa.OBJ_PERMANENT,
    oa.OBJ_CASE_INSENSITIVE,
    oa.OBJ_OPENIF,
}

local M = {}

-- Make sure \NLS exists. Without this, NtCreateSection on \NLS\Foo
-- returns STATUS_OBJECT_PATH_NOT_FOUND.
local function ensure_nls_directory()
    local DIRECTORY_ALL_ACCESS = 0xF000F
    local noa = oa.path(NLS_DIR, PERMANENT_NAMED)
    ob.NtCreateDirectoryObject(DIRECTORY_ALL_ACCESS, noa.oa):close()
end

-- Publish one named section backed by a file. No pcall — STATUS_*
-- from the underlying syscalls (file missing, ACL denied, namespace
-- collision the kernel didn't smooth via OBJ_OPENIF) come straight up.
-- Partial \NLS\ is worse than none — boot must die loud.
local function publish_one(sec_name, file_name)
    local file_oa = oa.path(SYS32 .. file_name)
    local hf = fs.NtOpenFile(
        bit.bor(fs.FILE_GENERIC_READ, fs.SYNCHRONIZE),
        file_oa.oa,
        fs.FILE_SHARE_READ,
        fs.FILE_SYNCHRONOUS_IO_NONALERT)

    local sec_oa = oa.path(NLS_DIR .. "\\" .. sec_name, PERMANENT_NAMED)
    local hs = mm.NtCreateSection(
        mm.SECTION_MAP_READ,
        sec_oa.oa,
        nil,                          -- max_size: take from file
        mm.PAGE_READONLY,
        mm.SEC_COMMIT,
        hf)

    -- The section keeps its own ref to the file's FILE_OBJECT, so
    -- closing the file handle doesn't tear down the backing pages.
    -- Same pattern as WINNLS/SECTION.C:781-787 in NT 3.5 source.
    hf:close()
    -- OBJ_PERMANENT detached the namespace entry from this handle's
    -- lifetime; the named section persists until next boot or until
    -- somebody calls NtMakeTemporaryObject on it.
    hs:close()
end

function M.publish()
    ensure_nls_directory()
    for _, s in ipairs(SECTIONS) do
        publish_one(s.sec, s.file)
    end
end

return M
