#!/usr/bin/env python3
"""
PiLab PLC UDP test client

This matches the firmware UDP server found in main/udp_server.c:
  - Server UDP port: 5005
  - Request: any UDP datagram up to 1500 bytes. If the first 4 bytes exist,
    firmware copies them as request_seq, little-endian uint32.
  - Response: fixed 1400-byte packed binary packet.

Response packet layout, little-endian:
  uint32 magic            0x50345544  // bytes: 44 55 34 50, string-ish "DU4P"
  uint16 version          1
  uint16 packet_size      1400
  uint32 request_seq      copied from request first 4 bytes, or 0 if request < 4 bytes
  uint32 response_seq     firmware-side incrementing counter
  uint32 rx_len           length of received UDP datagram
  uint32 flags            currently 0
  uint64 esp_rx_time_us   esp_timer_get_time() when packet was received
  uint64 esp_tx_time_us   esp_timer_get_time() just before sendto()
  uint8  payload[1360]    pattern bytes 0..255 repeating
"""

from __future__ import annotations

import argparse
import csv
import socket
import struct
import sys
import time
from dataclasses import dataclass
from typing import Optional

UDP_PORT = 5005
MAX_SERVER_RX = 1500
RESPONSE_SIZE = 1400
RESPONSE_HEADER_FMT = "<IHHIIIIQQ"  # packed little-endian, 40 bytes
RESPONSE_HEADER_SIZE = struct.calcsize(RESPONSE_HEADER_FMT)
EXPECTED_MAGIC = 0x50345544
EXPECTED_VERSION = 1


def suppress_windows_udp_connreset(sock: socket.socket) -> None:
    """
    On Windows, UDP recvfrom() can raise WinError 10054 when an ICMP
    Port Unreachable is received. That is useful information, but it should
    not crash a long-running load test. Disable that behavior when Python and
    Windows expose SIO_UDP_CONNRESET.
    """
    sio_udp_connreset = getattr(socket, "SIO_UDP_CONNRESET", None)
    if sio_udp_connreset is None:
        return
    try:
        sock.ioctl(sio_udp_connreset, False)
    except OSError:
        # Non-fatal. We still catch ConnectionResetError in the receive loop.
        pass


@dataclass
class UdpResponse:
    magic: int
    version: int
    packet_size: int
    request_seq: int
    response_seq: int
    rx_len: int
    flags: int
    esp_rx_time_us: int
    esp_tx_time_us: int
    payload: bytes

    @property
    def esp_turnaround_us(self) -> int:
        return int(self.esp_tx_time_us - self.esp_rx_time_us)


def build_payload(seq: int, size: int) -> bytes:
    """Build a request payload. First 4 bytes are little-endian request sequence."""
    if size < 0:
        raise ValueError("payload size must be >= 0")
    if size > MAX_SERVER_RX:
        raise ValueError(f"payload size {size} exceeds firmware recv buffer size {MAX_SERVER_RX}")
    if size == 0:
        return b""
    if size < 4:
        # Firmware will not read request_seq unless len >= 4.
        return bytes((seq + i) & 0xFF for i in range(size))
    body = bytearray(size)
    struct.pack_into("<I", body, 0, seq & 0xFFFFFFFF)
    for i in range(4, size):
        body[i] = (seq + i) & 0xFF
    return bytes(body)


def parse_response(data: bytes) -> UdpResponse:
    if len(data) < RESPONSE_HEADER_SIZE:
        raise ValueError(f"short response: {len(data)} bytes, expected at least {RESPONSE_HEADER_SIZE}")

    fields = struct.unpack_from(RESPONSE_HEADER_FMT, data, 0)
    resp = UdpResponse(
        magic=fields[0],
        version=fields[1],
        packet_size=fields[2],
        request_seq=fields[3],
        response_seq=fields[4],
        rx_len=fields[5],
        flags=fields[6],
        esp_rx_time_us=fields[7],
        esp_tx_time_us=fields[8],
        payload=data[RESPONSE_HEADER_SIZE:],
    )

    if resp.magic != EXPECTED_MAGIC:
        raise ValueError(f"bad magic 0x{resp.magic:08X}, expected 0x{EXPECTED_MAGIC:08X}")
    if resp.version != EXPECTED_VERSION:
        raise ValueError(f"unexpected version {resp.version}, expected {EXPECTED_VERSION}")
    if resp.packet_size != RESPONSE_SIZE:
        raise ValueError(f"unexpected packet_size {resp.packet_size}, expected {RESPONSE_SIZE}")
    if len(data) != resp.packet_size:
        raise ValueError(f"response length {len(data)} does not match packet_size {resp.packet_size}")

    return resp


