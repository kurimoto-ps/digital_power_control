#!/usr/bin/env python3
"""Set one real DAC raw code or read all FPGA DAC registers; emit CSV."""
import argparse
import csv
import sys
from spi4_client import Client, TestError, dac_set, uint


def identity(channel):
    chip = (channel - 1) // 4
    return chip + 1, f'IC{11 + 2 * chip}', 'ABCD'[(channel - 1) % 4], f'CN{3 + chip * 4}-CN{6 + chip * 4}'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='192.168.100.58')
    p.add_argument('--port', type=int, default=4244)
    sub = p.add_subparsers(dest='command', required=True)
    sub.add_parser('status', help='Read only: FPGA SET/SENT registers, not DAC analog readback')
    s = sub.add_parser('set', help='Resends ALL FOUR channels in the selected DAC IC; output persists')
    s.add_argument('channel', type=int, choices=range(1, 17))
    s.add_argument('code', type=uint(16))
    a = p.parse_args()
    try:
        with Client(a.host, a.port) as c:
            info = c.info()
            if info['version'] != 0x10003 or info['capabilities'] != 15 or c.read(0x001c) != 16:
                raise TestError('Requires integrated FPGA v1.3 with 16 DAC channels')
            if a.command == 'set':
                dac_set(c, a.channel, a.code)
                print(f'FPGA transmission complete for CH{a.channel}; measure analog output externally. Output is held after exit.', file=sys.stderr)
            status = c.read(0x0014)
            writer = csv.writer(sys.stdout)
            writer.writerow(['channel', 'dac', 'ic', 'output', 'connector_group', 'exact_connector',
                             'set_hex', 'sent_hex', 'initialized', 'busy', 'pending'])
            for ch in range(1, 17):
                chip, ic, output, group = identity(ch)
                bit = chip - 1
                writer.writerow([ch, chip, ic, output, group, 'UNCONFIRMED',
                                 f'0x{c.read(0x0200 + 4 * (ch - 1)):04x}',
                                 f'0x{c.read(0x0300 + 4 * (ch - 1)):04x}',
                                 (status >> (8 + bit)) & 1, (status >> bit) & 1,
                                 (status >> (4 + bit)) & 1])
        return 0
    except (OSError, TestError) as exc:
        print(f'FAIL: {exc}. Writes are not retried; inspect status before another set.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
