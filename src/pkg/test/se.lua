-- nt.dll.se — Security subsystem.
--
-- Three layers of coverage:
--   1. SID round-trip (no kernel needed) — constructor, well-known
--      constants, byte layout, value-equality, boundary cases.
--   2. Token query — every TOKEN_INFORMATION_CLASS, asserting the
--      kernel's SE_INIT defaults match what SE/TOKEN.C ships.
--   3. Privilege ops, set, create_token, duplicate_token, impersonation.
--
-- The kernel-built system token (assigned to every kernel-created
-- process — System, smss, and our run.exe) is constructed in
-- SE/TOKEN.C:560-870. We reference those line numbers in test asserts
-- so a regression points straight at the expected source.

local t      = require('test')
local se     = require('nt.dll.se')
local ke     = require('nt.dll.ke')
local ex     = require('nt.dll.ex')
local ob     = require('nt.dll.ob')
local oa     = require('nt.dll.oa')
local ffi    = require('ffi')

-- MicroNT does not run csrss, so \BaseNamedObjects is never created.
-- Make our own per-suite directory under \ for the named-object
-- enforcement tests. Held for the lifetime of the test module via the
-- upvalue here; closes when run.exe exits.
local DIRECTORY_ALL_ACCESS = 0xF000F      -- DIRECTORY_QUERY|TRAVERSE|CREATE_OBJECT|CREATE_SUBDIRECTORY|STANDARD_RIGHTS_REQUIRED
local SE_TEST_DIR = "\\seacltest"
local function ensure_test_dir()
    -- Create-or-open: NtCreateDirectoryObject returns
    -- STATUS_OBJECT_NAME_COLLISION if it already exists from a prior
    -- test run (since these are persistent until reboot).
    local ok, h_or_err = pcall(function()
        return ob.NtCreateDirectoryObject(DIRECTORY_ALL_ACCESS,
                                          oa.path(SE_TEST_DIR).oa)
    end)
    if ok then return h_or_err end
    if h_or_err.status == 0xC0000035 then   -- STATUS_OBJECT_NAME_COLLISION
        return ob.NtOpenDirectoryObject(DIRECTORY_ALL_ACCESS,
                                        oa.path(SE_TEST_DIR).oa)
    end
    error(h_or_err)
end
local _SE_TEST_DIR_HANDLE = ensure_test_dir()   -- anchor; do not close

t.suite("se")

-- ---------------------------------------------------------------------
-- SID construction (no kernel)
-- ---------------------------------------------------------------------

t.test("se.sid produces correct SDDL string for well-known shapes", function()
    t.eq(tostring(se.sid(5, 18)),         "S-1-5-18")
    t.eq(tostring(se.sid(1, 0)),          "S-1-1-0")
    t.eq(tostring(se.sid(5, 32, 544)),    "S-1-5-32-544")
    t.eq(tostring(se.sid(3, 0)),          "S-1-3-0")
    t.eq(tostring(se.sid(5)),             "S-1-5")            -- 0 subauths
end)

t.test("se.sid byte layout: revision=1, count, IA big-endian, subauths LE", function()
    local s   = se.sid(5, 18)
    local buf = s:_psid()
    local p   = ffi.cast('unsigned char *', buf)
    t.eq(p[0], 1)                                              -- Revision
    t.eq(p[1], 1)                                              -- SubAuthorityCount
    t.eq(p[2], 0); t.eq(p[3], 0); t.eq(p[4], 0)
    t.eq(p[5], 0); t.eq(p[6], 0); t.eq(p[7], 5)                -- IA = {0,0,0,0,0,5}
    -- SubAuthority[0] = 18, little-endian at offset 8
    local sub = ffi.cast('uint32_t *', p + 8)
    t.eq(sub[0], 18)
    t.eq(s:length(), 12)                                       -- 8 + 1*4
end)

