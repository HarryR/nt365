# dbg2dwf: broken DIE reference in ntoskrnl.dwf

## Symptom

```
$ gdb -batch -nx src/NT/PRIVATE/NTOS/INIT/UP/obj/i386/ntoskrnl.dwf \
      -ex 'info address KiAgentExit'
warning: Loadable section ".text" outside of ELF segments
Dwarf Error: Cannot find DIE at 0x5b19 referenced from DIE at 0x5b13
  [in module ntoskrnl.dwf]
```

gdb bails on the DWARF error and never indexes the DWARF symbol table.
The symbol is still in the ELF symtab (`nm | grep KiAgentExit` →
`80127b78 T KiAgentExit`), but gdb's `info address` / scripted
expressions can't see it.

This trips `tools/agent_run.sh`'s pre-flight (any invocation without
`--run-secs`, which is the only mode that skips it) and the
`set $pc = (unsigned long)KiAgentExit` exit-trick inside the gdb
script.

## Workaround

Use `--run-secs N` — agent_run skips the KiAgentExit preflight and
the post-run `set $pc` jump in that mode, relying on a SIGINT timer +
qemu termination instead.

## Investigation hooks

- DIE at 0x5b13 references DIE at 0x5b19 which dbg2dwf failed to emit.
  Find the CodeView record that produced these two DIEs (likely a
  type or typedef relation) — probably a specific NT 3.5 CV4 record
  shape dbg2dwf doesn't fully handle.
- `tools/dbg2dwf.py` (or wherever the converter lives) — look at how
  inter-DIE references are tracked vs emitted; the order of the two
  DIE numbers (5b19 > 5b13) hints at a forward reference that the
  emitter dropped on the floor.
- Not a regression from any recent edit — verified by reasoning over
  the pending diff (only changed numeric thresholds and comments, no
  type/symbol declarations).
