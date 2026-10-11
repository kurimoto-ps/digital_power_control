#!/usr/bin/env python3
"""PC Ethernet -> rev58 STM32 -> MAX10 SPI4 register test (Python stdlib only)."""
import argparse
import socket
import sys


class TestError(RuntimeError):
    pass


class Client:
    def __init__(self, host, port=4244, timeout=5):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.stream = self.sock.makefile('rb')

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.stream.close()
        self.sock.close()

    def request(self, command):
        # Never retry writes: a lost reply may follow a completed FPGA write.
        self.sock.sendall((command + '\n').encode('ascii'))
        raw = self.stream.readline(256)
        if not raw.endswith(b'\n'):
            raise TestError('Disconnected or oversized/incomplete reply')
        try:
            reply = raw.decode('ascii').strip()
        except UnicodeError as exc:
            raise TestError('Non-ASCII reply') from exc
        if reply.startswith('ERR '):
            raise TestError(reply)
        return reply

    def info(self):
        parts = self.request('INFO').split()
        if len(parts) != 7 or parts[:2] != ['OK', 'INFO']:
            raise TestError('Invalid INFO response: ' + ' '.join(parts))
        try:
            values = [int(p, 0) for p in parts[2:]]
        except ValueError as exc:
            raise TestError('Invalid INFO values') from exc
        if values[:4] != [0x4A503538, 0x10000, 1, 16] or not 0 <= values[4] <= 0xffffffff:
            raise TestError('Unexpected FPGA identity/capabilities: ' + repr(values))
        return dict(zip(('device_id', 'version', 'capabilities', 'dummy_count', 'write_count'), values))

    def access(self, op, addr, value=None):
        command = f'{op} 0x{addr:04x}'
        if value is not None:
            command += f' 0x{value:08x}'
        parts = self.request(command).split()
        if len(parts) != 4 or parts[:2] != ['OK', op]:
            raise TestError('Invalid register response: ' + ' '.join(parts))
        try:
            echoed_addr, actual = (int(p, 0) for p in parts[2:])
        except ValueError as exc:
            raise TestError('Invalid register values') from exc
        if echoed_addr != addr or not 0 <= actual <= 0xffffffff:
            raise TestError('Invalid address/value in response')
        if value is not None and actual != value:
            raise TestError(f'Readback mismatch: expected {value:#010x}, got {actual:#010x}')
        return actual

    def read(self, addr):
        return self.access('READ', addr)

    def write(self, addr, value):
        return self.access('WRITE', addr, value)


def run_test(client, expect_reset=False):
    """Destructive dummy-register test; hold one TCP connection throughout."""
    start = client.info()['write_count']
    addresses = list(range(0x100, 0x140, 4))
    if expect_reset:
        if start != 0 or any(client.read(addr) != 0 for addr in addresses):
            raise TestError('Reset values are not zero; reconfigure FPGA before --expect-reset')
    writes = 0
    for addr in addresses:
        for value in (0, 0xffffffff, 0xaaaaaaaa, 0x55555555, 0x12345678, 0x87654321):
            client.write(addr, value)
            writes += 1
    for bit in range(32):
        for value in (1 << bit, 0xffffffff ^ (1 << bit)):
            client.write(0x100, value)
            writes += 1
    # Separate write/read passes detect aliased register addresses.
    expected = {addr: 0xa5000000 | addr for addr in addresses}
    for addr, value in expected.items():
        client.write(addr, value)
        writes += 1
    for addr, value in expected.items():
        if client.read(addr) != value:
            raise TestError(f'Address independence failed at {addr:#06x}')
    for command, status in [('WRITE 0 0xffffffff', 8), ('READ 0x200', 4),
                            ('WRITE 0x200 1', 4), ('READ 0x101', 2),
                            ('WRITE 0x101 1', 2), ('READ 1', 6)]:
        try:
            client.request(command)
        except TestError as exc:
            if str(exc) != f'ERR FPGA 0x{status:02x}':
                raise
        else:
            raise TestError('Expected FPGA rejection: ' + command)
    for addr, value in expected.items():
        if client.read(addr) != value:
            raise TestError(f'Invalid command changed register {addr:#06x}')
    end = client.info()['write_count']
    if (end - start) & 0xffffffff != writes:
        raise TestError(f'WRITE_COUNT mismatch: {start} -> {end}, expected {writes} writes')
    return f'PASS: 16 registers, patterns/walking bits/address independence/errors; {writes} verified writes ({start} -> {end})'


def uint(bits):
    def parse(value):
        try:
            result = int(value, 16 if value.lower().startswith('0x') else 10)
        except ValueError as exc:
            raise argparse.ArgumentTypeError('Use decimal or 0x hexadecimal') from exc
        if not 0 <= result < 1 << bits:
            raise argparse.ArgumentTypeError(f'Expected unsigned {bits}-bit value')
        return result
    return parse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.100.58')
    parser.add_argument('--port', type=int, default=4244)
    parser.add_argument('--timeout', type=float, default=5)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('info')
    sub.add_parser('read').add_argument('address', type=uint(16))
    write = sub.add_parser('write')
    write.add_argument('address', type=uint(16))
    write.add_argument('value', type=uint(32))
    test = sub.add_parser('test', help='Overwrites all 16 FPGA dummy registers')
    test.add_argument('--expect-reset', action='store_true', help='Also require initial all-zero state')
    args = parser.parse_args()
    try:
        with Client(args.host, args.port, args.timeout) as client:
            if args.command == 'info':
                for name, value in client.info().items():
                    print(f'{name}: 0x{value:08x} ({value})')
            elif args.command == 'read':
                print(f'{args.address:#06x}: {client.read(args.address):#010x}')
            elif args.command == 'write':
                actual = client.write(args.address, args.value)
                print(f'WRITE/readback OK: {args.address:#06x} = {actual:#010x}')
            else:
                print(run_test(client, args.expect_reset))
    except (OSError, TestError) as exc:
        print(f'FAIL: {exc}. Writes are not retried; inspect readback/WRITE_COUNT before retrying.', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