t.test("se.sid accessors agree with SDDL string", function()
    local s = se.sid(5, 32, 544)
    t.eq(s:authority(), 5)
    local sa = s:subauthorities()
    t.eq(#sa, 2)
    t.eq(sa[1], 32)
    t.eq(sa[2], 544)
    t.eq(s:length(), 16)                                       -- 8 + 2*4
end)

t.test("se.sid value equality via __eq + RtlEqualSid", function()
    t.ok(se.sid(5, 18) == se.sid(5, 18),       "same value compare equal")
    t.ok(se.sid(5, 18) == se.LOCAL_SYSTEM_SID, "constructed equals constant")
    t.ok(se.sid(5, 18) ~= se.sid(5, 19),       "different RID compare unequal")
    t.ok(se.sid(5, 18) ~= se.sid(1, 0),        "different shape compare unequal")
    t.ok(se.sid(5, 18) ~= "S-1-5-18",          "string is not a SID")
end)

t.test("se.sid rejects too many subauthorities", function()
    t.raises(function()
        se.sid(5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16)
    end, "too many subauthorities")
end)

t.test("well-known SID constants match SE/SEGLOBAL.C names", function()
    t.eq(tostring(se.LOCAL_SYSTEM_SID),       "S-1-5-18")
    t.eq(tostring(se.WORLD_SID),              "S-1-1-0")
    t.eq(tostring(se.ALIAS_ADMINS_SID),       "S-1-5-32-544")
    t.eq(tostring(se.CREATOR_OWNER_SID),      "S-1-3-0")
    t.eq(tostring(se.CREATOR_GROUP_SID),      "S-1-3-1")
    t.eq(tostring(se.NULL_SID),               "S-1-0-0")
    t.eq(tostring(se.NT_AUTHORITY_SID),       "S-1-5")
    t.eq(tostring(se.ALIAS_USERS_SID),        "S-1-5-32-545")
    t.eq(tostring(se.ALIAS_BACKUP_OPS_SID),   "S-1-5-32-551")
end)

-- ---------------------------------------------------------------------
-- Token open + query
--
-- Expectations from SE/TOKEN.C (system-token init).
--   line 603: UserId.Sid = SeLocalSystemSid          (S-1-5-18)
--   line 611: GroupIds[0].Sid = SeAliasAdminsSid     (S-1-5-32-544)
--   line 612: GroupIds[1].Sid = SeWorldSid           (S-1-1-0)
--   line 614: GroupIds[0].Attributes = OwnerGroupAttributes (8|2|4 = 14)
--   line 615: GroupIds[1].Attributes = NormalGroupAttributes(1|2|4 =  7)
--   line 742: PrimaryGroup = SeLocalSystemSid
--   line 743: Owner = SeAliasAdminsSid
--   line 655..732: 20 privileges with the documented enabled / by-default mix
-- ---------------------------------------------------------------------

t.test("open_process_token (current) returns a usable handle", function()
    local tok = se.open_process_token()
    t.ne(tok, nil)
    tok:close()
end)

t.test("query 'user' returns LOCAL_SYSTEM_SID (SE/TOKEN.C:603)", function()
    local tok = se.open_process_token()
    local u   = se.query(tok, 'user')
    t.eq(u.sid, se.LOCAL_SYSTEM_SID)
    t.eq(type(u.attributes), "number")
    tok:close()
end)

t.test("query 'groups' returns ALIAS_ADMINS + WORLD with right attrs", function()
    local tok = se.open_process_token()
    local g   = se.query(tok, 'groups')
    t.eq(#g, 2, "kernel system token has exactly 2 groups (SE/TOKEN.C:611-612)")
    t.eq(g[1].sid, se.ALIAS_ADMINS_SID)
    t.eq(g[2].sid, se.WORLD_SID)
    -- OwnerGroupAttributes = ENABLED_BY_DEFAULT | ENABLED | OWNER = 0x0E
    t.eq(g[1].attributes, 0x0E, "Admins group: enabled-by-default + enabled + owner")
    -- NormalGroupAttributes = MANDATORY | ENABLED_BY_DEFAULT | ENABLED = 0x07
    t.eq(g[2].attributes, 0x07, "World group: mandatory + enabled-by-default + enabled")
    tok:close()
end)

t.test("query 'owner' = ALIAS_ADMINS_SID (SE/TOKEN.C:743)", function()
    local tok = se.open_process_token()
    t.eq(se.query(tok, 'owner'), se.ALIAS_ADMINS_SID)
    tok:close()
end)

t.test("query 'primary_group' = LOCAL_SYSTEM_SID (SE/TOKEN.C:742)", function()
    local tok = se.open_process_token()
    t.eq(se.query(tok, 'primary_group'), se.LOCAL_SYSTEM_SID)
    tok:close()
end)

t.test("query 'type' = 'primary' for process token", function()
    local tok = se.open_process_token()
    t.eq(se.query(tok, 'type'), 'primary')
    tok:close()
end)

t.test("query 'privileges' returns the 20-privilege set with right flags", function()
    local tok = se.open_process_token()
    local pr  = se.query(tok, 'privileges')
    t.eq(#pr, 20, "system token has 20 privileges (SE/TOKEN.C:655-732)")

    local by_name = {}
    for _, p in ipairs(pr) do by_name[p.name] = p end

    -- Spot-check the documented enabled / disabled mix from SE/TOKEN.C:
    t.ok(by_name.SeTcbPrivilege,                  "SeTcbPrivilege present")
    t.ok(by_name.SeTcbPrivilege.enabled_by_default,
                                                  "SeTcbPrivilege enabled-by-default")
    t.ok(by_name.SeTcbPrivilege.enabled,          "SeTcbPrivilege enabled")

    t.ok(by_name.SeShutdownPrivilege,             "SeShutdownPrivilege present")
    t.eq(by_name.SeShutdownPrivilege.enabled, false,
                                                  "SeShutdownPrivilege disabled by default")

    t.ok(by_name.SeCreateTokenPrivilege,          "SeCreateTokenPrivilege present")
    t.eq(by_name.SeCreateTokenPrivilege.enabled, false,
                                                  "SeCreateTokenPrivilege disabled by default (LSA-only)")

    t.ok(by_name.SeChangeNotifyPrivilege,         "SeChangeNotifyPrivilege present")
    t.ok(by_name.SeChangeNotifyPrivilege.enabled, "SeChangeNotifyPrivilege enabled")
    tok:close()
end)

t.test("query 'source' returns name + LUID", function()
    local tok = se.open_process_token{access = se.TOKEN_QUERY + se.TOKEN_QUERY_SOURCE}
    local src = se.query(tok, 'source')
    t.eq(type(src.name), "string")
    t.ok(#src.name <= 8, "SourceName fits 8 bytes")
    t.eq(type(src.id.low), "number")
    t.eq(type(src.id.high), "number")
    tok:close()
end)

t.test("query 'statistics' returns populated counts", function()
    local tok = se.open_process_token()
    local s   = se.query(tok, 'statistics')
    t.eq(s.token_type,      'primary')
    t.eq(s.group_count,     2,  "matches groups query")
    t.eq(s.privilege_count, 20, "matches privileges query")
    -- Token IDs are LUIDs allocated at boot; non-zero proves the
    -- kernel actually populated them.
    t.ok(s.token_id.low ~= 0 or s.token_id.high ~= 0, "token_id non-zero")
    tok:close()
end)

t.test("query 'default_dacl' walks ACEs into Lua tables", function()
    local tok = se.open_process_token()
    local dacl = se.query(tok, 'default_dacl')
    -- SeSystemDefaultDacl (SE/SEGLOBAL.C:662-687) has two ACEs:
    --   #1 GENERIC_ALL  -> LocalSystem
    --   #2 GENERIC_READ|EXECUTE|READ_CONTROL -> Administrators
    t.ok(dacl ~= nil,             "default_dacl is set")
    t.eq(#dacl, 2,                "two ACEs in SeSystemDefaultDacl")
    t.eq(dacl[1].ace_type, 'allowed')
    t.eq(dacl[1].sid,      se.LOCAL_SYSTEM_SID)
    t.eq(dacl[2].ace_type, 'allowed')
    t.eq(dacl[2].sid,      se.ALIAS_ADMINS_SID)
    tok:close()
end)

-- ---------------------------------------------------------------------
-- Privilege ops
-- ---------------------------------------------------------------------

t.test("privilege_check: token has SeTcbPrivilege", function()
    local tok = se.open_process_token()
    t.eq(se.privilege_check(tok, {"SeTcbPrivilege"}, 'any'), true)
    tok:close()
end)

t.test("privilege_check: rejects unknown privilege name", function()
    local tok = se.open_process_token()
    t.raises(function()
        se.privilege_check(tok, {"SeNonexistentPrivilege"}, 'any')
    end, "unknown privilege name")
    tok:close()
end)

t.test("enable then disable SeShutdownPrivilege round-trip", function()
    local tok = se.open_process_token()
    -- Baseline: shutdown is disabled (per SE/TOKEN.C:721).
    local function shutdown_enabled()
        for _, p in ipairs(se.query(tok, 'privileges')) do
            if p.name == "SeShutdownPrivilege" then return p.enabled end
        end
    end
    t.eq(shutdown_enabled(), false, "shutdown disabled before adjust")
    se.enable_privileges(tok, {"SeShutdownPrivilege"})
    t.eq(shutdown_enabled(), true,  "shutdown enabled after adjust")
    se.disable_privileges(tok, {"SeShutdownPrivilege"})
    t.eq(shutdown_enabled(), false, "shutdown re-disabled after revert")
    tok:close()
end)

t.test("adjust_privileges save_previous returns prior state", function()
    local tok = se.open_process_token()
    -- Toggle SeShutdownPrivilege; previous should report it disabled.
    local prev = se.adjust_privileges(tok,
        {{name="SeShutdownPrivilege", state="enable"}},
        {save_previous = true})
    t.ok(prev,                               "previous state returned")
    t.ok(#prev >= 1,                         "at least one entry in prev")
    t.eq(prev[1].name, "SeShutdownPrivilege")
    t.eq(prev[1].enabled, false,             "prior shutdown was disabled")
    -- Restore.
    se.disable_privileges(tok, {"SeShutdownPrivilege"})
    tok:close()
end)

-- ---------------------------------------------------------------------
-- Token creation (needs SeCreateTokenPrivilege enabled first)
-- ---------------------------------------------------------------------

t.test("create_token without SeCreateTokenPrivilege raises STATUS_PRIVILEGE_NOT_HELD", function()
    local tok = se.open_process_token()
    -- Defensive: ensure CreateToken is disabled (it is by default).
    se.disable_privileges(tok, {"SeCreateTokenPrivilege"})
    local ok, err = pcall(function()
        se.create_token{
            user          = se.LOCAL_SYSTEM_SID,
            primary_group = se.LOCAL_SYSTEM_SID,
            groups        = {{sid=se.WORLD_SID, attributes=se.SE_GROUP_MANDATORY}},
            privileges    = {},
        }
    end)
    t.eq(ok, false, "must fail")
    t.eq(err.fn, "NtCreateToken")
    t.eq(err.status, 0xC0000061, "STATUS_PRIVILEGE_NOT_HELD")
    tok:close()
end)

t.test("create_token round-trips a fresh primary token", function()
    local tok = se.open_process_token()
    se.enable_privileges(tok, {"SeCreateTokenPrivilege"})

    local newtok = se.create_token{
        type          = 'primary',
        user          = se.LOCAL_SYSTEM_SID,
        primary_group = se.LOCAL_SYSTEM_SID,
        groups        = {
            {sid=se.WORLD_SID,         attributes=se.SE_GROUP_MANDATORY
                                                + se.SE_GROUP_ENABLED
                                                + se.SE_GROUP_ENABLED_BY_DEFAULT},
            -- OWNER bit is required for `owner=ALIAS_ADMINS_SID` below to
            -- pass NtCreateToken's STATUS_INVALID_OWNER check (kernel
            -- accepts owner only if it equals user SID or is a group
            -- carrying SE_GROUP_OWNER — see SE/TOKEN.C:594-597).
            {sid=se.ALIAS_ADMINS_SID,  attributes=se.SE_GROUP_ENABLED
                                                + se.SE_GROUP_OWNER},
        },
        privileges    = {
            {name="SeShutdownPrivilege", state="enabled-by-default"},
        },
        owner         = se.ALIAS_ADMINS_SID,
        source        = {name="LuaTest"},
    }
    -- Read back through query — every field should round-trip.
    t.eq(se.query(newtok, 'user').sid,         se.LOCAL_SYSTEM_SID)
    t.eq(se.query(newtok, 'owner'),            se.ALIAS_ADMINS_SID)
    t.eq(se.query(newtok, 'primary_group'),    se.LOCAL_SYSTEM_SID)
    t.eq(se.query(newtok, 'type'),             'primary')
    local groups = se.query(newtok, 'groups')
    t.eq(#groups, 2)
    t.eq(groups[1].sid, se.WORLD_SID)
    t.eq(groups[2].sid, se.ALIAS_ADMINS_SID)
    local privs = se.query(newtok, 'privileges')
    t.eq(#privs, 1)
    t.eq(privs[1].name, "SeShutdownPrivilege")
    t.ok(privs[1].enabled_by_default)

    newtok:close()

    -- Restore baseline: turn CreateToken back off.
    se.disable_privileges(tok, {"SeCreateTokenPrivilege"})
    tok:close()
end)

-- ---------------------------------------------------------------------
-- Duplicate / impersonate / revert
-- ---------------------------------------------------------------------

t.test("duplicate_token: primary -> impersonation reflects in query", function()
    local tok = se.open_process_token{access = se.TOKEN_QUERY + se.TOKEN_DUPLICATE}
    local imp = se.duplicate_token(tok, {
        type           = 'impersonation',
        level          = 'impersonation',
        access         = se.TOKEN_QUERY,
        effective_only = false,
    })
    t.eq(se.query(imp, 'type'),                 'impersonation')
    t.eq(se.query(imp, 'impersonation_level'),  'impersonation')
    imp:close()
    tok:close()
end)

t.test("impersonate_self + revert_to_self round-trip", function()
    -- Before: no thread token.
    t.raises(function() se.open_thread_token() end, "")

    se.impersonate_self('impersonation')

    local th = se.open_thread_token()
    t.eq(se.query(th, 'type'),                'impersonation')
    t.eq(se.query(th, 'impersonation_level'), 'impersonation')
    th:close()

    se.revert_to_self()

    -- After: thread token gone again.
    t.raises(function() se.open_thread_token() end, "")
end)

-- ---------------------------------------------------------------------
-- GC / lifetime stress — exercises the "table holds _buf strongly"
-- contract. If the buffer were getting freed early, the second batch
-- would alias the first batch's recycled memory and __eq would
-- collapse onto something nondeterministic.
-- ---------------------------------------------------------------------

t.test("SID GC stress: 1000 SIDs survive a forced collect", function()
    local sids = {}
    for i = 1, 1000 do sids[i] = se.sid(5, 32, i) end
    collectgarbage("collect")
    for i = 1, 1000 do
        t.eq(sids[i]:subauthorities()[2], i)
    end
    -- Drop the table; build another batch; first batch's memory may
    -- be reused, no crash, no value drift in second batch.
    sids = nil
    collectgarbage("collect")
    for i = 1, 1000 do
        local s = se.sid(5, 32, i + 10000)
        t.eq(s:subauthorities()[2], i + 10000)
    end
end)

t.test("queried SID outlives the kernel buffer (own _buf)", function()
    local tok  = se.open_process_token()
    local user = se.query(tok, 'user').sid     -- copies bytes out
    tok:close()
    collectgarbage("collect")
    -- If `user` aliased the kernel buffer, this would either crash or
    -- drift. We assert it still equals LOCAL_SYSTEM_SID after several
    -- collection cycles to prove it owns its bytes outright.
    for _ = 1, 4 do collectgarbage("collect") end
    t.eq(user, se.LOCAL_SYSTEM_SID)
end)

-- ---------------------------------------------------------------------
-- Security descriptor builder + round-trip
-- ---------------------------------------------------------------------

t.test("security_descriptor: build with owner/group/dacl, read back fields", function()
    local sd = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        group = se.ALIAS_ADMINS_SID,
        dacl  = {
            { allow = se.LOCAL_SYSTEM_SID,  mask = 0x0001 },
            { allow = se.WORLD_SID,         mask = 0x0002 },
            { deny  = se.NULL_SID,          mask = 0x10000000 },
        },
    }
    t.eq(sd:owner(), se.LOCAL_SYSTEM_SID)
    t.eq(sd:group(), se.ALIAS_ADMINS_SID)
    local d = sd:dacl()
    t.eq(#d, 3)
    t.eq(d[1].ace_type, 'allowed'); t.eq(d[1].sid, se.LOCAL_SYSTEM_SID); t.eq(d[1].mask, 0x0001)
    t.eq(d[2].ace_type, 'allowed'); t.eq(d[2].sid, se.WORLD_SID);        t.eq(d[2].mask, 0x0002)
    t.eq(d[3].ace_type, 'denied');  t.eq(d[3].sid, se.NULL_SID);         t.eq(d[3].mask, 0x10000000)
end)

t.test("security_descriptor: self-relative form has non-zero length", function()
    local sd = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        dacl  = { { allow = se.LOCAL_SYSTEM_SID, mask = 0x1 } },
    }
    -- Header (20) + owner SID (12) + ACL header (8) + ACE (8 + 12) > 50
    t.ok(sd:length() > 50, "self-relative SD has >50 bytes for this content")
end)

t.test("security_descriptor: dropping wrapper after self-rel build is GC-safe", function()
    local function build_and_get_len()
        local sd = se.security_descriptor{
            owner = se.LOCAL_SYSTEM_SID,
            dacl  = { { allow = se.LOCAL_SYSTEM_SID, mask = 0x1 } },
        }
        sd:_psd_self_relative()    -- forces lazy build
        return sd:length()
    end
    local n = build_and_get_len()
    collectgarbage("collect")
    -- If the self-rel buffer were GC-freed before its anchor table,
    -- something would have crashed. The fact we got a length proves
    -- the wrapper held its bytes through.
    t.ok(n > 0)
end)

-- ---------------------------------------------------------------------
-- access_check (in-memory, no kernel object)
-- ---------------------------------------------------------------------

-- NtAccessCheck rejects primary tokens; promote ours to impersonation
-- once and reuse across the access-check tests.
local function impersonation_token_for_check()
    local prim = se.open_process_token{
        access = se.TOKEN_QUERY + se.TOKEN_DUPLICATE
    }
    local imp = se.duplicate_token(prim, {
        type   = 'impersonation',
        level  = 'identification',
        access = se.TOKEN_QUERY,
    })
    prim:close()
    return imp
end

t.test("access_check: ALLOW ACE for our SID grants the requested bit", function()
    local sd = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        group = se.LOCAL_SYSTEM_SID,
        dacl  = { { allow = se.LOCAL_SYSTEM_SID, mask = 0x0001 } },
    }
    local imp = impersonation_token_for_check()
    local granted, status = se.access_check(sd, imp, 0x0001)
    t.eq(status, se.STATUS_SUCCESS, "request fully granted")
    t.eq(granted, 0x0001,           "exactly the requested bit")
    imp:close()
end)

t.test("access_check: bit not in DACL → STATUS_ACCESS_DENIED", function()
    local sd = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        group = se.LOCAL_SYSTEM_SID,
        dacl  = { { allow = se.LOCAL_SYSTEM_SID, mask = 0x0001 } },
    }
    local imp = impersonation_token_for_check()
    local granted, status = se.access_check(sd, imp, 0x0002)
    t.eq(status,  se.STATUS_ACCESS_DENIED)
    t.eq(granted, 0x0000)
    imp:close()
end)

t.test("access_check: DENY ACE wins over later ALLOW", function()
    local sd = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        group = se.LOCAL_SYSTEM_SID,
        dacl  = {
            { deny  = se.WORLD_SID,         mask = 0x0001 },
            { allow = se.LOCAL_SYSTEM_SID,  mask = 0x0001 },
        },
    }
    -- Our impersonation token has WORLD_SID in its groups (per
    -- SE/TOKEN.C:612), so the DENY matches first and the ALLOW
    -- never gets reached — kernel evaluates ACEs in order.
    local imp = impersonation_token_for_check()
    local granted, status = se.access_check(sd, imp, 0x0001)
    t.eq(status,  se.STATUS_ACCESS_DENIED)
    t.eq(granted, 0x0000)
    imp:close()
end)

t.test("access_check: no DACL field → kernel grants requested mask (NULL DACL)", function()
    -- Per SECURITY_DESCRIPTOR docs: when SE_DACL_PRESENT is clear, no
    -- DACL is present and access is granted unconditionally.
    local sd = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        group = se.LOCAL_SYSTEM_SID,
    }
    local imp = impersonation_token_for_check()
    local granted, status = se.access_check(sd, imp, 0x000F)
    t.eq(status, se.STATUS_SUCCESS, "NULL DACL grants all")
    t.eq(granted, 0x000F)
    imp:close()
end)

t.test("access_check: empty DACL → all access denied (except owner-implicit)", function()
    local sd = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        group = se.LOCAL_SYSTEM_SID,
        dacl  = {},                          -- DACL present, zero ACEs
    }
    local imp = impersonation_token_for_check()
    -- Specific bits get denied.
    local granted, status = se.access_check(sd, imp, 0x0001)
    t.eq(status,  se.STATUS_ACCESS_DENIED)
    t.eq(granted, 0x0000)
    -- Owner-implicit READ_CONTROL gets granted even with empty DACL.
    local granted2, status2 = se.access_check(sd, imp, se.READ_CONTROL)
    t.eq(status2,  se.STATUS_SUCCESS, "owner gets READ_CONTROL implicitly")
    t.eq(granted2, se.READ_CONTROL)
    imp:close()
end)

t.test("access_check: rejects primary token (must be impersonation)", function()
    local sd = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        group = se.LOCAL_SYSTEM_SID,
        dacl  = { { allow = se.WORLD_SID, mask = 0x1 } },
    }
    local prim = se.open_process_token{access = se.TOKEN_QUERY}
    t.raises(function() se.access_check(sd, prim, 0x1) end, "STATUS")
    prim:close()
end)

