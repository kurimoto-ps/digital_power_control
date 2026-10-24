#!/usr/bin/env python3
"""Run a bounded FPGA v1.3 dummy ADC -> SPI1 circular DMA test over Ethernet."""
import argparse
import json
import socket
import sys


class TestError(RuntimeError):
    pass


def validate(result, duration_ms, values):
    if not isinstance(result, dict) or result.get('pass') is not True:
        raise TestError('Firmware reported failure: ' + json.dumps(result, ensure_ascii=False))
    for field in ('crc_errors', 'header_errors', 'discarded_bytes', 'trailing_bytes',
                  'dma_errors', 'dma_overruns', 'spi_error_flags', 'error', 'cleanup_error'):
        if type(result.get(field)) is not int or result[field] != 0:
            raise TestError('Invalid/nonzero ' + field)
    if result.get('control_error') is not False or result.get('requested_ms') != duration_ms:
        raise TestError('Control failure or duration mismatch')
    if type(result.get('elapsed_ms')) is not int or not duration_ms <= result['elapsed_ms'] <= duration_ms + 1000:
        raise TestError('Unexpected measured duration')
    channels = result.get('channels')
    if not isinstance(channels, list) or len(channels) != 3:
        raise TestError('Expected three channel results')
    total = 0
    nominal = duration_ms * 78125 // 1000
    for i, (channel, expected) in enumerate(zip(channels, values), 1):
        if not isinstance(channel, dict) or any(type(v) is not int for v in channel.values()):
            raise TestError('Invalid channel fields')
        count = channel.get('records', -1)
        if (channel.get('channel') != i or channel.get('expected') != expected or
                channel.get('last') != expected or channel.get('first_seq') != 0 or
                channel.get('last_seq') != (count - 1) & 0xffff or
                not nominal * 9 // 10 <= count <= nominal * 11 // 10):
            raise TestError(f'Channel {i}: value/sequence/rate mismatch: {channel}')
        for field in ('value_errors', 'sequence_errors', 'fpga_drop', 'fpga_restart'):
            if channel.get(field) != 0:
                raise TestError(f'Channel {i}: {field}={channel.get(field)}')
        total += count
    if result.get('received') != total or result.get('fpga_tx') != total:
        raise TestError('STM32 record count differs from FPGA TX count')
    return result


def run_test(host, port, duration_ms, values):
    # One request owns the acquisition. Never retry implicitly after a lost reply.
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.settimeout(duration_ms / 1000 + 5)
        with sock.makefile('rb') as stream:
            sock.sendall(('ADCTEST ' + str(duration_ms) + ' ' +
                          ' '.join(f'0x{v:06x}' for v in values) + '\n').encode('ascii'))
            raw = stream.readline(4096)
    if not raw.endswith(b'\n'):
        raise TestError('Disconnected or oversized/incomplete response')
    text = raw.decode('ascii').strip()
    if not text.startswith('OK ADC '):
        raise TestError(text or 'Empty response')
    return validate(json.loads(text[7:]), duration_ms, values)


def raw24(text):
    try:
        value = int(text, 16 if text.lower().startswith('0x') else 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError('Use decimal or 0x hexadecimal') from exc
    if not 0 <= value <= 0xffffff:
        raise argparse.ArgumentTypeError('Expected unsigned 24-bit value')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.100.58')
    parser.add_argument('--port', type=int, default=4244)
    parser.add_argument('--duration-ms', type=int, default=1000, help='100..3000 ms per capture')
    parser.add_argument('--values', nargs=3, type=raw24, default=[0x123456, 0x89abcd, 0xfedcba])
    parser.add_argument('--repeat', type=int, default=1, help='Repeated bounded captures (not gap-free between runs)')
    parser.add_argument('--json', action='store_true', help='Print each complete result as JSON')
    args = parser.parse_args()
    if not 100 <= args.duration_ms <= 3000 or not 1 <= args.repeat <= 1000 or not 1 <= args.port <= 65535:
        parser.error('duration-ms: 100..3000, repeat: 1..1000, port: 1..65535')
    try:
        for run in range(1, args.repeat + 1):
            result = run_test(args.host, args.port, args.duration_ms, args.values)
            if args.json:
                print(json.dumps(result))
            else:
                print(f'PASS {run}/{args.repeat}: {result["received"]} records; CRC/sequence/value/SPI/DMA/drop errors=0')
                for c in result['channels']:
                    print(f'  ADC{c["channel"]}: {c["records"]} records, raw=0x{c["last"]:06x}, '
                          f'seq={c["first_seq"]}..{c["last_seq"]}')
    except (OSError, UnicodeError, ValueError, TestError) as exc:
        print(f'FAIL: {exc}. No automatic retry. The firmware stops each capture within the bounded test; '
              'a control/cleanup error requires checking FPGA connectivity and stopping ADC_CONTROL.', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
