import ctypes as C
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('adc_client', ROOT / 'scripts/adc_dummy_client.py')
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


class Channel(C.Structure):
    _fields_ = [(key, C.c_uint32) for key in ('records', 'value_errors', 'sequence_errors',
                                             'first_sequence', 'last_sequence', 'last_value')]


class Stats(C.Structure):
    _fields_ = [('channel', Channel * 3), ('crc_errors', C.c_uint32),
                ('header_errors', C.c_uint32), ('discarded_bytes', C.c_uint32),
                ('pending', C.c_uint8 * 8), ('pending_size', C.c_size_t),
                ('expected', C.c_uint32 * 3)]


def crc_reference(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
    return crc


def record(ch, seq, raw):
    data = bytes([0xa0 + ch]) + seq.to_bytes(2, 'big') + raw.to_bytes(3, 'big')
    return data + crc_reference(data).to_bytes(2, 'big')


class DecoderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        so = Path(cls.tmp.name) / 'decoder.so'
        subprocess.run(['cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        str(ROOT / 'lib/fpga_link/adc_stream.c'), '-o', str(so)], check=True)
        cls.lib = C.CDLL(str(so))
        cls.lib.adc_stream_init.argtypes = [C.POINTER(Stats), C.POINTER(C.c_uint32)]
        cls.lib.adc_stream_feed.argtypes = [C.POINTER(Stats), C.c_char_p, C.c_size_t]
        cls.lib.adc_crc16.argtypes = [C.c_char_p, C.c_size_t]
        cls.lib.adc_crc16.restype = C.c_uint16

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.values = [0x123456, 0x89abcd, 0xfedcba]
        self.stats = Stats()
        self.lib.adc_stream_init(C.byref(self.stats), (C.c_uint32 * 3)(*self.values))

    def feed(self, data):
        self.lib.adc_stream_feed(C.byref(self.stats), data, len(data))

    def test_published_vectors(self):
        for text in ('a1000012345674f9', 'a2000089abcd08d6',
                     'a30000fffffff3b4', 'a1ffff000001abb5'):
            data = bytes.fromhex(text)
            self.assertEqual(self.lib.adc_crc16(data[:6], 6), int.from_bytes(data[6:], 'big'))
        self.assertEqual(self.lib.adc_crc16(b'123456789', 9), 0x29b1)

    def test_fragmentation_channels_and_sequence_wrap(self):
        # Three independent sequences, varying order; cross 65535 -> 0.
        data = b''.join(record(ch, seq & 0xffff, self.values[ch - 1])
                        for seq in range(65538) for ch in (3, 1, 2))
        for pos in range(0, len(data), 16381):
            self.feed(data[pos:pos + 16381])
        for c in self.stats.channel:
            self.assertEqual(c.records, 65538)
            self.assertEqual(c.last_sequence, 1)
            self.assertEqual(c.sequence_errors + c.value_errors, 0)
        self.assertEqual(self.stats.crc_errors + self.stats.discarded_bytes + self.stats.pending_size, 0)

    def test_resync_corruption_and_incomplete_tail(self):
        broken = bytearray(record(1, 0, self.values[0]))
        broken[-1] ^= 1
        self.feed(b'bad' + broken + record(1, 1, self.values[0]) + b'\xa2\x00')
        self.assertGreater(self.stats.crc_errors, 0)
        self.assertGreater(self.stats.discarded_bytes, 0)
        self.assertEqual(self.stats.channel[0].records, 1)
        self.assertEqual(self.stats.channel[0].sequence_errors, 1)
        self.assertEqual(self.stats.pending_size, 2)

    def test_duplicate_missing_wrong_value(self):
        for seq in (0, 0, 2):
            self.feed(record(2, seq, 0))
        self.assertEqual(self.stats.channel[1].sequence_errors, 2)
        self.assertEqual(self.stats.channel[1].value_errors, 3)
        self.assertEqual(self.stats.crc_errors, 0)


def good_result():
    return {'pass': True, 'requested_ms': 1000, 'elapsed_ms': 1002,
            'received': 234375, 'fpga_tx': 234375, 'crc_errors': 0, 'header_errors': 0,
            'discarded_bytes': 0, 'trailing_bytes': 0, 'dma_errors': 0, 'dma_overruns': 0,
            'spi_error_flags': 0, 'error': 0, 'control_error': False, 'cleanup_error': 0,
            'channels': [dict(channel=i + 1, records=78125, expected=v, last=v,
                              first_seq=0, last_seq=(78125 - 1) & 0xffff,
                              value_errors=0, sequence_errors=0, fpga_drop=0, fpga_restart=0)
                         for i, v in enumerate([0x123456, 0x89abcd, 0xfedcba])]}


class HostTests(unittest.TestCase):
    def test_fail_closed_metrics(self):
        values = [0x123456, 0x89abcd, 0xfedcba]
        self.assertTrue(client.validate(good_result(), 1000, values)['pass'])
        for key in ('crc_errors', 'header_errors', 'discarded_bytes', 'trailing_bytes',
                    'dma_errors', 'dma_overruns', 'spi_error_flags', 'error', 'cleanup_error'):
            data = good_result(); data[key] = 1
            with self.subTest(key=key), self.assertRaises(client.TestError):
                client.validate(data, 1000, values)
        for key in ('value_errors', 'sequence_errors', 'fpga_drop', 'fpga_restart', 'first_seq'):
            data = good_result(); data['channels'][1][key] = 1
            with self.subTest(key=key), self.assertRaises(client.TestError):
                client.validate(data, 1000, values)
        data = good_result(); data['fpga_tx'] += 1
        with self.assertRaises(client.TestError):
            client.validate(data, 1000, values)

    def test_tcp_fragmented_json_response(self):
        errors = []
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0)); listener.listen(1); listener.settimeout(5)
            def serve():
                try:
                    with listener.accept()[0] as conn:
                        conn.settimeout(5)
                        with conn.makefile('rb') as stream:
                            self.assertEqual(stream.readline(), b'ADCTEST 1000 0x123456 0x89abcd 0xfedcba\n')
                            data = ('OK ADC ' + json.dumps(good_result()) + '\n').encode()
                            conn.sendall(data[:7]); conn.sendall(data[7:])
                except Exception as exc:
                    errors.append(exc)
            worker = threading.Thread(target=serve, daemon=True); worker.start()
            self.assertTrue(client.run_test('127.0.0.1', listener.getsockname()[1], 1000,
                                           [0x123456, 0x89abcd, 0xfedcba])['pass'])
            worker.join(5)
            self.assertFalse(worker.is_alive())
            if errors: raise errors[0]


if __name__ == '__main__':
    unittest.main()