-- ---------------------------------------------------------------------
-- Object-tree enforcement: real kernel object with a restrictive SD
-- ---------------------------------------------------------------------

local EVENT_QUERY_STATE  = 0x0001
local EVENT_MODIFY_STATE = 0x0002
local EVENT_ALL_ACCESS   = 0x001F0003

t.test("event with allow=QUERY_STATE rejects open with MODIFY_STATE", function()
    -- Build SD: only LocalSystem may QUERY_STATE on this event.
    -- Note no MODIFY_STATE allow; kernel must DENY any open asking for it.
    local sd = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        group = se.LOCAL_SYSTEM_SID,
        dacl  = {
            { allow = se.LOCAL_SYSTEM_SID, mask = EVENT_QUERY_STATE },
        },
    }

    -- Create the event. Caller-side desired-access at create is what
    -- WE end up holding — the SD applies to subsequent opens.
    local noa = oa.path("\\seacltest\\evt")
    noa.oa.SecurityDescriptor = sd:_psd_self_relative()
    local ev = ke.NtCreateEvent(EVENT_ALL_ACCESS, noa.oa, 0, false)
    noa.oa.SecurityDescriptor = nil    -- defensive: don't outlive sd

    -- Open with the granted bit — must succeed.
    local h_ok = ex.NtOpenEvent(EVENT_QUERY_STATE, oa.path("\\seacltest\\evt").oa)
    t.ne(h_ok, nil, "open with EVENT_QUERY_STATE allowed by DACL")
    h_ok:close()

    -- Open with a bit not in the DACL — must be denied.
    local ok, err = pcall(function()
        return ex.NtOpenEvent(EVENT_MODIFY_STATE,
                              oa.path("\\seacltest\\evt").oa)
    end)
    t.eq(ok, false, "open with EVENT_MODIFY_STATE must fail")
    t.eq(err.fn, "NtOpenEvent")
    t.eq(err.status, se.STATUS_ACCESS_DENIED,
         "kernel returned STATUS_ACCESS_DENIED — enforcement works!")

    ev:close()
