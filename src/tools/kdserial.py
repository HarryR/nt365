#!/usr/bin/env python3
"""
kdserial.py - Minimal NT 3.5 kernel debugger serial client

Connects to QEMU's serial port, decodes KD packets, sends ACKs,
and prints DbgPrint output as plain text.

Usage:
  kdserial.py [--tcp HOST:PORT] [--file RAWFILE]

  --tcp   Connect to QEMU serial over TCP (default: localhost:4321)
  --file  Decode packets from a raw capture file (offline, no ACKs)
"""

import struct
import sys
import socket
import argparse
import select

# --- KD Protocol Constants ---

PACKET_LEADER              = 0x30303030
PACKET_LEADER_BYTE         = 0x30
CONTROL_PACKET_LEADER      = 0x69696969
CONTROL_PACKET_LEADER_BYTE = 0x69
BREAKIN_PACKET_BYTE        = 0x62
PACKET_TRAILING_BYTE       = 0xAA

PACKET_TYPE_UNUSED              = 0
PACKET_TYPE_KD_STATE_CHANGE     = 1
PACKET_TYPE_KD_STATE_MANIPULATE = 2
PACKET_TYPE_KD_DEBUG_IO         = 3
PACKET_TYPE_KD_ACKNOWLEDGE      = 4
PACKET_TYPE_KD_RESEND           = 5
PACKET_TYPE_KD_RESET            = 6

PACKET_TYPE_NAMES = {
    0: "UNUSED",
    1: "STATE_CHANGE",
    2: "STATE_MANIPULATE",
    3: "DEBUG_IO",
    4: "ACK",
    5: "RESEND",
    6: "RESET",
}

INITIAL_PACKET_ID = 0x80800000
SYNC_PACKET_ID    = 0x00000800

# DBGKD_DEBUG_IO ApiNumbers
DbgKdPrintStringApi = 0x00003230
DbgKdGetStringApi   = 0x00003231

# State change types
DbgKdExceptionStateChange = 0x00003030
DbgKdLoadSymbolsStateChange = 0x00003031


def compute_checksum(data: bytes) -> int:
    s = 0
    for b in data:
        s += b
    return s & 0xFFFFFFFF


def make_ack_packet(packet_id: int) -> bytes:
    """Build a KD ACK (control) packet.
    The kernel checks: ACK.PacketId == (KdpNextPacketIdToSend & ~SYNC_PACKET_ID)
    So we must strip the SYNC bit from the echoed ID."""
    return struct.pack('<IHHII',
        CONTROL_PACKET_LEADER,  # PacketLeader
        PACKET_TYPE_KD_ACKNOWLEDGE,  # PacketType
        0,                      # ByteCount
        packet_id & ~SYNC_PACKET_ID,  # PacketId (strip SYNC bit)
        0,                      # Checksum
    )


def make_continue_packet(packet_id: int) -> bytes:
    """Build a 'continue' response to a STATE_CHANGE (exception/load symbols).
    This tells the kernel to keep running instead of waiting for debugger commands."""
    # DBGKD_MANIPULATE_STATE for DbgKdContinueApi
    DbgKdContinueApi = 0x00003136
    # The manipulate state header: ApiNumber(4) + ProcessorType(2) + Processor(2) + ReturnStatus(4) = 12 bytes
    # Then DbgKdContinue: ContinueStatus(4) = STATUS_SUCCESS = 0
    header = struct.pack('<IHHI',
        DbgKdContinueApi,  # ApiNumber
        0x014C,             # ProcessorType (i386)
        0,                  # Processor
        0,                  # ReturnStatus (STATUS_SUCCESS)
    )
    # ContinueStatus = STATUS_SUCCESS
    data = struct.pack('<I', 0)  # STATUS_SUCCESS

    payload = header + data
    checksum = compute_checksum(payload)

    pkt = struct.pack('<IHHII',
        PACKET_LEADER,
        PACKET_TYPE_KD_STATE_MANIPULATE,
        len(payload),
        packet_id,
        checksum,
    )
    pkt += payload
    pkt += bytes([PACKET_TRAILING_BYTE])
    return pkt


