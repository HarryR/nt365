#!/usr/bin/env python3
# Host side of the MicroNT connect-back agent (src/pkg/main.lua).
#
# Hacky dev tool, meant to be adapted as we go. The guest agent dials us
# (slirp gateway 10.0.2.2 -> host loopback) and waits for a u32-LE length
# + that many bytes of Lua source. We hand it a chunk; the chunk runs on
# the guest and may stream output back over the same socket, which we print.
#
#   python3 tools/agenthost.py                # send a default "hello" probe
#   python3 tools/agenthost.py chunk.lua      # send chunk.lua to the guest
#
# One-shot by default: serve a single connection then exit (clean for
# verifying connectivity). To iterate, wrap the accept/handle in a loop.

import socket, struct, sys

PORT = 4444
DEFAULT_CHUNK = b'print("AGENT-CHUNK: hello from the host")'


def main():
    if len(sys.argv) > 1:
        with open(sys.argv[1], "rb") as f:
            chunk = f.read()
    else:
        chunk = DEFAULT_CHUNK

    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", PORT))
    srv.listen(1)
    print(f"[host] listening :{PORT} — run `make boot` (guest dials 10.0.2.2:{PORT})")

    c, a = srv.accept()
    print(f"[host] guest connected {a} — sending {len(chunk)}B Lua chunk")
    c.sendall(struct.pack("<I", len(chunk)) + chunk)

    # Print the agent's banner + anything the chunk streams back, until
    # the guest closes the connection.
    while True:
        data = c.recv(4096)
        if not data:
            break
        sys.stdout.buffer.write(data)
        sys.stdout.flush()

    print("\n[host] guest closed — done")
    c.close()
    srv.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
