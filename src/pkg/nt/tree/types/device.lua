-- Device — same shape as File in the NT object manager. Proxy to
-- the file handler so \Device\Null, \Device\Serial0, etc. share
-- open() and :read() without duplicating code.

return require('nt.tree.types.file')