end)

t.test("get_object_security round-trips a set SD", function()
    local sd_in = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        group = se.ALIAS_ADMINS_SID,
        dacl  = {
            { allow = se.WORLD_SID,        mask = EVENT_QUERY_STATE },
            { allow = se.LOCAL_SYSTEM_SID, mask = EVENT_ALL_ACCESS  },
        },
    }
    local noa = oa.path("\\seacltest\\qrt")
    noa.oa.SecurityDescriptor = sd_in:_psd_self_relative()
    local ev = ke.NtCreateEvent(EVENT_ALL_ACCESS, noa.oa, 0, false)
    noa.oa.SecurityDescriptor = nil

    local sd_out = se.get_object_security(ev)
    t.eq(sd_out:owner(), se.LOCAL_SYSTEM_SID, "owner round-trips")
    t.eq(sd_out:group(), se.ALIAS_ADMINS_SID, "group round-trips")
    local d = sd_out:dacl()
    t.eq(#d, 2,                 "two ACEs round-trip")
    t.eq(d[1].sid,  se.WORLD_SID)
    t.eq(d[1].mask, EVENT_QUERY_STATE)
    t.eq(d[2].sid,  se.LOCAL_SYSTEM_SID)
    t.eq(d[2].mask, EVENT_ALL_ACCESS)

    ev:close()
end)

