"""Hardware-independent command validation and operator workflow fault tests."""
import argparse
import ctypes as C
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import board_io_client as client


class Request(C.Structure):
    _fields_ = [('kind', C.c_int), ('mask', C.c_uint16), ('data', C.c_uint8 * 32), ('size', C.c_size_t)]


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        so = Path(cls.tmp.name) / 'io.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror', '-std=c11',
                        '-I' + str(ROOT / 'lib/fpga_link'), str(ROOT / 'lib/plc/board_io_protocol.c'),
                        str(ROOT / 'lib/fpga_link/spi4_protocol.c'), '-o', str(so)], check=True)
        cls.lib = C.CDLL(str(so)); cls.lib.board_io_parse.argtypes = [C.c_char_p]
        cls.lib.board_io_parse.restype = Request

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()

    def test_masks_and_binary_serial(self):
        for bit in range(16):
            r = self.lib.board_io_parse(f'PLC WRITE {1 << bit}'.encode())
            self.assertEqual((r.kind, r.mask), (3, 1 << bit))
        r = self.lib.board_io_parse(b'UART SEND 00ff0D0a55aa')
        self.assertEqual((r.kind, r.size, bytes(r.data[:r.size])), (7, 6, bytes.fromhex('00ff0d0a55aa')))
        for text, kind in [('PLC READ', 2), ('UART STATUS', 4), ('UART CLEAR', 5), ('UART READ', 6)]:
            self.assertEqual(self.lib.board_io_parse(text.encode()).kind, kind)

    def test_reject_oversize_and_extra_tokens(self):
        for text in ['PLC WRITE -1', 'PLC WRITE 65536', 'PLC WRITE 0xffff extra',
                     'PLC READ extra', 'UART SEND ffzz', 'UART SEND 123', 'UART SEND',
                     'UART SEND ' + 'ab' * 33, 'UART SEND aa bb', 'UART UNKNOWN', 'PLC']:
            self.assertEqual(self.lib.board_io_parse(text.encode()).kind, 1, text)
        self.assertEqual(self.lib.board_io_parse(b'INFO').kind, 0)
        self.assertEqual(self.lib.board_io_parse(('UART SEND ' + 'ab'*32).encode()).size, 32)


class FakeBoard:
    host = 'model'
    def __init__(self): self.mask = 0; self.inputs = 0; self.writes = []
    def read(self): return dict(input_raw=self.inputs, output_raw=self.mask, output_latch=self.mask)
    def write(self, mask): self.mask = mask; self.writes.append(mask); return self.read()


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.path = Path(self.tmp.name) / 'report.json'
        self.b = FakeBoard()
    def tearDown(self): self.tmp.cleanup()

    def operator(self, prompt):
        if 'turn ONLY' in prompt:
            bit = int(prompt.split('bit ')[1].split()[0]); self.b.inputs = 1 << bit
        elif 'Turn PLC DO' in prompt: self.b.inputs = 0
        return f'{self.b.mask:04x}'

    def test_all_32_steps_and_cleanup(self):
        with patch('builtins.print'):
            self.assertTrue(client.mapping_test(self.b, 'both', self.path, self.operator))
        r = json.loads(self.path.read_text())
        self.assertEqual(len(r['tests']), 32)
        self.assertTrue(r['complete'] and r['passed'] and r['outputs_cleared'])
        self.assertEqual([v for v in self.b.writes if v], [1 << i for i in range(16)])
        self.assertEqual(self.b.mask, 0)

    def test_wrong_channel_fails_report(self):
        def wrong(prompt):
            response = self.operator(prompt)
            if 'turn ONLY' in prompt: self.b.inputs ^= 3
            return response
        with patch('builtins.print'):
            self.assertFalse(client.mapping_test(self.b, 'in', self.path, wrong))
        self.assertFalse(json.loads(self.path.read_text())['passed'])

    def test_abort_clears_active_output_and_saves_partial(self):
        def stop(prompt):
            if self.b.mask: raise KeyboardInterrupt
            return '0'
        with patch('builtins.print'), self.assertRaises(KeyboardInterrupt):
            client.mapping_test(self.b, 'out', self.path, stop)
        r = json.loads(self.path.read_text())
        self.assertFalse(r['complete']); self.assertTrue(r['outputs_cleared'])
        self.assertEqual(self.b.mask, 0)

    def test_disconnected_cleanup_is_not_reported_as_pass(self):
        original = self.b.write
        def disconnect(mask):
            if self.b.mask and not mask: raise client.TestError('lost link')
            return original(mask)
        self.b.write = disconnect
        with patch('builtins.print'), self.assertRaises(client.TestError):
            client.mapping_test(self.b, 'out', self.path, self.operator)
        r = json.loads(self.path.read_text())
        self.assertFalse(r['outputs_cleared']); self.assertFalse(r['passed'])

    def test_payload_and_receive_errors(self):
        self.assertEqual(client.payload(argparse.Namespace(hex='00 ff 0d 0a', text=None)), b'\x00\xff\r\n')
        for hextext in ('', 'gg', 'aa'*33):
            with self.assertRaises(client.TestError): client.payload(argparse.Namespace(hex=hextext, text=None))
        class UART:
            def __init__(self, error): self.error = error; self.n = 0
            def command(self, command):
                if command == 'UART CLEAR': return {}
                self.n += 1
                return dict(hex='55aa' if self.n == 1 else '', buffered=0,
                            error_flags=self.error, rx_dropped=0)
        for error in (0, 1):
            with patch.object(client.time, 'monotonic', side_effect=[0, 0, 2]), patch.object(client.time, 'sleep'), patch('builtins.print'):
                r = client.uart_receive(UART(error), 1, b'\x55\xaa')
            self.assertEqual(r['passed'], not error)


if __name__ == '__main__': unittest.main()
