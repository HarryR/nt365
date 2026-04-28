-- nt.dll.se — Security subsystem (NTOS/SE) bindings.
--
-- The C surface for SE is awkward in three ways:
--   1. SIDs are variable-length (8 byte header + 4*N subauths)
--   2. Privileges identify by LUID, not name
--   3. Token query/set goes through a single info-class dispatch with
--      ten different result/argument shapes
--
-- This wrapper turns each into Lua-idiomatic shape:
--   1. SID is a constructor: se.sid(authority, ...subauths) — Lua table
--      holding an unsigned-char buffer with the on-wire bytes. Cached
--      SDDL string on the side for tostring(). Value-equality via
--      RtlEqualSid in __eq.
--   2. Privileges go in/out by name ("SeShutdownPrivilege"). The LUID
--      table is hard-coded from NTSEAPI.H:941-972 (ABI-stable).
--   3. se.query(tok, 'user'|'groups'|...) and se.set(tok, class, value)
--      dispatch on a class string and normalize to plain Lua values.
--      No cdata escapes — every queried SID is copied into a fresh
--      se.sid() wrapper with its own buffer.
--
-- GC / lifetime contract
-- ----------------------
-- An NT_SID wrapper is a Lua table { _buf=cdata, _str=string }. The
-- table holds _buf as a strong reference, so the bytes stay alive as
-- long as the table is reachable. The cast to PSID happens via :_psid()
-- — underscore-prefixed because it returns a *borrowed* pointer and is
-- only valid while the parent table is reachable. Internal call sites
-- always pass it inline so LuaJIT keeps the table on the Lua stack
-- through the FFI call. Never extract it to a local that outlives the
-- table. (Same rule as handle.lua's __raw / handle.raw().)
--
-- Variable-length token records (TOKEN_USER, TOKEN_GROUPS, TOKEN_OWNER,
-- TOKEN_PRIVILEGES) used as syscall *input* are built as one fused
-- cdata buffer with the SID/LUID bytes inline — same one-allocation
-- pattern as oa.lua's NT_OA_PATH (OBJECT_ATTRIBUTES + UNICODE_STRING +
-- wbuf in one cdata). No PSID points into a separately-allocated SID;
-- everything lives or dies together.
--
-- Token records read OUT of the kernel are walked, the SID/LUID bytes
-- copied into freshly-allocated NT_SID wrappers, and the kernel buffer
-- is dropped. Returned values have no cdata aliasing with the kernel
-- buffer — caller may keep them indefinitely.

local ffi    = require('ffi')
local bit    = require('bit')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')
local handle = require('nt.dll.handle')

-- LuaJIT 2.1 ships both `unpack` (5.1) and `table.unpack` (5.2). Pin
-- one alias so the rest of the file doesn't dance around the diff.
local unpack = table.unpack or unpack

-- =====================================================================
-- ffi.cdef: types + ntdll function prototypes
--
-- pack(4) — NTSEAPI.H wraps its variable-length structs (LUID,
-- LUID_AND_ATTRIBUTES) in pshpack4.h. The other records here use
-- natural alignment that already lands on 4 (UCHAR/USHORT/ULONG only,
-- plus LARGE_INTEGER which we want 4-aligned to match how the kernel
-- was built). Forcing pack(4) globally avoids any LuaJIT-default-vs-MSC
-- 8.50 alignment drift.
-- =====================================================================
ffi.cdef[[
#pragma pack(push, 4)

typedef unsigned char  BOOLEAN;
typedef unsigned long  ACCESS_MASK;

typedef struct _LUID {
    ULONG LowPart;
    long  HighPart;
} LUID;

typedef struct _LUID_AND_ATTRIBUTES {
    LUID  Luid;
    ULONG Attributes;
} LUID_AND_ATTRIBUTES;

typedef struct _SID_AND_ATTRIBUTES {
    void *Sid;
    ULONG Attributes;
} SID_AND_ATTRIBUTES;

typedef struct _SID_IDENTIFIER_AUTHORITY {
    UCHAR Value[6];
} SID_IDENTIFIER_AUTHORITY;

/* The SID layout the kernel actually walks. We don't typedef SID with
 * an embedded array because NT defines it ANYSIZE_ARRAY[1] and our
 * buffers are sized exactly to fit on-wire bytes; we cast our raw
 * unsigned char[?] to this type when we need named field access. */
typedef struct _SID_HEADER {
    UCHAR                    Revision;
    UCHAR                    SubAuthorityCount;
    SID_IDENTIFIER_AUTHORITY IdentifierAuthority;
} SID_HEADER;

typedef struct _ACL {
    UCHAR  AclRevision;
    UCHAR  Sbz1;
    USHORT AclSize;
    USHORT AceCount;
    USHORT Sbz2;
} ACL;

typedef struct _ACE_HEADER {
    UCHAR  AceType;
    UCHAR  AceFlags;
    USHORT AceSize;
} ACE_HEADER;

/* KNOWN_ACE shape (allowed/denied/audit): header + mask + SID inline.
 * SidStart is the offset where SID bytes begin; we treat it as a marker
 * and take its address. */
typedef struct _KNOWN_ACE {
    ACE_HEADER  Header;
    ACCESS_MASK Mask;
    ULONG       SidStart;
} KNOWN_ACE;

/* TOKEN_* records — fixed headers; variable trailers walked manually. */
typedef struct _TOKEN_USER {
    SID_AND_ATTRIBUTES User;
} TOKEN_USER;

typedef struct _TOKEN_GROUPS_HDR {
    ULONG GroupCount;
    /* SID_AND_ATTRIBUTES Groups[GroupCount] follows */
} TOKEN_GROUPS_HDR;

typedef struct _TOKEN_PRIVILEGES_HDR {
    ULONG PrivilegeCount;
    /* LUID_AND_ATTRIBUTES Privileges[PrivilegeCount] follows */
} TOKEN_PRIVILEGES_HDR;

typedef struct _TOKEN_OWNER {
    void *Owner;
} TOKEN_OWNER;

typedef struct _TOKEN_PRIMARY_GROUP {
    void *PrimaryGroup;
} TOKEN_PRIMARY_GROUP;

typedef struct _TOKEN_DEFAULT_DACL {
    ACL *DefaultDacl;
} TOKEN_DEFAULT_DACL;

typedef struct _TOKEN_SOURCE {
    char SourceName[8];
    LUID SourceIdentifier;
} TOKEN_SOURCE;

typedef struct _TOKEN_STATISTICS {
    LUID          TokenId;
    LUID          AuthenticationId;
    LARGE_INTEGER ExpirationTime;
    int           TokenType;
    int           ImpersonationLevel;
    ULONG         DynamicCharged;
    ULONG         DynamicAvailable;
    ULONG         GroupCount;
    ULONG         PrivilegeCount;
    LUID          ModifiedId;
} TOKEN_STATISTICS;

/* SECURITY_QUALITY_OF_SERVICE — defined in nt.dll (init.lua), shared
 * with nt.dll.lpc. */

/* SECURITY_DESCRIPTOR — absolute form. NTSEAPI.H:793. In self-relative
 * form the same struct is followed by inline SID/ACL bytes and the
 * pointer fields are reused as ULONG offsets from the SD start. */
typedef unsigned short SECURITY_DESCRIPTOR_CONTROL;
typedef struct _SECURITY_DESCRIPTOR {
    UCHAR                       Revision;
    UCHAR                       Sbz1;
    SECURITY_DESCRIPTOR_CONTROL Control;
    void                       *Owner;
    void                       *Group;
    ACL                        *Sacl;
    ACL                        *Dacl;
} SECURITY_DESCRIPTOR;

typedef struct _GENERIC_MAPPING {
    ACCESS_MASK GenericRead;
    ACCESS_MASK GenericWrite;
    ACCESS_MASK GenericExecute;
    ACCESS_MASK GenericAll;
} GENERIC_MAPPING;

typedef struct _PRIVILEGE_SET_HDR {
    ULONG PrivilegeCount;
    ULONG Control;
    /* LUID_AND_ATTRIBUTES Privilege[PrivilegeCount] follows */
} PRIVILEGE_SET_HDR;

/* Token open + query + set */
NTSTATUS __stdcall NtOpenProcessToken(HANDLE Process, ACCESS_MASK Access,
                                      HANDLE *TokenHandle);
NTSTATUS __stdcall NtOpenThreadToken (HANDLE Thread,  ACCESS_MASK Access,
                                      BOOLEAN OpenAsSelf, HANDLE *TokenHandle);

NTSTATUS __stdcall NtQueryInformationToken(HANDLE TokenHandle,
                                           int  TokenInformationClass,
                                           void *TokenInformation,
                                           ULONG TokenInformationLength,
                                           ULONG *ReturnLength);
NTSTATUS __stdcall NtSetInformationToken  (HANDLE TokenHandle,
                                           int  TokenInformationClass,
                                           void *TokenInformation,
                                           ULONG TokenInformationLength);

NTSTATUS __stdcall NtAdjustPrivilegesToken(HANDLE TokenHandle,
                                           BOOLEAN DisableAllPrivileges,
                                           void *NewState,
                                           ULONG BufferLength,
                                           void *PreviousState,
                                           ULONG *ReturnLength);

NTSTATUS __stdcall NtAdjustGroupsToken(HANDLE TokenHandle,
                                       BOOLEAN ResetToDefault,
                                       void *NewState,
                                       ULONG BufferLength,
                                       void *PreviousState,
                                       ULONG *ReturnLength);

NTSTATUS __stdcall NtPrivilegeCheck(HANDLE ClientToken,
                                    void *RequiredPrivileges,
                                    BOOLEAN *Result);

NTSTATUS __stdcall NtCreateToken(HANDLE *TokenHandle,
                                 ACCESS_MASK DesiredAccess,
                                 OBJECT_ATTRIBUTES *ObjectAttributes,
                                 int  TokenType,
                                 LUID *AuthenticationId,
                                 LARGE_INTEGER *ExpirationTime,
                                 void *User,
                                 void *Groups,
                                 void *Privileges,
                                 void *Owner,
                                 void *PrimaryGroup,
                                 void *DefaultDacl,
                                 TOKEN_SOURCE *TokenSource);

NTSTATUS __stdcall NtDuplicateToken(HANDLE ExistingTokenHandle,
                                    ACCESS_MASK DesiredAccess,
                                    OBJECT_ATTRIBUTES *ObjectAttributes,
                                    BOOLEAN EffectiveOnly,
                                    int TokenType,
                                    HANDLE *NewTokenHandle);

NTSTATUS __stdcall NtAllocateLocallyUniqueId(LUID *Luid);

NTSTATUS __stdcall NtAccessCheck(void *SecurityDescriptor,
                                 HANDLE ClientToken,
                                 ACCESS_MASK DesiredAccess,
                                 GENERIC_MAPPING *GenericMapping,
                                 void *PrivilegeSet,
                                 ULONG *PrivilegeSetLength,
                                 ACCESS_MASK *GrantedAccess,
                                 NTSTATUS *AccessStatus);

NTSTATUS __stdcall NtSetSecurityObject(HANDLE Handle,
                                       ULONG SecurityInformation,
                                       void *SecurityDescriptor);
NTSTATUS __stdcall NtQuerySecurityObject(HANDLE Handle,
                                         ULONG SecurityInformation,
                                         void *SecurityDescriptor,
                                         ULONG Length,
                                         ULONG *LengthNeeded);

/* Thread-side: ThreadImpersonationToken for impersonate / revert. */
NTSTATUS __stdcall NtSetInformationThread(HANDLE ThreadHandle,
                                          int ThreadInformationClass,
                                          void *ThreadInformation,
                                          ULONG ThreadInformationLength);

/* Rtl helpers from RTL/SERTL.C + RTL/ACLEDIT.C — used internally only. */
NTSTATUS __stdcall RtlInitializeSid(void *Sid,
                                    SID_IDENTIFIER_AUTHORITY *Authority,
                                    UCHAR SubAuthorityCount);
ULONG    __stdcall RtlLengthSid(void *Sid);
BOOLEAN  __stdcall RtlValidSid(void *Sid);
BOOLEAN  __stdcall RtlEqualSid(void *Sid1, void *Sid2);
BOOLEAN  __stdcall RtlEqualPrefixSid(void *Sid1, void *Sid2);
ULONG *  __stdcall RtlSubAuthoritySid(void *Sid, ULONG SubAuthority);
NTSTATUS __stdcall RtlCopySid(ULONG DestinationLength, void *Destination,
                              void *Source);

NTSTATUS __stdcall RtlCreateSecurityDescriptor(void *SecurityDescriptor,
                                               ULONG Revision);
NTSTATUS __stdcall RtlSetOwnerSecurityDescriptor(void *SecurityDescriptor,
                                                 void *Owner,
                                                 BOOLEAN OwnerDefaulted);
NTSTATUS __stdcall RtlSetGroupSecurityDescriptor(void *SecurityDescriptor,
                                                 void *Group,
                                                 BOOLEAN GroupDefaulted);
NTSTATUS __stdcall RtlSetDaclSecurityDescriptor(void *SecurityDescriptor,
                                                BOOLEAN DaclPresent,
                                                ACL *Dacl,
                                                BOOLEAN DaclDefaulted);
NTSTATUS __stdcall RtlGetOwnerSecurityDescriptor(void *SecurityDescriptor,
                                                 void **Owner,
                                                 BOOLEAN *OwnerDefaulted);
NTSTATUS __stdcall RtlGetGroupSecurityDescriptor(void *SecurityDescriptor,
                                                 void **Group,
                                                 BOOLEAN *GroupDefaulted);
NTSTATUS __stdcall RtlGetDaclSecurityDescriptor(void *SecurityDescriptor,
                                                BOOLEAN *DaclPresent,
                                                ACL **Dacl,
                                                BOOLEAN *DaclDefaulted);
NTSTATUS __stdcall RtlAbsoluteToSelfRelativeSD(void *AbsoluteSD,
                                               void *SelfRelativeSD,
                                               ULONG *BufferLength);
BOOLEAN  __stdcall RtlValidSecurityDescriptor(void *SecurityDescriptor);
ULONG    __stdcall RtlLengthSecurityDescriptor(void *SecurityDescriptor);

NTSTATUS __stdcall RtlCreateAcl(ACL *Acl, ULONG AclLength, ULONG AclRevision);
NTSTATUS __stdcall RtlAddAccessAllowedAce(ACL *Acl, ULONG AceRevision,
                                          ACCESS_MASK AccessMask, void *Sid);
NTSTATUS __stdcall RtlAddAccessDeniedAce (ACL *Acl, ULONG AceRevision,
                                          ACCESS_MASK AccessMask, void *Sid);
NTSTATUS __stdcall RtlAddAuditAccessAce  (ACL *Acl, ULONG AceRevision,
                                          ACCESS_MASK AccessMask, void *Sid,
                                          BOOLEAN AuditSuccess,
                                          BOOLEAN AuditFailure);
NTSTATUS __stdcall RtlGetAce(ACL *Acl, ULONG AceIndex, void **Ace);

void __stdcall RtlMapGenericMask(ACCESS_MASK *AccessMask,
                                 GENERIC_MAPPING *GenericMapping);

NTSTATUS __stdcall RtlImpersonateSelf(int ImpersonationLevel);

#pragma pack(pop)
]]