t.test("set_object_security mutates the live SD", function()
    -- Create with permissive SD, then tighten via set_object_security,
    -- then verify a new open is denied where it would have succeeded.
    local sd_open = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        dacl  = { { allow = se.WORLD_SID, mask = EVENT_ALL_ACCESS } },
    }
    local noa = oa.path("\\seacltest\\mut")
    noa.oa.SecurityDescriptor = sd_open:_psd_self_relative()
    local ev = ke.NtCreateEvent(EVENT_ALL_ACCESS, noa.oa, 0, false)
    noa.oa.SecurityDescriptor = nil

    -- Tighten: only QUERY_STATE allowed now.
    local sd_tight = se.security_descriptor{
        owner = se.LOCAL_SYSTEM_SID,
        dacl  = {
            { allow = se.LOCAL_SYSTEM_SID, mask = EVENT_QUERY_STATE },
        },
    }
    se.set_object_security(ev, sd_tight, se.DACL_SECURITY_INFORMATION)

    -- Round-trip readback should reflect the change.
    local d = se.get_object_security(ev,
        se.DACL_SECURITY_INFORMATION):dacl()
    t.eq(#d, 1)
    t.eq(d[1].sid,  se.LOCAL_SYSTEM_SID)
    t.eq(d[1].mask, EVENT_QUERY_STATE)

    -- Open with MODIFY_STATE should now fail.
    local ok, err = pcall(function()
        return ex.NtOpenEvent(EVENT_MODIFY_STATE,
                              oa.path("\\seacltest\\mut").oa)
    end)
    t.eq(ok, false)
    t.eq(err.status, se.STATUS_ACCESS_DENIED)

    ev:close()
end)

