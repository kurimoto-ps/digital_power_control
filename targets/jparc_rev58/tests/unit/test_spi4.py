"""Real portable firmware command handler + independent FPGA model + loopback PC client."""
import ctypes as C
import importlib.util
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('spi4_client', ROOT / 'scripts/spi4_client.py')
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)
CALLBACK = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint8), C.POINTER(C.c_uint8))


class Firmware:
    def __init__(self, library):
        self.lib = library
        self.regs = {0: 0x4a503538, 4: 0x10000, 8: 1, 12: 16, 16: 0}
        self.regs.update({a: 0 for a in range(0x100, 0x140, 4)})
        self.frames = []
        self.error = 0
        self.bad_prefix = False
        self.drop_write = False
        self.callback = CALLBACK(self.transfer)

    def transfer(self, ctx, tx, rx):
        frame = bytes(tx[:8])
        self.frames.append(frame)
        if self.error:
            return self.error
        op, addr, value = frame[0], int.from_bytes(frame[1:3], 'big'), int.from_bytes(frame[4:], 'big')
        status = (0 if op in (2, 3) else 1) | (2 if addr % 4 else 0)
        if addr not in (0, 4, 8, 12, 16) and not 0x100 <= addr < 0x140:
            status |= 4
        if op == 2 and addr in (0, 4, 8, 12, 16):
            status |= 8
        result = 0
        if not status:
            if op == 2:
                if not self.drop_write:
                    self.regs[addr] = value
                    self.regs[16] = (self.regs[16] + 1) & 0xffffffff
            else:
                result = self.regs[addr]
        response = bytes([1 if self.bad_prefix else 0, 0, 0, status]) + result.to_bytes(4, 'big')
        for i, b in enumerate(response):
            rx[i] = b
        return 0

    def command(self, text):
        line = C.create_string_buffer(text.encode('ascii'))
        reply = C.create_string_buffer(2048)
        self.lib.spi4_command(line, reply, self.callback, None)
        return reply.value.decode('ascii')


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        so = Path(cls.tmp.name) / 'protocol.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', str(ROOT / 'lib/fpga_link/spi4_protocol.c'),
                        '-o', str(so)], check=True)
        cls.library = C.CDLL(str(so))
        cls.library.spi4_command.argtypes = [C.c_char_p, C.c_char_p, CALLBACK, C.c_void_p]
        cls.library.spi4_command.restype = None
        cls.library.spi4_encode.argtypes = [C.POINTER(C.c_uint8), C.c_uint8, C.c_uint16, C.c_uint32]
        cls.library.spi4_encode.restype = None

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.fw = Firmware(self.library)

    def test_reference_vectors_and_write_readback(self):
        self.assertEqual(self.fw.command('READ 0'), 'OK READ 0x0000 0x4a503538\n')
        self.assertEqual(self.fw.frames[0], bytes.fromhex('03 00 00 00 00 00 00 00'))
        self.fw.frames.clear()
        self.assertEqual(self.fw.command('WRITE 0x100 0x12345678'), 'OK WRITE 0x0100 0x12345678\n')
        self.assertEqual(self.fw.frames[-2:], [bytes.fromhex('02 01 00 00 12 34 56 78'),
                                              bytes.fromhex('03 01 00 00 00 00 00 00')])
        self.assertEqual(self.fw.regs[16], 1)

    def test_command_rejections_never_touch_spi(self):
        for text in ('', ' ', 'INFO x', 'READ', 'read 0', 'WRITE 256', 'WRITE 256 1 x',
                     'READ -1', 'READ +1', 'READ 65536', 'READ 0x', 'READ 12z',
                     'WRITE 256 4294967296', 'WRITE 256 -1', 'READ 999999999999999999999'):
            with self.subTest(text=text):
                self.assertEqual(self.fw.command(text), 'ERR COMMAND\n')
        self.assertEqual(self.fw.frames, [])

    def test_numeric_bounds_whitespace_and_case(self):
        self.assertEqual(self.fw.command(' \tWRITE\t0X013C 4294967295\r'),
                         'OK WRITE 0x013c 0xffffffff\n')
        self.assertEqual(self.fw.command('READ 65535'), 'ERR FPGA 0x06\n')

    def test_fpga_errors_preserve_state(self):
        initial = self.fw.regs.copy()
        for command, status in [('WRITE 0 1', 8), ('READ 0x200', 4),
                                ('WRITE 0x200 1', 4), ('READ 0x101', 2),
                                ('WRITE 0x101 1', 2), ('READ 1', 6)]:
            self.assertEqual(self.fw.command(command), f'ERR FPGA 0x{status:02x}\n')
        self.assertEqual(self.fw.regs, initial)

    def test_disconnected_miso_and_wrong_version_block_write(self):
        for addr in (0, 4):
            fw = Firmware(self.library)
            fw.regs[addr] = 0
            self.assertTrue(fw.command('WRITE 256 0').startswith('ERR ID '))
            self.assertFalse(any(f[0] == 2 for f in fw.frames))
        self.fw.bad_prefix = True
        self.assertEqual(self.fw.command('READ 0'), 'ERR FRAME\n')

    def test_transport_failure_is_not_retried(self):
        self.fw.error = -5
        self.assertEqual(self.fw.command('WRITE 256 1'), 'ERR TRANSPORT -5\n')
        self.assertEqual(len(self.fw.frames), 1)

    def test_header_acceptance_is_not_write_success(self):
        self.fw.drop_write = True
        self.assertEqual(self.fw.command('WRITE 256 1'), 'ERR VERIFY 0x0100 0x00000001 0x00000000\n')
        self.assertEqual(len([f for f in self.fw.frames if f[0] == 2]), 1)

    def with_server(self, action, responder=None):
        errors = []
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(5)
            def serve():
                try:
                    with listener.accept()[0] as conn:
                        conn.settimeout(5)
                        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                        with conn.makefile('rb') as stream:
                            while line := stream.readline(96):
                                response = (responder or self.fw.command)(line.decode().rstrip('\n'))
                                if response is None:
                                    return
                                data = response.encode('ascii')
                                # Deliberately split responses across socket writes.
                                conn.sendall(data[:1])
                                conn.sendall(data[1:])
                except Exception as exc:
                    errors.append(exc)
            worker = threading.Thread(target=serve, daemon=True)
            worker.start()
            try:
                with client.Client('127.0.0.1', listener.getsockname()[1]) as pc:
                    action(pc)
            finally:
                worker.join(6)
            self.assertFalse(worker.is_alive(), 'loopback server did not stop')
            if errors:
                raise errors[0]

    def test_end_to_end_host_suite_and_reset(self):
        self.with_server(lambda pc: self.assertIn('176 verified writes', client.run_test(pc, True)))
        self.assertEqual(self.fw.regs[16], 176)

    def test_v13_info_and_legacy_dummy_suite_do_not_write_dac(self):
        self.fw.regs[4] = 0x10003
        self.fw.regs[8] = 15
        self.with_server(lambda pc: self.assertIn('176 verified writes', client.run_test(pc)))
        self.assertFalse(any(f[0] == 2 and 0x200 <= int.from_bytes(f[1:3], 'big') <= 0x23c
                             for f in self.fw.frames))

    def test_v13_control_clear_masked_readback(self):
        # Special fake only for this self-clearing register, after normal identity reads.
        self.fw.regs[4] = 0x10003
        original = self.fw.transfer
        def transfer(ctx, tx, rx):
            if int.from_bytes(bytes(tx[1:3]), 'big') == 0x400:
                for i in range(8): rx[i] = 0
                if tx[0] == 3: rx[7] = 1
                return 0
            return original(ctx, tx, rx)
        self.fw.callback = CALLBACK(transfer)
        self.assertEqual(self.fw.command('WRITE 0x0400 3'), 'OK WRITE 0x0400 0x00000001\n')

    def test_write_count_wrap(self):
        self.fw.regs[16] = 0xfffffff0
        self.with_server(lambda pc: self.assertIn('176 verified writes', client.run_test(pc)))
        self.assertEqual(self.fw.regs[16], 160)

    def test_reset_check_failure(self):
        self.fw.regs[0x100] = 1
        def check(pc):
            with self.assertRaisesRegex(client.TestError, 'Reset values'):
                client.run_test(pc, True)
        self.with_server(check)

    def test_lost_write_reply_does_not_retry(self):
        def drop_reply(command):
            self.fw.command(command)
            return None
        def check(pc):
            with self.assertRaisesRegex(client.TestError, 'Disconnected'):
                pc.write(0x100, 0x12345678)
        self.with_server(check, drop_reply)
        self.assertEqual(self.fw.regs[0x100], 0x12345678)
        self.assertEqual(self.fw.regs[16], 1)

    def test_dac_command_waits_for_initialized_and_sent_code(self):
        class FakeDAC:
            def __init__(self): self.writes = []; self.polls = 0
            def info(self): return {'capabilities': 15}
            def write(self, addr, code): self.writes.append((addr, code))
            def read(self, addr):
                if addr == 0x001c: return 16
                if addr == 0x0014:
                    self.polls += 1
                    return (1 << 3) if self.polls == 1 else (1 << 11)
                if addr == 0x033c: return 0xabcd
                raise AssertionError(addr)
        dac = FakeDAC()
        client.dac_set(dac, 16, 0xabcd)
        self.assertEqual(dac.writes, [(0x023c, 0xabcd)])
        self.assertEqual(dac.polls, 2)

    def test_host_rejects_corrupt_reply(self):
        for reply in ('OK READ 0x100 1\n', 'OK READ 0 0x100000000\n', 'garbage\n'):
            def check(pc):
                with self.assertRaises(client.TestError):
                    pc.read(0)
            self.with_server(check, lambda _: reply)


if __name__ == '__main__':
    unittest.main()
