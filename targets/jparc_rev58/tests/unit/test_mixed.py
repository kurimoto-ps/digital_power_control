"""Actual C v2 decoder/stream state plus host APPLY fault injection; no hardware."""
import binascii
import ctypes as C
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import mixed_adc_client as client


class Frame(C.Structure):
    _fields_ = [('sequence', C.c_uint32), ('adc24_sequence', C.c_uint16 * 3),
                ('adc24_age_us', C.c_uint16 * 3), ('adc24_raw', C.c_uint32 * 3),
                ('adc16', C.c_uint16 * 8), ('flags', C.c_uint8), ('adc24_dummy', C.c_uint8)]


class Stats(C.Structure):
    _fields_ = [('last', Frame)] + [(k, C.c_uint32) for k in
        ('frames', 'crc_errors', 'discarded', 'sequence_errors', 'flag_errors', 'value_errors')] + [
        (k, C.c_uint32 * 3) for k in ('known', 'fresh', 'stale', 'max_age')] + [
        ('minimum', C.c_int32 * 8), ('maximum', C.c_int32 * 8), ('expected', C.c_uint32 * 3)] + [
        (k, C.c_uint32) for k in ('required', 'age_limit', 'dummy', 'negate')] + [
        ('pending', C.c_uint8 * 48), ('used', C.c_size_t)]


VALUES = [0x123456, 0x89abcd, 0xfedcba]


def frame(seq=0, adcseq=0, age=2, flags=0x7f, dummy=1, values=VALUES, adc16=None):
    adc16 = adc16 or [0, 1, 0x7fff, 0x8000, 0xffff, 0x1234, 0x5678, 0xabcd]
    p = bytes([0xa5, 0x5a, 2, flags]) + struct.pack('>I6H', seq, *([adcseq] * 3), *([age] * 3))
    p += b''.join(v.to_bytes(3, 'big') for v in values) + struct.pack('>8HB', *adc16, dummy)
    return p + binascii.crc_hqx(p, 0xffff).to_bytes(2, 'big')


class StreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        source = Path(cls.tmp.name) / 'wrap.c'
        source.write_text('#include "mixed_stream.h"\nsize_t size(void){return sizeof(struct mixed_stats);}\n'
                          'int decode(const uint8_t *p, struct mixed_adc_frame *f){return mixed_adc_decode(p,48,f);}\n')
        so = Path(cls.tmp.name) / 'mixed.so'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'lib/fpga_link'), str(source),
                        str(ROOT / 'lib/fpga_link/mixed_stream.c'), '-o', str(so)], check=True)
        cls.lib = C.CDLL(str(so))
        cls.lib.mixed_stream_feed.argtypes = [C.POINTER(Stats), C.c_char_p, C.c_size_t]
        cls.lib.mixed_stream_init.argtypes = [C.POINTER(Stats), C.c_uint32, C.c_uint32,
                                             C.c_uint32, C.c_uint32, C.POINTER(C.c_uint32)]
        cls.lib.decode.argtypes = [C.c_char_p, C.POINTER(Frame)]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.assertEqual(self.lib.size(), C.sizeof(Stats))
        self.s = Stats()
        self.lib.mixed_stream_init(C.byref(self.s), 1, 7, 100, 0, (C.c_uint32 * 3)(*VALUES))

    def feed(self, data, step=13):
        for i in range(0, len(data), step):
            part = data[i:i + step]
            self.lib.mixed_stream_feed(C.byref(self.s), part, len(part))

    def test_spec_vector(self):
        p = bytes.fromhex('A5 5A 02 7F 01 02 03 04 00 11 00 12 00 13 00 01 '
                          '00 02 00 03 12 34 56 FE DC BA 65 43 21 00 01 80 '
                          '00 FF FF 12 34 56 78 9A BC DE F0 01 02 00 25 F5')
        f = Frame()
        self.assertEqual(self.lib.decode(p, C.byref(f)), 0)
        self.assertEqual(f.sequence, 0x01020304)
        self.assertEqual(list(f.adc24_sequence), [17, 18, 19])
        self.assertEqual(list(f.adc24_age_us), [1, 2, 3])
        self.assertEqual(list(f.adc24_raw), [0x123456, 0xfedcba, 0x654321])
        self.assertEqual(list(f.adc16), [1, 0x8000, 0xffff, 0x1234, 0x5678, 0x9abc, 0xdef0, 0x102])

    def test_downsampling_wrap_fragmentation(self):
        self.feed(b''.join(frame(i, (65530 + i * 8) & 65535) for i in range(10000)), 15360)
        self.assertEqual(self.s.frames, 10000)
        self.assertEqual(self.s.sequence_errors + self.s.flag_errors + self.s.value_errors, 0)
        self.assertEqual(self.s.known[0], 10000)
        self.assertEqual(self.s.minimum[3], -32768)
        self.assertEqual(self.s.maximum[4], -1)

    def test_frame_wrap(self):
        self.feed(frame())
        self.s.last.sequence = 0xffffffff
        self.feed(frame(0, 8))
        self.assertEqual(self.s.sequence_errors, 0)

    def test_crc_resync_gap_reserved_and_tail(self):
        bad = bytearray(frame(1, 8)); bad[30] ^= 1
        self.feed(frame() + bad + frame(2, 16) + frame(3, 24, dummy=3) + frame(4, 32) + b'\xa5\x5a')
        self.assertEqual(self.s.frames, 3)
        self.assertEqual(self.s.crc_errors, 1)
        self.assertEqual(self.s.sequence_errors, 2)
        self.assertGreater(self.s.flag_errors, 0)
        self.assertEqual(self.s.used, 2)

    def test_stale_unknown_and_fresh_consistency(self):
        self.feed(frame(0, 0, age=65535, flags=8, values=[0, 0, 0]))
        self.assertEqual(self.s.flag_errors, 0)
        self.feed(frame(1, 7))
        self.feed(frame(2, 7, age=200, flags=15))
        self.assertEqual(list(self.s.stale), [1, 1, 1])
        self.assertEqual(self.s.flag_errors, 0)
        self.feed(frame(3, 8, age=201, flags=15))
        self.assertEqual(self.s.flag_errors, 3)

    def test_wrong_dummy_and_negation_flag(self):
        self.feed(frame(values=[0, 0, 0], flags=0xff))
        self.assertEqual(self.s.value_errors, 3)
        self.assertEqual(self.s.flag_errors, 1)