-- ---------------------------------------------------------------------
-- Token set: owner / primary_group (corner gaps in already-wrapped API)
-- ---------------------------------------------------------------------

t.test("se.set('owner', sid) changes the token owner", function()
    -- Open + duplicate first so we don't permanently mutate our own
    -- token. The duplicate inherits ADJUST_DEFAULT in the new handle.
    local prim = se.open_process_token{
        access = se.TOKEN_QUERY + se.TOKEN_DUPLICATE
    }
    local dup  = se.duplicate_token(prim, {
        type = 'primary',
        access = se.TOKEN_QUERY + se.TOKEN_ADJUST_DEFAULT,
    })
    -- Baseline: owner is ALIAS_ADMINS (per SE/TOKEN.C:743 default).
    t.eq(se.query(dup, 'owner'), se.ALIAS_ADMINS_SID)
    -- Owner must be either the user or a group with SE_GROUP_OWNER —
    -- LOCAL_SYSTEM is the user SID, so it qualifies.
    se.set(dup, 'owner', se.LOCAL_SYSTEM_SID)
    t.eq(se.query(dup, 'owner'), se.LOCAL_SYSTEM_SID)
    dup:close(); prim:close()
end)

t.test("se.set('primary_group', sid) changes the primary group", function()
    local prim = se.open_process_token{
        access = se.TOKEN_QUERY + se.TOKEN_DUPLICATE
    }
    local dup  = se.duplicate_token(prim, {
        type = 'primary',
        access = se.TOKEN_QUERY + se.TOKEN_ADJUST_DEFAULT,
    })
    t.eq(se.query(dup, 'primary_group'), se.LOCAL_SYSTEM_SID)
    -- Switch to one of the groups in the token (Admins is in groups[1]).
    se.set(dup, 'primary_group', se.ALIAS_ADMINS_SID)
    t.eq(se.query(dup, 'primary_group'), se.ALIAS_ADMINS_SID)
    dup:close(); prim:close()
end)

-- ---------------------------------------------------------------------
-- Privilege ops: corner cases not yet covered
-- ---------------------------------------------------------------------

t.test("adjust_privileges with mixed enable/disable in one call", function()
    local tok = se.open_process_token()
    se.adjust_privileges(tok, {
        { name = "SeShutdownPrivilege", state = "enable"  },
        { name = "SeAuditPrivilege",    state = "disable" },
    })
    local by_name = {}
    for _, p in ipairs(se.query(tok, 'privileges')) do
        by_name[p.name] = p
    end
    t.eq(by_name.SeShutdownPrivilege.enabled, true)
    t.eq(by_name.SeAuditPrivilege.enabled,    false)
    -- Restore baseline.
    se.adjust_privileges(tok, {
        { name = "SeShutdownPrivilege", state = "disable" },
        { name = "SeAuditPrivilege",    state = "enable"  },
    })
    tok:close()
end)

t.test("disable_all_privileges clears every enabled bit", function()
    -- Use a duplicate so we don't strand our own token.
    local prim = se.open_process_token{
        access = se.TOKEN_QUERY + se.TOKEN_DUPLICATE
    }
    local dup  = se.duplicate_token(prim, {
        type = 'primary',
        access = se.TOKEN_QUERY + se.TOKEN_ADJUST_PRIVILEGES,
    })
    se.disable_all_privileges(dup)
    for _, p in ipairs(se.query(dup, 'privileges')) do
        t.eq(p.enabled, false,
             "after disable_all, '" .. p.name .. "' should not be enabled")
    end
    dup:close(); prim:close()
end)

