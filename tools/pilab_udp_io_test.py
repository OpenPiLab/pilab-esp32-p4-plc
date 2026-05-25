#!/usr/bin/env python3
"""
PiLab UDP remote-I/O/tag simulation tester.

Default mode sends the v2 P4IO tag packet:
  - 128 values total
  - 32 UDP_DI values as 4-byte bool-ish uint32 values
  - 32 UDP_DO values as 4-byte bool-ish uint32 values
  - 32 UDP_AI values as float32 bit patterns
  - 32 UDP_AO values as float32 bit patterns
  - 512 bytes of value payload total

The firmware validates the packet, publishes it to a double buffer, and the
PLC 1 ms path writes the values into cached UDP_* tags without name lookup.
"""

from __future__ import annotations

import argparse
import csv
import socket
import struct
import time
from pathlib import Path

UDP_PORT = 5005
P4IO_MAGIC = 0x4F493450  # "P4IO" little endian
P4IO_VERSION_LEGACY = 1
P4IO_VERSION_TAGS = 2
P4IO_TAG_VALUE_COUNT = 128
P4IO_TAG_PACKET_SIZE = 544
UDP4_MAGIC = 0x50345544

FLAG_CHECKSUM_BAD = 1 << 0
FLAG_BAD_MAGIC = 1 << 1
FLAG_BAD_VERSION = 1 << 2
FLAG_BAD_SIZE = 1 << 3
FLAG_ACCEPTED = 1 << 8
FLAG_LEGACY_ECHO = 1 << 9
REQ_FLAG_RESET_STATS = 1 << 0

# v1 process-image packet, still available with --legacy-packet
REQ_V1_STRUCT = struct.Struct("<IHHIIQIIffffII8s")
# v2 tag packet: header = 32 bytes, values = 128 * 4 = 512 bytes
REQ_TAG_HDR = struct.Struct("<IHHIIQII")
RESP_HDR = struct.Struct("<IHHIIIIQQ")
# PlcUdpIoStats: 21 uint32 + 4 float
STATS_STRUCT = struct.Struct("<" + "I" * 21 + "ffff")

assert REQ_V1_STRUCT.size == 64
assert REQ_TAG_HDR.size == 32
assert REQ_TAG_HDR.size + (P4IO_TAG_VALUE_COUNT * 4) == P4IO_TAG_PACKET_SIZE
assert RESP_HDR.size == 40


