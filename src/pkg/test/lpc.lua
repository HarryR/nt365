-- nt.dll.lpc — struct layout and message-buffer helpers.

local ffi = require('ffi')
local t   = require('test')
local lpc = require('nt.dll.lpc')

t.suite("lpc")

t.test("PORT_MESSAGE matches NT 3.5 layout (24 bytes)", function()
    t.eq(ffi.sizeof('PORT_MESSAGE'), 24)
end)

t.test("PORT_VIEW / REMOTE_PORT_VIEW / PORT_DATA_ENTRY sizes", function()
    t.eq(ffi.sizeof('PORT_VIEW'),        24)
    t.eq(ffi.sizeof('REMOTE_PORT_VIEW'), 12)
    t.eq(ffi.sizeof('PORT_DATA_ENTRY'),  8)
end)

t.test("new_message allocates fused header + payload", function()
    local msg = lpc.new_message(100)
    -- Struct should expose both hdr and data fields.
    t.ok(msg.hdr ~= nil)
    t.ok(msg.data ~= nil)
    -- Payload is writable and reads back.
    msg.data[0] = 0x41
    msg.data[99] = 0x42
    t.eq(msg.data[0], 0x41)
    t.eq(msg.data[99], 0x42)
end)

t.test("init_message stamps DataLength/TotalLength/Type", function()
    local msg = lpc.new_message(64)
    lpc.init_message(msg, 64, lpc.LPC_REQUEST)
    t.eq(msg.hdr.u1.s1.DataLength,  64)
    t.eq(msg.hdr.u1.s1.TotalLength, 64 + lpc.PORT_HEADER_SIZE)
    t.eq(msg.hdr.u2.s2.Type,        lpc.LPC_REQUEST)
    t.eq(msg.hdr.MessageId,         0)
end)

t.test("init_message defaults type to LPC_REQUEST", function()
    local msg = lpc.new_message(16)
    lpc.init_message(msg, 16)
    t.eq(msg.hdr.u2.s2.Type, lpc.LPC_REQUEST)
end)

t.test("default_qos produces valid SECURITY_QUALITY_OF_SERVICE", function()
    local qos = lpc.default_qos()
    t.eq(qos.Length, ffi.sizeof('SECURITY_QUALITY_OF_SERVICE'))
    t.eq(qos.ImpersonationLevel, lpc.SecurityImpersonation)
end)

t.test("live LPC round-trip", function()
    -- Requires a second thread so the server can block on
    -- NtReplyWaitReceivePort while the client issues
    -- NtRequestWaitReplyPort. Waiting on NtCreateThread bridge.
    t.skip("needs NtCreateThread")
end)
