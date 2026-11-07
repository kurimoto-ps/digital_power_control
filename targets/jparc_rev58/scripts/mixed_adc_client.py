#!/usr/bin/env python3
"""FPGA v2.0 configuration and bounded 48-byte SPI1 DMA capture (stdlib only)."""
import argparse
import json
import sys
import time
from spi4_client import Client, TestError, uint

SHADOW = (0x504, 0x508, 0x50c, 0x510, 0x514)
APPLIED = (0x540, 0x544, 0x548, 0x54c, 0x550)


def probe(c):
    info = c.info()
    if (info['version'], info['capabilities']) != (0x20000, 0x3f):
        raise TestError('Requires FPGA v2.0 (0x00020000), capabilities 0x3f')
    if c.read(0x40c) != 3 or c.read(0x560) != 8:
        raise TestError('Unexpected ADC channel counts')


def configure(c, delay, conversion, phase, negate, period, enable=0):
    values = (delay, conversion, phase, negate, period)
    if not (0 <= delay <= 65535 and 0 <= conversion <= 65535 and
            0 <= phase <= 3 and negate in (0, 1) and enable in (0, 1) and
            2 <= period <= 0xffffffff and period % 2 == 0 and
            period >= conversion + delay + 270):
        raise TestError('Invalid timing: even period >= conversion + delay + 270 (25 ns ticks)')
    if c.read(0x518) & 1:
        raise TestError('Configuration already pending; do not overwrite shadows')
    before = c.read(0x530)
    rejected = c.read(0x52c)
    for reg, value in zip(SHADOW, values):
        c.write(reg, value)
    c.write(0x500, 2 | enable)
    submitted = c.read(0x530)
    if submitted != (before + 1) & 0xffffffff:
        raise TestError('APPLY not submitted; inspect STATUS/REJECTED')
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        status = c.read(0x518)
        if status & 2 or c.read(0x52c) != rejected:
            raise TestError('APPLY rejected')
        if not status & 1:
            if c.read(0x51c) != submitted or c.read(0x554) != enable:
                raise TestError('APPLY completion ID/ENABLE mismatch')
            if tuple(c.read(reg) for reg in APPLIED) != values:
                raise TestError('Applied values differ from requested snapshot')
            return submitted
        time.sleep(0.005)
    raise TestError('APPLY timeout; state unknown, inspect status before retry')


def capture(c, a):
    if c.read(0x60):
        raise TestError('Capture requires all DAC channels manual: run dac-source 0 explicitly')
    configure(c, a.delay, a.conversion, a.phase, a.negate, a.period)
    raw = c.request('MIXCAP ' + ' '.join(str(v) for v in
                    (a.ms, int(a.dummy24), a.required24, a.max_age_us, *a.values24)))
    if not raw.startswith('OK MIX '):
        raise TestError('Unexpected capture response: ' + raw)
    result = json.loads(raw[7:])
    result['configuration'] = {name: getattr(a, name) for name in
        ('delay', 'conversion', 'phase', 'negate', 'period', 'ms', 'dummy24',
         'values24', 'required24', 'max_age_us', 'expect16', 'tolerance')}
    if len(result['adc24']) != 3 or len(result['adc16']) != 8:
        raise TestError('Unexpected capture channel counts')
    for item in result['adc24']:
        value = item['raw']
        item['signed'] = value - (1 << 24) if value & (1 << 23) else value
        item['nominal_volts'] = item['signed'] * 3 / (1 << 23)
    for item in result['adc16']:
        item['signed'] = item['raw'] - 65536 if item['raw'] & 0x8000 else item['raw']
    if a.expect16 is not None:
        result['adc16_reference_pass'] = bool(result['frames']) and all(
            expected - a.tolerance <= item['min'] <= item['max'] <= expected + a.tolerance
            for expected, item in zip(a.expect16, result['adc16']))
        result['pass'] = result['pass'] and result['adc16_reference_pass']
    # MCU cleanup must complete before sending this reply; check its stable end state too.
    result['stopped'] = c.read(0x554) == 0 and c.read(0x400) == 0 and not (c.read(0x518) & 3)
    result['pass'] = result['pass'] and result['stopped']
    return result


