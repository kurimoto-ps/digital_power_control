#!/usr/bin/env python3
"""Read FPGA ACC levels and v2.0.2 12 MHz detection and STM32 PA0/PD13; test static coax wiring."""
import argparse
import datetime
import json
from pathlib import Path
import sys
import time
from spi4_client import Client, TestError, uint


def decode(raw):
    if not raw.startswith('OK ACC '):
        raise TestError('Unexpected ACC response: ' + raw)
    try:
        r = json.loads(raw[7:])
        for key, maximum in [('version', 0xffffffff), ('capabilities', 0xffffffff),
                             ('fpga_raw', 7), ('stm32_before', 6), ('stm32_after', 6),
                             ('stm32_mask', 6)]:
            if type(r[key]) is not int or not 0 <= r[key] <= maximum:
                raise ValueError(key)
        if (r['version'] not in (0x20001, 0x20002) or not r['capabilities'] & 0x40 or r['stm32_mask'] != 6 or
                (r['stm32_before'] | r['stm32_after']) & 1):
            raise ValueError('identity/mask')
    except (ValueError, KeyError, TypeError) as exc:
        raise TestError('Invalid ACC diagnostic reply') from exc
    supported = r['version'] == 0x20002 and bool(r['capabilities'] & 0x80)
    if r.get('clock_supported', False) is not supported:
        raise TestError('Clock capability/reply mismatch; update STM32 image')
    status, count = r.get('clock_status'), r.get('clock_count')
    if supported:
        if type(status) is not int or not 0 <= status <= 7 or type(count) is not int or not 0 <= count <= 0x1ffff:
            raise TestError('Invalid/missing clock registers; update STM32 image')
    elif status is not None or count is not None:
        raise TestError('Unexpected unsupported clock registers')
    r['clock'] = dict(supported=supported, window_valid=bool(status & 1) if supported else None,
                      active=bool(status & 2) if supported else None,
                      near_12mhz=bool(status & 4) if supported else None,
                      count_per_ms=count, estimated_hz=count * 1000 if supported else None,
                      paired_snapshot=False)
    level = lambda value, bit: 'HIGH' if value & (1 << bit) else 'LOW'
    r['sampled_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    r['signals'] = {}
    for bit, name, pin in [(0, 'ACC_12MHZ', None), (1, 'ACC_96KHZ', 'PA0'), (2, 'ACC_TRIG', 'PD13')]:
        r['signals'][name] = dict(fpga=level(r['fpga_raw'], bit), stm32_pin=pin,
                                 stm32_before=level(r['stm32_before'], bit) if pin else None,
                                 stm32_after=level(r['stm32_after'], bit) if pin else None)
    r['shared_levels_equal'] = (r['fpga_raw'] & 6) == r['stm32_before'] == r['stm32_after']
    r['sampling'] = 'GPIO/0x24 are sequential instantaneous levels; clock registers are separate 1 ms windows; no trigger history'
    return r


def static_match(r, expected, mask):
    return ((r['fpga_raw'] & mask) == (expected & mask) and
            (r['stm32_before'] & mask & 6) == (expected & mask & 6) and
            (r['stm32_after'] & mask & 6) == (expected & mask & 6))


class Monitor:
    def __init__(self, host, port): self.host, self.port = host, port
    def read(self):
        with Client(self.host, self.port, timeout=5) as c:
            return decode(c.request('ACC READ'))


def clock_match(r, expected):
    if not r['clock']['supported']:
        raise TestError('12 MHz detection requires FPGA v2.0.2, capability bit7 and updated STM32')
    # STATUS is one atomic flag set. COUNT may refer to a different completed window.
    return r['clock_status'] == (7 if expected == 'on' else 1)


def samples(monitor, count, interval, expected=None, mask=6, clock_expected=None):
    rows = []
    for i in range(count):
        r = monitor.read()
        if expected is not None:
            r['expected'] = expected; r['tested_mask'] = mask
            r['passed'] = static_match(r, expected, mask)
        if clock_expected is not None:
            matched = clock_match(r, clock_expected)
            r['clock_expected'] = clock_expected
            r['clock_passed'] = matched
            r['passed'] = r.get('passed', True) and matched
        rows.append(r)
        if i + 1 < count: time.sleep(interval)
    return rows


def coax_test(monitor, report_path, ask=input):
    report = dict(host=monitor.host, started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  tested_fpga_mask=6, tested_stm32_mask=6,
                  not_tested=['ACC_12MHZ: optical J6 path, not J16/J17 coax'],
                  complete=False, passed=False, steps=[])
    steps = [('baseline', 'Hold BOTH J16 and J17 LOW', 0),
             ('clk96_high', 'Hold J16 HIGH and J17 LOW', 2),
             ('clk96_low', 'Return J16 LOW; keep J17 LOW', 0),
             ('trig_high', 'Hold J17 HIGH and J16 LOW', 4),
             ('trig_low', 'Return J17 LOW; keep J16 LOW', 0),
             ('both_high', 'Hold BOTH J16 and J17 HIGH', 6),
             ('final_low', 'Return BOTH J16 and J17 LOW', 0)]
    try:
        for name, prompt, expected in steps:
            answer = ask(prompt + '; wait for stable DC, then Enter (q=abort): ')
            if answer.strip().lower() == 'q': raise KeyboardInterrupt
            time.sleep(0.01)
            rows = samples(monitor, 3, 0.1, expected, 6)
            passed = all(r['passed'] for r in rows)
            report['steps'].append(dict(name=name, expected=expected, samples=rows, passed=passed))
            print(f'{name}: {"PASS" if passed else "FAIL"}; expected mask 0x{expected:x}/0x6')
        report['complete'] = True
        report['passed'] = all(s['passed'] for s in report['steps'])
    except BaseException as exc:
        report['abort'] = type(exc).__name__ + ': ' + str(exc)
        raise
    finally:
        Path(report_path).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'Report: {report_path}. Return external generators to LOW manually. 12 MHz path is NOT tested.')
    return report['passed']