class KdReader:
    """Reads and decodes KD packets from a byte stream."""

    def __init__(self, verbose=False):
        self.buf = bytearray()
        self.verbose = verbose
        self.next_id_to_ack = INITIAL_PACKET_ID
        self.next_id_to_send = INITIAL_PACKET_ID | SYNC_PACKET_ID

    def feed(self, data: bytes):
        self.buf.extend(data)

    def try_parse(self):
        """Try to parse one packet from the buffer.
        Returns (packet_info, ack_bytes) or (None, None) if not enough data."""
        # Scan for packet leader
        while len(self.buf) >= 4:
            b = self.buf[0]
            if b == PACKET_LEADER_BYTE or b == CONTROL_PACKET_LEADER_BYTE:
                # Check for 4 consecutive leader bytes
                if len(self.buf) < 4:
                    return None, None
                if self.buf[0] == self.buf[1] == self.buf[2] == self.buf[3] == b:
                    break
                else:
                    # Not 4 consecutive, skip one byte
                    self.buf.pop(0)
                    continue
            elif b == BREAKIN_PACKET_BYTE:
                self.buf.pop(0)
                if self.verbose:
                    sys.stderr.write("[KD: breakin byte]\n")
                continue
            else:
                self.buf.pop(0)
                continue

        # Need at least 16 bytes for header
        if len(self.buf) < 16:
            return None, None

        leader = struct.unpack_from('<I', self.buf, 0)[0]
        pkt_type = struct.unpack_from('<H', self.buf, 4)[0]
        byte_count = struct.unpack_from('<H', self.buf, 6)[0]
        pkt_id = struct.unpack_from('<I', self.buf, 8)[0]
        checksum = struct.unpack_from('<I', self.buf, 12)[0]

        # Validate header — reject false positives from ASCII '0000' in text
        if pkt_type > 7 or byte_count > 4096:
            # Not a real packet, skip the leader bytes
            del self.buf[:4]
            return self.try_parse()

        # Need header + payload + trailing byte
        total_needed = 16 + byte_count + 1
        if len(self.buf) < total_needed:
            return None, None

        payload = bytes(self.buf[16:16 + byte_count])
        trailing = self.buf[16 + byte_count]

        # Verify trailing byte — if wrong, this was a false match
        if trailing != PACKET_TRAILING_BYTE:
            del self.buf[:4]
            return self.try_parse()

        # Consume the packet from buffer
        del self.buf[:total_needed]

        is_control = (leader == CONTROL_PACKET_LEADER)
        type_name = PACKET_TYPE_NAMES.get(pkt_type, f"UNKNOWN({pkt_type})")

        # Verify checksum
        actual_cksum = compute_checksum(payload)
        cksum_ok = (actual_cksum == checksum) or byte_count == 0

        info = {
            'leader': 'CONTROL' if is_control else 'DATA',
            'type': pkt_type,
            'type_name': type_name,
            'byte_count': byte_count,
            'packet_id': pkt_id,
            'checksum_ok': cksum_ok,
            'payload': payload,
        }

        # Build response
        response = b''
        if not is_control and pkt_type != PACKET_TYPE_KD_ACKNOWLEDGE:
            # Send ACK for data packets
            ack = make_ack_packet(pkt_id)
            if self.verbose:
                sys.stderr.write(f"  -> ACK {ack.hex()}\n")
            response = ack

        # Decode payload
        self._decode_payload(info)

        return info, response

    def _decode_payload(self, info):
        """Decode known payload types."""
        payload = info['payload']

        if info['type'] == PACKET_TYPE_KD_DEBUG_IO and len(payload) >= 12:
            api_num = struct.unpack_from('<I', payload, 0)[0]
            proc_type = struct.unpack_from('<H', payload, 4)[0]
            processor = struct.unpack_from('<H', payload, 6)[0]

            if api_num == DbgKdPrintStringApi:
                str_len = struct.unpack_from('<I', payload, 8)[0]
                text = payload[12:12 + str_len]
                try:
                    info['print_string'] = text.decode('ascii', errors='replace')
                except:
                    info['print_string'] = repr(text)

            elif api_num == DbgKdGetStringApi:
                info['get_string'] = True

        elif info['type'] == PACKET_TYPE_KD_STATE_CHANGE and len(payload) >= 12:
            api_num = struct.unpack_from('<I', payload, 0)[0]
            proc_type = struct.unpack_from('<H', payload, 4)[0]
            processor = struct.unpack_from('<H', payload, 6)[0]

            if api_num == DbgKdExceptionStateChange and len(payload) >= 48:
                # Exception record starts at offset 32 in DBGKD_WAIT_STATE_CHANGE
                # But the structure is complex - just extract key fields
                # ProgramCounter at offset 8
                pc = struct.unpack_from('<I', payload, 8)[0]
                info['state_change'] = 'EXCEPTION'
                info['program_counter'] = pc
                # Exception code at offset 32 (start of EXCEPTION_RECORD)
                if len(payload) >= 36:
                    exc_code = struct.unpack_from('<I', payload, 32)[0]
                    info['exception_code'] = exc_code
                if len(payload) >= 44:
                    exc_addr = struct.unpack_from('<I', payload, 44)[0]
                    info['exception_address'] = exc_addr

            elif api_num == DbgKdLoadSymbolsStateChange:
                info['state_change'] = 'LOAD_SYMBOLS'