-- =====================================================================
-- Constants — published surface
-- =====================================================================
local M = {}

-- TOKEN_INFORMATION_CLASS values (from NTSEAPI.H:1111).
local TOKEN_CLASS = {
    user                = 1,
    groups              = 2,
    privileges          = 3,
    owner               = 4,
    primary_group       = 5,
    default_dacl        = 6,
    source              = 7,
    type                = 8,
    impersonation_level = 9,
    statistics          = 10,
}

-- TOKEN_TYPE.
local TOKEN_TYPE = { primary = 1, impersonation = 2 }
local TOKEN_TYPE_NAME = { [1] = 'primary', [2] = 'impersonation' }

-- SECURITY_IMPERSONATION_LEVEL.
local IMP_LEVEL = {
    anonymous      = 0,
    identification = 1,
    impersonation  = 2,
    delegation     = 3,
}
local IMP_LEVEL_NAME = {
    [0] = 'anonymous', [1] = 'identification',
    [2] = 'impersonation', [3] = 'delegation',
}

-- ThreadImpersonationToken info-class index (NTPSAPI.H:299-304).
local THREAD_IMPERSONATION_TOKEN = 5

-- Token access masks (NTSEAPI.H:1061-1082).
M.TOKEN_ASSIGN_PRIMARY     = 0x0001
M.TOKEN_DUPLICATE          = 0x0002
M.TOKEN_IMPERSONATE        = 0x0004
M.TOKEN_QUERY              = 0x0008
M.TOKEN_QUERY_SOURCE       = 0x0010
M.TOKEN_ADJUST_PRIVILEGES  = 0x0020
M.TOKEN_ADJUST_GROUPS      = 0x0040
M.TOKEN_ADJUST_DEFAULT     = 0x0080
M.TOKEN_ALL_ACCESS         = 0x000F00FF
M.TOKEN_READ               = 0x00020008
M.TOKEN_WRITE              = 0x000200E0
M.TOKEN_EXECUTE            = 0x00020000

-- Privilege attribute bits (NTSEAPI.H:861-863).
M.SE_PRIVILEGE_ENABLED_BY_DEFAULT = 0x00000001
M.SE_PRIVILEGE_ENABLED            = 0x00000002
M.SE_PRIVILEGE_USED_FOR_ACCESS    = 0x80000000

-- Group attribute bits (NTSEAPI.H:445-448).
M.SE_GROUP_MANDATORY          = 0x00000001
M.SE_GROUP_ENABLED_BY_DEFAULT = 0x00000002
M.SE_GROUP_ENABLED            = 0x00000004
M.SE_GROUP_OWNER              = 0x00000008

-- ACE types (NTSEAPI.H).
local ACCESS_ALLOWED_ACE_TYPE = 0
local ACCESS_DENIED_ACE_TYPE  = 1
local SYSTEM_AUDIT_ACE_TYPE   = 2
local ACE_TYPE_NAME = {
    [ACCESS_ALLOWED_ACE_TYPE] = 'allowed',
    [ACCESS_DENIED_ACE_TYPE]  = 'denied',
    [SYSTEM_AUDIT_ACE_TYPE]   = 'audit',
}

-- ACE flags (NTSEAPI.H — inheritance bits + audit bits).
M.OBJECT_INHERIT_ACE         = 0x01
M.CONTAINER_INHERIT_ACE      = 0x02
M.NO_PROPAGATE_INHERIT_ACE   = 0x04
M.INHERIT_ONLY_ACE           = 0x08
M.SUCCESSFUL_ACCESS_ACE_FLAG = 0x40
M.FAILED_ACCESS_ACE_FLAG     = 0x80

-- Security-descriptor control bits (NTSEAPI.H:712-718).
M.SE_OWNER_DEFAULTED = 0x0001
M.SE_GROUP_DEFAULTED = 0x0002
M.SE_DACL_PRESENT    = 0x0004
M.SE_DACL_DEFAULTED  = 0x0008
M.SE_SACL_PRESENT    = 0x0010
M.SE_SACL_DEFAULTED  = 0x0020
M.SE_SELF_RELATIVE   = 0x8000