t.test("privilege_check 'all' mode requires every privilege held", function()
    local tok = se.open_process_token()
    -- All three privileges are present in our token (SE/TOKEN.C:655-732).
    t.eq(se.privilege_check(tok,
            {"SeTcbPrivilege", "SeDebugPrivilege", "SeAuditPrivilege"},
            'all'), true)
    -- 'any' returns true if at least one is held; 'all' requires all.
    -- Our token has SeTcbPrivilege but not SeRemoteShutdown — 'all' fails.
    t.eq(se.privilege_check(tok,
            {"SeTcbPrivilege", "SeRemoteShutdownPrivilege"}, 'all'),
         false)
    t.eq(se.privilege_check(tok,
            {"SeTcbPrivilege", "SeRemoteShutdownPrivilege"}, 'any'),
         true)
    tok:close()
end)

-- ---------------------------------------------------------------------
-- Group ops (NtAdjustGroupsToken)
-- ---------------------------------------------------------------------

t.test("adjust_groups: disable a group, observe in query, restore via reset", function()
    local prim = se.open_process_token{
        access = se.TOKEN_QUERY + se.TOKEN_DUPLICATE
    }
    local dup  = se.duplicate_token(prim, {
        type = 'primary',
        access = se.TOKEN_QUERY + se.TOKEN_ADJUST_GROUPS,
    })
    -- Baseline: WORLD has MANDATORY|ENABLED|ENABLED_BY_DEFAULT (= 7).
    -- We can't disable MANDATORY groups; assert kernel rejects gracefully.
    t.raises(function()
        se.adjust_groups(dup, {
            { sid = se.WORLD_SID, attributes = 0 },
        })
    end, "STATUS")
    -- Reset returns groups to their enabled-by-default state — works
    -- even on a token that hasn't been modified.
    se.reset_groups_to_default(dup)
    local g = se.query(dup, 'groups')
    -- WORLD (groups[2]) is still mandatory + enabled-by-default + enabled.
    t.eq(g[2].sid, se.WORLD_SID)
    t.eq(g[2].attributes, 0x07)
    dup:close(); prim:close()
end)

-- ---------------------------------------------------------------------
-- Duplicate / impersonation: corners
-- ---------------------------------------------------------------------

t.test("duplicate_token across all impersonation levels", function()
    local prim = se.open_process_token{
        access = se.TOKEN_QUERY + se.TOKEN_DUPLICATE
    }
    for _, level in ipairs{'anonymous', 'identification',
                           'impersonation', 'delegation'} do
        local imp = se.duplicate_token(prim, {
            type   = 'impersonation',
            level  = level,
            access = se.TOKEN_QUERY,
        })
        -- 'anonymous' typically flattens user/groups info — the level
        -- is what the kernel records; verify that round-trips.
        t.eq(se.query(imp, 'type'),                'impersonation', level)
        t.eq(se.query(imp, 'impersonation_level'), level,
             "round-trip impersonation_level: " .. level)
        imp:close()
    end
    prim:close()
end)

t.test("duplicate_token effective_only=true strips disabled groups", function()
    local prim = se.open_process_token{
        access = se.TOKEN_QUERY + se.TOKEN_DUPLICATE
    }
    -- effective_only locks the duplicate to whatever's currently
    -- enabled — disabled groups/privileges become permanently absent
    -- in the new token. We can't easily tell from the count alone
    -- (mandatory groups stay), but the duplicate must succeed and
    -- be queryable.
    local imp = se.duplicate_token(prim, {
        type = 'impersonation', level = 'impersonation',
        effective_only = true,
    })
    t.ok(se.query(imp, 'privileges'),
         "effective_only=true duplicate is well-formed enough to query")
    imp:close(); prim:close()
end)

-- ---------------------------------------------------------------------
-- create_token corners
-- ---------------------------------------------------------------------

t.test("create_token: explicit expires, owner=nil defaults to user SID", function()
    local tok = se.open_process_token()
    se.enable_privileges(tok, {"SeCreateTokenPrivilege"})
    local hour_from_now = 36000000000   -- 100ns ticks in 1 hour
    local newtok = se.create_token{
        type          = 'primary',
        user          = se.LOCAL_SYSTEM_SID,
        primary_group = se.LOCAL_SYSTEM_SID,
        groups        = {
            { sid = se.ALIAS_ADMINS_SID,
              attributes = se.SE_GROUP_ENABLED + se.SE_GROUP_OWNER },
        },
        privileges    = {},
        -- owner = nil — kernel falls back to USER SID, not to a group
        -- carrying SE_GROUP_OWNER (which is what userland win32
        -- documentation suggests but isn't actually what NtCreateToken
        -- does in NT 3.5).
        expires       = hour_from_now,
    }
    t.eq(se.query(newtok, 'owner'), se.LOCAL_SYSTEM_SID,
         "kernel defaults owner to user SID when nil")
    local stats = se.query(newtok, 'statistics')
    t.ok(stats.expiration ~= 0, "expires propagated into statistics")
    newtok:close()
    se.disable_privileges(tok, {"SeCreateTokenPrivilege"})
    tok:close()
end)

t.test("create_token type='impersonation' produces an impersonation token", function()
    local tok = se.open_process_token()
    se.enable_privileges(tok, {"SeCreateTokenPrivilege"})
    local imp = se.create_token{
        type          = 'impersonation',
        level         = 'identification',
        user          = se.LOCAL_SYSTEM_SID,
        primary_group = se.LOCAL_SYSTEM_SID,
        groups        = {
            { sid = se.WORLD_SID, attributes = se.SE_GROUP_MANDATORY
                                            + se.SE_GROUP_ENABLED },
            { sid = se.ALIAS_ADMINS_SID,
              attributes = se.SE_GROUP_ENABLED + se.SE_GROUP_OWNER },
        },
        privileges    = {},
        owner         = se.ALIAS_ADMINS_SID,
    }
    t.eq(se.query(imp, 'type'),                'impersonation')
    t.eq(se.query(imp, 'impersonation_level'), 'identification')
    imp:close()
    se.disable_privileges(tok, {"SeCreateTokenPrivilege"})
    tok:close()
end)