def percentile(values: list[float], pct: float) -> Optional[float]:
    if not values:
        return None
    ordered = sorted(values)
    k = (len(ordered) - 1) * pct / 100.0
    lo = int(k)
    hi = min(lo + 1, len(ordered) - 1)
    if lo == hi:
        return ordered[lo]
    frac = k - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


def main() -> int:
    parser = argparse.ArgumentParser(description="Send UDP test packets to a PiLab PLC firmware UDP server.")
    parser.add_argument("host", help="PiLab PLC IP address, e.g. 192.168.1.50")
    parser.add_argument("--port", type=int, default=UDP_PORT, help="UDP port, default 5005")
    parser.add_argument("--count", type=int, default=100, help="number of packets to send")
    parser.add_argument("--rate", type=float, default=20.0, help="send rate in packets/sec. Use 0 for no delay")
    parser.add_argument("--size", type=int, default=4, help="request payload size in bytes, 0..1500. Use >=4 for sequence echo")
    parser.add_argument("--timeout", type=float, default=1.0, help="receive timeout in seconds")
    parser.add_argument("--csv", default=None, help="optional CSV output path")
    parser.add_argument("--quiet", action="store_true", help="only print summary")
    parser.add_argument("--stop-on-reset", action="store_true", help="stop immediately if Windows reports ICMP port unreachable / WinError 10054")
    args = parser.parse_args()

    if args.count <= 0:
        print("count must be > 0", file=sys.stderr)
        return 2
    if args.rate < 0:
        print("rate must be >= 0", file=sys.stderr)
        return 2
    if not (0 <= args.size <= MAX_SERVER_RX):
        print(f"size must be between 0 and {MAX_SERVER_RX}", file=sys.stderr)
        return 2

    period_s = 0.0 if args.rate == 0 else 1.0 / args.rate
    rows: list[dict[str, object]] = []
    rtts_us: list[float] = []
    esp_turnarounds_us: list[int] = []
    lost = 0
    bad = 0
    resets = 0

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        suppress_windows_udp_connreset(sock)
        sock.settimeout(args.timeout)
        target = (args.host, args.port)

        next_send_time = time.perf_counter()
        for seq in range(1, args.count + 1):
            if period_s > 0:
                now = time.perf_counter()
                if now < next_send_time:
                    time.sleep(next_send_time - now)
                next_send_time += period_s

            payload = build_payload(seq, args.size)
            t0_ns = time.perf_counter_ns()
            sock.sendto(payload, target)

            try:
                data, addr = sock.recvfrom(4096)
                t1_ns = time.perf_counter_ns()
            except socket.timeout:
                lost += 1
                row = {
                    "seq": seq,
                    "ok": False,
                    "error": "timeout",
                    "rtt_us": "",
                    "request_seq": "",
                    "response_seq": "",
                    "rx_len": "",
                    "esp_turnaround_us": "",
                    "from": "",
                }
                rows.append(row)
                if not args.quiet:
                    print(f"seq={seq} TIMEOUT")
                continue
            except ConnectionResetError as exc:
                # Common on Windows UDP when the peer sends ICMP Port Unreachable.
                # Usually means the IP is reachable but nothing is listening on that UDP port,
                # or the firmware/network stack rejected the datagram.
                resets += 1
                lost += 1
                row = {
                    "seq": seq,
                    "ok": False,
                    "error": f"connection_reset: {exc}",
                    "rtt_us": "",
                    "request_seq": "",
                    "response_seq": "",
                    "rx_len": "",
                    "esp_turnaround_us": "",
                    "from": "",
                }
                rows.append(row)
                if not args.quiet:
                    print(f"seq={seq} CONNECTION_RESET / ICMP_PORT_UNREACHABLE: {exc}")
                if args.stop_on_reset:
                    break
                continue

            rtt_us = (t1_ns - t0_ns) / 1000.0
            try:
                resp = parse_response(data)
                expected_request_seq = seq if args.size >= 4 else 0
                seq_ok = resp.request_seq == expected_request_seq
                rx_len_ok = resp.rx_len == args.size
                ok = seq_ok and rx_len_ok
                if not ok:
                    bad += 1

                rtts_us.append(rtt_us)
                esp_turnarounds_us.append(resp.esp_turnaround_us)

                row = {
                    "seq": seq,
                    "ok": ok,
                    "error": "" if ok else "mismatch",
                    "rtt_us": f"{rtt_us:.1f}",
                    "request_seq": resp.request_seq,
                    "response_seq": resp.response_seq,
                    "rx_len": resp.rx_len,
                    "esp_turnaround_us": resp.esp_turnaround_us,
                    "from": f"{addr[0]}:{addr[1]}",
                }
                rows.append(row)

                if not args.quiet:
                    status = "OK" if ok else "MISMATCH"
                    print(
                        f"seq={seq} {status} rtt={rtt_us:8.1f} us "
                        f"esp={resp.esp_turnaround_us:4d} us "
                        f"req_seq={resp.request_seq} resp_seq={resp.response_seq} rx_len={resp.rx_len}"
                    )
            except Exception as exc:
                bad += 1
                row = {
                    "seq": seq,
                    "ok": False,
                    "error": str(exc),
                    "rtt_us": f"{rtt_us:.1f}",
                    "request_seq": "",
                    "response_seq": "",
                    "rx_len": "",
                    "esp_turnaround_us": "",
                    "from": f"{addr[0]}:{addr[1]}",
                }
                rows.append(row)
                if not args.quiet:
                    print(f"seq={seq} BAD_RESPONSE rtt={rtt_us:.1f} us error={exc}")

    if args.csv:
        with open(args.csv, "w", newline="") as f:
            writer = csv.DictWriter(
                f,
                fieldnames=[
                    "seq",
                    "ok",
                    "error",
                    "rtt_us",
                    "request_seq",
                    "response_seq",
                    "rx_len",
                    "esp_turnaround_us",
                    "from",
                ],
            )
            writer.writeheader()
            writer.writerows(rows)
        print(f"CSV written to {args.csv}")

    received = len(rtts_us)
    print("\nSummary")
    print(f"  target              {args.host}:{args.port}")
    print(f"  request_size        {args.size} bytes")
    print(f"  response_size       {RESPONSE_SIZE} bytes")
    print(f"  sent                {args.count}")
    print(f"  received_valid-ish  {received}")
    print(f"  timeouts/resets     {lost}")
    print(f"  connection_resets   {resets}")
    print(f"  bad/mismatched      {bad}")

    if rtts_us:
        print(f"  RTT us avg/min/max   {sum(rtts_us)/len(rtts_us):.1f} / {min(rtts_us):.1f} / {max(rtts_us):.1f}")
        print(f"  RTT us p50/p95/p99   {percentile(rtts_us, 50):.1f} / {percentile(rtts_us, 95):.1f} / {percentile(rtts_us, 99):.1f}")
    if esp_turnarounds_us:
        print(
            f"  ESP turnaround us   avg/min/max "
            f"{sum(esp_turnarounds_us)/len(esp_turnarounds_us):.1f} / "
            f"{min(esp_turnarounds_us)} / {max(esp_turnarounds_us)}"
        )

    if resets and not rtts_us:
        print("\nNo valid UDP responses were received.")
        print("On Windows, connection_reset usually means ICMP Port Unreachable.")
        print("Check that the PLC is reachable at that IP, that firmware started udp_server_start(),")
        print("and that the serial log shows: UDP binary server listening on port 5005")

    return 0 if lost == 0 and bad == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