def fnv1a32(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def f32_to_u32(v: float) -> int:
    return struct.unpack("<I", struct.pack("<f", float(v)))[0]


def make_tag_values(seq: int) -> list[int]:
    values = [0] * P4IO_TAG_VALUE_COUNT

    # UDP_DI0..31: moving pattern and sequence-derived bits.
    for i in range(32):
        values[i] = 1 if ((seq >> (i % 8)) & 1) else 0

    # UDP_DO0..31: a different toggling/status pattern.
    for i in range(32):
        values[32 + i] = 1 if (((seq + i) // 8) & 1) else 0

    # UDP_AI0..31: float32 ramp/sine-ish deterministic values.
    for i in range(32):
        values[64 + i] = f32_to_u32((seq * 0.01) + i)

    # UDP_AO0..31: float32 status/setpoint-style deterministic values.
    for i in range(32):
        values[96 + i] = f32_to_u32(((seq % 1000) / 1000.0) + (i * 0.1))

    return values


def make_tag_request(seq: int, flags: int = 0) -> tuple[bytes, int]:
    now_us = time.perf_counter_ns() // 1000
    values = make_tag_values(seq)
    values_blob = struct.pack("<" + "I" * P4IO_TAG_VALUE_COUNT, *values)

    hdr_zero_crc = REQ_TAG_HDR.pack(
        P4IO_MAGIC,
        P4IO_VERSION_TAGS,
        P4IO_TAG_PACKET_SIZE,
        seq,
        flags,
        now_us,
        P4IO_TAG_VALUE_COUNT,
        0,
    )
    pkt_zero_crc = hdr_zero_crc + values_blob
    checksum = fnv1a32(pkt_zero_crc)

    hdr = REQ_TAG_HDR.pack(
        P4IO_MAGIC,
        P4IO_VERSION_TAGS,
        P4IO_TAG_PACKET_SIZE,
        seq,
        flags,
        now_us,
        P4IO_TAG_VALUE_COUNT,
        checksum,
    )
    # Return first 8 DI bits as a convenient log mask.
    di_mask = 0
    for i in range(8):
        if values[i]:
            di_mask |= 1 << i
    return hdr + values_blob, di_mask


def make_legacy_request(seq: int, di_mask: int, ai_base: float, flags: int = 0) -> bytes:
    now_us = time.perf_counter_ns() // 1000
    ai0 = float(ai_base)
    ai1 = float(seq & 0xFFFF)
    ai2 = float(di_mask & 0xFF)
    ai3 = float((seq % 1000) / 1000.0)
    payload_counter = seq
    reserved = b"\x00" * 8

    pkt_zero_crc = REQ_V1_STRUCT.pack(
        P4IO_MAGIC, P4IO_VERSION_LEGACY, REQ_V1_STRUCT.size, seq, flags, now_us,
        di_mask & 0xFFFFFFFF, 0, ai0, ai1, ai2, ai3, payload_counter, 0, reserved,
    )
    checksum = fnv1a32(pkt_zero_crc)
    return REQ_V1_STRUCT.pack(
        P4IO_MAGIC, P4IO_VERSION_LEGACY, REQ_V1_STRUCT.size, seq, flags, now_us,
        di_mask & 0xFFFFFFFF, 0, ai0, ai1, ai2, ai3, payload_counter, checksum, reserved,
    )


def parse_response(data: bytes):
    if len(data) < RESP_HDR.size:
        raise ValueError(f"short response: {len(data)} bytes")
    magic, version, packet_size, req_seq, resp_seq, rx_len, flags, esp_rx_us, esp_tx_us = RESP_HDR.unpack_from(data, 0)
    stats = None
    if len(data) >= RESP_HDR.size + STATS_STRUCT.size:
        values = STATS_STRUCT.unpack_from(data, RESP_HDR.size)
        stats = {
            "rx_publish_count": values[0],
            "consume_count": values[1],
            "missed_update_count": values[2],
            "active": values[3],
            "published_seq": values[4],
            "consumed_seq": values[5],
            "last_di_mask": values[6],
            "last_age_us": values[7],
            "max_age_us": values[8],
            "stale_count": values[9],
            "tag_publish_count": values[10],
            "tag_consume_count": values[11],
            "tag_missed_update_count": values[12],
            "tag_published_seq": values[13],
            "tag_consumed_seq": values[14],
            "tag_values_written": values[15],
            "tag_write_last_us": values[16],
            "tag_write_max_us": values[17],
            "tag_cache_ready": values[18],
            "tag_last_age_us": values[19],
            "tag_max_age_us": values[20],
            "last_ai0": values[21],
            "last_ai1": values[22],
            "last_ai2": values[23],
            "last_ai3": values[24],
        }
    return {
        "magic": magic,
        "version": version,
        "packet_size": packet_size,
        "request_seq": req_seq,
        "response_seq": resp_seq,
        "rx_len": rx_len,
        "flags": flags,
        "esp_rx_us": esp_rx_us,
        "esp_tx_us": esp_tx_us,
        "stats": stats,
    }


def pct(sorted_values, p: float) -> float:
    if not sorted_values:
        return 0.0
    k = int(round((len(sorted_values) - 1) * p))
    return sorted_values[max(0, min(k, len(sorted_values) - 1))]


def main() -> int:
    ap = argparse.ArgumentParser(description="Send simulated PiLab UDP I/O/tag packets")
    ap.add_argument("host", help="PiLab PLC IP address")
    ap.add_argument("--port", type=int, default=UDP_PORT)
    ap.add_argument("--count", type=int, default=1000)
    ap.add_argument("--rate", type=float, default=500.0, help="packets per second; 0 = no pacing")
    ap.add_argument("--timeout", type=float, default=1.0)
    ap.add_argument("--print-every", type=int, default=100)
    ap.add_argument("--csv", type=Path)
    ap.add_argument("--stop-on-error", action="store_true")
    ap.add_argument("--no-reset", action="store_true", help="do not reset firmware UDP I/O diagnostics on seq=1")
    ap.add_argument("--legacy-packet", action="store_true", help="send old 64-byte process-image packet instead of 544-byte tag packet")
    args = ap.parse_args()

    target = (args.host, args.port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(args.timeout)

    period = 0.0 if args.rate <= 0 else 1.0 / args.rate
    start = time.perf_counter()
    next_send = start

    rtts = []
    esp_turnarounds = []
    sent = received = bad = timeouts = resets = accepted = 0
    last_stats = None

    csv_file = None
    writer = None
    if args.csv:
        csv_file = args.csv.open("w", newline="")
        writer = csv.writer(csv_file)
        writer.writerow([
            "seq", "ok", "rtt_us", "esp_us", "flags", "rx_len", "di_mask",
            "rx_publish_count", "consume_count", "missed_update_count",
            "tag_publish_count", "tag_consume_count", "tag_missed_update_count",
            "tag_values_written", "tag_write_last_us", "tag_write_max_us",
            "published_seq", "consumed_seq", "tag_published_seq", "tag_consumed_seq",
            "last_age_us", "tag_last_age_us", "tag_max_age_us",
        ])

    try:
        for seq in range(1, args.count + 1):
            if period > 0:
                now = time.perf_counter()
                if next_send > now:
                    time.sleep(next_send - now)
                next_send += period

            req_flags = 0
            if seq == 1 and not args.no_reset:
                req_flags |= REQ_FLAG_RESET_STATS

            if args.legacy_packet:
                di_mask = seq & 0xFF
                pkt = make_legacy_request(seq, di_mask=di_mask, ai_base=(seq % 10000) / 10.0, flags=req_flags)
            else:
                pkt, di_mask = make_tag_request(seq, flags=req_flags)

            t0 = time.perf_counter_ns()
            try:
                sock.sendto(pkt, target)
                sent += 1
                data, _addr = sock.recvfrom(4096)
                t1 = time.perf_counter_ns()
            except socket.timeout:
                timeouts += 1
                if writer:
                    writer.writerow([seq, 0, "", "", "timeout", "", di_mask])
                if args.stop_on_error:
                    break
                continue
            except ConnectionResetError:
                resets += 1
                if writer:
                    writer.writerow([seq, 0, "", "", "reset", "", di_mask])
                if args.stop_on_error:
                    break
                continue

            rtt_us = (t1 - t0) / 1000.0
            try:
                resp = parse_response(data)
                stats = resp["stats"] or {}
                esp_us = int(resp["esp_tx_us"] - resp["esp_rx_us"])
                ok = (
                    resp["magic"] == UDP4_MAGIC
                    and resp["request_seq"] == seq
                    and resp["rx_len"] == len(pkt)
                    and (resp["flags"] & FLAG_ACCEPTED)
                    and not (resp["flags"] & (FLAG_CHECKSUM_BAD | FLAG_BAD_MAGIC | FLAG_BAD_VERSION | FLAG_BAD_SIZE))
                )
            except Exception:
                bad += 1
                if args.stop_on_error:
                    raise
                continue

            received += 1
            if resp["flags"] & FLAG_ACCEPTED:
                accepted += 1
            if not ok:
                bad += 1

            rtts.append(rtt_us)
            esp_turnarounds.append(esp_us)
            last_stats = stats

            if writer:
                writer.writerow([
                    seq, int(ok), f"{rtt_us:.1f}", esp_us, f"0x{resp['flags']:08x}", resp["rx_len"], di_mask,
                    stats.get("rx_publish_count", ""), stats.get("consume_count", ""), stats.get("missed_update_count", ""),
                    stats.get("tag_publish_count", ""), stats.get("tag_consume_count", ""), stats.get("tag_missed_update_count", ""),
                    stats.get("tag_values_written", ""), stats.get("tag_write_last_us", ""), stats.get("tag_write_max_us", ""),
                    stats.get("published_seq", ""), stats.get("consumed_seq", ""), stats.get("tag_published_seq", ""), stats.get("tag_consumed_seq", ""),
                    stats.get("last_age_us", ""), stats.get("tag_last_age_us", ""), stats.get("tag_max_age_us", ""),
                ])

            if seq == 1 or seq == args.count or (args.print_every and seq % args.print_every == 0):
                print(
                    f"seq={seq} {'OK' if ok else 'BAD'} "
                    f"rtt={rtt_us:8.1f} us esp={esp_us:4d} us "
                    f"flags=0x{resp['flags']:08x} di=0x{di_mask:02x} "
                    f"pub={stats.get('rx_publish_count', 0)} cons={stats.get('consume_count', 0)} missed={stats.get('missed_update_count', 0)} "
                    f"tag_pub={stats.get('tag_publish_count', 0)} tag_cons={stats.get('tag_consume_count', 0)} "
                    f"tag_missed={stats.get('tag_missed_update_count', 0)} "
                    f"tag_written={stats.get('tag_values_written', 0)} tag_us={stats.get('tag_write_last_us', 0)}"
                )

            if args.stop_on_error and not ok:
                break
    finally:
        if csv_file:
            csv_file.close()
        sock.close()

    elapsed = time.perf_counter() - start
    rtts_sorted = sorted(rtts)
    esp_sorted = sorted(esp_turnarounds)
    mode = "64 bytes P4IO legacy" if args.legacy_packet else "544 bytes P4IO tags, 512-byte value payload"

    print("\nSummary")
    print(f"  target              {args.host}:{args.port}")
    print(f"  request_size        {mode}")
    print(f"  response_size       1400 bytes UDP4")
    print(f"  elapsed             {elapsed:.3f} s")
    print(f"  requested_rate      {args.rate:g} packets/s")
    if elapsed > 0:
        print(f"  actual_send_rate    {sent / elapsed:.1f} packets/s")
    print(f"  sent                {sent}")
    print(f"  accepted            {accepted}")
    print(f"  timeouts/resets     {timeouts} / {resets}")
    print(f"  bad/mismatched      {bad}")
    if rtts:
        print(f"  RTT us avg/min/max   {sum(rtts)/len(rtts):.1f} / {min(rtts):.1f} / {max(rtts):.1f}")
        print(f"  RTT us p50/p95/p99   {pct(rtts_sorted,0.50):.1f} / {pct(rtts_sorted,0.95):.1f} / {pct(rtts_sorted,0.99):.1f}")
    if esp_turnarounds:
        print(f"  ESP turnaround us   avg/min/max {sum(esp_turnarounds)/len(esp_turnarounds):.1f} / {min(esp_turnarounds)} / {max(esp_turnarounds)}")
    if last_stats:
        print("  UDP I/O stats")
        print(f"    published          {last_stats.get('rx_publish_count', 0)}")
        print(f"    consumed           {last_stats.get('consume_count', 0)}")
        print(f"    missed_by_scan     {last_stats.get('missed_update_count', 0)}")
        print(f"    published_seq      {last_stats.get('published_seq', 0)}")
        print(f"    consumed_seq       {last_stats.get('consumed_seq', 0)}")
        print(f"    active             {last_stats.get('active', 0)}")
        print(f"    last_di_mask       0x{last_stats.get('last_di_mask', 0):02x}")
        print(f"    last_age_us/max    {last_stats.get('last_age_us', 0)} / {last_stats.get('max_age_us', 0)}")
        print("  UDP Tag stats")
        print(f"    tag_published      {last_stats.get('tag_publish_count', 0)}")
        print(f"    tag_consumed       {last_stats.get('tag_consume_count', 0)}")
        print(f"    tag_missed_by_scan {last_stats.get('tag_missed_update_count', 0)}")
        print(f"    tag_published_seq  {last_stats.get('tag_published_seq', 0)}")
        print(f"    tag_consumed_seq   {last_stats.get('tag_consumed_seq', 0)}")
        print(f"    values_written     {last_stats.get('tag_values_written', 0)}")
        print(f"    tag_write_us/max   {last_stats.get('tag_write_last_us', 0)} / {last_stats.get('tag_write_max_us', 0)}")
        print(f"    tag_cache_ready    {last_stats.get('tag_cache_ready', 0)}")
        print(f"    tag_age_us/max     {last_stats.get('tag_last_age_us', 0)} / {last_stats.get('tag_max_age_us', 0)}")

    return 0 if bad == 0 and timeouts == 0 and resets == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
