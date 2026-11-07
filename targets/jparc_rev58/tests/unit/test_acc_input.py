"""Actual C ACC read path and static-level host diagnostics; no hardware."""
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
import acc_input_client as host
import mixed_adc_client
TRANSFER = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint8), C.POINTER(C.c_uint8))
GPIO = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint32))


def reply(fpga=0, before=0, after=0):
    return 'OK ACC ' + json.dumps(dict(version=0x20001, capabilities=127,
        fpga_raw=fpga, stm32_before=before, stm32_after=after, stm32_mask=6))


class FirmwareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(); so = Path(cls.tmp.name) / 'acc.so'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-std=c11', '-shared', '-fPIC',
                        str(ROOT / 'lib/fpga_link/acc_monitor.c'),
                        str(ROOT / 'lib/fpga_link/spi4_protocol.c'), '-o', str(so)], check=True)
        cls.lib = C.CDLL(str(so))
        cls.lib.acc_monitor_command.argtypes = [C.c_char_p, C.c_char_p, TRANSFER, C.c_void_p, GPIO, C.c_void_p]
        cls.lib.acc_monitor_command.restype = C.c_bool
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def setUp(self):
        self.regs = {0: 0x4a503538, 4: 0x20001, 8: 127, 0x24: 0, 0x28: 12000, 0x2c: 7}
        self.frames = []; self.gpio_calls = 0; self.gpio_values = [0, 0]
        self.gpio_fail = 0; self.status = 0; self.bad_prefix = False; self.transport_error = False
        self.transfer = TRANSFER(self.exchange); self.gpio = GPIO(self.read_gpio)
    def exchange(self, context, tx, rx):
        data = bytes(tx[:8]); self.frames.append(data)
        if self.transport_error: return -5
        self.assertEqual(data[0], 3)  # Read-only diagnostic, never writes.
        addr = int.from_bytes(data[1:3], 'big')
        answer = bytes([int(self.bad_prefix), 0, 0, self.status if addr == getattr(self, "error_addr", 0x24) else 0])
        answer += self.regs[addr].to_bytes(4, 'big')
        for i, b in enumerate(answer): rx[i] = b
        return 0
    def read_gpio(self, context, value):
        self.gpio_calls += 1
        if self.gpio_calls == self.gpio_fail: return -5
        value[0] = self.gpio_values[(self.gpio_calls - 1) % 2]
        return 0
    def command(self, line='ACC READ'):
        out = C.create_string_buffer(2048)
        handled = self.lib.acc_monitor_command(line.encode(), out, self.transfer, None, self.gpio, None)
        return handled, out.value.decode()
    def test_all_levels_and_wire_frame(self):
        for raw in range(8):
            self.regs[0x24] = raw; self.gpio_values = [raw & 6] * 2
            handled, text = self.command(); self.assertTrue(handled)
            r = host.decode(text); self.assertEqual(r['fpga_raw'], raw)
            self.assertTrue(r['shared_levels_equal'])
            self.assertEqual(self.frames[-1], bytes.fromhex('03 00 24 00 00 00 00 00'))
    def test_old_version_missing_capability_never_reads_new_register(self):
        for version, caps in [(0x10003, 15), (0x20000, 63), (0x20000, 127), (0x20001, 63), (0x20003, 127)]:
            self.regs[4] = version; self.regs[8] = caps; self.frames.clear()
            self.assertIn('ERR ACC_REQUIRES', self.command()[1])
            self.assertEqual([int.from_bytes(p[1:3], 'big') for p in self.frames], [0, 4, 8])
            self.assertEqual(self.gpio_calls, 0)
    def test_capability_bit_not_exact_capability_value(self):
        self.regs[8] = 0x40
        self.assertTrue(self.command()[1].startswith('OK ACC '))
    def test_gpio_spi_and_reserved_errors(self):
        self.status = 4
        self.assertIn('ERR FPGA', self.command()[1])
        self.status = 0; self.regs[0x24] = 8
        self.assertIn('ERR ACC_RESERVED_BITS', self.command()[1])
        self.regs[0x24] = 0; self.gpio_fail = self.gpio_calls + 1
        self.assertIn('ERR ACC_GPIO', self.command()[1])
        self.gpio_fail = 0; self.transport_error = True
        self.assertTrue(self.command()[1].startswith('ERR '))
        self.transport_error = False; self.bad_prefix = True
        self.assertTrue(self.command()[1].startswith('ERR '))
    def test_non_atomic_mismatch_is_reported_not_a_transport_error(self):
        self.regs[0x24] = 2; self.gpio_values = [0, 6]
        r = host.decode(self.command()[1])
        self.assertFalse(r['shared_levels_equal'])
        self.assertEqual((r['stm32_before'], r['stm32_after']), (0, 6))
    def test_v202_clock_registers_and_gating(self):
        self.regs.update({4: 0x20002, 8: 255})
        r = host.decode(self.command()[1])
        self.assertEqual(r['clock']['count_per_ms'], 12000)
        self.assertEqual(r['clock']['estimated_hz'], 12000000)
        self.assertTrue(host.clock_match(r, 'on'))
        self.assertEqual(self.frames[-2:], [bytes.fromhex('03 00 2c 00 00 00 00 00'),
                                           bytes.fromhex('03 00 28 00 00 00 00 00')])
        for version, caps in [(0x20001, 255), (0x20002, 127)]:
            self.frames.clear(); self.regs.update({4: version, 8: caps})
            r = host.decode(self.command()[1])
            self.assertFalse(r['clock']['supported'])
            self.assertFalse(any(int.from_bytes(p[1:3], 'big') in (0x28, 0x2c) for p in self.frames))
            with self.assertRaises(host.TestError): host.clock_match(r, 'off')

    def test_clock_waiting_stopped_wrong_frequency_and_boundaries(self):
        self.regs.update({4: 0x20002, 8: 255})
        for count, status, on, off in [(0, 0, False, False), (0, 1, False, True),
                (6000, 3, False, False), (10799, 3, False, False),
                (10800, 7, True, False), (12000, 7, True, False),
                (13200, 7, True, False), (13201, 3, False, False)]:
            self.regs.update({0x28: count, 0x2c: status})
            r = host.decode(self.command()[1])
            self.assertEqual(host.clock_match(r, 'on'), on)
            self.assertEqual(host.clock_match(r, 'off'), off)
        # Window rollover between status and count: status is authoritative.
        self.regs.update({0x28: 0, 0x2c: 7})
        self.assertTrue(host.clock_match(host.decode(self.command()[1]), 'on'))

    def test_clock_reserved_bits_and_spi_errors(self):
        self.regs.update({4: 0x20002, 8: 255})
        for addr, bad in [(0x28, 0x20000), (0x2c, 8)]:
            saved = self.regs[addr]; self.regs[addr] = bad
            self.assertIn('ERR ACC_CLOCK_RESERVED_BITS', self.command()[1])
            self.regs[addr] = saved
            self.error_addr = addr; self.status = 4
            self.assertIn('ERR FPGA', self.command()[1])
            self.status = 0

    def test_command_rejection_without_access(self):
        for line in ['ACC WRITE 1', 'ACC READ 1', 'ACC']:
            self.assertIn('ERR COMMAND', self.command(line)[1])
        self.assertFalse(self.command('INFO')[0]); self.assertEqual(self.frames, [])