-- SECURITY_INFORMATION mask bits (NTSEAPI.H:1216-1219).
M.OWNER_SECURITY_INFORMATION = 0x01
M.GROUP_SECURITY_INFORMATION = 0x02
M.DACL_SECURITY_INFORMATION  = 0x04
M.SACL_SECURITY_INFORMATION  = 0x08
local DEFAULT_SECURITY_INFORMATION = 0x07   -- owner + group + dacl

-- Generic access bits (NTSEAPI.H:158-161). Almost every object type
-- defines its own per-bit mapping; pass via opts.mapping = {read=,
-- write=, execute=, all=} when calling se.access_check.
M.GENERIC_READ    = 0x80000000
M.GENERIC_WRITE   = 0x40000000
M.GENERIC_EXECUTE = 0x20000000
M.GENERIC_ALL     = 0x10000000

-- Standard rights / READ_CONTROL etc. — needed for owner-implicit
-- access bits (READ_CONTROL is granted to the SD owner automatically).
M.READ_CONTROL    = 0x00020000
M.WRITE_DAC       = 0x00040000
M.WRITE_OWNER     = 0x00080000

-- Revisions.
local SECURITY_DESCRIPTOR_REVISION = 1
local ACL_REVISION                 = 2

-- Status codes we surface as data (not raises) in access_check, since
-- "access denied" is a valid result of a successful syscall.
M.STATUS_ACCESS_DENIED = 0xC0000022
M.STATUS_SUCCESS       = 0x00000000

-- =====================================================================
-- Privilege LUIDs (NTSEAPI.H:941-972, ABI-stable in NT 3.5).
-- High part is always 0 for well-known privileges. Names match the
-- SE_*_NAME macros (NTSEAPI.H:910-933) and the kernel's own usage in
-- SE/TOKEN.C:655-732 — keep doc-grep against the NT source trivial.
-- =====================================================================
local PRIVILEGE_LUID_LOW = {
    SeCreateTokenPrivilege          = 2,
    SeAssignPrimaryTokenPrivilege   = 3,
    SeLockMemoryPrivilege           = 4,
    SeIncreaseQuotaPrivilege        = 5,
    SeUnsolicitedInputPrivilege     = 6, -- kept as alias; same LUID as MachineAccount
    SeMachineAccountPrivilege       = 6,
    SeTcbPrivilege                  = 7,
    SeSecurityPrivilege             = 8,
    SeTakeOwnershipPrivilege        = 9,
    SeLoadDriverPrivilege           = 10,
    SeSystemProfilePrivilege        = 11,
    SeSystemtimePrivilege           = 12,
    SeProfileSingleProcessPrivilege = 13,
    SeIncreaseBasePriorityPrivilege = 14,
    SeCreatePagefilePrivilege       = 15,
    SeCreatePermanentPrivilege      = 16,
    SeBackupPrivilege               = 17,
    SeRestorePrivilege              = 18,
    SeShutdownPrivilege             = 19,
    SeDebugPrivilege                = 20,
    SeAuditPrivilege                = 21,
    SeSystemEnvironmentPrivilege    = 22,
    SeChangeNotifyPrivilege         = 23,
    SeRemoteShutdownPrivilege       = 24,
}
M.PRIVILEGE_NAMES = PRIVILEGE_LUID_LOW   -- exposed read-only for callers

-- Reverse: LUID low part → name. Always pick the canonical name when
-- LUIDs collide (SeUnsolicitedInput is obsolete; report MachineAccount).
local LUID_LOW_TO_NAME = {}
for name, low in pairs(PRIVILEGE_LUID_LOW) do
    if name ~= 'SeUnsolicitedInputPrivilege' then
        LUID_LOW_TO_NAME[low] = name
    end
end

local function luid_low_for(name)
    local low = PRIVILEGE_LUID_LOW[name]
    if low == nil then
        error("se: unknown privilege name '" .. tostring(name) .. "'", 3)
    end
    return low
end

-- =====================================================================
-- NT_SID — Lua table holding an unsigned-char buffer with the on-wire
-- SID bytes. See file header for the lifetime contract.
-- =====================================================================

-- Build the on-wire byte layout from (authority, subauths) into a fresh
-- ffi.new('unsigned char[?]', ...) sized exactly. Returns the buffer.
local function build_sid_buf(authority, subauths)
    local n = #subauths
    if n > 15 then
        error("se: too many subauthorities (max 15, got " .. n .. ")", 3)
    end
    if authority < 0 or authority > 0xFFFFFFFFFFFFLL then
        error("se: authority out of range (0..2^48-1)", 3)
    end

    local sz  = 8 + n * 4   -- header (Rev+Cnt+IA[6]) + N*ULONG subauths
    local buf = ffi.new('unsigned char[?]', sz)

    buf[0] = 1       -- Revision (always 1; SID_REVISION from NTSEAPI.H:253)
    buf[1] = n       -- SubAuthorityCount

    -- IdentifierAuthority is 6 bytes, big-endian. Use bit ops on a
    -- 64-bit cdata to handle the upper 16 bits cleanly even though
    -- everything we ship uses only the low byte.
    local ia = ffi.cast('uint64_t', authority)
    for i = 0, 5 do
        buf[2 + i] = tonumber(bit.band(
            ffi.cast('uint64_t', bit.rshift(ia, (5 - i) * 8)),
            0xFF))
    end

    -- SubAuthorities are little-endian ULONGs at offsets 8, 12, 16, ...
    -- Cast a ULONG* over the tail and assign directly.
    if n > 0 then
        local subptr = ffi.cast('uint32_t *', buf + 8)
        for i = 0, n - 1 do
            subptr[i] = subauths[i + 1]
        end
    end

    return buf
end

-- Decode a SID buffer back into (authority:number, subauths:table).
-- Used both for tostring caching and for query-path SID copy-out.
local function decode_sid_buf(buf)
    local rev = buf[0]
    if rev ~= 1 then
        error("se: bad SID revision " .. tostring(rev), 3)
    end
    local n   = buf[1]
    local ia  = 0LL
    for i = 0, 5 do
        ia = bit.bor(bit.lshift(ia, 8), ffi.cast('uint64_t', buf[2 + i]))
    end
    local authority = tonumber(ia)
    local subauths  = {}
    if n > 0 then
        local subptr = ffi.cast('uint32_t *', buf + 8)
        for i = 0, n - 1 do
            subauths[i + 1] = subptr[i]
        end
    end
    return authority, subauths
end

local function sid_to_sddl(authority, subauths)
    local out = "S-1-" .. tostring(authority)
    for i = 1, #subauths do
        out = out .. "-" .. tostring(subauths[i])
    end
    return out
end

-- Copy a SID out of a foreign buffer (kernel result) into a fresh
-- NT_SID wrapper that owns its own bytes. The foreign buffer may be
-- freed immediately after.
local function sid_copy_from_buf(foreign_ptr)
    local p = ffi.cast('unsigned char *', foreign_ptr)
    local authority, subauths = decode_sid_buf(p)
    return M.sid(authority, unpack(subauths))
end

