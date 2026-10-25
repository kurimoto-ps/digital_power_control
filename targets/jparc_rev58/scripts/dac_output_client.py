#!/usr/bin/env python3
"""Set one real DAC raw code or read all FPGA DAC registers; emit CSV."""
import argparse
import csv
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
import sys
from spi4_client import Client, TestError, dac_set, uint


def identity(channel):
    chip = (channel - 1) // 4
    return chip + 1, f'IC{11 + 2 * chip}', 'ABCD'[(channel - 1) % 4], f'CN{3 + chip * 4}-CN{6 + chip * 4}'


def voltage_to_code(text):
    """±10 V two's complement; nearest LSB, ties away from zero; +FS saturates."""
    try:
        volts = Decimal(text)
    except InvalidOperation as exc:
        raise argparse.ArgumentTypeError('Voltage must be a finite number in [-10, 10] V') from exc
    if not volts.is_finite() or not Decimal(-10) <= volts <= Decimal(10):
        raise argparse.ArgumentTypeError('Voltage must be a finite number in [-10, 10] V')
    signed = int((volts * 32768 / 10).to_integral_value(rounding=ROUND_HALF_UP))
    return min(32767, max(-32768, signed)) & 0xffff


def code_to_voltage(code):
    signed = code - 65536 if code & 0x8000 else code
    return signed * 10 / 32768


def channel_or_connector(text):
    try:
        channel = int(text[2:]) - 2 if text.upper().startswith('CN') else int(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError('Use CH number 1..16 or connector CN3..CN18') from exc
    if not 1 <= channel <= 16:
        raise argparse.ArgumentTypeError('Use CH number 1..16 or connector CN3..CN18')
    return channel


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='192.168.100.58')
    p.add_argument('--port', type=int, default=4244)
    sub = p.add_subparsers(dest='command', required=True)
    sub.add_parser('status', help='Read only: FPGA SET/SENT registers, not DAC analog readback')
    s = sub.add_parser('set', help='Resends ALL FOUR channels in the selected DAC IC; output persists')
    s.add_argument('channel', type=channel_or_connector, help='1..16 or CN3..CN18')
    s.add_argument('code', type=uint(16))
    v = sub.add_parser('set-voltage', help='Set volts [-10, 10]; resends all four channels of the IC')
    v.add_argument('channel', type=channel_or_connector, help='1..16 or CN3..CN18')
    v.add_argument('volts', type=voltage_to_code, help='Requested volts; +10 clips to +9.999694824 V')
    a = p.parse_args()
    try:
        with Client(a.host, a.port) as c:
            info = c.info()
            if info['version'] != 0x10003 or info['capabilities'] != 15 or c.read(0x001c) != 16:
                raise TestError('Requires integrated FPGA v1.3 with 16 DAC channels')
            if a.command in ('set', 'set-voltage'):
                code = a.code if a.command == 'set' else a.volts
                dac_set(c, a.channel, code)
                print(f'CH{a.channel} / CN{a.channel + 2}: code=0x{code:04x}, nominal={code_to_voltage(code):.9f} V', file=sys.stderr)
                print(f'FPGA transmission complete for CH{a.channel}; measure analog output externally. Output is held after exit.', file=sys.stderr)
            status = c.read(0x0014)
            writer = csv.writer(sys.stdout)
            writer.writerow(['channel', 'dac', 'ic', 'output', 'connector_group', 'exact_connector',
                             'set_hex', 'sent_hex', 'set_nominal_volts', 'sent_nominal_volts', 'initialized', 'busy', 'pending'])
            for ch in range(1, 17):
                chip, ic, output, group = identity(ch)
                bit = chip - 1
                set_code = c.read(0x0200 + 4 * (ch - 1))
                sent_code = c.read(0x0300 + 4 * (ch - 1))
                writer.writerow([ch, chip, ic, output, group, f'CN{ch + 2}',
                                 f'0x{set_code:04x}', f'0x{sent_code:04x}',
                                 f'{code_to_voltage(set_code):.9f}', f'{code_to_voltage(sent_code):.9f}',
                                 (status >> (8 + bit)) & 1, (status >> bit) & 1,
                                 (status >> (4 + bit)) & 1])
        return 0
    except (OSError, TestError) as exc:
        print(f'FAIL: {exc}. Writes are not retried; inspect status before another set.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