class HostTests(unittest.TestCase):
    def test_masks_and_shared_comparison(self):
        r = host.decode(reply(3, 2, 2))
        self.assertTrue(host.static_match(r, 2, 6))  # Uncontrolled optical bit excluded.
        self.assertFalse(host.static_match(r, 2, 7))
        self.assertIsNone(r['signals']['ACC_12MHZ']['stm32_pin'])
        self.assertEqual(r['signals']['ACC_96KHZ']['stm32_pin'], 'PA0')
        self.assertFalse(host.static_match(host.decode(reply(2, 2, 0)), 2, 6))
    def test_malformed_reply(self):
        for data in ['ERR FPGA', 'OK ACC {}', reply(8), reply(0, 1, 0)]:
            with self.assertRaises(host.TestError): host.decode(data)
    def test_coax_workflow_and_abort_report(self):
        class Model:
            host = 'model'; value = 0
            def read(self): return host.decode(reply(self.value | 1, self.value, self.value))
        m = Model()
        expected = iter([0, 2, 0, 4, 0, 6, 0])
        def operator(prompt): m.value = next(expected); return ''
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'report.json'
            with patch.object(host.time, 'sleep'), patch('builtins.print'):
                self.assertTrue(host.coax_test(m, path, operator))
            report = json.loads(path.read_text())
            self.assertEqual(len(report['steps']), 7)
            self.assertEqual(report['tested_fpga_mask'], 6)
            self.assertTrue(report['not_tested'])
            with patch('builtins.print'), self.assertRaises(KeyboardInterrupt):
                host.coax_test(m, path, lambda prompt: 'q')
            self.assertFalse(json.loads(path.read_text())['complete'])
    def test_clock_workflow_waits_and_saves_report(self):
        class Model:
            host = 'clock-model'; running = False
            def read(self):
                data = json.loads(reply()[7:])
                data.update(version=0x20002, capabilities=255, clock_supported=True,
                            clock_status=7 if self.running else 1,
                            clock_count=12000 if self.running else 0)
                return host.decode('OK ACC ' + json.dumps(data))
        m = Model()
        def operator(prompt): m.running = prompt.startswith('Apply'); return ''
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'clock.json'
            with patch.object(host.time, 'sleep') as sleep, patch('builtins.print'):
                self.assertTrue(host.clock_test(m, path, operator))
            self.assertEqual(sum(c.args == (0.003,) for c in sleep.call_args_list), 3)
            report = json.loads(path.read_text())
            self.assertTrue(report['complete'] and report['passed'])
            self.assertEqual(len(report['steps']), 3)
            with patch('builtins.print'), self.assertRaises(KeyboardInterrupt):
                host.clock_test(m, path, lambda prompt: 'q')
            self.assertFalse(json.loads(path.read_text())['complete'])

    def test_v202_missing_clock_fields_is_not_success(self):
        data = json.loads(reply()[7:]); data.update(version=0x20002, capabilities=255)
        with self.assertRaises(host.TestError): host.decode('OK ACC ' + json.dumps(data))

    def test_mixed_adc_probe_accepts_both_versions(self):
        class Model:
            def __init__(self, version, caps): self.version, self.caps = version, caps
            def info(self): return dict(version=self.version, capabilities=self.caps)
            def read(self, addr): return {0x40c: 3, 0x560: 8}[addr]
        for version, caps in [(0x20000, 63), (0x20001, 127), (0x20002, 255)]:
            mixed_adc_client.probe(Model(version, caps))
        with self.assertRaises(host.TestError): mixed_adc_client.probe(Model(0x20001, 63))


if __name__ == '__main__': unittest.main()
