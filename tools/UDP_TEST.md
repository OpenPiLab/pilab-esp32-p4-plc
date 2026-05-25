# PiLab UDP Test Tools

This folder intentionally keeps two UDP test clients:

- `pilab_udp_test_v2.py`
- `pilab_udp_io_test.py`

The older duplicate/obsolete files can be removed:

- `pilab_udp_test.py`
- `pilab_udp_io_test1.py`
- any `__pycache__/` folders

---

## 1. `pilab_udp_test_v2.py` — raw UDP transport / echo benchmark

Use this when you want to test the basic UDP server path without writing PLC tags.

This script sends arbitrary UDP payloads to the PiLab firmware UDP server on port `5005`.

The firmware response is a fixed 1400-byte binary packet containing:

- magic/version/packet size
- request sequence
- response sequence
- received packet length
- ESP receive timestamp
- ESP transmit timestamp
- payload pattern

This is the best first test after flashing firmware because it proves:

- the PLC IP address is correct
- UDP port `5005` is reachable
- the UDP task is running
- packet timing and loss are healthy
- large request/response traffic works

### Basic sanity test

```powershell
python tools/pilab_udp_test_v2.py 192.168.5.210 --count 10 --rate 1 --size 4
```

### 500 Hz raw UDP load test

```powershell
python tools/pilab_udp_test_v2.py 192.168.5.210 --count 10000 --rate 500 --size 1024
```

### Flood-style test

```powershell
python tools/pilab_udp_test_v2.py 192.168.5.210 --count 50000 --rate 0 --size 1024
```

### CSV output

```powershell
python tools/pilab_udp_test_v2.py 192.168.5.210 --count 10000 --rate 500 --size 1024 --csv udp_raw_500hz.csv
```

### Important values

```text
RTT us
    PC -> PLC -> PC round-trip time.

ESP turnaround us
    Time inside the ESP UDP task between recvfrom() and sendto().
    This is the firmware-side UDP handler time, not full network latency.

timeouts/resets
    Lost/no-response packets or Windows UDP ICMP reset events.

bad/mismatched
    Response was malformed, wrong sequence, wrong rx_len, etc.
```

---

## 2. `pilab_udp_io_test.py` — simulated UDP coupler / tag-write test

Use this when you want to simulate real UDP remote I/O coupler traffic.

Default mode sends a 544-byte `P4IO` v2 packet:

- 32 `UDP_DI` values
- 32 `UDP_DO` values
- 32 `UDP_AI` values
- 32 `UDP_AO` values
- 128 total values
- 4 bytes per value
- 512 bytes of tag payload

The firmware validates the packet, publishes the tag image into a double buffer, and the PLC 1 ms path consumes the latest complete image and writes it into cached `UDP_*` tags without name lookup.

This test is the meaningful system-load test because it exercises:

- UDP receive
- binary packet validation
- checksum
- double-buffer publish
- scan-side consume
- cached tag writes
- HMI/API visibility
- PLC timing while remote I/O data is changing

### Basic 200 Hz coupler simulation

```powershell
python tools/pilab_udp_io_test.py 192.168.5.210 --count 10000 --rate 200
```

### 500 Hz coupler stress test

```powershell
python tools/pilab_udp_io_test.py 192.168.5.210 --count 10000 --rate 500
```

### Long 200 Hz test

```powershell
python tools/pilab_udp_io_test.py 192.168.5.210 --count 100000 --rate 200 --csv udp_tags_200hz.csv
```

### Run two simulated couplers

Start two PowerShell windows.

Terminal 1:

```powershell
python tools/pilab_udp_io_test.py 192.168.5.210 --count 10000 --rate 200
```

Terminal 2:

```powershell
python tools/pilab_udp_io_test.py 192.168.5.210 --count 10000 --rate 200 --no-reset
```

Together this approximates:

```text
2 clients x 200 packets/sec = 400 UDP packets/sec
```

Because both scripts currently write the same UDP tag bank, the tag values are last-writer-wins. This is fine for stress testing, but a real multi-coupler design should eventually include node IDs or separate tag banks.

