-- nt.harderr -- hard-error port helper.
--
-- Wraps the LPC plumbing the kernel uses to deliver NtRaiseHardError
-- messages to a userspace daemon.  Each kernel-side raise turns into
-- a HARDERROR_MSG on our port; we decode it, log it, and reply with
-- a HARDERROR_RESPONSE so NtRaiseHardError unblocks the raising
-- thread with a meaningful status.
--
-- Background: see docs-wip/HALT-ON-USER-ERROR.md.  The default
-- handler in NTOS/EX/HARDERR.C halts the kernel when no port is
-- registered AND the status is NT_ERROR -- our daemon's job is to
-- be that port so the kernel never falls into the bugcheck branch.
--
-- WARNING (kernel recursion guard, HARDERR.C:404-417):
--   The process that called NtSetDefaultHardErrorPort cannot itself
--   raise a hard error with NT_ERROR(status).  The kernel detects
--   this recursion and invokes ExpSystemErrorHandler(CallShutdown=
--   TRUE), which halts the box via the qemu debug-exit port.  This
--   helper throws a clear Lua error if listen{default=true} is
--   called twice on the same process; cross-process raising is the
--   intended pattern (see pkg/test/harderr_xproc.lua).
--
-- Usage:
--
--   local harderr = require('nt.harderr')
--   local port = harderr.listen('\\HardErrorPort', { default = true })
--
--   -- Manual receive loop
--   while true do
--       local msg = port:recv()
--       print(string.format("HARDERR pid=%d status=%x", msg.pid, msg.status))
--       for i, p in ipairs(msg.params) do print("  ["..i.."]", p) end
--       port:reply(msg, harderr.RESPONSE.RETURN_TO_CALLER)
--   end
--
--   -- Or callback loop
--   port:run(function(msg)
--       print("HARDERR", msg.status)
--       return harderr.RESPONSE.RETURN_TO_CALLER
--   end)

local ffi    = require('ffi')
local bit    = require('bit')
local ntdll  = require('nt.dll')
local lpc    = require('nt.dll.lpc')
local ex     = require('nt.dll.ex')
local oa     = require('nt.dll.oa')
local str    = require('nt.dll.str')
local handle = require('nt.dll.handle')
local err    = require('nt.dll.errors')

local M = {}

-- Re-export the response/option constants so callers don't have to
-- reach into nt.dll.ex directly.
M.RESPONSE = ex.HARDERROR_RESPONSE
M.OPTION   = ex.HARDERROR_OPTION

-- Module-level guard against double-registration.  Same process can
-- only own the default port once; the kernel doesn't enforce this
-- (NtSetDefaultHardErrorPort overwrites ExpDefaultErrorPort), but our
-- self-recursion property gets confused if we register twice.
local _default_registered = false

-- ------------------------------------------------------------------
-- Message decoding
-- ------------------------------------------------------------------

-- Decode Parameters[i] given UnicodeStringParameterMask.  Bit i set
-- means Parameters[i] is a UNICODE_STRING * (the kernel marshalled
-- the underlying wide-string buffer into the daemon's address space
-- via ZwAllocateVirtualMemory; see HARDERR.C:651-758).  Otherwise
-- the value is a ULONG.
local function decode_params(harderr_msg)
    local out = {}
    local n = tonumber(harderr_msg.NumberOfParameters)
    local mask = tonumber(harderr_msg.UnicodeStringParameterMask)
    for i = 0, n - 1 do
        local raw = harderr_msg.Parameters[i]
        if bit.band(mask, bit.lshift(1, i)) ~= 0 then
            -- PUNICODE_STRING in daemon address space
            local us = ffi.cast('UNICODE_STRING *', raw)
            out[i + 1] = str.from_utf16(us)
        else
            out[i + 1] = tonumber(raw)
        end
    end
    return out
end