def timing(p):
    p.add_argument('--delay', type=uint(16), default=9, help='25 ns ticks')
    p.add_argument('--conversion', type=uint(16), default=68, help='25 ns ticks')
    p.add_argument('--phase', type=int, choices=range(4), default=3)
    p.add_argument('--negate', type=int, choices=(0, 1), default=0)
    p.add_argument('--period', type=uint(32), default=4000, help='Even 25 ns ticks; 4000 = 10 kHz')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='192.168.100.58')
    p.add_argument('--port', type=int, default=4244)
    sub = p.add_subparsers(dest='command', required=True)
    sub.add_parser('status')
    cfg = sub.add_parser('configure', help='APPLY timing; acquisition remains stopped unless --enable 1')
    timing(cfg)
    cfg.add_argument('--enable', type=int, choices=(0, 1), default=0)
    sub.add_parser('stop', help='Stop acquisition with APPLY; hold existing DAC codes')
    dac = sub.add_parser('dac-source', help='Explicitly select per-channel ADC linkage; affects outputs immediately')
    dac.add_argument('mask', type=uint(11), help='0=all manual; 0x7ff=CH1..11 ADC-linked')
    cap = sub.add_parser('capture', help='100..3000 ms circular DMA; return statistics and latest values')
    timing(cap)
    cap.add_argument('--ms', type=int, default=1000)
    cap.add_argument('--dummy24', action='store_true', help='Only ADC24 is synthetic; ADC16 is always optical input')
    cap.add_argument('--values24', nargs=3, type=uint(24), default=[0x123456, 0x89abcd, 0xfedcba])
    cap.add_argument('--required24', type=uint(3), default=7, help='Required known/non-stale ADC24 mask; 0 permits disconnected inputs')
    cap.add_argument('--max-age-us', type=uint(16), default=100)
    cap.add_argument('--expect16', type=int, nargs=8, help='Eight known signed raw codes, A0 A1 B0 B1 C0 C1 D0 D1')
    cap.add_argument('--tolerance', type=int, default=0, help='Allowed signed code deviation for every captured sample')
    a = p.parse_args()
    if a.command == 'capture' and (not 100 <= a.ms <= 3000 or a.tolerance < 0 or
            (a.expect16 is not None and any(not -32768 <= v <= 32767 for v in a.expect16))):
        p.error('ms must be 100..3000; expected codes -32768..32767; tolerance >= 0')
    try:
        with Client(a.host, a.port, timeout=10) as c:
            probe(c)
            if a.command == 'configure':
                applied = configure(c, a.delay, a.conversion, a.phase, a.negate, a.period, a.enable)
                print(json.dumps({'applied_id': applied, 'enable': a.enable}))
            elif a.command == 'stop':
                values = [c.read(reg) for reg in APPLIED]
                if not c.read(0x51c):
                    values = [c.read(reg) for reg in SHADOW]
                configure(c, *values, enable=0)
                c.write(0x400, 0)
                print('Acquisition stopped; DAC source selection and held outputs are unchanged.')
            elif a.command == 'dac-source':
                c.write(0x60, a.mask)
                print(f'DAC source mask = {c.read(0x60):#05x} (selection readback, not analog verification)')
            elif a.command == 'capture':
                result = capture(c, a)
                print(json.dumps(result, indent=2))
                return 0 if result['pass'] else 1
            else:
                regs = dict(status=0x518, submitted_id=0x530, applied_id=0x51c, rejected=0x52c,
                            delay=0x540, conversion=0x544, phase=0x548, negate=0x54c,
                            period=0x550, enable=0x554, dac_source=0x60, tx=0x408,
                            source_drop=0x524, missed=0x528, frame_drop=0x538)
                print(json.dumps({k: c.read(v) for k, v in regs.items()}, indent=2))
    except (OSError, TestError, ValueError, KeyError, TypeError) as exc:
        print(f'FAIL: {exc}. No write retries. Inspect status; use stop if acquisition state is uncertain.', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