-- Walk a kernel ACL into the {allow|deny|audit=sid, mask=, ...} ACE
-- list shape — the same shape se.security_descriptor's input takes,
-- so a query result can round-trip directly into a fresh SD.
local function aces_from_acl(pacl)
    local out    = {}
    local cursor = ffi.cast('unsigned char *', pacl) + ffi.sizeof('ACL')
    for _ = 1, pacl.AceCount do
        local hdr  = ffi.cast('ACE_HEADER *', cursor)
        local kind = ACE_TYPE_NAME[hdr.AceType]
        if kind then
            local known    = ffi.cast('KNOWN_ACE *', cursor)
            local sid_addr = cursor + ffi.offsetof('KNOWN_ACE', 'SidStart')
            local sid      = sid_copy_from_buf(sid_addr)
            local mask     = known.Mask
            local flg      = hdr.AceFlags
            if kind == 'allowed' then
                out[#out + 1] = { allow = sid, mask = mask }
            elseif kind == 'denied' then
                out[#out + 1] = { deny  = sid, mask = mask }
            elseif kind == 'audit' then
                out[#out + 1] = {
                    audit = sid, mask = mask,
                    on_success = bit.band(flg, M.SUCCESSFUL_ACCESS_ACE_FLAG) ~= 0,
                    on_failure = bit.band(flg, M.FAILED_ACCESS_ACE_FLAG)     ~= 0,
                }
            end
        end
        cursor = cursor + hdr.AceSize
    end
    return out
end

-- Layer (ace_type, sid, flags) onto each ACE so query callers see the
-- legacy verbose shape on top of the canonical input shape.
local function decorate_aces_for_query(aces)
    for _, ace in ipairs(aces) do
        local sid = ace.allow or ace.deny or ace.audit
        ace.sid = sid
        if     ace.allow then ace.ace_type = 'allowed'
        elseif ace.deny  then ace.ace_type = 'denied'
        elseif ace.audit then ace.ace_type = 'audit'  end
        if not ace.flags then
            ace.flags = (ace.on_success and M.SUCCESSFUL_ACCESS_ACE_FLAG or 0)
                      + (ace.on_failure and M.FAILED_ACCESS_ACE_FLAG     or 0)
        end
    end
    return aces
end

local sid_mt = {}
sid_mt.__index = sid_mt

function sid_mt:authority()        return self._authority end
function sid_mt:subauthorities()
    -- Return a copy so the caller can't mutate our cached table.
    local out = {}
    for i, v in ipairs(self._subauths) do out[i] = v end
    return out
end
function sid_mt:length()           return ffi.sizeof(self._buf) end

-- BORROWED — valid only while `self` is reachable. Wrapper-internal.
function sid_mt:_psid()
    return ffi.cast('void *', self._buf)
end

sid_mt.__tostring = function(self) return self._str end

sid_mt.__eq = function(a, b)
    -- Both have to be NT_SID. Comparing against a string would silently
    -- be false (different metatables) which is fine — match Lua semantics.
    if getmetatable(a) ~= sid_mt or getmetatable(b) ~= sid_mt then
        return false
    end
    return ntdll.RtlEqualSid(a:_psid(), b:_psid()) ~= 0
end

-- se.sid(authority, sa1, sa2, ...) — primary constructor.
function M.sid(authority, ...)
    local subauths = { ... }
    local buf      = build_sid_buf(authority, subauths)
    local sid = {
        _buf       = buf,
        _authority = authority,
        _subauths  = subauths,
        _str       = sid_to_sddl(authority, subauths),
    }
    return setmetatable(sid, sid_mt)
end

-- Type predicate (mirrors handle.raw / ffi.istype style).
function M.is_sid(x)
    return type(x) == 'table' and getmetatable(x) == sid_mt
end

-- =====================================================================
-- Well-known SIDs the kernel allocates at boot in SE/SEGLOBAL.C.
--
-- The kernel itself only references LOCAL_SYSTEM_SID, ALIAS_ADMINS_SID,
-- WORLD_SID, CREATOR_OWNER_SID and CREATOR_GROUP_SID in its own default
-- DACLs and token init (SE/TOKEN.C, SE/SEASSIGN.C, SE/ACCESSCK.C,
-- IO/ARCSEC.C). The rest are constructed at boot and exported for
-- LSA/SAM/userland — we provide them for completeness, but in MicroNT
-- (no LSA) they are never assigned to any token by the kernel itself.
-- =====================================================================
M.NULL_SID                = M.sid(0, 0)            -- S-1-0-0
M.WORLD_SID               = M.sid(1, 0)            -- S-1-1-0
M.LOCAL_SID               = M.sid(2, 0)            -- S-1-2-0
M.CREATOR_OWNER_SID       = M.sid(3, 0)            -- S-1-3-0
M.CREATOR_GROUP_SID       = M.sid(3, 1)            -- S-1-3-1
M.NT_AUTHORITY_SID        = M.sid(5)               -- S-1-5
M.DIALUP_SID              = M.sid(5, 1)            -- S-1-5-1
M.NETWORK_SID             = M.sid(5, 2)            -- S-1-5-2
M.BATCH_SID               = M.sid(5, 3)            -- S-1-5-3
M.INTERACTIVE_SID         = M.sid(5, 4)            -- S-1-5-4
M.SERVICE_SID             = M.sid(5, 6)            -- S-1-5-6
M.LOCAL_SYSTEM_SID        = M.sid(5, 18)           -- S-1-5-18
M.ALIAS_ADMINS_SID        = M.sid(5, 32, 544)      -- S-1-5-32-544
M.ALIAS_USERS_SID         = M.sid(5, 32, 545)
M.ALIAS_GUESTS_SID        = M.sid(5, 32, 546)
M.ALIAS_POWER_USERS_SID   = M.sid(5, 32, 547)
M.ALIAS_ACCOUNT_OPS_SID   = M.sid(5, 32, 548)
M.ALIAS_SYSTEM_OPS_SID    = M.sid(5, 32, 549)
M.ALIAS_PRINT_OPS_SID     = M.sid(5, 32, 550)
M.ALIAS_BACKUP_OPS_SID    = M.sid(5, 32, 551)

-- SYSTEM_LUID — auth-id of the kernel-built system token.
-- LSA reserves the low 0x3e7 for "system"; see SE/RMVARS.C.
M.SYSTEM_AUTH_ID = { low = 0x3E7, high = 0 }

-- =====================================================================
-- Pseudo-handles for current process / thread
-- =====================================================================
local CURRENT_PROCESS = ffi.cast('HANDLE', ffi.cast('intptr_t', -1))
local CURRENT_THREAD  = ffi.cast('HANDLE', ffi.cast('intptr_t', -2))

local function proc_or_current(h)
    if h == nil then return CURRENT_PROCESS end
    return handle.raw(h)
end
local function thread_or_current(h)
    if h == nil then return CURRENT_THREAD end
    return handle.raw(h)
end

-- =====================================================================
-- Token open
-- =====================================================================

-- Default access for our common case: the test suite needs to query
-- the current token AND adjust privileges to flip SeCreateTokenPrivilege
-- on/off. Callers tightening security can pass an explicit access mask.
local DEFAULT_OPEN_ACCESS =
    M.TOKEN_QUERY + M.TOKEN_ADJUST_PRIVILEGES

function M.open_process_token(opts)
    opts = opts or {}
    local h  = ffi.new('HANDLE[1]')
    local st = ntdll.NtOpenProcessToken(
        proc_or_current(opts.process),
        opts.access or DEFAULT_OPEN_ACCESS,
        h)
    if err.is_error(st) then err.raise('NtOpenProcessToken', st) end
    return handle.wrap(h[0])
end

function M.open_thread_token(opts)
    opts = opts or {}
    local h  = ffi.new('HANDLE[1]')
    local st = ntdll.NtOpenThreadToken(
        thread_or_current(opts.thread),
        opts.access or DEFAULT_OPEN_ACCESS,
        opts.open_as_self and 1 or 0,
        h)
    if err.is_error(st) then err.raise('NtOpenThreadToken', st) end
    return handle.wrap(h[0])
end

-- =====================================================================
-- Generic info-class query buffer with grow-loop
-- =====================================================================
local STATUS_BUFFER_TOO_SMALL = 0xC0000023
local STATUS_BUFFER_OVERFLOW  = 0x80000005   -- informational, not error

local function query_token_buffer(tok, class_id, initial_size)
    local size = initial_size or 256
    local buf  = ffi.new('unsigned char[?]', size)
    local ret  = ffi.new('ULONG[1]')
    for _ = 1, 8 do
        local st = ntdll.NtQueryInformationToken(
            handle.raw(tok), class_id, buf, size, ret)
        local stu = err.normalize(st)
        if stu == STATUS_BUFFER_TOO_SMALL then
            local needed = ret[0]
            size = needed > size and needed or size * 2
            buf  = ffi.new('unsigned char[?]', size)
        elseif err.is_error(st) then
            err.raise('NtQueryInformationToken', st)
        else
            return buf, ret[0]
        end
    end
    error('NtQueryInformationToken: buffer did not converge')
end

-- =====================================================================
-- Token query — per-class normalizers
-- =====================================================================

-- Each query_<name> consumes the kernel buffer and returns a plain Lua
-- value. The buffer is dropped at the end of the dispatch — no aliasing
-- with returned SIDs (sid_copy_from_buf takes a private copy).

local function decode_priv_attrs(attrs)
    return {
        attributes         = attrs,
        enabled            = bit.band(attrs, M.SE_PRIVILEGE_ENABLED) ~= 0,
        enabled_by_default = bit.band(attrs, M.SE_PRIVILEGE_ENABLED_BY_DEFAULT) ~= 0,
        used_for_access    = bit.band(attrs, M.SE_PRIVILEGE_USED_FOR_ACCESS) ~= 0,
    }
end

local function query_user(buf)
    local rec = ffi.cast('TOKEN_USER *', buf)
    return {
        sid        = sid_copy_from_buf(rec.User.Sid),
        attributes = rec.User.Attributes,
    }
end

local function query_groups(buf)
    local hdr = ffi.cast('TOKEN_GROUPS_HDR *', buf)
    -- Groups[] starts immediately after the ULONG count. Each entry is
    -- SID_AND_ATTRIBUTES { void *Sid; ULONG Attributes; } — 8 bytes.
    local arr = ffi.cast('SID_AND_ATTRIBUTES *',
                         ffi.cast('unsigned char *', buf)
                       + ffi.sizeof('TOKEN_GROUPS_HDR'))
    local out = {}
    for i = 0, hdr.GroupCount - 1 do
        out[i + 1] = {
            sid        = sid_copy_from_buf(arr[i].Sid),
            attributes = arr[i].Attributes,
        }
    end
    return out
end

local function query_privileges(buf)
    local hdr = ffi.cast('TOKEN_PRIVILEGES_HDR *', buf)
    local arr = ffi.cast('LUID_AND_ATTRIBUTES *',
                         ffi.cast('unsigned char *', buf)
                       + ffi.sizeof('TOKEN_PRIVILEGES_HDR'))
    local out = {}
    for i = 0, hdr.PrivilegeCount - 1 do
        local low  = arr[i].Luid.LowPart
        local high = arr[i].Luid.HighPart
        local name = (high == 0) and LUID_LOW_TO_NAME[low] or nil
        local rec  = decode_priv_attrs(arr[i].Attributes)
        rec.name      = name or string.format("LUID(0x%x:0x%x)", high, low)
        rec.luid_low  = low
        rec.luid_high = high
        out[i + 1] = rec
    end
    return out
end

local function query_owner(buf)
    local rec = ffi.cast('TOKEN_OWNER *', buf)
    return sid_copy_from_buf(rec.Owner)
end

local function query_primary_group(buf)
    local rec = ffi.cast('TOKEN_PRIMARY_GROUP *', buf)
    return sid_copy_from_buf(rec.PrimaryGroup)
end

local function query_default_dacl(buf)
    local rec = ffi.cast('TOKEN_DEFAULT_DACL *', buf)
    if rec.DefaultDacl == nil then return nil end
    return decorate_aces_for_query(aces_from_acl(rec.DefaultDacl))
end

-- LUID → {low,high} table. LUID's HighPart is `long` (i32), LowPart is
-- ULONG (u32) — both fit a Lua number, no tonumber dance.
local function luid_to_table(luid)
    return { low = luid.LowPart, high = luid.HighPart }
end

local function query_source(buf)
    local rec = ffi.cast('TOKEN_SOURCE *', buf)
    -- SourceName is CHAR[8], NUL-padded but not necessarily NUL-terminated.
    local name_ptr = ffi.cast('const char *', rec.SourceName)
    return {
        name = ffi.string(name_ptr, 8):match("^%Z*"),  -- strip trailing NULs
        id   = luid_to_table(rec.SourceIdentifier),
    }
end

local function query_type(buf)
    local v = ffi.cast('int *', buf)[0]
    return TOKEN_TYPE_NAME[v] or string.format("unknown(%d)", v)
end

local function query_impersonation_level(buf)
    return IMP_LEVEL_NAME[ffi.cast('int *', buf)[0]]
end

local function query_statistics(buf)
    local rec = ffi.cast('TOKEN_STATISTICS *', buf)
    return {
        token_id            = luid_to_table(rec.TokenId),
        auth_id             = luid_to_table(rec.AuthenticationId),
        -- QuadPart is int64_t — the only field here that genuinely
        -- needs tonumber to cross from cdata to Lua.
        expiration          = tonumber(rec.ExpirationTime.QuadPart),
        token_type          = TOKEN_TYPE_NAME[rec.TokenType],
        impersonation_level = IMP_LEVEL_NAME[rec.ImpersonationLevel],
        dynamic_charged     = rec.DynamicCharged,
        dynamic_available   = rec.DynamicAvailable,
        group_count         = rec.GroupCount,
        privilege_count     = rec.PrivilegeCount,
        modified_id         = luid_to_table(rec.ModifiedId),
    }
end

local QUERY_DECODE = {
    [1]  = query_user,
    [2]  = query_groups,
    [3]  = query_privileges,
    [4]  = query_owner,
    [5]  = query_primary_group,
    [6]  = query_default_dacl,
    [7]  = query_source,
    [8]  = query_type,
    [9]  = query_impersonation_level,
    [10] = query_statistics,
}

function M.query(tok, class)
    local class_id = TOKEN_CLASS[class]
    if class_id == nil then
        error("se.query: unknown class '" .. tostring(class) .. "'", 2)
    end
    local buf = query_token_buffer(tok, class_id, 256)
    return QUERY_DECODE[class_id](buf)
end

-- =====================================================================
-- Token set (settable subset only)
-- =====================================================================

-- TOKEN_OWNER / TOKEN_PRIMARY_GROUP each have a single PSID field that
-- points at the SID bytes. Build a fused buffer { void *ptr; SID bytes }
-- so the pointer aliases inside the same allocation — no stale-PSID
-- risk if the input NT_SID gets dropped between build and syscall.
local function fused_sid_record(sid)
    if not M.is_sid(sid) then
        error("se.set: expected NT_SID, got " .. tostring(sid), 3)
    end
    local sid_len = sid:length()
    -- Layout: [void* ptr (4 bytes on i386)] [sid bytes]
    local total   = 4 + sid_len
    local buf     = ffi.new('unsigned char[?]', total)
    -- Copy SID bytes into the trailer.
    ffi.copy(buf + 4, sid:_psid(), sid_len)
    -- Stamp pointer at offset 0 = address of trailer.
    ffi.cast('void **', buf)[0] = ffi.cast('void *', buf + 4)
    return buf, total
end

function M.set(tok, class, value)
    local class_id = TOKEN_CLASS[class]
    if class_id == nil then
        error("se.set: unknown class '" .. tostring(class) .. "'", 2)
    end
    if class == 'owner' or class == 'primary_group' then
        local buf, total = fused_sid_record(value)
        local st = ntdll.NtSetInformationToken(
            handle.raw(tok), class_id, buf, total)
        if err.is_error(st) then err.raise('NtSetInformationToken', st) end
        return
    end
    if class == 'default_dacl' then
        error("se.set('default_dacl'): needs SD/DACL builder (v2)", 2)
    end
    error("se.set: class '" .. class .. "' is not settable", 2)
end

-- =====================================================================
-- Privilege ops
-- =====================================================================

-- ------------------------------------------------------------------
-- TOKEN_X record builders (input shape for create_token, set, adjust).
-- All produce a single fused buffer with PSID/PACL pointers aliasing
-- inside the same allocation — same one-allocation discipline as
-- oa.lua's NT_OA_PATH. Caller holds the buffer through the syscall;
-- when it goes out of scope, every byte goes with it.
-- ------------------------------------------------------------------

-- Fused TOKEN_USER: { void *Sid; ULONG Attributes; <SID bytes inline> }.
local function build_token_user(sid, attributes)
    local sid_len = sid:length()
    local total   = 8 + sid_len           -- header (PSID + Attrs) + SID
    local buf     = ffi.new('unsigned char[?]', total)
    ffi.copy(buf + 8, sid:_psid(), sid_len)
    ffi.cast('void **', buf)[0]    = ffi.cast('void *', buf + 8)
    ffi.cast('uint32_t *', buf + 4)[0] = attributes or 0
    return buf
end

-- Fused TOKEN_GROUPS: { ULONG Count; SID_AND_ATTRIBUTES Groups[N];
--                       <SID bytes for each, packed in order> }.
-- Each SID_AND_ATTRIBUTES.Sid points into the trailer for its own SID.
local function build_token_groups(groups)
    local n = #groups
    local sid_lens = {}
    local total_sid_bytes = 0
    for i, g in ipairs(groups) do
        if not M.is_sid(g.sid) then
            error("se: groups[" .. i .. "].sid must be NT_SID", 3)
        end
        sid_lens[i]      = g.sid:length()
        total_sid_bytes  = total_sid_bytes + sid_lens[i]
    end
    local hdr_size  = 4 + n * 8           -- ULONG count + N * (PSID+Attrs)
    local total     = hdr_size + total_sid_bytes
    local buf       = ffi.new('unsigned char[?]', total)
    ffi.cast('uint32_t *', buf)[0] = n
    local arr       = ffi.cast('SID_AND_ATTRIBUTES *', buf + 4)
    local sid_cur   = hdr_size
    for i, g in ipairs(groups) do
        ffi.copy(buf + sid_cur, g.sid:_psid(), sid_lens[i])
        arr[i - 1].Sid        = ffi.cast('void *', buf + sid_cur)
        arr[i - 1].Attributes = g.attributes or 0
        sid_cur = sid_cur + sid_lens[i]
    end
    return buf
end

-- TOKEN_SOURCE: { CHAR SourceName[8]; LUID SourceIdentifier; }. Fixed
-- size, no fused-buffer dance — the LUID is inline and the name is a
-- char[8] field, so a plain cdata struct works.
local function build_token_source(name, id_low, id_high)
    local rec = ffi.new('TOKEN_SOURCE')
    -- SourceName: 8-byte, NUL-pad. ffi.copy clamps at the smaller length.
    local s = name or ""
    if #s > 8 then s = s:sub(1, 8) end
    ffi.copy(rec.SourceName, s, #s)
    rec.SourceIdentifier.LowPart  = id_low
    rec.SourceIdentifier.HighPart = id_high
    return rec
end

-- Build a TOKEN_PRIVILEGES record from {{name=, state=}, ...} as a fused
-- buffer (count + LUID_AND_ATTRIBUTES[N]). The buffer is the syscall
-- input/output; its lifetime is bounded by the calling function.
local function build_token_privileges(list)
    local n     = #list
    local total = 4 + n * 12   -- ULONG count + n * (LUID 8 + Attrs 4)
    local buf   = ffi.new('unsigned char[?]', total)
    ffi.cast('uint32_t *', buf)[0] = n
    local arr = ffi.cast('LUID_AND_ATTRIBUTES *',
                         buf + ffi.sizeof('TOKEN_PRIVILEGES_HDR'))
    for i, entry in ipairs(list) do
        local low, high = 0, 0
        if entry.luid then
            low, high = entry.luid.low or 0, entry.luid.high or 0
        else
            low = luid_low_for(entry.name)
        end
        local attrs
        if entry.state == 'enable' or entry.state == 'enabled' then
            attrs = M.SE_PRIVILEGE_ENABLED
        elseif entry.state == 'disable' or entry.state == 'disabled' then
            attrs = 0
        elseif entry.state == 'enabled-by-default' then
            attrs = M.SE_PRIVILEGE_ENABLED_BY_DEFAULT + M.SE_PRIVILEGE_ENABLED
        elseif type(entry.attributes) == 'number' then
            attrs = entry.attributes
        else
            error("se: privilege entry needs state ('enable'|'disable'|'enabled-by-default') or numeric attributes", 3)
        end
        arr[i - 1].Luid.LowPart    = low
        arr[i - 1].Luid.HighPart   = high
        arr[i - 1].Attributes      = attrs
    end
    return buf, total
end

function M.adjust_privileges(tok, list, opts)
    opts = opts or {}
    local in_buf, in_size = build_token_privileges(list)

    local prev_buf, prev_size, ret_ptr
    if opts.save_previous then
        -- Same shape, sized identically — kernel may expand if attrs
        -- shape grows, but in NT 3.5 the in/out shapes match exactly.
        prev_buf  = ffi.new('unsigned char[?]', in_size)
        prev_size = in_size
        ret_ptr   = ffi.new('ULONG[1]')
    end

    local st = ntdll.NtAdjustPrivilegesToken(
        handle.raw(tok), 0, in_buf, in_size,
        prev_buf, ret_ptr)
    if err.is_error(st) then err.raise('NtAdjustPrivilegesToken', st) end

    if opts.save_previous then
        return query_privileges(prev_buf)
    end
end

function M.enable_privileges(tok, names)
    local list = {}
    for i, name in ipairs(names) do
        list[i] = { name = name, state = 'enable' }
    end
    return M.adjust_privileges(tok, list)
end

function M.disable_privileges(tok, names)
    local list = {}
    for i, name in ipairs(names) do
        list[i] = { name = name, state = 'disable' }
    end
    return M.adjust_privileges(tok, list)
end

function M.disable_all_privileges(tok)
    local st = ntdll.NtAdjustPrivilegesToken(
        handle.raw(tok), 1, nil, 0, nil, nil)
    if err.is_error(st) then err.raise('NtAdjustPrivilegesToken', st) end
end

-- ------------------------------------------------------------------
-- Group ops (NtAdjustGroupsToken).
--
-- Token group attributes can be flipped at runtime, but the GROUP
-- LIST itself is fixed at token creation — you can't add/remove
-- groups via this API, only change which existing groups are enabled.
--
-- The caller's token must hold TOKEN_ADJUST_GROUPS in its handle's
-- access mask (open with se.TOKEN_ADJUST_GROUPS).
-- ------------------------------------------------------------------
function M.adjust_groups(tok, list, opts)
    opts = opts or {}
    local in_buf  = build_token_groups(list)
    local in_size = ffi.sizeof(in_buf)

    local prev_buf, prev_size, ret_ptr
    if opts.save_previous then
        -- Make the previous-state buffer twice the input size: kernel
        -- writes the entire current group state, which has more entries
        -- than `list` if we're touching only a subset.
        prev_size = in_size * 2 + 256
        prev_buf  = ffi.new('unsigned char[?]', prev_size)
        ret_ptr   = ffi.new('ULONG[1]')
    end

    local st = ntdll.NtAdjustGroupsToken(
        handle.raw(tok), 0, in_buf, in_size,
        prev_buf, ret_ptr)
    if err.is_error(st) then err.raise('NtAdjustGroupsToken', st) end

    if opts.save_previous then return query_groups(prev_buf) end
end

-- ResetToDefault = TRUE: revert every group to its enabled-by-default
-- state. NewState is ignored when this flag is set.
function M.reset_groups_to_default(tok)
    local st = ntdll.NtAdjustGroupsToken(
        handle.raw(tok), 1, nil, 0, nil, nil)
    if err.is_error(st) then err.raise('NtAdjustGroupsToken', st) end
end

-- PRIVILEGE_SET on the wire is { ULONG PrivilegeCount; ULONG Control;
-- LUID_AND_ATTRIBUTES Privileges[]; }. Build it as a fused buffer.
local PRIVILEGE_SET_ALL_NECESSARY = 1
function M.privilege_check(tok, names, mode)
    local n     = #names
    local total = 8 + n * 12
    local buf   = ffi.new('unsigned char[?]', total)
    ffi.cast('uint32_t *', buf)[0] = n                            -- PrivilegeCount
    ffi.cast('uint32_t *', buf)[1] = (mode == 'all')              -- Control
        and PRIVILEGE_SET_ALL_NECESSARY or 0
    local arr = ffi.cast('LUID_AND_ATTRIBUTES *', buf + 8)
    for i, name in ipairs(names) do
        arr[i - 1].Luid.LowPart  = luid_low_for(name)
        arr[i - 1].Luid.HighPart = 0
        arr[i - 1].Attributes    = 0
    end
    local result = ffi.new('BOOLEAN[1]')
    local st = ntdll.NtPrivilegeCheck(handle.raw(tok), buf, result)
    if err.is_error(st) then err.raise('NtPrivilegeCheck', st) end
    return result[0] ~= 0
end

-- =====================================================================
-- Token creation
-- =====================================================================

-- (build_token_user / build_token_groups / build_token_source moved
-- earlier in the file so adjust_groups can reach build_token_groups
-- without a forward declaration. They sit with build_token_privileges
-- and fused_sid_record in the "TOKEN_X record builders" cluster.)

function M.create_token(spec)
    if not M.is_sid(spec.user) then
        error("se.create_token: spec.user must be NT_SID", 2)
    end
    if not M.is_sid(spec.primary_group) then
        error("se.create_token: spec.primary_group must be NT_SID", 2)
    end
    if spec.default_dacl ~= nil then
        error("se.create_token: default_dacl not yet supported (needs SD builder, v2)", 2)
    end

    local ttype = TOKEN_TYPE[spec.type or 'primary']
    if ttype == nil then
        error("se.create_token: bad type '" .. tostring(spec.type) .. "'", 2)
    end

    -- Impersonation tokens need an impersonation level, carried via
    -- the OA's SecurityQualityOfService field. Primary tokens don't.
    -- Build a one-shot OA + SQOS when needed; locals stay alive
    -- through the syscall.
    local oa, sqs
    if ttype == TOKEN_TYPE.impersonation then
        local lvl = IMP_LEVEL[spec.level or 'impersonation']
        if lvl == nil then
            error("se.create_token: bad level '" .. tostring(spec.level) .. "'", 2)
        end
        sqs = ffi.new('SECURITY_QUALITY_OF_SERVICE')
        sqs.Length              = ffi.sizeof('SECURITY_QUALITY_OF_SERVICE')
        sqs.ImpersonationLevel  = lvl
        sqs.ContextTrackingMode = 0          -- static
        sqs.EffectiveOnly       = 0
        oa = ffi.new('OBJECT_ATTRIBUTES')
        oa.Length = ffi.sizeof('OBJECT_ATTRIBUTES')
        oa.SecurityQualityOfService = ffi.cast('void *', sqs)
    end

    -- Authentication ID
    local auth = ffi.new('LUID')
    if spec.auth_id == 'system' or spec.auth_id == nil then
        auth.LowPart  = M.SYSTEM_AUTH_ID.low
        auth.HighPart = M.SYSTEM_AUTH_ID.high
    else
        auth.LowPart  = spec.auth_id.low or 0
        auth.HighPart = spec.auth_id.high or 0
    end

    -- Expiration time (LARGE_INTEGER)
    local expires = ffi.new('LARGE_INTEGER')
    expires.QuadPart = spec.expires or 0x7FFFFFFFFFFFFFFFLL

    -- TokenSource — name + LUID. Allocate a fresh LUID via
    -- NtAllocateLocallyUniqueId when the caller didn't supply one.
    local src_id_low, src_id_high
    if spec.source and spec.source.id then
        src_id_low  = spec.source.id.low  or 0
        src_id_high = spec.source.id.high or 0
    else
        local luid = ffi.new('LUID[1]')
        local st   = ntdll.NtAllocateLocallyUniqueId(luid)
        if err.is_error(st) then err.raise('NtAllocateLocallyUniqueId', st) end
        src_id_low  = luid[0].LowPart
        src_id_high = luid[0].HighPart
    end
    local src = build_token_source(
        (spec.source and spec.source.name) or "Lua",
        src_id_low, src_id_high)

    -- Variable-length records. Each lives until end of this function;
    -- LuaJIT keeps the locals reachable across the FFI call.
    local user_buf       = build_token_user(spec.user, 0)
    local groups_buf     = build_token_groups(spec.groups or {})
    local privs_buf      = build_token_privileges(spec.privileges or {})
    local owner_buf      = spec.owner and fused_sid_record(spec.owner) or nil
    local pg_buf         = fused_sid_record(spec.primary_group)

    local h  = ffi.new('HANDLE[1]')
    local st = ntdll.NtCreateToken(
        h,
        spec.access or M.TOKEN_ALL_ACCESS,
        oa,                                       -- nil for primary
        ttype,
        auth,
        expires,
        user_buf,
        groups_buf,
        privs_buf,
        owner_buf,                                -- nullable
        pg_buf,
        nil,                                      -- DefaultDacl (v2)
        src)
    if err.is_error(st) then err.raise('NtCreateToken', st) end
    return handle.wrap(h[0])
end

-- =====================================================================
-- Duplicate / impersonate / revert
-- =====================================================================

function M.duplicate_token(tok, opts)
    opts = opts or {}
    local ttype = TOKEN_TYPE[opts.type or 'impersonation']
    if ttype == nil then
        error("se.duplicate_token: bad type '" .. tostring(opts.type) .. "'", 2)
    end

    -- Carry the impersonation level through OBJECT_ATTRIBUTES'
    -- SecurityQualityOfService field — the NtDuplicateToken contract.
    local oa  = ffi.new('OBJECT_ATTRIBUTES')
    local sqs
    oa.Length = ffi.sizeof('OBJECT_ATTRIBUTES')
    if opts.level then
        local lvl = IMP_LEVEL[opts.level]
        if lvl == nil then
            error("se.duplicate_token: bad level '" .. tostring(opts.level) .. "'", 2)
        end
        sqs = ffi.new('SECURITY_QUALITY_OF_SERVICE')
        sqs.Length              = ffi.sizeof('SECURITY_QUALITY_OF_SERVICE')
        sqs.ImpersonationLevel  = lvl
        sqs.ContextTrackingMode = 0          -- static
        sqs.EffectiveOnly       = opts.effective_only and 1 or 0
        oa.SecurityQualityOfService = ffi.cast('void *', sqs)
    end

    local h  = ffi.new('HANDLE[1]')
    local st = ntdll.NtDuplicateToken(
        handle.raw(tok),
        opts.access or M.TOKEN_ALL_ACCESS,
        oa,
        opts.effective_only and 1 or 0,
        ttype,
        h)
    if err.is_error(st) then err.raise('NtDuplicateToken', st) end
    return handle.wrap(h[0])
end

function M.impersonate_self(level)
    local lvl = IMP_LEVEL[level or 'impersonation']
    if lvl == nil then
        error("se.impersonate_self: bad level '" .. tostring(level) .. "'", 2)
    end
    local st = ntdll.RtlImpersonateSelf(lvl)
    if err.is_error(st) then err.raise('RtlImpersonateSelf', st) end
end

-- Clear the current thread's impersonation token (revert to primary).
-- The trick: write a NULL HANDLE into ThreadImpersonationToken.
function M.revert_to_self()
    local null_h = ffi.new('HANDLE[1]')        -- zero-initialised
    local st = ntdll.NtSetInformationThread(
        CURRENT_THREAD, THREAD_IMPERSONATION_TOKEN,
        null_h, ffi.sizeof('HANDLE'))
    if err.is_error(st) then err.raise('NtSetInformationThread', st) end
end

-- =====================================================================
-- Security descriptor wrapper (NT_SD)
--
-- Lua table holding the absolute-form SECURITY_DESCRIPTOR plus the
-- backing buffers it points into:
--
--   sd._abs        SECURITY_DESCRIPTOR cdata (20 bytes on i386)
--   sd._owner_buf  unsigned char[?] copy of owner SID bytes
--   sd._group_buf  unsigned char[?] copy of group SID bytes
--   sd._dacl_buf   unsigned char[?] full ACL (header + ACEs + inline SIDs)
--   sd._self_rel   lazily-built self-relative form; cached on first use.
--
-- `_abs.Owner` / `_abs.Group` / `_abs.Dacl` point at the matching buffer.
-- All buffers are strong fields of the table; as long as the table is
-- reachable, every byte the SD references is reachable.
--
-- Self-relative form is built on demand (some syscalls accept absolute,
-- some don't — NtSetSecurityObject + NtAccessCheck both accept either,
-- but self-relative is the canonical wire form). We compute it once
-- and cache; it doesn't observably mutate after the SD is built.
-- =====================================================================

local sd_mt = {}
sd_mt.__index = sd_mt

-- Compute the byte length the DACL needs: 8-byte ACL header plus, for
-- each ACE, 8-byte (header + mask) + the SID bytes inline. Matches the
-- KNOWN_ACE layout the Rtl helpers write.
local function dacl_byte_length(aces)
    local total = 8                            -- ACL header (NT 3.5 sizeof)
    for _, ace in ipairs(aces) do
        local s = ace.allow or ace.deny or ace.audit
        if not M.is_sid(s) then
            error("se.security_descriptor: ace needs allow/deny/audit = NT_SID", 3)
        end
        total = total + 8 + s:length()
    end
    return total
end

-- Build the DACL into a freshly-allocated buffer. Returns the buffer
-- and a Lua-side echo of the input ACE list (for sd:dacl()).
local function build_dacl_buf(aces)
    local total = dacl_byte_length(aces)
    -- Pad slightly to give the Rtl helpers headroom; the helpers strict-
    -- check fit but a few bytes of slack costs nothing.
    local buf   = ffi.new('unsigned char[?]', total + 8)
    local pacl  = ffi.cast('ACL *', buf)
    local st    = ntdll.RtlCreateAcl(pacl, total + 8, ACL_REVISION)
    if err.is_error(st) then err.raise('RtlCreateAcl', st) end
    for i, ace in ipairs(aces) do
        local mask  = ace.mask or 0
        if ace.allow then
            st = ntdll.RtlAddAccessAllowedAce(pacl, ACL_REVISION,
                                              mask, ace.allow:_psid())
            if err.is_error(st) then err.raise('RtlAddAccessAllowedAce', st) end
        elseif ace.deny then
            st = ntdll.RtlAddAccessDeniedAce(pacl, ACL_REVISION,
                                             mask, ace.deny:_psid())
            if err.is_error(st) then err.raise('RtlAddAccessDeniedAce', st) end
        elseif ace.audit then
            local on_succ = ace.on_success and 1 or 0
            local on_fail = ace.on_failure and 1 or 0
            if on_succ == 0 and on_fail == 0 then
                error("se.security_descriptor: audit ace["..i.."] needs on_success or on_failure", 3)
            end
            st = ntdll.RtlAddAuditAccessAce(pacl, ACL_REVISION,
                                            mask, ace.audit:_psid(),
                                            on_succ, on_fail)
            if err.is_error(st) then err.raise('RtlAddAuditAccessAce', st) end
        end
    end
    return buf
end

-- Copy a SID's bytes into a fresh buffer this SD owns.
local function sid_byte_copy(sid)
    local n   = sid:length()
    local buf = ffi.new('unsigned char[?]', n)
    ffi.copy(buf, sid:_psid(), n)
    return buf
end

function M.security_descriptor(spec)
    local sd = {
        _abs       = ffi.new('SECURITY_DESCRIPTOR'),
        _owner_buf = nil,
        _group_buf = nil,
        _dacl_buf  = nil,
        _self_rel  = nil,
        _dacl_aces = spec.dacl,                -- echoed verbatim for :dacl()
    }
    setmetatable(sd, sd_mt)

    local st = ntdll.RtlCreateSecurityDescriptor(sd._abs,
                                                 SECURITY_DESCRIPTOR_REVISION)
    if err.is_error(st) then err.raise('RtlCreateSecurityDescriptor', st) end

    if spec.owner then
        if not M.is_sid(spec.owner) then
            error("se.security_descriptor: owner must be NT_SID", 2)
        end
        sd._owner_buf = sid_byte_copy(spec.owner)
        st = ntdll.RtlSetOwnerSecurityDescriptor(sd._abs,
                                                 ffi.cast('void *', sd._owner_buf),
                                                 0)
        if err.is_error(st) then err.raise('RtlSetOwnerSecurityDescriptor', st) end
    end
    if spec.group then
        if not M.is_sid(spec.group) then
            error("se.security_descriptor: group must be NT_SID", 2)
        end
        sd._group_buf = sid_byte_copy(spec.group)
        st = ntdll.RtlSetGroupSecurityDescriptor(sd._abs,
                                                 ffi.cast('void *', sd._group_buf),
                                                 0)
        if err.is_error(st) then err.raise('RtlSetGroupSecurityDescriptor', st) end
    end
    -- spec.dacl == nil  → DACL absent (kernel grants all to anyone).
    -- spec.dacl == {}   → empty DACL present (kernel denies all to non-owner).
    -- spec.dacl == {..} → DACL present with ACEs.
    if spec.dacl then
        sd._dacl_buf = build_dacl_buf(spec.dacl)
        st = ntdll.RtlSetDaclSecurityDescriptor(sd._abs, 1,
                                                ffi.cast('ACL *', sd._dacl_buf),
                                                0)
        if err.is_error(st) then err.raise('RtlSetDaclSecurityDescriptor', st) end
    end
    return sd
end

function M.is_sd(x)
    return type(x) == 'table' and getmetatable(x) == sd_mt
end

-- Public accessors. Return Lua-shaped values, no cdata.
function sd_mt:owner()
    if not self._owner_buf then return nil end
    return sid_copy_from_buf(self._owner_buf)
end
function sd_mt:group()
    if not self._group_buf then return nil end
    return sid_copy_from_buf(self._group_buf)
end
function sd_mt:dacl()
    if not self._dacl_buf then return nil end
    -- Walk fresh from the buffer rather than echoing the input, so this
    -- works the same for SDs we built and SDs read off the kernel via
    -- get_object_security. Same decoder + decorator as token-query.
    return decorate_aces_for_query(
        aces_from_acl(ffi.cast('ACL *', self._dacl_buf)))
end

-- BORROWED — valid only while `self` is reachable. Wrapper-internal.
function sd_mt:_psd_absolute()
    return ffi.cast('void *', self._abs)
end

-- Convert an absolute SD into a freshly-allocated self-relative byte
-- buffer. Wraps the FFI plumbing (sizing call + in/out length pointer)
-- so callers see a single value-returning function.
local function make_self_relative(abs_sd)
    if ntdll.RtlValidSecurityDescriptor(abs_sd) == 0 then
        error("se: SD failed RtlValidSecurityDescriptor", 3)
    end
    local needed   = ntdll.RtlLengthSecurityDescriptor(abs_sd)
    local buf      = ffi.new('unsigned char[?]', needed)
    local size_box = ffi.new('ULONG[1]', needed)
    local st = ntdll.RtlAbsoluteToSelfRelativeSD(abs_sd, buf, size_box)
    if err.is_error(st) then err.raise('RtlAbsoluteToSelfRelativeSD', st) end
    return buf
end

-- Build (or return cached) self-relative form. Buffer is held on the
-- wrapper, anchored as long as `self` is reachable. Same lifetime
-- caveat as :_psid().
function sd_mt:_psd_self_relative()
    if self._self_rel == nil then
        self._self_rel = make_self_relative(self._abs)
    end
    return ffi.cast('void *', self._self_rel)
end

function sd_mt:length()
    -- Length of the self-relative form (the wire representation).
    self:_psd_self_relative()
    return ffi.sizeof(self._self_rel)
end

-- =====================================================================
-- Access check
--
-- Client token MUST be an impersonation token (NtAccessCheck rejects
-- primary tokens with STATUS_NO_IMPERSONATION_TOKEN — see SE/ACCESSCK.C
-- comment block at line 827). Caller can use se.duplicate_token to
-- promote a primary token to impersonation first.
--
-- Returns (granted_mask, status_uint32). status == 0 means "request
-- fully granted"; status == STATUS_ACCESS_DENIED (0xC0000022) means
-- "DACL denied"; granted_mask is the bits that survived ACL evaluation.
-- =====================================================================
function M.access_check(sd, token, desired, opts)
    if not M.is_sd(sd) then
        error("se.access_check: sd must be NT_SD from se.security_descriptor", 2)
    end
    -- SE/ACCESSCK.C:1097-1108: NtAccessCheck returns
    -- STATUS_INVALID_SECURITY_DESCR if either owner or group is missing.
    -- Surface that as a clear Lua error rather than a magic NTSTATUS,
    -- since the constraint isn't documented anywhere user-facing.
    if not sd._owner_buf or not sd._group_buf then
        error("se.access_check: SD must have both owner and group "
           .. "(NtAccessCheck-only requirement, see SE/ACCESSCK.C:1097)", 2)
    end
    opts = opts or {}

    -- GenericMapping is mandatory per the syscall contract. If the
    -- caller didn't supply one, build a no-op (every generic bit maps
    -- to itself — meaning desired must already be specific bits).
    local gm = ffi.new('GENERIC_MAPPING')
    if opts.mapping then
        gm.GenericRead    = opts.mapping.read    or 0
        gm.GenericWrite   = opts.mapping.write   or 0
        gm.GenericExecute = opts.mapping.execute or 0
        gm.GenericAll     = opts.mapping.all     or 0
    else
        gm.GenericRead    = M.GENERIC_READ
        gm.GenericWrite   = M.GENERIC_WRITE
        gm.GenericExecute = M.GENERIC_EXECUTE
        gm.GenericAll     = M.GENERIC_ALL
    end

    -- PrivilegeSet output buffer: room for ~16 LUIDs on top of the
    -- 8-byte header. Kernel writes used-privileges into here for the
    -- access-check (e.g. SeTakeOwnershipPrivilege when matching owner).
    local priv_size = 8 + 16 * 12
    local priv_buf  = ffi.new('unsigned char[?]', priv_size)
    local priv_len  = ffi.new('ULONG[1]', priv_size)
    local granted   = ffi.new('ACCESS_MASK[1]')
    local status    = ffi.new('NTSTATUS[1]')

    local st = ntdll.NtAccessCheck(
        sd:_psd_self_relative(),
        handle.raw(token),
        desired,
        gm,
        priv_buf, priv_len,
        granted, status)
    if err.is_error(st) then err.raise('NtAccessCheck', st) end

    return granted[0], err.normalize(status[0])
end

-- =====================================================================
-- Object SD round-trip
-- =====================================================================

function M.set_object_security(handle_obj, sd, info)
    if not M.is_sd(sd) then
        error("se.set_object_security: sd must be NT_SD", 2)
    end
    local st = ntdll.NtSetSecurityObject(
        handle.raw(handle_obj),
        info or DEFAULT_SECURITY_INFORMATION,
        sd:_psd_self_relative())
    if err.is_error(st) then err.raise('NtSetSecurityObject', st) end
end

-- Forward-declared so get_object_security below can reference it.
-- Defined immediately after.
local sd_from_self_relative

function M.get_object_security(handle_obj, info)
    info = info or DEFAULT_SECURITY_INFORMATION
    local size = 256
    local buf  = ffi.new('unsigned char[?]', size)
    local need = ffi.new('ULONG[1]')
    for _ = 1, 6 do
        local st = ntdll.NtQuerySecurityObject(
            handle.raw(handle_obj), info, buf, size, need)
        local stu = err.normalize(st)
        if stu == STATUS_BUFFER_TOO_SMALL then
            size = need[0] > size and need[0] or size * 2
            buf  = ffi.new('unsigned char[?]', size)
        elseif err.is_error(st) then
            err.raise('NtQuerySecurityObject', st)
        else
            return sd_from_self_relative(buf)
        end
    end
    error('NtQuerySecurityObject: buffer did not converge')
end

-- RtlGet{Owner,Group,Dacl}SecurityDescriptor wrappers — turn the
-- "out-pointer + defaulted-flag" idiom into plain Lua returns.
local function sd_get_owner(sd_ptr)
    local pp  = ffi.new('void *[1]')
    local def = ffi.new('BOOLEAN[1]')
    local st  = ntdll.RtlGetOwnerSecurityDescriptor(sd_ptr, pp, def)
    if err.is_error(st) then err.raise('RtlGetOwnerSecurityDescriptor', st) end
    return pp[0], def[0] ~= 0       -- pointer (or NULL), defaulted-bool
end
local function sd_get_group(sd_ptr)
    local pp  = ffi.new('void *[1]')
    local def = ffi.new('BOOLEAN[1]')
    local st  = ntdll.RtlGetGroupSecurityDescriptor(sd_ptr, pp, def)
    if err.is_error(st) then err.raise('RtlGetGroupSecurityDescriptor', st) end
    return pp[0], def[0] ~= 0
end
local function sd_get_dacl(sd_ptr)
    local present = ffi.new('BOOLEAN[1]')
    local pp      = ffi.new('ACL *[1]')
    local def     = ffi.new('BOOLEAN[1]')
    local st = ntdll.RtlGetDaclSecurityDescriptor(sd_ptr, present, pp, def)
    if err.is_error(st) then err.raise('RtlGetDaclSecurityDescriptor', st) end
    return pp[0], present[0] ~= 0, def[0] ~= 0
end

-- (aces_from_acl + decorate_aces_for_query are defined earlier, with
-- the SID helpers, so both the token-query path and the SD-wrapper
-- path share the one decoder.)

-- Walk a self-relative SD buffer (typically from NtQuerySecurityObject)
-- back into a fresh NT_SD wrapper. The new SD owns its own bytes; the
-- input buffer can be discarded after this returns.
sd_from_self_relative = function(self_rel_buf)
    -- RtlGet*SecurityDescriptor accept either form; they read offsets
    -- transparently when SE_SELF_RELATIVE is set.
    local sd_in = ffi.cast('void *', self_rel_buf)
    local owner_p                = sd_get_owner(sd_in)
    local group_p                = sd_get_group(sd_in)
    local dacl_p, dacl_present   = sd_get_dacl(sd_in)

    -- Reassemble via the public builder so the resulting SD owns its
    -- bytes and is symmetric with one we constructed in Lua.
    local spec = {}
    if owner_p ~= nil then spec.owner = sid_copy_from_buf(owner_p) end
    if group_p ~= nil then spec.group = sid_copy_from_buf(group_p) end
    if dacl_present and dacl_p ~= nil then
        spec.dacl = aces_from_acl(dacl_p)
    end
    return M.security_descriptor(spec)
end

-- =====================================================================
-- Misc Rtl helpers — small utilities, exposed so callers don't have
-- to reach into ntdll directly.
-- =====================================================================

-- Validity check on a SID. Catches corruption from manual byte poking
-- or from constructing one with an out-of-range subauth count.
function M.is_valid_sid(sid)
    if not M.is_sid(sid) then return false end
    return ntdll.RtlValidSid(sid:_psid()) ~= 0
end

-- Same authority + same first (N-1) subauthorities. Useful for asking
-- "is this SID in the same domain as that SID?" without caring about
-- the trailing RID. NULL_SID compares equal to any zero-subauth SID
-- of the same authority.
function M.sid_prefix_eq(a, b)
    if not (M.is_sid(a) and M.is_sid(b)) then return false end
    return ntdll.RtlEqualPrefixSid(a:_psid(), b:_psid()) ~= 0
end

-- Translate GENERIC_READ/WRITE/EXECUTE/ALL bits in `mask` to per-object
-- specifics via `mapping = {read=, write=, execute=, all=}`. Returns
-- a copy with the generic bits replaced. The kernel does this internally
-- inside NtAccessCheck, but pre-mapping in Lua lets callers see what
-- specific bits they're actually asking for, and lets test asserts
-- compare against concrete masks.
function M.map_generic_mask(mask, mapping)
    local box = ffi.new('ACCESS_MASK[1]', mask)
    local gm  = ffi.new('GENERIC_MAPPING')
    gm.GenericRead    = mapping.read    or 0
    gm.GenericWrite   = mapping.write   or 0
    gm.GenericExecute = mapping.execute or 0
    gm.GenericAll     = mapping.all     or 0
    ntdll.RtlMapGenericMask(box, gm)
    return box[0]
end

-- Mask comparison helpers. Trivial bit ops, named for readability at
-- the test site: `t.ok(se.all_granted(g, want))` reads better than
-- `t.ok(bit.band(g, want) == want)`.
function M.all_granted(granted, wanted)
    return bit.band(granted, wanted) == wanted
end
function M.any_granted(granted, wanted)
    return bit.band(granted, wanted) ~= 0
end

-- Now that se.set('default_dacl', sd) can actually be implemented:
-- NT 3.5's TokenDefaultDacl set takes a TOKEN_DEFAULT_DACL whose
-- DefaultDacl field points at a raw ACL — easier to express by handing
-- in an SD wrapper and having us extract the DACL bytes.
local _set_orig = M.set
function M.set(tok, class, value)
    if class == 'default_dacl' then
        if not M.is_sd(value) then
            error("se.set('default_dacl', sd): expected NT_SD with a DACL", 2)
        end
        if value._dacl_buf == nil then
            error("se.set('default_dacl', sd): the SD has no DACL", 2)
        end
        -- TOKEN_DEFAULT_DACL is { ACL *DefaultDacl; } — build fused
        -- record { ptr; ACL bytes inline } so the pointer aliases
        -- inside the same allocation.
        local acl_len = ffi.sizeof(value._dacl_buf)
        local total   = 4 + acl_len
        local buf     = ffi.new('unsigned char[?]', total)
        ffi.copy(buf + 4, value._dacl_buf, acl_len)
        ffi.cast('void **', buf)[0] = ffi.cast('void *', buf + 4)
        local class_id = TOKEN_CLASS[class]
        local st = ntdll.NtSetInformationToken(
            handle.raw(tok), class_id, buf, total)
        if err.is_error(st) then err.raise('NtSetInformationToken', st) end
        return
    end
    return _set_orig(tok, class, value)
end

return M
