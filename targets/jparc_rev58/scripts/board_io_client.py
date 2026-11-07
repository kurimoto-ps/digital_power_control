#!/usr/bin/env python3
"""PLC 16-in/16-out mapping and motherboard RS232 tests, via Ethernet."""
import argparse
import datetime
import json
from pathlib import Path
import sys
import time
from spi4_client import Client, TestError, uint

INPUT_PINS = 'PD6 PD3 PF10 PC11 PB7 PC9 PH10 PG3 PH12 PG2 PH9 PI3 PI2 PJ7 PI1 PI0'.split()
OUTPUT_PINS = 'PE3 PG12 PK2 PA3 PF8 PH2 PI12 PH14 PH15 PI4 PK5 PG9 PB9 PD5 PD7 PD4'.split()


class Board:
    def __init__(self, host, port):
        self.host, self.port = host, port

    def command(self, command):
        # Operators may take minutes between steps: never hold an idle TCP session.
        with Client(self.host, self.port, timeout=5) as c:
            raw = c.request(command)
        prefix = 'OK ' + command.split()[0] + ' '
        if not raw.startswith(prefix):
            raise TestError('Unexpected response: ' + raw)
        try:
            result = json.loads(raw[len(prefix):])
        except ValueError as exc:
            raise TestError('Invalid JSON reply') from exc
        if prefix == 'OK PLC ':
            for key in ('input_raw', 'output_raw', 'output_latch'):
                if type(result.get(key)) is not int or not 0 <= result[key] <= 65535:
                    raise TestError('Invalid GPIO reply: ' + key)
        else:
            for key in ('rx_total', 'rx_dropped', 'error_flags', 'buffered', 'tx_total'):
                if type(result.get(key)) is not int or not 0 <= result[key] <= 0xffffffff:
                    raise TestError('Invalid UART reply: ' + key)
            try:
                bytes.fromhex(result['hex'])
            except (ValueError, KeyError, TypeError) as exc:
                raise TestError('Invalid UART hex reply') from exc
        return result

    def read(self):
        return self.command('PLC READ')

    def write(self, mask):
        if not 0 <= mask <= 65535:
            raise TestError('GPIO mask must be 0..0xffff')
        r = self.command(f'PLC WRITE 0x{mask:04x}')
        if r['output_raw'] != mask or r['output_latch'] != mask:
            raise TestError('STM32 output readback mismatch; inspect output state')
        return r


def observed_mask(prompt, ask=input):
    while True:
        text = ask(prompt + ' [hex, e.g. 0001; q=abort]: ').strip()
        if text.lower() == 'q':
            raise KeyboardInterrupt
        try:
            value = int(text, 16)
            if 0 <= value <= 65535:
                return value
        except ValueError:
            pass
        print('Enter a 16-bit hexadecimal mask.')


def mapping_test(board, direction, report_path, ask=input):
    report = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  host=board.host, direction=direction, tests=[], complete=False, passed=False)
    try:
        board.write(0)
        if direction in ('both', 'in'):
            ask('PLC DO: turn ALL 16 outputs OFF, then press Enter. ')
            baseline = board.read()
            report['input_baseline'] = baseline
            if baseline['input_raw'] != 0:
                raise TestError('Expected input baseline 0000; check PLC outputs/polarity/wiring first')
            for bit, pin in enumerate(INPUT_PINS):
                expected = 1 << bit
                ask(f'IN {bit:02d}: turn ONLY PLC DO bit {bit} ON; PDI{bit} -> {pin}. Enter to read. ')
                active = board.read()
                print(f"STM32 input={active['input_raw']:04x}, expected={expected:04x}")
                ask(f'Turn PLC DO bit {bit} OFF (all OFF), then Enter. ')
                inactive = board.read()
                passed = active['input_raw'] == expected and inactive['input_raw'] == 0
                report['tests'].append(dict(direction='PLC_to_STM32', bit=bit, pin=pin,
                                            active=active, inactive=inactive, passed=passed))
                print('PASS' if passed else 'FAIL')
        if direction in ('both', 'out'):
            baseline = observed_mask('PLC DI: read ALL 16 bits with STM32 outputs zero', ask)
            report['output_baseline'] = baseline
            if baseline:
                raise TestError('Expected PLC DI baseline 0000; check wiring/polarity first')
            for bit, pin in enumerate(OUTPUT_PINS):
                expected = 1 << bit
                active = board.write(expected)
                observed = observed_mask(f'OUT {bit:02d}: {pin}/PDO{bit} ON. Enter actual PLC DI mask', ask)
                inactive = board.write(0)
                cleared = observed_mask('All STM32 outputs now OFF. Enter actual PLC DI mask', ask)
                passed = observed == expected and cleared == 0
                report['tests'].append(dict(direction='STM32_to_PLC', bit=bit, pin=pin,
                                            active=active, inactive=inactive, plc_active=observed,
                                            plc_inactive=cleared, passed=passed))
                print('PASS' if passed else 'FAIL')
        report['complete'] = True
        report['passed'] = all(t['passed'] for t in report['tests'])
    except BaseException as exc:
        report['abort'] = type(exc).__name__ + ': ' + str(exc)
        raise
    finally:
        try:
            board.write(0)
            report['outputs_cleared'] = True
        except (OSError, TestError) as exc:
            report['outputs_cleared'] = False
            report['cleanup_error'] = str(exc)
            report['passed'] = False
            print('Output reset NOT confirmed; inspect PLC and reconnect to write 0.', file=sys.stderr)
        Path(report_path).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'Report: {report_path}; completed steps: {len(report["tests"])}')
    return report['passed']