def format_packet(info: dict, verbose=False) -> str:
    """Format a packet for display."""
    parts = []

    if 'print_string' in info:
        # For print strings, just show the text
        return info['print_string']

    type_name = info['type_name']
    leader = info['leader']

    if verbose or info['type'] not in (PACKET_TYPE_KD_ACKNOWLEDGE, PACKET_TYPE_KD_RESEND):
        parts.append(f"[KD {leader} {type_name} id=0x{info['packet_id']:08X} len={info['byte_count']}")

        if not info['checksum_ok']:
            parts.append(" BAD_CHECKSUM")

        if 'state_change' in info:
            parts.append(f" {info['state_change']}")
            if 'program_counter' in info:
                parts.append(f" PC=0x{info['program_counter']:08X}")
            if 'exception_code' in info:
                parts.append(f" code=0x{info['exception_code']:08X}")
            if 'exception_address' in info:
                parts.append(f" addr=0x{info['exception_address']:08X}")

        if 'get_string' in info:
            parts.append(" GET_STRING")

        parts.append("]\n")

    return ''.join(parts)


def run_tcp(host: str, port: int, verbose: bool, listen: bool = False):
    """Connect to or listen for QEMU serial over TCP and act as a KD client."""
    if listen:
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((host, port))
        srv.listen(1)
        print(f"Listening on {host}:{port} — start QEMU now...", file=sys.stderr)
        sock, addr = srv.accept()
        print(f"QEMU connected from {addr}. Decoding KD packets...", file=sys.stderr)
        srv.close()
    else:
        print(f"Connecting to {host}:{port}...", file=sys.stderr)
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((host, port))
        print(f"Connected. Waiting for KD packets...", file=sys.stderr)
    sock.setblocking(False)

    reader = KdReader(verbose=verbose)

    try:
        while True:
            readable, _, _ = select.select([sock], [], [], 1.0)
            if readable:
                data = sock.recv(4096)
                if not data:
                    print("\n[Connection closed]", file=sys.stderr)
                    break
                reader.feed(data)

                while True:
                    info, response = reader.try_parse()
                    if info is None:
                        break

                    text = format_packet(info, verbose)
                    if text:
                        sys.stdout.write(text)
                        sys.stdout.flush()

                    if response:
                        try:
                            sock.sendall(response)
                        except:
                            pass
    except KeyboardInterrupt:
        print("\n[Interrupted]", file=sys.stderr)
    finally:
        sock.close()


def run_file(path: str, verbose: bool):
    """Decode packets from a raw capture file."""
    with open(path, 'rb') as f:
        data = f.read()

    print(f"Read {len(data)} bytes from {path}", file=sys.stderr)
    reader = KdReader(verbose=verbose)
    reader.feed(data)

    while True:
        info, _ = reader.try_parse()
        if info is None:
            break

        text = format_packet(info, verbose)
        if text:
            sys.stdout.write(text)
            sys.stdout.flush()

    remaining = len(reader.buf)
    if remaining > 0:
        print(f"\n[{remaining} trailing bytes unparsed]", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description='NT 3.5 KD serial packet decoder')
    parser.add_argument('--tcp', metavar='HOST:PORT', default=None,
                        help='Connect to QEMU serial over TCP (e.g., localhost:4321)')
    parser.add_argument('--file', metavar='FILE', default=None,
                        help='Decode packets from a raw capture file')
    parser.add_argument('-l', '--listen', action='store_true',
                        help='Listen for QEMU connection (start before QEMU)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Show all packets including ACK/control')
    args = parser.parse_args()

    if args.file:
        run_file(args.file, args.verbose)
    else:
        addr = args.tcp or 'localhost:4321'
        if ':' in addr:
            host, port = addr.rsplit(':', 1)
            port = int(port)
        else:
            host, port = addr, 4321
        run_tcp(host, port, args.verbose, listen=args.listen)


if __name__ == '__main__':
    main()