### Legacy 64-byte process-image mode

The newer `pilab_udp_io_test.py` can still send the older 64-byte process-image packet:

```powershell
python tools/pilab_udp_io_test.py 192.168.5.210 --count 10000 --rate 500 --legacy-packet
```

This mode updates the simulated process image but does not exercise the full 128-tag write path.

---

## Recommended test sequence

After flashing firmware:

```powershell
python tools/pilab_udp_test_v2.py 192.168.5.210 --count 10 --rate 1 --size 4
```

Then raw UDP load:

```powershell
python tools/pilab_udp_test_v2.py 192.168.5.210 --count 10000 --rate 500 --size 1024
```

Then simulated coupler/tag write:

```powershell
python tools/pilab_udp_io_test.py 192.168.5.210 --count 10000 --rate 200
```

Then heavier tag-write load:

```powershell
python tools/pilab_udp_io_test.py 192.168.5.210 --count 10000 --rate 500
```

Then two-coupler test:

```powershell
# Terminal 1
python tools/pilab_udp_io_test.py 192.168.5.210 --count 10000 --rate 200

# Terminal 2
python tools/pilab_udp_io_test.py 192.168.5.210 --count 10000 --rate 200 --no-reset
```

---

## Fields to watch in `pilab_udp_io_test.py`

```text
published
    UDP process-image packets accepted/published.

consumed
    Number consumed by the PLC 1 ms path.

missed_by_scan
    A newer UDP image arrived before the scan consumed the previous one.
    This is not packet loss. It means latest-image-wins behavior occurred.

tag_published
    UDP tag images accepted/published.

tag_consumed
    UDP tag images consumed by the scan-side tag-write path.

tag_missed_by_scan
    A newer tag image replaced an older one before scan-side consumption.

values_written
    Should be 128 in normal v2 tag mode.

tag_write_us/max
    Time to write all 128 cached UDP tags from the scan-side path.

tag_cache_ready
    Should be 1. If 0, the UDP_* tag cache was not ready.

tag_age_us/max
    Time between UDP tag image publish and scan-side tag write.
```

---

## Fields to watch in the ESP monitor

While tests are running, watch the firmware performance report:

```text
Period min/max/avg
Worst late jitter
Worst early jitter
Samples >50 us error
Samples >100 us error
Missed/coalesced notifications
PLC Work avg/max
Work >100 us
Work >250 us
Script total avg/max
Script >1 ms
Script >2.5 ms
Script >5 ms
Script missed/coalesced notifications
```

For a healthy run:

```text
Missed/coalesced notifications should stay 0.
Work >500 us should normally stay 0.
Script >5 ms should normally stay 0 for a 5 ms script task.
UDP bad/mismatched should stay 0.
UDP timeouts/resets should stay 0.
```

---

## Cleanup before committing

Recommended files to keep:

```text
tools/pilab_udp_test_v2.py
tools/pilab_udp_io_test.py
tools/UDP_TEST.md
```

Recommended files to delete:

```text
tools/pilab_udp_test.py
tools/pilab_udp_io_test1.py
main/tools/pilab_udp_io_test.py   # unless intentionally keeping duplicate tools under main/
main/tools/__pycache__/
```

PowerShell cleanup example:

```powershell
Remove-Item tools/pilab_udp_test.py
Remove-Item tools/pilab_udp_io_test1.py
Remove-Item -Recurse -Force main/tools/__pycache__ -ErrorAction SilentlyContinue
Remove-Item main/tools/pilab_udp_io_test.py -ErrorAction SilentlyContinue
```

Then add and commit:

```powershell
git add tools/pilab_udp_test_v2.py tools/pilab_udp_io_test.py tools/UDP_TEST.md
git add main/ethernet_web.c main/plc_io.cpp main/plc_io.hpp main/plc_tags.cpp main/plc_tags.hpp main/udp_server.c
git commit -m "Add UDP coupler simulation and tag write stress tests"
git push
```