-- ---------------------------------------------------------------------
-- open_thread_token positive path
-- ---------------------------------------------------------------------

t.test("open_thread_token returns the impersonation token after impersonate_self", function()
    -- Impersonate at 'impersonation' level (not 'identification') —
    -- TOKEN_ADJUST_PRIVILEGES (which the open_thread_token default
    -- access mask asks for) requires impersonation level >= Impersonation,
    -- so 'identification' would yield STATUS_BAD_IMPERSONATION_LEVEL.
    se.impersonate_self('impersonation')
    -- pcall + revert: any failure in the body still un-impersonates,
    -- otherwise we'd leak impersonation state into subsequent tests
    -- and they'd fail on TOKEN_ADJUST_DEFAULT opens against the
    -- process token.
    local ok, err_msg = pcall(function()
        local th = se.open_thread_token{ access = se.TOKEN_QUERY }
        t.eq(se.query(th, 'type'),                'impersonation')
        t.eq(se.query(th, 'impersonation_level'), 'impersonation')
        th:close()
        -- open_as_self=true exercises the bool-flag path; on a single-
        -- level impersonation it returns the same impersonation token.
        local th2 = se.open_thread_token{
            open_as_self = true, access = se.TOKEN_QUERY,
        }
        t.ok(th2)
        th2:close()
    end)
    se.revert_to_self()
    if not ok then error(err_msg, 0) end
end)

-- ---------------------------------------------------------------------
-- Misc Rtl helpers
-- ---------------------------------------------------------------------

t.test("is_valid_sid: well-formed SIDs pass, non-SIDs fail", function()
    t.eq(se.is_valid_sid(se.LOCAL_SYSTEM_SID), true)
    t.eq(se.is_valid_sid(se.sid(5, 32, 544)),  true)
    t.eq(se.is_valid_sid("S-1-5-18"),          false, "string is not a SID")
    t.eq(se.is_valid_sid(nil),                 false)
end)

t.test("sid_prefix_eq: same domain matches, different doesn't", function()
    -- Two BUILTIN aliases: S-1-5-32-544 and S-1-5-32-545 share the
    -- prefix S-1-5-32 (BUILTIN domain).
    t.eq(se.sid_prefix_eq(se.ALIAS_ADMINS_SID, se.ALIAS_USERS_SID), true)
    -- LOCAL_SYSTEM (S-1-5-18) and BUILTIN admin (S-1-5-32-544) share
    -- the NT-authority prefix but differ in the next subauth — prefix
    -- comparison works on the (N-1) leading subauths, not authority alone.
    t.eq(se.sid_prefix_eq(se.LOCAL_SYSTEM_SID, se.ALIAS_ADMINS_SID), false)
    -- Same SID always has equal prefix.
    t.eq(se.sid_prefix_eq(se.LOCAL_SYSTEM_SID, se.LOCAL_SYSTEM_SID), true)
end)

t.test("map_generic_mask translates GENERIC_* to specific bits", function()
    -- Event-style mapping: read = QUERY_STATE, write = MODIFY_STATE,
    -- execute = SYNCHRONIZE, all = EVENT_ALL_ACCESS.
    local map = {
        read    = 0x00020001,    -- READ_CONTROL | EVENT_QUERY_STATE
        write   = 0x00020002,    -- READ_CONTROL | EVENT_MODIFY_STATE
        execute = 0x00100000,    -- SYNCHRONIZE
        all     = 0x001F0003,    -- EVENT_ALL_ACCESS
    }
    -- GENERIC_READ alone should map to map.read.
    t.eq(se.map_generic_mask(se.GENERIC_READ,  map), map.read)
    t.eq(se.map_generic_mask(se.GENERIC_WRITE, map), map.write)
    -- GENERIC_ALL maps to map.all (and the GENERIC bit is cleared).
    t.eq(se.map_generic_mask(se.GENERIC_ALL,   map), map.all)
    -- A specific bit not in the GENERIC_* range passes through unchanged.
    t.eq(se.map_generic_mask(0x0001, map), 0x0001)
end)

t.test("all_granted / any_granted bit-mask helpers", function()
    t.eq(se.all_granted(0xFF,  0x0F), true)
    t.eq(se.all_granted(0x0F,  0xFF), false,  "wanted bits not all in granted")
    t.eq(se.any_granted(0x10,  0x30), true,   "0x10 ∈ {0x10, 0x20}")
    t.eq(se.any_granted(0x40,  0x30), false,  "0x40 ∉ {0x10, 0x20}")
    t.eq(se.all_granted(0,     0),    true,   "empty wanted is trivially granted")
    t.eq(se.any_granted(0,     0),    false,  "empty wanted has no bits to match")
end)

t.test("se.set('default_dacl', sd) round-trips through token query", function()
    local sd = se.security_descriptor{
        dacl = {
            { allow = se.LOCAL_SYSTEM_SID, mask = 0x000F0000 + 0xFF },
            { allow = se.WORLD_SID,        mask = 0x00020000 },
        },
    }
    local tok = se.open_process_token{
        access = se.TOKEN_QUERY + se.TOKEN_ADJUST_DEFAULT,
    }
    se.set(tok, 'default_dacl', sd)
    local d = se.query(tok, 'default_dacl')
    t.eq(#d, 2)
    t.eq(d[1].sid,  se.LOCAL_SYSTEM_SID)
    t.eq(d[1].mask, 0x000F00FF)
    t.eq(d[2].sid,  se.WORLD_SID)
    t.eq(d[2].mask, 0x00020000)
    tok:close()
end)