def payload(a):
    try:
        data = bytes.fromhex(a.hex) if a.hex is not None else a.text.encode('ascii')
    except (ValueError, UnicodeError) as exc:
        raise TestError('Use hexadecimal bytes or ASCII text') from exc
    if not 1 <= len(data) <= 32:
        raise TestError('Payload must be 1..32 bytes')
    return data


def uart_receive(board, seconds, expected):
    board.command('UART CLEAR')
    print('RX buffer cleared. Send the test bytes from the RS232 terminal now.', file=sys.stderr)
    deadline = time.monotonic() + seconds
    received = bytearray()
    r = {}
    while time.monotonic() < deadline:
        r = board.command('UART READ')
        data = bytes.fromhex(r['hex'])
        received.extend(data)
        if data:
            print(f'RX {data.hex()} {data!r}', file=sys.stderr)
        if len(received) > 65536:
            raise TestError('Receive test exceeded 64 KiB; reduce incoming data')
        time.sleep(0.05)
    # Drain any tail already in software RAM without waiting for more bytes.
    while True:
        r = board.command('UART READ')
        received.extend(bytes.fromhex(r['hex']))
        if len(received) > 65536:
            raise TestError('Receive test exceeded 64 KiB')
        if not r['buffered']:
            break
    return dict(received_hex=received.hex(), expected_hex=expected.hex(), status=r,
                passed=bytes(received) == expected and not r['rx_dropped'] and not r['error_flags'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='192.168.100.58')
    p.add_argument('--port', type=int, default=4244)
    sub = p.add_subparsers(dest='command', required=True)
    sub.add_parser('plc-read')
    sub.add_parser('plc-write').add_argument('mask', type=uint(16))
    test = sub.add_parser('plc-test', help='Operator-guided 16 input + 16 output tests; saves JSON')
    test.add_argument('--direction', choices=('both', 'in', 'out'), default='both')
    test.add_argument('--report', default='plc_mapping_report.json')
    for name in ('uart-status', 'uart-clear', 'uart-read'):
        sub.add_parser(name)
    for name in ('uart-send', 'uart-receive'):
        cmd = sub.add_parser(name)
        group = cmd.add_mutually_exclusive_group(required=True)
        group.add_argument('--text', help='ASCII, no implicit newline')
        group.add_argument('--hex', help='Hexadecimal bytes, spaces permitted when quoted')
        if name == 'uart-receive':
            cmd.add_argument('--seconds', type=int, default=30, choices=range(1, 301), metavar='1..300')
    a = p.parse_args()
    try:
        b = Board(a.host, a.port)
        if a.command == 'plc-test':
            # Fail before driving outputs if the report cannot be opened.
            with open(a.report, 'a', encoding='utf-8'):
                pass
            return 0 if mapping_test(b, a.direction, a.report) else 1
        if a.command == 'plc-read':
            r = b.read()
        elif a.command == 'plc-write':
            r = b.write(a.mask)
        elif a.command == 'uart-send':
            r = b.command('UART SEND ' + payload(a).hex())
        elif a.command == 'uart-receive':
            r = uart_receive(b, a.seconds, payload(a))
        else:
            r = b.command(a.command.replace('-', ' ').upper())
        print(json.dumps(r, indent=2))
        return 0 if r.get('passed', True) else 1
    except (OSError, TestError, EOFError, KeyboardInterrupt) as exc:
        print(f'FAIL/ABORT: {exc}. No automatic write retries.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