-- Build the Lua-side message table the user sees.  We keep the
-- underlying cdata anchored on the table (`_raw`) so reply() can
-- write back into the SAME buffer the kernel wrote into -- the kernel
-- needs MessageId / ClientId intact to match the reply to the waiting
-- thread.
local function decode_msg(harderr_msg)
    return {
        status               = tonumber(harderr_msg.Status),
        response_options     = tonumber(harderr_msg.ValidResponseOptions),
        unicode_string_mask  = tonumber(harderr_msg.UnicodeStringParameterMask),
        pid                  = tonumber(ffi.cast('uintptr_t',
                                  harderr_msg.h.u3.ClientId.UniqueProcess)),
        tid                  = tonumber(ffi.cast('uintptr_t',
                                  harderr_msg.h.u3.ClientId.UniqueThread)),
        params               = decode_params(harderr_msg),
        _raw                 = harderr_msg,
    }
end

-- ------------------------------------------------------------------
-- Port object
-- ------------------------------------------------------------------

local Port = {}
Port.__index = Port

-- Block until the next hard-error message arrives.  Returns a
-- decoded msg table.  No timeout: NT 3.5 LPC waits cannot be bounded,
-- so daemons that need to exit on shutdown should be killed from
-- outside (or the port closed, which makes the receive fail cleanly).
function Port:recv()
    local msg = ffi.new('HARDERROR_MSG')
    lpc.NtReplyWaitReceivePort(self._h, nil, nil, msg)
    return decode_msg(msg)
end

-- Reply to a previously-received msg with one of M.RESPONSE.*.
-- Stamps Response in place and ships the same buffer back via
-- NtReplyPort -- the kernel matches by MessageId stored in the
-- PORT_MESSAGE header which we never touched.
function Port:reply(msg, response_code)
    local raw = msg._raw
    raw.Response = response_code or M.RESPONSE.RETURN_TO_CALLER
    raw.h.u2.s2.Type = lpc.LPC_REPLY
    lpc.NtReplyPort(self._h, raw)
end

-- Convenience loop: call handler(msg) for each incoming message;
-- handler returns the response code (defaults to RETURN_TO_CALLER if
-- nil).  Runs forever until handler raises or the port closes.
function Port:run(handler)
    while true do
        local msg = self:recv()
        local response = handler(msg) or M.RESPONSE.RETURN_TO_CALLER
        self:reply(msg, response)
    end
end

function Port:handle() return self._h end

function Port:close()
    if self._h then self._h:close(); self._h = nil end
    if self._is_default then
        _default_registered = false
        self._is_default = false
    end
end

M.Port = Port

-- ------------------------------------------------------------------
-- Factory
-- ------------------------------------------------------------------

-- Create a connection port (NtCreatePort) and optionally register it
-- as the system default hard-error port.
--
-- opts.default  -- if true, also call NtSetDefaultHardErrorPort.
--                  Required for the production daemon.  Throws if
--                  another caller in this process already registered.
-- opts.oa       -- override OBJECT_ATTRIBUTES (default: path-only).
-- opts.max_msg  -- override max message size (default: sizeof(HARDERROR_MSG)).
function M.listen(name, opts)
    opts = opts or {}

    if opts.default and _default_registered then
        error("nt.harderr.listen: process already owns the default " ..
              "hard-error port (the kernel recursion guard would " ..
              "bugcheck if we re-registered)", 2)
    end

    local port_oa = opts.oa
    if not port_oa and name then
        port_oa = oa.path(name).oa
    end

    local port = lpc.NtCreatePort(port_oa, 0,
                                  opts.max_msg or ffi.sizeof('HARDERROR_MSG'),
                                  0)

    if opts.default then
        ex.NtSetDefaultHardErrorPort(port)
        _default_registered = true
    end

    return setmetatable({
        _h          = port,
        _is_default = opts.default and true or false,
    }, Port)
end

-- For tests / introspection.
function M._is_default_registered() return _default_registered end

return M