def clock_test(monitor, report_path, ask=input):
    report = dict(host=monitor.host, complete=False, passed=False, steps=[],
                  tested='12 MHz J6 optical path / MAX10 M9; 96 kHz and TRIG not tested')
    try:
        initial = monitor.read()
        report['initial'] = initial
        clock_match(initial, 'off')  # Capability validation only; initial state is not assumed.
        for name, prompt, expected in [
            ('stopped_before', 'Stop the J6 optical clock', 'off'),
            ('running', 'Apply a 12 MHz optical clock to J6', 'on'),
            ('stopped_after', 'Stop the J6 optical clock again', 'off')]:
            if ask(prompt + ', then Enter (q=abort): ').strip().lower() == 'q':
                raise KeyboardInterrupt
            time.sleep(0.003)  # At least 3 ms after the operator confirms the change.
            rows = samples(monitor, 3, 0.01, clock_expected=expected)
            passed = all(r['passed'] for r in rows)
            report['steps'].append(dict(name=name, expected=expected, samples=rows, passed=passed))
            print(f'{name}: {"PASS" if passed else "FAIL"}; counts/ms=' +
                  ','.join(str(r['clock']['count_per_ms']) for r in rows))
        report['complete'] = True
        report['passed'] = all(s['passed'] for s in report['steps'])
    except BaseException as exc:
        report['abort'] = type(exc).__name__ + ': ' + str(exc)
        raise
    finally:
        Path(report_path).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'Report: {report_path}. Stop external generators manually.')
    return report['passed']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='192.168.100.58')
    p.add_argument('--port', type=int, default=4244)
    sub = p.add_subparsers(dest='command', required=True)
    read = sub.add_parser('read', help='Read levels; comparison is meaningful only with stable inputs')
    read.add_argument('--count', type=int, default=1)
    read.add_argument('--interval', type=float, default=0.2)
    read.add_argument('--expect', type=uint(3), help='Expected static FPGA bits; also checks shared STM32 pins')
    read.add_argument('--mask', type=uint(3), default=6, help='Checked bits, default 6: J16+J17 only')
    read.add_argument('--report')
    read.add_argument('--clock-expect', choices=('on', 'off'), help='Check valid near-12MHz or stopped STATUS after a 3 ms wait')
    test = sub.add_parser('coax-test', help='Guided J16/J17 fixed-level mapping test; excludes optical 12 MHz')
    test.add_argument('--report', default='acc_coax_report.json')
    clock = sub.add_parser('clock-test', help='Guided optical 12 MHz stop/start/stop test; FPGA v2.0.2 required')
    clock.add_argument('--report', default='acc_12mhz_report.json')
    a = p.parse_args()
    if a.command == 'read' and (not 1 <= a.count <= 1000 or not 0.01 <= a.interval <= 5 or not a.mask):
        p.error('count 1..1000, interval 0.01..5 seconds, nonzero mask required')
    try:
        m = Monitor(a.host, a.port)
        if a.report:
            with open(a.report, 'a', encoding='utf-8'): pass
        if a.command == 'coax-test':
            return 0 if coax_test(m, a.report) else 1
        if a.command == 'clock-test':
            return 0 if clock_test(m, a.report) else 1
        if a.clock_expect is not None: time.sleep(0.003)
        rows = samples(m, a.count, a.interval, a.expect, a.mask, a.clock_expect)
        result = dict(samples=rows, passed=all(r['passed'] for r in rows) if a.expect is not None or a.clock_expect is not None else None,
                      tested_mask=a.mask if a.expect is not None else 0)
        text = json.dumps(result, indent=2) + '\n'
        if a.report: Path(a.report).write_text(text, encoding='utf-8')
        print(text, end='')
        return 1 if result['passed'] is False else 0
    except (OSError, TestError, EOFError, KeyboardInterrupt) as exc:
        print(f'FAIL/ABORT: {exc}. External signal sources are not controlled by this script.', file=sys.stderr)
        return 1


if __name__ == '__main__': sys.exit(main())
