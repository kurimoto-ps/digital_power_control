#!/usr/bin/env python3
"""Read real ADC latest-value snapshots through Ethernet/SPI4; emit CSV."""
import argparse
import csv
import math
import sys
import time
from spi4_client import Client, TestError


def decode(raw, encoding):
    raw &= 0xffffff
    if encoding == 'signed24' and raw & 0x800000:
        return raw - 0x1000000
    if encoding == 'offset-binary':
        return raw - 0x800000
    return raw


def snapshot(client, channel):
    i = channel - 1
    before = client.read(0x0410 + 4 * i)
    raw = client.read(0x0440 + 4 * i) & 0xffffff
    after = client.read(0x0410 + 4 * i)
    restart = client.read(0x0430 + 4 * i)
    return raw, before, after, restart


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='192.168.100.58')
    p.add_argument('--port', type=int, default=4244)
    p.add_argument('--encoding', required=True, choices=['signed24', 'unsigned24', 'offset-binary'])
    p.add_argument('--channels', nargs='+', type=int, choices=[1, 2, 3], default=[1, 2, 3])
    p.add_argument('--count', type=int, default=20, help='Number of polling rounds')
    p.add_argument('--interval', type=float, default=0.5, help='Seconds between rounds, 0.01..2')
    p.add_argument('--initialize', action='store_true', help='Stop SPI1 TX, select real inputs and clear ADC counters/FIFO')
    a = p.parse_args()
    if not 1 <= a.count <= 1000000 or not math.isfinite(a.interval) or not 0.01 <= a.interval <= 2:
        p.error('count must be 1..1000000; interval must be 0.01..2 seconds')
    channels = list(dict.fromkeys(a.channels))
    try:
        with Client(a.host, a.port) as c:
            info = c.info()
            if info['version'] != 0x10003 or info['capabilities'] != 15 or c.read(0x040c) != 3:
                raise TestError('Requires integrated FPGA v1.3 with three ADC inputs')
            if a.initialize:
                c.write(0x0400, 0)
                deadline = time.monotonic() + 1
                while c.read(0x0404) & (1 << 16):
                    if time.monotonic() >= deadline:
                        raise TestError('SPI1 transmitter did not stop')
                    time.sleep(0.01)
                c.write(0x0480, 0)
                c.write(0x0400, 2)
            if c.read(0x0480) != 0 or c.read(0x0400) != 0:
                raise TestError('Requires real-input mode and SPI1 TX disabled; use --initialize')
            baseline = {ch: c.read(0x0410 + 4 * (ch - 1)) for ch in channels}
            previous = dict(baseline)
            previous_restart = {ch: c.read(0x0430 + 4 * (ch - 1)) for ch in channels}
            active = {ch: False for ch in channels}
            restart_errors = False
            writer = csv.writer(sys.stdout)
            writer.writerow(['elapsed_s', 'channel', 'raw_hex', 'encoding', 'digital_value', 'volts',
                             'rx_before', 'rx_after', 'rx_delta', 'fresh', 'changed_during_read',
                             'restart_count', 'restart_delta'])
            start = time.monotonic()
            for _ in range(a.count):
                time.sleep(a.interval)
                for ch in channels:
                    raw, before, after, restart = snapshot(c, ch)
                    delta = (after - previous[ch]) & 0xffffffff
                    # Require a new complete input before reading LAST, not just afterwards.
                    fresh = ((before - previous[ch]) & 0xffffffff) != 0
                    active[ch] |= fresh
                    restart_delta = (restart - previous_restart[ch]) & 0xffffffff
                    restart_errors |= restart_delta != 0
                    value = decode(raw, a.encoding)
                    writer.writerow([f'{time.monotonic()-start:.6f}', ch, f'0x{raw:06x}', a.encoding,
                                     value, f'{value * 3 / (1 << 23):.9f}', before, after, delta,
                                     int(fresh), int(before != after), restart, restart_delta])
                    previous[ch], previous_restart[ch] = after, restart
                sys.stdout.flush()
            missing = [ch for ch in channels if not active[ch]]
            if missing or restart_errors:
                raise TestError(f'No fresh input on channels {missing}; frame restart errors={restart_errors}')
            print('Input activity observed; compare values/volts with the applied input. Not an analog accuracy PASS.', file=sys.stderr)
        return 0
    except (OSError, TestError) as exc:
        print(f'FAIL: {exc}. No automatic retries.', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