class FPGA:
    def __init__(self, fault=None):
        self.regs = {0x518: 0, 0x530: 0xffffffff, 0x52c: 0, 0x51c: 0xffffffff}
        self.writes = []
        self.fault = fault
        self.polls = 0

    def read(self, addr):
        if addr == 0x518 and self.regs[addr] & 1 and self.fault != 'timeout':
            self.polls += 1
            if self.polls > 2:
                self.regs[addr] = 0
        return self.regs.get(addr, 0)

    def write(self, addr, value):
        self.writes.append((addr, value))
        self.regs[addr] = value
        if addr == 0x500:
            if self.fault == 'reject':
                self.regs[0x518] = 2
                self.regs[0x52c] += 1
                return
            self.regs[0x530] = (self.regs[0x530] + 1) & 0xffffffff
            self.regs[0x51c] = self.regs[0x530] + (self.fault == 'id')
            self.regs[0x554] = value & 1
            for shadow, applied in zip(client.SHADOW, client.APPLIED):
                self.regs[applied] = self.regs[shadow]
            if self.fault == 'snapshot': self.regs[0x548] ^= 1
            self.regs[0x518] = 1


class ApplyTests(unittest.TestCase):
    def test_atomic_snapshot_wait_and_id_wrap(self):
        c = FPGA()
        self.assertEqual(client.configure(c, 9, 68, 3, 0, 4000, 1), 0)
        self.assertGreater(c.polls, 2)
        self.assertEqual(c.writes[-1], (0x500, 3))
        self.assertEqual(c.regs[0x554], 1)

    def test_invalid_period_no_writes(self):
        for period in (0, 1, 345, 346, 3999):
            c = FPGA()
            with self.assertRaises(client.TestError): client.configure(c, 9, 68, 3, 0, period)
            self.assertFalse(c.writes)

    def test_faults_and_timeout(self):
        from unittest.mock import patch
        for fault, message in (('reject', 'not submitted'), ('id', 'ID/ENABLE mismatch'),
                               ('snapshot', 'Applied values differ')):
            c = FPGA(fault)
            with patch.object(client.time, 'sleep'):
                with self.assertRaisesRegex(client.TestError, message):
                    client.configure(c, 9, 68, 3, 0, 4000)
        with patch.object(client.time, 'monotonic', side_effect=range(100)), patch.object(client.time, 'sleep'):
            with self.assertRaisesRegex(client.TestError, 'timeout'):
                client.configure(FPGA('timeout'), 9, 68, 3, 0, 4000)

    def test_capture_reference_and_metadata(self):
        import argparse
        import json
        from unittest.mock import patch
        class CaptureFPGA(FPGA):
            def request(self, command):
                self.command = command
                return (
                    'OK MIX ' + json.dumps({'pass': True, 'frames': 10000,
                    'adc24': [{'raw': v} for v in VALUES],
                    'adc16': [{'raw': 0xffff, 'min': -2, 'max': 0} for _ in range(8)]}))
        a = argparse.Namespace(delay=9, conversion=68, phase=3, negate=0, period=4000,
                               ms=1000, dummy24=True, required24=7, max_age_us=100,
                               values24=VALUES, expect16=[-1] * 8, tolerance=1)
        c = CaptureFPGA()
        with patch.object(client.time, 'sleep'):
            r = client.capture(c, a)
        self.assertTrue(r['pass'])
        self.assertEqual(r['adc16'][0]['signed'], -1)
        self.assertEqual(r['adc24'][1]['signed'], VALUES[1] - (1 << 24))
        self.assertEqual(r['configuration']['phase'], 3)
        self.assertTrue(c.command.startswith('MIXCAP 1000 1 7 100 '))
        a.tolerance = 0
        with patch.object(client.time, 'sleep'):
            self.assertFalse(client.capture(c, a)['pass'])
        c.regs[0x60] = 1
        c.writes.clear()
        with self.assertRaisesRegex(client.TestError, 'manual'):
            client.capture(c, a)
        self.assertEqual(c.writes, [])

    def test_pending_no_writes(self):
        c = FPGA('timeout'); c.regs[0x518] = 1
        with self.assertRaises(client.TestError): client.configure(c, 9, 68, 3, 0, 4000)
        self.assertFalse(c.writes)


if __name__ == '__main__':
    unittest.main()
