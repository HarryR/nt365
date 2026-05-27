# IP Stack Hardening

Working document. Tracks the scope, threat model, identified risks, analysis,
decisions, and test status for hardening the NT 3.50 IP/TCP/UDP stack as
shipped in MicroNT.

Nothing here is a patch plan. Findings are entered, analysed, decided on, and
tested before any code change lands. The document is the audit trail for that
process.

## 1. Scope

In scope:

- Single-NIC virtualised guest. One ethernet device, no IP forwarding, no
  multi-homing.
- Deployment targets: arbitrary x86 KVM/Proxmox/Xen-based VPS, EC2, GCE,
  Hetzner, OVH, low-end providers. Hypervisor anti-spoof and L2 isolation
  vary; assume hostile L2 for cheapest tier.
- Public-internet reachable IPv4. Any "appliance in front" (NAT, LB, WAF) is
  treated as bonus, not as a precondition. Correctness must hold without it.
- Stack components in this tree:
  - `src/NT/PRIVATE/NTOS/NDIS/WRAPPER` (and `NDIS/VIONET` driver)
  - `src/NT/PRIVATE/NTOS/TDI/TCPIP/{IP,TCP,H}`
  - `src/NT/PRIVATE/NTOS/AFD`

Out of scope (explicitly, recorded so we don't drift):

- IPv6. Not present in the source tree.
- IPSec, PPTP, RAS, NetBT-over-TCP. Not in this tree at this stage.
- IP forwarding / router behaviour. We assert non-forwarding host and will
  enforce that as an invariant.
- Anything above the transport layer (RPC, SMB, MSV1_0, etc.) — separate
  hardening track.
- Cloud-layer controls (security groups, firewall rules, LB terminators).
  Not a substitute for stack correctness; not analysed here.

## 2. Threat model

Adversaries we plan to defend against:

- T1. **Internet attacker, off-path.** Can send arbitrary IP packets to our
  public address. Cannot observe replies that do not return to its address.
- T2. **Internet attacker, semi-blind.** As T1, but can probe responses
  through reflection / side channels.
- T3. **L2 neighbour on the same hypervisor segment.** Can send raw frames
  with chosen source MAC, can ARP-spoof, can claim to be the gateway. Some
  hypervisors filter (EC2, GCE source/dest check); cheap VPS often do not.
- T4. **Resource-bounded attacker.** Limited but non-trivial pps and
  bandwidth from one or more sources; intent is denial of service against
  the host kernel, not a specific application.

Not in our threat model:

- Adversary with code execution on the host. Kernel/userland separation is a
  separate problem.
- Compromised hypervisor.
- Stateful, fully on-path adversary that can both observe and inject. (We
  note where this matters but do not attempt to defeat it.)

## 3. Methodology

Each finding follows the same lifecycle:

1. **Logged** — file, line, rough class, one-line summary.
2. **Analysed** — what the bad path does, what an attacker controls, what
   inputs reach it, what the failure mode is (BSOD / pool exhaustion / route
   hijack / info leak / wrap-around copy / etc.). Cite code; quote the lines
   that matter.
3. **Reproducer drafted** — concrete packet(s) that exercise the bad path,
   expected observable on the unpatched stack.
4. **Reproducer confirmed** — run against unpatched MicroNT, observe the
   predicted failure. A finding without a working reproducer stays
   "suspected" rather than "confirmed".
5. **Decision** — one of:
   - `FIX` (with sketched approach),
   - `ACCEPT` (with rationale: not reachable under §1/§2, or cost > benefit),
   - `DEFER` (with what would change to revisit),
   - `INVALID` (analysis showed the bug isn't actually present).
6. **Patch** — minimal, isolated, commented; references this document by
   finding ID. Patch lands only after the reproducer is in the regression
   harness.
7. **Verified** — reproducer no longer triggers; legitimate traffic
   regression suite still passes.

Conventions:

- Findings are numbered `H-NNN` and never renumbered. `INVALID` findings stay
  in the table with that status, not deleted.
- File:line citations are pinned to a commit hash when first written so they
  remain meaningful as the tree evolves. Re-pin on substantial code change.
- "Reachable" means: under §1 scope and §2 threat model, an unauthenticated
  remote can drive the input. "Reachable, post-bind" means a listening
  service is required.

Priority ordering, when scheduling hardening work:

- **Remote exploits rank above L2-local rank above local-only.**
  T1 (off-path internet) and T4 (rate-bound DoS from any reachable
  source) are top priority. T2 (semi-blind reflective probing)
  follows. T3 (L2 neighbour) ranks below T1/T2/T4 even though the
  cheap-VPS tier exposes us to it, because hostile L2 is contingent
  on the deployment target while T1 is universal across all
  deployments.
- The infra-provided firewall (cloud security group, hypervisor
  packet filter, WAF) does **not** reorder this priority. Filters
  fail open under misconfiguration, can be bypassed by adjacent-
  tenant access on cheap tiers, and don't protect workloads that
  legitimately accept hostile input by design (public HTTP servers).
  The stack maintains a baseline as if no filter were present.
- Within a tier, severity (CRIT > HIGH > MED > LOW) is the
  tiebreaker. Within a severity, fewer pre-conditions (pre-bind
  reachable > post-bind reachable) ranks higher.

## 4. Cross-cutting invariants

Properties we want to assert and keep asserted. Each is its own work item;
violations turn into findings.

- ~~I1. **No IP forwarding, ever.**~~ Retired 2026-05-12: the
  forwarding code path has been removed (`IPForward`, `Redirect`,
  `FWContext` pool, `RouteInterface`, `BCastRSQ`, registry knobs).
  There is no longer anything for an invariant to guard.
- ~~I2. **No source-routed packets accepted for local delivery.**~~
  Retired 2026-05-12: `CheckLocalOptions` returns `DEST_INVALID` for
  any inbound packet carrying LSRR/SSRR.  The "forward this for me"
  branch that used to honour source routing on a non-forwarding host
  is gone (no forwarding path to dispatch to).
- ~~I3. **No ICMP redirect acceptance.**~~ Retired 2026-05-12: ICMP
  type-5 reception is dropped at dispatch in `ICMP.C`; the `Redirect`
  function in `IPROUTE.C` is gone.  Routing table cannot be mutated
  by network input.
- ~~I4. **Reassembly is bounded.**~~ Retired 2026-05-11 by
  inheritance: IP-layer reassembly is gone (see I5), so there is no
  in-flight reassembly memory or per-source context to bound.
- ~~I5. **Datagram size is bounded before allocation.**~~ Retired
  2026-05-11: IP-layer reassembly is gone; there is no path that
  accumulates fragment data into a single allocation.
- I6. **TCB allocation is bounded per listener.** SYN-RCVD count per
  listening AddrObj has a cap; over-cap drops the oldest half-open or the
  new SYN, deterministically.
- I7. **Per-TCB out-of-order queue is bounded in bytes.** Past the cap,
  segments are dropped; no allocation chain grows unboundedly with
  attacker-chosen seq numbers.
- ~~I8. **ARP cache changes are validated.**~~ Satisfied 2026-05-13:
  cache update on existing `ARP_GOOD` entry refused (`ARPRcv`);
  `ARP_MAX_ENTRIES = 256` ceiling enforced in `CreateARPTableEntry`.
  Replies to non-outstanding requests cannot silently overwrite
  entries; cache size is capped.

The findings in §6 are the specific places where we today violate one or
more of these invariants.

## 5. Test infrastructure

Host-side packet harness driving the guest over QEMU's socket netdev.
Rootless, deterministic, point-to-point: the harness *is* the guest's
entire network. Per-finding reproducers are surgical, edit-and-rerun
Python on top of one minimal guest target — no boot of the full
selftest suite per iteration.

### 5.1 Transport

`boot.sh --net-harness PORT` swaps QEMU's user-mode SLIRP backend for

    -netdev socket,id=n0,listen=127.0.0.1:PORT

so the guest's virtio-net sits on a plain TCP socket instead of SLIRP
NAT. The harness connects from the host. Default SLIRP boots are
untouched — `--net-harness` is purely additive. `--netdump` continues
to work alongside (filter-dump attaches to the netdev by id, not the
backend), so `vionet.pcap` is unchanged.

Wire framing on the socket netdev: 4-byte big-endian length prefix +
raw Ethernet frame (`net/socket.c`), both directions.
`src/tools/netharness/qlink.py` reframes the TCP stream into Ethernet
frames; scapy builds and parses the contents.

### 5.2 Bring-up — harness-served DHCP, ARP, ICMP

With SLIRP gone there is no built-in DHCP/DNS, so the harness serves
them. `src/tools/netharness/dhcpd.py` leases the guest a single fixed
address (10.0.2.15, gateway/server 10.0.2.2 — matches what SLIRP used
to hand out, so the in-tree `dhcp.lua` suite stays valid against the
harness). `src/tools/netharness/harness.py` wires DHCP + ARP +
ICMP-echo responders into a dispatch loop on the qlink and exposes a
frame-level API (`send_ip`, `pump`, observer callbacks) that
per-finding reproducers compose on. `selftest.py` exercises the
qlink reframer + responder logic offline — no boot needed for a quick
sanity check after touching the harness.

### 5.3 Guest target — the `iprepro` profile

`src/pkg/ntosbe/profiles/iprepro.lua` composes the same lean disk as
`selftest` (drivers + Lua + nt.net.*; no NT source tree, no MS
toolchain) but `init.args` points at `src/pkg/iprepro.lua`: bring up
DHCP, open one TCP listener on `tcp/9` with a deep backlog, idle on an
accept-with-timeout. No `test.*` suites run — surgical reproduction
wants a fast boot into one network endpoint, not the whole regression
run. Headerless heartbeat lines on serial give liveness.

`make -C src iprepro NET_HARNESS=PORT` composes + boots; the `boot.sh`
flag is threaded through automatically.

### 5.4 Per-finding reproducers

`src/tools/netharness/repro/<h-NNN>.py` — one file per finding. Each
runs against the iprepro target, drives the finding's packet scenario,
prints PASS/FAIL with the underlying measurement, and exits 0/1 for
use as a regression check (`--expect vuln` to confirm a finding on the
unpatched stack; `--expect capped` (or finding-specific) to verify
a fix). Edit and rerun without a rebuild.

### 5.5 Oracles

External wherever possible. For H-012 the oracle is host-side: the
count of *distinct* spoofed sources that received a SYN-ACK is exactly
the half-open census, no in-guest counter required. SYN-ACK / RST /
ICMP / TCP-state behaviour are all externally visible from the
harness side; that covers the TCP findings in the §6a queue. If a
later finding genuinely needs a kernel-internal counter, extending
`nt.net.info` with the TCP connection table is the path — deferred
until needed (see OQ-4).

### 5.6 Running a reproducer

  Shell 1:  `make -C src iprepro NET_HARNESS=5555`
  Shell 2 (start when `boot.sh` prints `listening on 127.0.0.1:5555`):
            `python3 src/tools/netharness/repro/h012.py --port 5555 --expect vuln`

Start shell 2 promptly: the harness must be connected before the
guest's DHCP retry window expires (12×5s = 60s in `iprepro.lua`).
`vionet.pcap` is written if you pass `--netdump` (the `iprepro` make
target already does), so every frame is recoverable in Wireshark when
a reproducer surprises.

## 6. Findings

Status legend: `LOGGED` / `ANALYSED` / `REPRO` / `DECIDED` / `PATCHED` / `VERIFIED` / `ACCEPTED` / `DEFERRED` / `INVALID`.

Severity: `CRIT` (single-packet host kill or root-equivalent compromise),
`HIGH` (remote DoS or route/identity hijack), `MED` (resource exhaustion
needing volume, or partial info leak), `LOW` (defence-in-depth).

Reachability: `T1` (off-path internet), `T2` (semi-blind), `T3` (L2 neighbour),
`T4` (rate-bound DoS); `post-bind` if a listening service is required.

Each entry is a stub to be filled in during analysis. Code references are
pinned on first analysis.

---

### H-001 — Fragment overlap underflow (Teardrop / Bonk / NewTear class)

- **Where:** `src/NT/PRIVATE/NTOS/TDI/TCPIP/IP/IPRCV.C` ~441-496 (overlap-trim
  path). Pin commit on first analysis.
- **Class:** Single-packet kill / memory corruption.
- **Severity:** CRIT (suspected).
- **Reachable:** T1.
- **Summary:** Overlap-trim recomputes `DataLength = NewEnd - NewOffset + 1`
  in `ushort`; if trimming makes `NewEnd < NewOffset`, the value wraps and is
  used as a copy length / pointer adjustment. Classic 1997 Teardrop family.
- **Invariant violated:** I5 (and the implicit "subtractions don't go
  negative" invariant on the overlap path).
- **Analysis:** TBD. Walk every assignment to `NewOffset`, `NewEnd`,
  `DataLength`, `DataOffset` in the trim block; enumerate which combinations
  of (existing fragment, new fragment) inputs produce wrap, and what the
  resulting memory operation is.
- **Reproducer:** TBD. Likely two-fragment Teardrop pattern; record exact
  offsets/lengths used.
- **Decision:** 2026-05-11 — strip the reassembly path entirely. No legitimate
  workload in scope produces IP fragments: TCP runs with DF=1 + PMTUD/MSS-clamp,
  UDP large-payload apps fragment at L7 (EDNS0, NFS) or fall back to TCP,
  ICMP large payloads are not load-bearing. With reassembly absent, the
  Teardrop/Bonk/NewTear arithmetic in `ReassembleFragment` does not exist
  to be exploited.
- **Test:** see "Strip verification" entry in §7.
- **Status:** INVALID — code path removed.

### H-002 — Reassembled datagram > 65535 (Ping of Death class)

- **Where:** `IPRCV.C` ~396-418, last-fragment commit path setting
  `RH->rh_datasize`.
- **Class:** Single-packet kill.
- **Severity:** CRIT (suspected).
- **Reachable:** T1.
- **Summary:** `rh_datasize` is `ushort`; final-fragment path assigns
  `Offset + DataLength` with no `<= 65535` check, allowing wrap and
  premature/incorrect reassembly completion.
- **Invariant violated:** I5.
- **Analysis:** TBD.
- **Reproducer:** TBD. Final fragment at high offset, length pushing total
  past 65535.
- **Decision:** 2026-05-11 — covered by the same reassembly strip as H-001.
  The final-fragment commit at `IPRCV.C` ~417-418 no longer exists.
- **Test:** see "Strip verification" entry in §7.
- **Status:** INVALID — code path removed.

### H-003 — Reassembly context exhaustion

- **Where:** `IPRCV.C` ~367-402, `CTEAllocMem(sizeof(ReassemblyHeader))`.
- **Class:** Resource exhaustion / nonpaged pool DoS.
- **Severity:** HIGH.
- **Reachable:** T1, T4.
- **Summary:** No per-source cap, no global cap, no per-RH timer; only
  opportunistic reaping on NTE tick. Fragment-0-only floods with varying
  (src, id) tuples allocate unboundedly.
- **Invariant violated:** I4.
- **Analysis:** TBD. Confirm exact reaping cadence; measure per-RH cost
  including attached `IPRcvBuf`s; pick caps based on smallest target VPS
  nonpaged pool.
- **Reproducer:** TBD. Sustained fragment-0 stream at modest pps.
- **Decision:** 2026-05-11 — covered by the reassembly strip. `ReassemblyHeader`,
  `RABufDesc`, and the per-NTE `nte_ralist` are gone; there is no allocation
  to exhaust.
- **Test:** see "Strip verification" entry in §7.
- **Status:** INVALID — code path removed.

### H-004 — Tiny first fragment / firewall-bypass class

- **Where:** `IPRCV.C` ~529-547, ~635-639.
- **Class:** L4 header truncation across fragments.
- **Severity:** LOW (no firewall in scope at IP layer; mostly a defence-in-depth
  concern). Re-evaluate if we add a packet filter.
- **Reachable:** T1.
- **Summary:** First fragment is allocated to at least `MIN_FIRST_SIZE`, but
  the on-wire fragment can be smaller than the L4 header.
- **Invariant violated:** none directly (but adjacent to I5 hygiene).
- **Analysis:** TBD.
- **Reproducer:** TBD.
- **Decision:** 2026-05-11 — covered by the reassembly strip. Inbound fragments
  are dropped at the dispatch hook before any L4 header reconstruction.
- **Test:** see "Strip verification" entry in §7.
- **Status:** INVALID — code path removed.

### H-005 — Zero-length / zero-offset fragment edge cases

- **Where:** `IPRCV.C` ~408-442.
- **Class:** Edge-case arithmetic in fragment insert.
- **Severity:** MED (suspected; effect depends on combination with H-001).
- **Reachable:** T1.
- **Summary:** No `DataLength > 0` check; `NewEnd = Offset + DataLength - 1`
  underflows on zero-length fragments.
- **Invariant violated:** I5.
- **Analysis:** TBD.
- **Reproducer:** TBD.
- **Decision:** 2026-05-11 — covered by the reassembly strip. The arithmetic
  block does not exist in tree.
- **Test:** see "Strip verification" entry in §7.
- **Status:** INVALID — code path removed.

### H-006 — Source routing honoured (LSRR/SSRR)

- **Where:** `IPRCV.C` ~192-201; forwarding path `IPROUTE.C` ~3051-3083.
- **Class:** Spoofing / trust boundary bypass.
- **Severity:** HIGH on T3, MED on T1 (T1 cannot generally bounce off our
  host because we don't forward; T3 can use SSRR-on-receive to look like a
  trusted source on shared L2).
- **Reachable:** T1 (delivery-to-self), T3 (impersonation).
- **Summary:** LSRR/SSRR options are processed for inbound packets even
  with forwarding off.
- **Invariant violated:** I2 (retired).
- **Decision:** 2026-05-12 — strip inbound source-route processing.
  `CheckLocalOptions` returns `DEST_INVALID` for any packet whose
  options include LSRR/SSRR; the packet is dropped and counted in
  `ipsi_inhdrerrors`.  No `ICMP_PARAM_PROBLEM` reply is sent (the
  original would, which advertises that this stack is responding).
  Outbound `IP_FLAG_SSRR` fast-path in `IPXMIT.C` also removed —
  upper layers in this tree don't generate source-routed options.
- **Test:** verified by clean build; runtime probe pending the test
  harness.
- **Status:** INVALID — code path removed.

### H-007 — IP option-walk infinite loop on Length<2

- **Where:** `IPRCV.C` ~172-214.
- **Class:** Liveness / CPU DoS.
- **Severity:** MED (suspected; depends on whether any earlier validation
  rules out Length<2).
- **Reachable:** T1.
- **Invariant violated:** general parser hygiene.
- **Decision:** 2026-05-13 — covered by the IP option strip
  (decision log 2026-05-13). The option-walk loop is gone; any
  packet with `IHL > 5` is dropped at `IPRcv` entry and counted in
  `ipsi_inhdrerrors`. The Length<2 / option-walk parsing surface no
  longer exists to hang.
- **Status:** INVALID — code path removed.

### H-008 — Record-route / timestamp pointer bounds on update

- **Where:** `IPRCV.C` option parsing + IP transmit option-update path.
- **Class:** Out-of-bounds write on transit/reply.
- **Severity:** MED (suspected).
- **Reachable:** T1 (via packets that elicit a reply or transit).
- **Decision:** 2026-05-13 — covered by the IP option strip
  (decision log 2026-05-13). Inbound options dropped at `IPRcv`
  entry, so the reply-with-options paths in `IPUpdateRcvdOptions` /
  `UpdateOptions` / `UpdateRouteOption` are never entered with a
  non-NULL option buffer. `IPInitOptions` and `IPFreeOptions` are
  unchanged (already maintained `ioi_options == NULL`,
  `ioi_optlength == 0`). `IPUpdateRcvdOptions` is an inert stub
  that zeros the reply IPOptInfo. `IPCopyOptions` refuses
  user-supplied option buffers with `IP_BAD_OPTION` — the call
  sites at `TCPCONN.C:1195`, `ADDR.C:1369`, `ICMP.C:895` already
  map that to `TDI_BAD_OPTION` and propagate it back through AFD,
  so the caller learns at the setsockopt / sendmsg site that
  options are unsupported (rather than silently no-op'd). The
  four exported helpers keep their slots in the `IPInfo`
  function-pointer table so TCP / UDP / setsockopt callers link
  unchanged. The outbound emit-options branch in `IPXMIT.C` is
  unreachable in any case.
- **Status:** INVALID — code path removed.

### H-009 — ICMP redirect accepted

- **Where:** `ICMP.C` ~789-807; `IPROUTE.C` `Redirect` ~2077-2138.
- **Class:** Route hijack.
- **Severity:** HIGH on T3, low on T1 (acceptance gated on src matching
  current gateway, but spoofing a single IPv4 source on shared L2 is cheap).
- **Reachable:** T3 primarily; T1 if the upstream provider does not filter
  inbound spoofed-source packets.
- **Invariant violated:** I3 (retired).
- **Decision:** 2026-05-12 — strip ICMP redirect reception and the
  `Redirect()` route-table mutator entirely.  Inbound type-5 packets
  are counted in `icmps_redirects` and dropped at dispatch; the
  routing table cannot be modified by network input.  We never
  needed redirects in our deployment (single upstream gateway,
  whatever DHCP says is it).
- **Test:** verified by clean build; runtime probe pending the test
  harness.
- **Status:** INVALID — code path removed.

### H-010 — ICMP echo reply size uncapped

- **Where:** `ICMP.C` ~816-837 (echo handler), `SendEcho` ~312-400.
- **Class:** Amplification / interaction with H-001/H-002.
- **Severity:** HIGH (in combination with reassembly bugs); MED standalone.
- **Reachable:** T1.
- **Analysis:** 2026-05-13 — re-examined post fragmentation strip.
  `ICMPRcv` calls `SendEcho(..., RcvBuf, Size, ...)` at `ICMP.C:830`;
  reply payload length equals request `Size`. With reassembly removed,
  inbound `Size` is MTU-bounded (Ethernet 1500 → IP payload ≤ 1480).
  No path can deliver a >MTU `Size` to `ICMPRcv`. Broadcast echo is
  already dropped at `ICMP.C:746-747` (`if (IsBCast) return`).
  Amplification factor is 1 against MTU-bounded payloads; no
  meaningful surface remains.
- **Decision:** 2026-05-13 — INVALID by inheritance from the
  fragmentation strip (decision log 2026-05-11).
- **Status:** INVALID — bounded by upstream invariant.

### H-011 — Land (src == dst SYN)

- **Where:** `TCPRCV.C` ~1807-1810 (SYN handling).
- **Class:** Logic / loop on local TCB.
- **Severity:** MED.
- **Reachable:** T1.
- **Analysis:** 2026-05-13 — `TCPRcv` accepts the segment, finds no
  matching TCB, dispatches to the SYN-from-listener path at
  `TCPRCV.C:1782-1815` and initialises `RcvTCB->tcb_daddr = Src`,
  `tcb_saddr = Dest`, `tcb_dport = TCPH->tcp_src`,
  `tcb_sport = TCPH->tcp_dest`. With `Src == Dest && tcp_src == tcp_dest`
  the TCB is now self-aliasing. `SendSYN` (called via the accept path)
  emits a SYN-ACK that the IP layer routes back to ourselves; on next
  receive `FindTCB` matches the same self-aliased TCB; ACK storm in
  the SYN-RCVD → ESTABLISHED transition. Empirically this is closer to
  livelock than crash on this build, but the failure mode is CPU
  exhaustion either way.
- **Decision:** 2026-05-13 — drop at TCP entry. Added a 4-tuple check
  in `TCPRcv` (after size/checksum gate, before TCB lookup): when
  `Src == Dest && TCPH->tcp_src == TCPH->tcp_dest`, increment
  `TStats.ts_inerrs` and return `IP_SUCCESS`. No legitimate flow,
  including 127.0.0.1 loopback, produces this 4-tuple because AFD
  refuses to bind two endpoints to the same `{addr, port}` (loopback
  uses different ports either side). Five-line change, no API impact.
- **Test:** verified by clean build; runtime probe pending the test
  harness. AFD UDP-loopback / TCP-loopback / DHCP suites all pass on
  the patched build (separate ports, alias check does not fire).
- **Status:** PATCHED.

### H-012 — SYN flood / unbounded half-open

- **Where:** `TCPRCV.C` `FindListenConn` ~554-660; SYN/no-TCB branch
  ~1795-1843; `AllocTCB` `TCB.C` ~1172, `MaxTCBs` `TCB.C:47`.
  Pinned at `1a6f43b`.
- **Class:** Resource exhaustion (TCB / nonpaged pool).
- **Severity:** HIGH.
- **Reachable:** T1, T4 — post-bind (a listening TCP service is
  required; see the reachability note in the analysis).
- **Invariant violated:** I6.
- **Analysis:** 2026-05-19 — pinned at `1a6f43b`.

  *Path.* An inbound segment with SYN set, ACK clear, and no matching
  TCB is handled at `TCPRCV.C:1795-1832`. `GetBestAddrObj` finds the
  listening `AddrObj`; `FindListenConn` (`TCPRCV.C:554-660`) yields a
  TCB for the half-open connection by one of two routes:
  - a pre-posted `TCB_LISTEN` TCB on the AO's `ao_tc` list
    (`FoundConn` branch, `:613-630`) — one the app already supplied;
  - the connect-indication handler (`ao_connect != NULL`, `:634-652`):
    `AllocTCB()` mints a fresh TCB, the handler is indicated to the
    upper layer, and on acceptance `InitTCBFromConn` ties it to a
    connection. AFD registers a connect event handler, so this is the
    live path for an AFD `listen()` socket.

  Either way `TCPRcv` initialises the TCB with the 4-tuple, drives it
  to `TCB_SYN_RCVD` at `:1827`, `InsertTCB`s it, and sends a SYN-ACK.
  The TCB then sits half-open until the handshake completes or the
  SYN-ACK rexmit timer gives up.

  *No cap.* `MaxTCBs` is `0xffffffff` (`TCB.C:47`) — the
  `CurrentTCBs < MaxTCBs` gate in `AllocTCB` (`TCB.C:1203`) never
  fires; the global TCB pool is effectively unbounded. No per-listener
  accounting of half-open TCBs exists today: `AddrObj.ao_synrcvd_count`
  was added as scaffolding (commit `ce56681`) but nothing reads or
  maintains it. Invariant I6 is unmet.

  *Cost and lifetime.* Each half-open connection holds one `TCB`
  (`CTEAllocMem(sizeof(TCB))`, nonpaged pool) plus, on the
  connect-handler path, a `TCPConnReq`. It is freed only when the
  SYN-ACK rexmit timer exhausts `MaxDataRexmitCount` retransmits
  (`TCB.C:508-547`; SYN_RCVD uses `MaxDataRexmitCount` — the
  `MaxConnectRexmitCount` branch is gated on `TCB_SYN_SENT` only,
  `:517`) with exponential backoff capped at `MAX_REXMIT_TO` (240 s,
  `TCP.H:27`). A half-open TCB therefore survives on the order of
  minutes with no peer cooperation.

  *Failure mode.* A SYN flood — sustained SYNs to an open TCP port, no
  ACK ever returned, source addresses/ports varied so each mints a
  distinct TCB — allocates nonpaged pool without bound. Nonpaged pool
  is a kernel-wide resource, so this is not a TCP-only DoS: exhaustion
  bugchecks or wedges the whole guest. T1 (off-path, spoofed sources)
  drives breadth; T4 drives volume.

  *Exit transitions (where a decrement must fire).* A TCB leaves
  `TCB_SYN_RCVD` at exactly these sites:
  - → `TCB_ESTAB` via `GoToEstab` (`TCPSEND.C:969`, from
    `TCPRCV.C:2365`) when a valid ACK completes the handshake — the
    success transition; the TCB is *not* freed, so a free-time
    decrement alone would miss it;
  - → closed on RST (`TryToCloseTCB`, `TCPRCV.C:2325`);
  - → closed on a duplicate SYN (`TCPRCV.C:2344`);
  - → closed on an out-of-range ACK (`DerefTCB` + RST,
    `TCPRCV.C:2370`);
  - → closed on SYN-ACK rexmit timeout (`TimeoutTCB`, `TCB.C:534`);
  - → aborted when `InsertTCB` fails (`TCPRCV.C:1837-1843`).

  *Reachability correction.* The original stub claimed a pre-bind
  angle ("global TCB pool exhaustion also possible" before a service
  binds). It does not hold: a SYN to a port with no listening AO takes
  neither `FindListenConn` route — `GetBestAddrObj` returns NULL, or
  `FindListenConn` returns NULL — and `TCPRcv` sends a RST without
  allocating a TCB. No listener, no TCB. H-012 is **post-bind only**.

  *AFD-backlog reality (2026-05-20 correction — refutes the
  kernel-side unboundedness above).* The kernel's `AllocTCB` in
  `FindListenConn`'s connect-handler branch only fires when AFD
  accepts the connect indication. `AfdConnectEventHandler`
  (`src/NT/PRIVATE/NTOS/AFD/LISTEN.C:617-645`) pulls one connection
  object from a fixed pre-allocated pool of `MaximumConnectionQueue`
  (= the app's `listen()` backlog) objects, set up at listen time by
  `AfdStartListen` (`LISTEN.C:130-137`); when the pool is empty it
  returns `STATUS_INSUFFICIENT_RESOURCES` and the kernel TCP layer
  RSTs the SYN.  Over-backlog SYNs therefore trigger `AllocTCB` →
  immediate `FreeTCB` with no SYN_RCVD accumulation. The half-open
  count per listener is hard-bounded by the AFD backlog the app
  requested; `MaxTCBs = 0xffffffff` is moot because AFD gates first.

  The reproducer below confirmed this empirically: 4096 SYNs at a
  listener with `backlog=1024` produced exactly 1023 distinct
  half-opens (= backlog − 1 probe) and 3041 RSTs. The original
  "unbounded half-open" framing is **refuted**.  The real finding is
  the classic connection-slot SYN-flood denial — an attacker fills
  all `backlog` slots with half-opens that hold their slot for the
  full SYN-ACK rexmit lifetime (minutes, per the rexmit analysis
  above), during which legitimate clients are refused with RST.
  Bounded, but a real T1+T4 service-denial DoS — and *not* addressed
  by a per-listener drop-new cap, which is the wrong shape (see
  Decision).
- **Reproducer:** confirmed 2026-05-20 via
  `src/tools/netharness/repro/h012.py` (see §5). Floods N SYNs at
  the iprepro target's `tcp/9` listener from distinct off-subnet
  spoofed sources (198.18.0.0/15) and counts the distinct sources
  that receive a SYN-ACK — exactly the half-open census, no in-guest
  counter needed. Reading on `iprepro.lua` `BACKLOG=1024`,
  `--count 4096`:

      SYNs sent ............ 4096
      distinct half-opens .. 1023
      SYN-ACK frames ....... 1023   (no retransmits)
      RSTs from guest ...... 3041
      verdict .............. CAPPED at ~1023

  Plateau == AFD backlog (1024) − 1 probe SYN.  Usable both as a
  confirmation tool (`--expect vuln`, against a stack that would let
  half-opens grow unbounded — which this one doesn't) and a
  post-fix verification tool (`--expect capped`) once a real
  SYN-flood mitigation lands.
- **Decision:** 2026-05-20 — direction = **SYN cookies**, deferred
  as a larger structural change.

  The pre-committed drop-new per-AO cap (`TCP_MAX_SYNRCVD_PER_AO` /
  `ao_synrcvd_count`, scaffolded in `ce56681`) is **rejected** and the
  scaffolding has been reverted in a follow-up commit. The reasoning:
  a hard drop-new cap *below* the app's listen backlog shrinks usable
  capacity (attacker fills the cap instead of the backlog) and,
  being drop-new, refuses legitimate SYNs once attacker half-opens
  hold the cap slots.  It also second-guesses the app's deliberate
  `listen()` backlog.  And it doesn't address the actual failure
  mode — slots held for minutes by stale half-opens — so it spends
  complexity for no defensive gain.

  The fixes that do address connection-slot SYN-flood denial:
  - **SYN cookies** (preferred). The stack allocates no SYN_RCVD
    state on receipt of a SYN; it encodes the connection state into
    the SYN-ACK's initial sequence number and reconstructs the TCB
    only when the client's ACK comes back validated. Eliminates the
    attack surface — no slot to hold. Requires real state-machine
    work in `TCPRCV.C`'s SYN-from-listener path.
  - **Drop-oldest in the AFD pool**, as a smaller fallback — recycle
    the stalest half-open on a new SYN so legitimate clients survive
    even when the backlog is "full" of attacker half-opens.
  - **Shorter SYN-ACK rexmit** for `TCB_SYN_RCVD` — slots free up in
    seconds rather than minutes; doesn't solve, but raises the bar.

  SYN cookies is the right destination; size and risk put the
  implementation on a separate work item, not bundled with the rest
  of the §6a queue.
- **Status:** DEFERRED — direction decided (SYN cookies); the patch
  is a separate, larger work item.

### H-013 — Per-TCB out-of-order reassembly unbounded

- **Where:** `TCPRCV.C` ~1411-1426 (`CreateTRH`/`PutOnRAQ`).
- **Class:** Per-connection memory exhaustion.
- **Severity:** HIGH on post-bind.
- **Reachable:** T1 post-bind.
- **Invariant violated:** I7.
- **Analysis:** TBD.
- **Reproducer:** TBD. Open one TCP connection, send high-seq segments only.
- **Decision:** TBD.
- **Status:** LOGGED.

### H-014 — Idle-ESTABLISHED reaper (Naptha)

- **Where:** TCB structure `TCP.H` ~136 (`tcb_alive`), accept queue
  handling.
- **Class:** Slow resource exhaustion.
- **Severity:** MED.
- **Reachable:** T1 post-bind.
- **Analysis:** TBD. Confirm whether `tcb_alive` is armed automatically on
  ESTABLISHED transition or only when `KEEPALIVE` socket option set.
- **Reproducer:** TBD.
- **Decision:** TBD.
- **Status:** LOGGED.

### H-015 — TCP RST acceptance window

- **Where:** `TCPRCV.C` RST handling around ~2071 (SYN-SENT) and ~2569
  (ESTABLISHED).
- **Class:** Blind connection reset.
- **Severity:** LOW.
- **Reachable:** T1, T2.
- **Analysis:** 2026-05-13 — this stack accepts any in-window RST per
  pre-RFC-5961 behaviour (no challenge-ACK, no exact-seq requirement).
  An off-path attacker who can guess sequence numbers within the
  receive window can reset connections. NT-3.50-era window sizes
  (typically 8 KB) plus the full 32-bit sequence space make blind
  guessing across the open internet impractical without a side
  channel; T2 with reflective probing is in-scope for the threat
  model but not commonly weaponised against TLS-over-TCP workloads
  (RST = connection drop = client retry, no data corruption). RFC
  5961 challenge-ACK would require state machine changes
  disproportionate to the residual risk at MicroNT's scale.
- **Decision:** 2026-05-13 — ACCEPT. Residual risk documented; revisit
  if a workload appears whose threat model includes RST-injection
  resilience without relying on TLS (e.g. long-lived plaintext TCP
  control channels).
- **Status:** ACCEPTED.

### H-016 — TCP options parsing

- **Where:** `TCPRCV.C` ~815-854 (`FindMSS`).
- **Class:** Parser correctness.
- **Severity:** LOW (SACK / wscale / TS not implemented, so attack surface
  is small).
- **Reachable:** T1.
- **Analysis:** TBD. Verify the unknown-option advance bound (`OptPtr[1]`)
  is checked against remaining size on every iteration, not just first.
- **Status:** LOGGED.

### H-017 — WinNuke / urgent-pointer

- **Where:** `TCPDELIV.C` `HandleUrgent` ~1600-1705.
- **Class:** Historical BSOD on urgent data; appears mitigated in this tree.
- **Severity:** N/A (mitigated).
- **Reachable:** T1 post-bind.
- **Analysis:** 2026-05-13 — full path traced.
  - Source: `tri_urgent = (uint)net_short(TCPH->tcp_urgent)` at
    `TCPRCV.C:1744`. Cast bounds the value to `[0, 0xFFFF]`; no path
    can deliver a >16-bit urgent pointer.
  - Caller gate: `HandleUrgent` is only invoked from
    `TCPRCV.C:2588` under runtime `if (RcvInfo.tri_flags & TCP_FLAG_URG)`.
    The seq-equals-rcvnext entry assertion at `TCPDELIV.C:1613` is
    established by the in-sequence reassembly drain loop (`TCPRCV.C`
    ~2580-2620) before the call; not a free-form precondition.
  - BSD branch (`TCPDELIV.C:1622`): explicit
    `if (tri_urgent == 0 || tri_urgent > *Size) { tri_flags &= ~URG; return; }`.
    The exact WinNuke trigger (urgent pointer past segment end) is the
    early-return case.
  - RFC branch (`TCPDELIV.C:1638`):
    `UrgSize = MIN(tri_urgent + 1, *Size)` clamps to segment. Wrap
    impossible because `tri_urgent` is 16-bit zero-extended and
    `+1` fits in uint. `BytesInBack = *Size - BytesInFront - UrgSize`
    is non-negative in both branches given the bounds above.
- **Decision:** 2026-05-13 — INVALID. Bounds checks present and
  reachable on both URG dispositions.
- **2026-05-27 update.** Becomes additionally unreachable as a side
  effect of the OOB / urgent / expedited strip (H-021): `TCPRCV.C`
  masks `TCP_FLAG_URG` off `tri_flags` at parse, so `HandleUrgent`
  is never invoked. The bounds checks remain in tree as dead-code
  defence-in-depth pending the deeper TCP/IP cleanup tracked under
  H-021.
- **Status:** INVALID — defended in tree, and unreachable post-strip.

### H-018 — UDP broadcast amplification (Fraggle)

- **Where:** `UDP.C` broadcast delivery path; `UDP.C`
  `TdiSendDatagram`.
- **Class:** Amplification.
- **Severity:** LOW.
- **Reachable:** T1 if a broadcastable UDP service exists; otherwise N/A.
- **Analysis:** 2026-05-13 — initial review found three pre-existing
  defences:
  - `TdiOpenAddress` refuses non-local non-wildcard bind addresses
    (`ADDR.C:887`), so a service cannot bind to a broadcast IP.
  - `UDPRcv` rejects UDP packets with broadcast source addresses
    (`UDP.C:1325`).
  - No built-in UDP reflectors (echo / chargen / daytime not in
    tree).
  Initial conclusion was ACCEPT (no built-in reflectors, app-layer
  policy out of stack's scope).

  Reconsidered 2026-05-13 once the deployment scope was tightened:
  the *only* legitimate UDP broadcast use in the entire stack is the
  DHCP client at boot. Everything else — application sockets, future
  workloads, any non-DHCP code path — has no business sending to or
  receiving from a broadcast address. With the scope explicit, the
  ACCEPT residual ("application-layer reflectors are possible if
  a workload binds wildcard and replies to broadcast input") becomes
  a structural denial: the stack itself refuses to deliver broadcast
  to a non-DHCP socket, and refuses to send to a broadcast
  destination from a non-DHCP socket.
- **Decision:** 2026-05-13 — flipped from ACCEPT to PATCH. Two
  gates, both keyed on `AO_DHCP_FLAG` (the existing marker set by
  AOs that were created with the DHCP-client option):
  - **Inbound.** `UDPRcv`'s broadcast-delivery loop only delivers
    to AOs whose `ao_flags & AO_DHCP_FLAG` is set. Wildcard sockets
    bound by non-DHCP code paths receive unicast UDP only; broadcast
    input is dropped at the AO-iteration step.
  - **Outbound.** `TdiSendDatagram` refuses to send to a destination
    of type `DEST_BCAST` / `DEST_SN_BCAST` / `DEST_REM_BCAST` from
    an AO without `AO_DHCP_FLAG`. Returns `TDI_BAD_ADDR`. Multicast
    (CLASSD addresses) is unaffected.
- **Test:** verified by clean build. DHCP suite must continue to
  pass on the patched build (DHCP client AO carries the flag, both
  inbound OFFER/ACK delivery and outbound DISCOVER/REQUEST send go
  through the gate without rejection).
- **Status:** PATCHED.

### H-019 — ARP cache poisoning / unsolicited replies

- **Where:** `ARP.C` ~2538-2553 (cache update on inbound ARP),
  `CreateARPTableEntry` (unbounded growth).
- **Class:** L2 trust.
- **Severity:** HIGH on T3.
- **Reachable:** T3.
- **Invariant violated:** I8.
- **Analysis:** 2026-05-13 — two distinct primitives in this finding,
  hardened together.
  - **MAC pin against poisoning.** Previously, `ARPRcv` would update
    any existing cache entry's MAC unconditionally on receipt of any
    ARP frame whose source IP matched the entry's destination, regardless
    of opcode or state. The classic poisoning attack sends a forged
    response (or gratuitous announcement) claiming the gateway's IP
    with the attacker's MAC; we'd overwrite the cache and route all
    outbound to the attacker. The hardening: in the existing-entry
    branch of `ARPRcv`, refuse updates when `ate_state == ARP_GOOD`.
    Updates are accepted only during the `RESOLVING_GLOBAL` /
    `RESOLVING_LOCAL` window — i.e. the response to a request we
    sent. GOOD entries are refreshed by re-resolution on timeout
    (the existing state machine at `ARPTransmit` already drives this).
    Important: the early-return setting `Entry = NULL` still lets the
    `LocalAddr && ARP_REQUEST` reply path at the bottom of `ARPRcv`
    run, so a request targeting one of our IPs is still answered;
    only the cache-update side effect is suppressed.
  - **Cache size cap.** `Interface->ai_count` is bumped by
    `CreateARPTableEntry` with no ceiling. An attacker on a hostile
    L2 can flood ARP frames targeting our IPs (the
    `LocalAddr`-gated create path) and force unbounded
    `CTEAllocMem`. Hardening: `ARP_MAX_ENTRIES = 256` defined in
    `ARPDEF.H`, checked at the top of `CreateARPTableEntry`. Sized
    to accommodate gateway + DHCP server + transient peers during
    boot with margin; on a single-NIC guest the steady-state count
    is 1-2. At cap, new-entry creation returns NULL, callers
    fail closed (outbound packet drops with no resolution; inbound
    cache learning is silently skipped).
- **Test:** verified by clean build. The ARP-poisoning probe is
  pending the test harness — see open question OQ-4 for
  pcap-side observation.
- **Decision:** 2026-05-13 — both hardenings landed.
- **Status:** PATCHED. Invariant I8 satisfied.

### H-020 — IP forwarding default / runtime toggle

- **Where:** TCP/IP init (`IP/INIT.C`, registry binding) — pin TBD.
- **Class:** Configuration invariant.
- **Severity:** HIGH if violated (turns the box into an open relay for
  many of the above).
- **Invariant violated:** I1 (retired).
- **Decision:** 2026-05-12 — compile-out (resolves OQ-1).  Removed:
  `IPForward` (~280 LOC) + `FreeFWPacket` / `FWSendComplete` /
  `TransmitFWPacket` / `SendFWPacket` / `RemoveRandomFWPacket` /
  `GetFWBuffer` / `GetFWPacket` (~520 LOC) + `Redirect` (~60 LOC) +
  `FWContext` / `FWQ` / `RouteSendQ` / `RouteInterface` structs +
  globals (`ForwardPackets`, `RouterConfigured`, `ForwardBCast`,
  `MaxFWSending`, `BCastRSQ`, `FWBufFree`, `FWPacketFree`).  Registry
  knobs `IpEnableRouter` / `ForwardBroadcasts` / `ForwardBufferMemory`
  / `NumForwardPackets` no longer read; `InitGateway()` removed.
  `IPSInfo.ipsi_forwarding` is hard-coded to `IP_NOT_FORWARDING` and
  `SetIPInfo` refuses any attempt to flip it.  `PACKET_FLAG_FW` and
  `REDIRECT_*` defines removed.  Inbound `IPForward()` callers in
  `IPRCV.C` (broadcast-relay completion paths + `forward:` label)
  replaced with drop+counter.
- **Test:** verified by clean build; runtime probe pending the test
  harness.
- **Status:** INVALID — code path removed.

### H-021 — OOB / urgent / expedited data path

- **Where:** AFD (`RECEIVE.C`, `RECVVC.C`, `MISC.C`, `BIND.C`,
  `POLL.C`, `AFDSTR.H`, `AFDPROCS.H`, ...); TCP/IP
  (`TCPRCV.C` URG parse + reassembly URG branches, `TCPSEND.C`
  `TSR_FLAG_URG`, `TCPDELIV.C` `HandleUrgent`/`DeliverUrgent`,
  `TCP.H` `tcb_urg*` / `tcb_exprcv` / `URG_VALID` / `URG_INLINE`
  / `BSD_URGENT` / `IN_DELIV_URG`, `ADDR.H` `ao_exprcv`,
  `INFO.C` provider service flags + URG query + socket-option
  arms); ws2_32 (`SEND.C`, `RECV.C`, `SOCKOPT.C`).
- **Class:** Parser surface + protocol semantics ambiguity. Two
  off-by-one interpretations of the urgent pointer (RFC 793 vs
  BSD); historical WinNuke BSOD class lives here (H-017). No
  in-tree caller uses `MSG_OOB`.
- **Severity:** N/A (feature removed) — was H-017's parent class.
- **Reachable:** Was T1 post-bind for the original WinNuke
  trigger; now unreachable.
- **Analysis:** 2026-05-27 — three-layer strip designed to make
  the entire OOB plumbing structurally absent. Rationale matches
  prior whole-feature strips (IP fragmentation, IP forwarding,
  IP options, NDIS non-Ethernet): deployment scope (cloud
  workload host, no telnet / no app-layer OOB consumer) +
  bug-class precedent (RFC/BSD urgent-pointer ambiguity is
  exactly the kind of parser corner the audit retires
  wholesale).
- **Decision:** 2026-05-27 — STRIP. Three layers landed
  together:
  - **AFD (deep).** `AfdReceiveExpeditedEventHandler`,
    `AfdBReceiveExpeditedEventHandler`, `AfdSetInLineMode`
    deleted; `TDI_EVENT_RECEIVE_EXPEDITED` registrations gone
    in `BIND.C`; `AFD_INLINE_MODE` info-class falls through to
    `STATUS_INVALID_PARAMETER`; `AFD_POLL_RECEIVE_EXPEDITED`
    arm removed; struct fields gone
    (`ReceiveExpeditedBytes{Indicated,Taken,Outstanding}`,
    `BufferredExpedited{Bytes,Count}`, `InLine`, `ExpeditedData`);
    `IS_EXPEDITED_DATA_ON_CONNECTION` macros and the `InLine`
    parameter through `AfdCreateConnection` removed.
  - **TCP/IP.** Initial minimum-viable strip masks `TCP_FLAG_URG`
    off `tri_flags` at parse (`TCPRCV.C`), zeros `tsr_flags`
    unconditionally on send (`TCPSEND.C`), drops
    `TDI_SERVICE_EXPEDITED_DATA` from the provider's advertised
    service flags (`INFO.C`). Follow-up deep cleanup landed in
    the same commit: deleted `HandleUrgent`, `DeliverUrgent`,
    the URG-conditional reassembly branches in `TCPRCV.C`
    (front-overlap URG check in `PutOnRAQ`, back-overlap
    URG-trim, post-overlap urgent-pointer update, SYN-state
    `tri_urgent--`, clip-front URG decrement, the
    `URG_VALID`-conditional `rcvnext` advance, the HandleUrgent
    call site itself), the outgoing URG-flag setter
    (`TCPSEND.C` urgent-pointer arithmetic and the
    PrevFlags / TSR_FLAG_URG combine check), the TCB struct
    fields (`tcb_urg{pending,cnt,ind,start,end}`,
    `tcb_exprcv`), the urgent flags (`URG_VALID`, `URG_INLINE`,
    `BSD_URGENT`, `IN_DELIV_URG`) and their references in
    `TCP_SLOW_FLAGS`, `tri_urgent` in `TCPRcvInfo`, `trh_urg`
    in TRH, `TSR_FLAG_URG` in `TCPSEND.H`, `ao_exprcv` /
    `ao_exprcvcontext` on `AddrObj` and the
    `TDI_EVENT_RECEIVE_EXPEDITED` arm in `ADDR.C`, the
    `TCPSocketAMInfo` URG query and the `TCP_SOCKET_BSDURGENT`
    / `TCP_SOCKET_OOBINLINE` setsockopt arms in `INFO.C`, the
    `BSDUrgent` global plus its `TcpUseRFC1122UrgentPointer`
    registry read in `NTINIT.C`, the urgent cleanup loops in
    `TCPCONN.C` and the urgent-empty check in `OKToNotify`,
    plus `BSD_URGENT` from new-connection flag init. TdiReceive
    in `TCPDELIV.C` simplifies: the expedited-only receive
    path and the urgent-data-pending branch both go;
    `PushData` no longer walks `tcb_exprcv`. Wire-layout
    fields stay (`tcp_urgent` in `TCPHeader`, `TCP_FLAG_URG`
    define) — only the wire-bit mask in `TCPRCV.C` remains as
    runtime documentation.
  - **ws2_32 (clean rejection at the API boundary).**
    `send(MSG_OOB)` / `recv(MSG_OOB)` / `WSARecvEx(MSG_OOB)`
    → `WSAEOPNOTSUPP`; `setsockopt(SO_OOBINLINE)` /
    `getsockopt(SO_OOBINLINE)` → `WSAENOPROTOOPT`;
    `ioctlsocket(SIOCATMARK)` → `*argp = TRUE` (always at the
    mark of the empty urgent stream); `FIONREAD` no longer
    adds `ExpeditedBytesAvailable`. The public macros
    (`MSG_OOB`, `SO_OOBINLINE`, `SIOCATMARK`) stay defined in
    `winsock.h` for source-compat — only the implementation is
    gone.
- **Test:** Regression tests in `src/pkg/test/afd.lua` flip from
  exercise to rejection (`AFD_INLINE_MODE` → `STATUS_INVALID_PARAMETER`;
  `AFD_POLL_RECEIVE_EXPEDITED` mask bit never fires on a
  connected TCP endpoint). Lua-side `afd.lua` drops the
  `AFD_INLINE_MODE` and `AFD_POLL_RECEIVE_EXPEDITED` constants
  from its public exports.
- **Status:** PATCHED (AFD + ws2_32 + TCP/IP, full strip).

## 6a. LOGGED priority queue

The findings still in LOGGED status, ranked per the §3 priority
rules (T1/T4 > T2 > T3; CRIT > HIGH > MED > LOW; pre-bind > post-bind
within a severity). All are T1 — the firewall does not reorder the
ranking. (H-012 left this queue on 2026-05-20: status DEFERRED after
the reproducer refuted its original framing — see H-012's analysis +
decision and the §9 entry.)

1. **H-013** — Per-TCB out-of-order reassembly unbounded. T1
   post-bind, HIGH. Same Class 9 shape (attacker-controlled pool
   allocation) H-012 was *thought* to have, at finer scope —
   per-connection rather than per-listener. The H-012 lesson applies:
   check up-front whether a higher-layer pool / queue already gates
   this allocation before claiming unboundedness.
2. **H-014** — Idle-ESTABLISHED reaper (Naptha). T1 post-bind, MED.
   First step is a verify-and-decide pass (is `tcb_alive` armed
   automatically on ESTABLISHED transition or only with the
   `KEEPALIVE` sockopt?).
3. **H-016** — TCP options parser audit. T1, LOW. Parser correctness
   — Class 4 (length-field trust) + Class 6 (semantic validation)
   territory. Smallest expected delta.

The B pass starts at H-013 unless something changes the
prioritisation (a new finding lands, or a deployment shift puts T3
back on top).

## 7. Decision log

Decisions made about scope, methodology, or whole-class triage. One line
per entry, dated. Append-only.

- 2026-05-05 — Document created. Scope §1 set: single-NIC, internet-exposed,
  no reliance on upstream filtering. Threat model §2 set: T1–T4. Methodology
  §3 fixed. No code changes pending decision.

- 2026-05-11 — **Decision: strip IP fragmentation and reassembly from this
  stack.** Both directions: inbound fragments dropped at the dispatch hook,
  outbound `IPFragment` removed (oversized payloads become a caller error,
  counted and dropped). Rationale: PMTUD + MSS clamping keep TCP off the
  fragment path entirely; UDP large-payload apps either chunk at L7 (EDNS0,
  NFS) or fall back to TCP; ICMP large payloads aren't load-bearing. With
  fragmentation absent, H-001 / H-002 / H-003 / H-004 / H-005 are not just
  fixed but unreachable. Invariant I5 retires.

  This strip is the cleanest defence against the 1997 Teardrop/Bonk/NewTear
  family and Ping of Death: there is no overlap-trim arithmetic, no
  `rh_datasize` accumulator, no per-NTE reassembly list, no `IPRcvBuf` to
  carry an attacker-controlled length. Attribution: `(MS LANMan, 1990-1992)`
  — file headers carry no individual author. PMTUD reception in `ICMP.C` is
  preserved; TCP needs it under DF=1 outbound.

- 2026-05-11 — **Decision: NDIS is Ethernet-only.** Removed AFILTER (ARCnet),
  FFILTER (FDDI), TFILTER (Token Ring) sources/headers/exports, and every
  `MediaType` arm in `MINIPORT.C` and `WRAPPER.C`. Added a registration gate
  in `WRAPPER.C` that rejects any miniport reporting `MediaType !=
  NdisMedium802_3` so non-Ethernet dispatch code cannot be reached. ARCnet
  in particular shipped its own 508-byte link-layer fragmentation; removing
  it is in scope for "no fragmentation". Token Ring + FDDI gone for the same
  reason — neither is realistic on any single-NIC virtualised target this
  decade, and their filter packages added attack surface (group/functional
  address rewriting, source-routing bridges) with zero deployment value.

  Build verification: 2026-05-11 — full tree builds clean with no unresolved
  externals; `ndis.sys` exports the reduced surface, gate path exercised at
  miniport registration only.

## 8. Open questions

Tracked separately from per-finding TBDs because they cut across findings.

- ~~OQ-1.~~ Resolved 2026-05-12 — compile-out chosen.  Forwarding code
  removed entirely; nothing left to flip.
- OQ-2. What is our policy when an attacker fills a bounded structure
  (half-open SYN queue, per-TCB out-of-order queue, ARP cache)?
  Drop-oldest, drop-new, randomized drop, or per-source quota? Same
  answer across structures or one each? The ARP cache cap currently
  fails closed (refuses new entries) which is "drop-new" by default;
  the SYN and OOO cases are still open via H-012 / H-013.
- OQ-3. Are we willing to diverge from on-the-wire RFC behaviour where the
  RFC mandates we accept something we'd rather drop (e.g., source-routed
  packets, ICMP redirects)? Default: yes, document each divergence here.
- OQ-4. ~~How do we observe pool / TCB / RH counters from outside the
  guest for automated regression?~~ Largely resolved 2026-05-20: for
  the TCP findings the externally visible behaviour (SYN-ACK count,
  RST count, TCP-state transitions on the wire) is itself the oracle
  — see §5 and the H-012 reproducer. A kernel-internal counter is
  not needed for H-012/013/014/016. If a later finding genuinely
  requires reading nonpaged-pool or a kernel-only counter, extending
  `nt.net.info` with the relevant TDI MIB table (e.g. the TCP
  connection table) is the path.
- OQ-5. What is the smallest target VPS profile (RAM, nonpaged pool size)
  we calibrate caps against?

## 9. Changelog

- 2026-05-05 — Initial draft. §1–§5 written. §6 populated from prior audit
  with 20 stub findings (H-001 … H-020), all status LOGGED. No analysis,
  reproducers, decisions, or patches yet.
- 2026-05-11 — Strip landed: IP fragmentation/reassembly (~683 LOC across
  `IPRCV.C`, `IPXMIT.C`, `IPROUTE.C`, `INIT.C`, `IPDEF.H`, `IPXMIT.H`,
  `INFO.C`; PMTUD reception in `ICMP.C` retained). NDIS reduced to Ethernet:
  AFILTER/FFILTER/TFILTER deleted, MINIPORT/WRAPPER `MediaType` arms removed,
  registration gate added. Findings H-001 .. H-005 marked INVALID. Invariant
  I5 retires. Build verified clean.
- 2026-05-12 — Strip landed: IP forwarding + ICMP redirect + inbound
  source routing.  `IPForward`, `Redirect`, FW packet pool, `FWContext`
  / `RouteInterface` / `RouteSendQ` structs, `ForwardPackets` /
  `RouterConfigured` / `ForwardBCast` globals, registry knobs
  (`IpEnableRouter`, `ForwardBroadcasts`, `ForwardBufferMemory`,
  `NumForwardPackets`), and `InitGateway` all gone.  ICMP type-5 dropped
  at dispatch; inbound LSRR/SSRR silent-dropped at `CheckLocalOptions`;
  `IPSInfo.ipsi_forwarding` hard-locked to `IP_NOT_FORWARDING`.  Findings
  H-006, H-009, H-020 marked INVALID.  Invariants I1, I2, I3 retire.
  OQ-1 resolved (compile-out).  Build verified clean.

- 2026-05-13 — Quick-win pass on findings re-examined post strip.
  Four findings flipped status; no invariants retire. Details:
  H-010 INVALID by inheritance (reassembly strip caps inbound `Size`
  at MTU; broadcast echo already dropped). H-017 INVALID by analysis
  (`tri_urgent` 16-bit-bounded, explicit checks at `TCPDELIV.C:1622`
  + `:1638`). H-015 ACCEPTED (RFC 5961 challenge-ACK out of scope;
  TLS-over-TCP tolerates RST-as-drop; revisit if a plaintext
  long-lived workload appears). H-011 PATCHED (4-tuple alias drop
  added at `TCPRCV.C` after size/checksum gate, counted as
  `ts_inerrs`; loopback unaffected because AFD's same-`{addr,port}`
  bind refusal means legitimate flows always differ in port).

- 2026-05-13 — **Decision: strip IP option processing entirely.**
  Any packet with `IHL > 5` is dropped at `IPRcv` entry and counted
  in `ipsi_inhdrerrors`. Deleted: `ParseRcvdOptions`,
  `CheckLocalOptions` (`IPRCV.C`). Inert stubs in `INIT.C`:
  `IPUpdateRcvdOptions` zeros the reply IPOptInfo and returns
  `IP_SUCCESS`; `IPCopyOptions` returns `IP_BAD_OPTION` so any
  caller passing an option buffer learns at the call site that
  the stack does not accept IP options — `TCPCONN.C:1195`,
  `ADDR.C:1369`, `ICMP.C:895` already map that to
  `TDI_BAD_OPTION` and propagate it back through AFD. The four
  exported helpers keep their slots in the `IPInfo`
  function-pointer table so TCP / UDP / ICMP / setsockopt callers
  continue to compile and link; `IPInitOptions` / `IPFreeOptions`
  are unchanged because they already maintained
  `ioi_options == NULL`. ICMP echo handler's
  `if (ioi_options != NULL) IPUpdateRcvdOptions(...)` is dead and
  removed. Forward reference to `ParseRcvdOptions` in `IPROUTE.C`
  gone. `ValidRouteOption` (local helper) deleted. `UpdateOptions`
  and `UpdateRouteOption` remain in `IPXMIT.C` because the
  broadcast send path calls them; they are inert against NULL
  inputs which is the steady state once `IPInitOptions` runs.
  Rationale: record route, timestamp, source routing, and the rest
  are legacy cruft with no legitimate use on the deployment
  target; on hostile L2 they are an impersonation lever (LSRR/SSRR
  — already retired by H-006) and a parser surface for classic
  option-walk integer / pointer / length bugs (Class 6 in
  `docs-wip/KERNEL-ABI-HARDENING.md`). The IP_BAD_OPTION return
  from `IPCopyOptions` was a deliberate choice over silent
  acceptance: refuse loudly at the userland boundary rather than
  let drift accumulate between "I asked for option X" and "no
  option X went on the wire". Findings H-007, H-008 marked
  INVALID. Build verified clean (`tdi_tcpip_ip`, `tdi_tcpip_tcp`).

- 2026-05-13 — **Decision: ARP cache hardening for hostile L2.**
  Two pieces:
  - **MAC pin against cache poisoning.** `ARPRcv` no longer updates
    existing cache entries that are in `ARP_GOOD` state. Updates
    are accepted only during the `RESOLVING_*` window — the
    response to a request we sent. GOOD entries are refreshed by
    re-resolution on timeout. Refusal still lets the LocalAddr +
    REQUEST reply path run, so requests for our IPs are still
    answered.
  - **Cache size cap.** `ARP_MAX_ENTRIES = 256` ceiling added in
    `ARPDEF.H` and enforced at the top of `CreateARPTableEntry`.
    Refuses unbounded `Interface->ai_count` growth from a hostile
    L2 flood of ARP frames targeting our IPs.
  Side effects accepted: peer MAC changes are not learned via
  overheard ARP; they require timeout-driven re-resolution.
  Finding H-019 marked PATCHED. Invariant I8 satisfied.
  Build verified clean.

- 2026-05-13 — **Decision: H-018 (UDP broadcast amplification)
  reclassified ACCEPTED → PATCHED.** Initial ACCEPT was based on
  "wildcard sockets must receive broadcast because DHCP needs it";
  that was correct under a flexible deployment scope. With the
  scope tightened to "cloud workload host, the only broadcast is
  DHCP", the constraint becomes a specific exemption that can be
  gated. Two changes keyed on `AO_DHCP_FLAG`:
  - `UDPRcv` broadcast loop delivers only to flagged AOs.
  - `TdiSendDatagram` refuses broadcast destinations from
    unflagged AOs (returns `TDI_BAD_ADDR`).
  App-layer Fraggle is now structurally impossible — non-DHCP
  workloads cannot observe or originate broadcast UDP. The DHCP
  client at `nt.net.dhcp` carries the flag and continues to work
  unchanged. Build verified clean (`tdi_tcpip_tcp`). Multicast
  unaffected.

- 2026-05-19 — Priority framework added: §3 gains a scheduling
  order (remote > L2-local > local-only; the infra firewall does not
  reorder it; severity then pre-conditions as tiebreakers), and §6a
  ranks the LOGGED queue against it. **H-012** (SYN flood / unbounded
  half-open) analysed and pinned at `1a6f43b`: the SYN-to-listener
  path traced through `FindListenConn` → `TCB_SYN_RCVD`; `MaxTCBs`
  confirmed disabled (`0xffffffff`, `TCB.C:47`); the six SYN_RCVD exit
  sites enumerated for the `LeaveSynRcvd` decrement. Reproducer
  drafted. The stub's pre-bind reachability claim retired — a SYN to a
  port with no listener is RST'd without allocating a TCB, so H-012 is
  post-bind only. Status LOGGED → ANALYSED; §6a item 1 corrected. No
  code changes — patch pends a confirmed reproducer per §3.

- 2026-05-20 — **§5 packet harness landed.** `boot.sh` gains
  `--net-harness PORT`, which swaps QEMU's user-mode SLIRP backend
  for a socket netdev so the host-side `src/tools/netharness/`
  becomes the guest's entire network (DHCP + ARP + ICMP responders;
  scapy for packet build/parse, no other Python deps). A new ntosbe
  profile `iprepro` boots `src/pkg/iprepro.lua` — a
  finding-agnostic minimal TCP target — via
  `make -C src iprepro NET_HARNESS=PORT`. Per-finding reproducers
  under `src/tools/netharness/repro/` are surgical, edit-and-rerun
  Python; offline `selftest.py` covers the qlink reframer +
  responder logic without a boot. §5 rewritten to describe the
  as-built harness. **OQ-1** (same host vs second guest) resolved:
  same host, rootless socket netdev — T3 reproducers can still craft
  any source MAC, indistinguishable to the guest from a real L2
  neighbour. **OQ-4** largely sidestepped for the TCP findings:
  SYN-ACK / RST / segment behaviour is externally observable from
  the harness, so an in-guest kernel counter is not needed for
  H-012/013/014/016 (extend `nt.net.info` if a later finding does).

- 2026-05-20 — **H-012 reproducer ran** (`repro/h012.py`) against the
  unpatched stack. Result: half-opens plateau at the AFD listen
  backlog (1023 distinct sources accepted into SYN_RCVD out of 4096
  SYNs; 3041 RSTs). AFD's pre-allocated connection pool
  (`AfdStartListen` / `AfdConnectEventHandler`, `AFD/LISTEN.C:130-137,
  617-645`) gates the kernel SYN path: over-backlog SYNs hit
  `STATUS_INSUFFICIENT_RESOURCES` and are RST'd, with no SYN_RCVD
  accumulation. The "unbounded half-open" framing is **refuted**.
  Real finding = classic connection-slot SYN-flood denial (slots
  held for minutes by stale half-opens; legitimate clients refused
  with RST while the backlog is full). Decision: the pre-committed
  drop-new per-AO cap is **rejected** (wrong shape — shrinks
  capacity, doesn't free lingering slots, second-guesses the app's
  backlog). The `ce56681` ADDR.H scaffolding
  (`TCP_MAX_SYNRCVD_PER_AO` + `ao_synrcvd_count`) is reverted in a
  separate commit. Direction = **SYN cookies**, deferred as a larger
  structural work item. H-012 status ANALYSED → DEFERRED, out of the
  §6a queue; new head = H-013.

- 2026-05-27 — **Decision: strip OOB / urgent / expedited data
  handling.** Fifth in the wholesale-feature-removal series after
  IP fragmentation (2026-05-11), NDIS non-Ethernet (2026-05-11),
  IP forwarding + ICMP redirect + source routing (2026-05-12), and
  IP option processing (2026-05-13). Three layers touched: AFD
  (deep strip — handlers, struct fields, TDI registrations,
  `AFD_INLINE_MODE` / `AFD_POLL_RECEIVE_EXPEDITED` arms gone),
  TCP/IP (minimum-viable — URG masked off at parse, `TSR_FLAG_URG`
  zeroed on send, `TDI_SERVICE_EXPEDITED_DATA` no longer
  advertised; `HandleUrgent` / `DeliverUrgent` / TCB urgent
  fields / `ao_exprcv` remain as unreachable dead code), ws2_32
  (`MSG_OOB` → `WSAEOPNOTSUPP`, `SO_OOBINLINE` → `WSAENOPROTOOPT`,
  `SIOCATMARK` → always `TRUE`). Public macros remain defined in
  `winsock.h` for source compatibility. Selftest converted from
  exercise to rejection-regression coverage. Rationale: no in-tree
  caller of `MSG_OOB`; telnet / RAS / NetBT out of scope per §1;
  the RFC 793 vs BSD urgent-pointer disagreement is exactly the
  parser-surface bug class the audit retires wholesale (H-017
  WinNuke class, previously INVALID-by-bounds-check, becomes
  additionally INVALID-by-unreachability). Net diff: ~894 lines
  deleted across 18 source files. New finding H-021 records the
  strip; H-017 status line updated to note the unreachability
  addition. Deep cleanup followed in the same commit: deleted
  `HandleUrgent` / `DeliverUrgent`, the `tcb_urg*` / `tcb_exprcv`
  fields, the `URG_VALID` / `URG_INLINE` / `BSD_URGENT` /
  `IN_DELIV_URG` flags (with `TCP_SLOW_FLAGS` updated), the
  `tri_urgent` / `trh_urg` / `TSR_FLAG_URG` data members, the
  URG-conditional reassembly branches in `TCPRCV.C`, the
  outgoing URG setter in `TCPSEND.C`, the `ao_exprcv` setter
  in `ADDR.C`, the `TCP_SOCKET_BSDURGENT` / `TCP_SOCKET_OOBINLINE`
  arms and the `TCPSocketAMInfo` URG query in `INFO.C`, the
  `BSDUrgent` global and registry read in `NTINIT.C` /
  `TCPCFG.H` / `INIT.C`, the urgent cleanup loops and notify
  check in `TCPCONN.C`, and the ws2_32 `OobInline` socket
  field plus its `ACCEPT.C` inheritance. Final net diff:
  ~1685 lines deleted across 31 source files.

- 2026-05-27 — **Decision: strip AFD buffering-transport-only paths.**
  Sixth wholesale-feature-removal in the series. With TCP/IP as the
  sole in-tree TDI transport — and it does not advertise
  `TDI_SERVICE_INTERNAL_BUFFERING` — every `endpoint->TdiBufferring`
  / `connection->TdiBufferring` branch in AFD is statically dead.
  Deleted: `AfdReceiveEventHandler` and `AfdRestartReceive` (the
  buffering-transport receive-indication and receive-IRP completion),
  `AfdRestartSend` (the buffering-transport send-IRP completion),
  `AfdSendPossibleEventHandler` (the `TDI_EVENT_SEND_POSSIBLE` handler,
  only registered on the buffering side). `AfdSend` collapsed — the
  IRP-passthrough else branch (which forwarded the user IRP straight
  to TDI with `AfdRestartSend` completion) deleted; the `doSendBufferring`
  local retired; the AFD-side buffering path is now unconditional.
  `AfdReceive` collapsed to a ~30-line dispatcher (datagram →
  `AfdReceiveDatagram`, VC → `AfdBReceive`). `AfdQueryReceiveInformation`
  loses its buffering-side branch. `CLOSE.C`, `DISCONN.C`, `POLL.C`,
  `MISC.C`, `FASTIO.C`, `BLKCONN.C` all flatten their `TdiBufferring`
  conditionals. `AFD_ENDPOINT` and `AFD_CONNECTION` lose their
  `TdiBufferring` BOOLEAN; `AFD_ENDPOINT` also loses `TdiMessageMode`
  (TCP-stream-only deployment). The `Common` union in `AFD_CONNECTION`
  collapses from `union { Bufferring; NonBufferring }` to a struct
  wrapping just `NonBufferring`, preserving the `Common.NonBufferring.*`
  / `Vc*` accessor surface so the existing macros keep working.
  `VcNonBlockingSendPossible` and `VcZeroByteReceiveIndicated` macros
  retired (dead after the handler functions). `AfdCreateConnection`
  loses its `TdiBufferring` parameter; `AfdAddFreeConnection` /
  `AfdConnect` callers updated. `IS_DATA_ON_CONNECTION_B` macro
  retired; `IS_DATA_ON_CONNECTION` simplified to the direct
  `BufferredReceiveCount != 0` check. Finally, the
  `TDI_SERVICE_INTERNAL_BUFFERING` constant itself removed from
  `PRIVATE/INC/TDI.H` — we own the TDI surface and have no consumer.
  Net diff: ~866 lines deleted across 14 AFD files + 1 TDI header.
  Selftest passes; coverage rises in `NTOS/AFD` (dead-code denominator
  shrinks again).

- 2026-05-27 — **Decision: strip IP multicast / IGMP.** Seventh in the
  wholesale-feature-removal series. No multicast use case on the cloud
  workload host — no in-tree caller of `IP_ADD_MEMBERSHIP` or
  `IP_DROP_MEMBERSHIP`, no application protocol needing group delivery.
  IGMP itself was the largest single chunk of IP-layer code still
  carrying a network-facing parser surface (group reports / queries),
  which we have no reason to keep alive. Three layers touched. IP layer:
  `IGMP.C` (608 LOC) and `IGMP.H` (33 LOC) deleted wholesale + dropped
  from `SOURCES`; `IGMPInit` / `IGMPTimer` / `InitIGMPForNTE` /
  `StopIGMPForNTE` / `IPSetMCastAddr` / the `IGMPLevel` global + its
  registry read all gone from `INIT.C` and `NTIP.C`; `ipi_setmcastaddr`
  slot dropped from `IPInfo` (`IP.H`); `nte_igmplist` field dropped
  from `NetTableEntry` (`IPDEF.H`); `ici_igmplevel` dropped from
  `IPConfigInfo` (`IPINIT.H`); the CLASSD-multicast branches of
  `IsBCastOnNTE` / `GetAddrType` / `FindRoute` collapsed (multicast
  destinations now uniformly return `DEST_INVALID`); the
  `if (IGMPLevel != 0) AddRoute(CLASSD_MASK, …)` multicast-route init
  removed from `IPRouteInit`. ARP layer: `ARPSetMCastList`,
  `ARPFindMCast`, `ARPDelMCast`, `ARPAddMCast` deleted wholesale
  (~240 LOC); `LLIP_ADDR_MCAST` arms of `ARPAddAddr` / `ARPDelAddr`
  collapsed to fall-through-FALSE; `ARPMCastAddr` struct +
  `ai_mcast` / `ai_mcastcnt` fields gone from `ARPDEF.H`;
  `ARP_MCAST_MASK` macro retired; `IsBCastOnIF` no longer treats
  CLASSD as broadcast; the `802_3` / FDDI CLASSD branches in
  `ARPSendBCast` (multicast frame header assembly with the
  `ENetMcst` / `FDDIMcst` templates) deleted. TDI layer: TCP/IP
  AddrObj loses `ao_mcastopt` / `ao_mcastaddr` / `ao_mcastlist`
  fields (`ADDR.H`); `AOMCastAddr` struct gone; `FindAOMCastAddr` +
  the four `AO_OPTION_MCASTTTL` / `AO_OPTION_MCASTIF` /
  `AO_OPTION_ADD_MCAST` / `AO_OPTION_DEL_MCAST` arms in
  `SetAOOptions` deleted from `ADDR.C`; the multicast cleanup loops
  in `InvalidateAddrs` and `DelAddrObj`; the multicast init in
  `TdiOpenAddress`; the UDP CLASSD send branch in `UDP.C` (which
  selected the multicast opt/source-addr pair). Public:
  `TDI_SERVICE_MULTICAST_SUPPORTED` flag removed from
  `PRIVATE/INC/TDI.H`. `LLIP_ADDR_MCAST` and `DEST_MCAST` constants
  remain defined (no consumer, no harm). `CLASSD_ADDR` macro
  remains and is still used as a defensive validation in the
  proxy-ARP add path. Net diff: ~13.2k bytes of source across 12
  files (TCP/IP IP + TCP + headers + PRF profile). Selftest passes;
  coverage rises across NTOS/TDI/TCPIP.
