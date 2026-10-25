"""Customer scripts: conversion boundaries, input activity, and write scope."""
import argparse
import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import adc_input_client as adc
import dac_output_client as dac


class FakeClient:
    def __init__(self, active=True, restart=False):
        self.active = active
        self.restart = restart
        self.counts = {}
        self.writes = []
        self.registers = {0x400: 0, 0x480: 0, 0x40c: 3, 0x1c: 16, 0x14: 0xf00}

    def __enter__(self): return self
    def __exit__(self, *args): pass
    def info(self): return {'version': 0x10003, 'capabilities': 15}

    def read(self, address):
        if 0x410 <= address <= 0x418 or 0x430 <= address <= 0x438:
            increment = self.active if address < 0x420 else self.restart
            self.counts[address] = self.counts.get(address, 0) + int(increment)
            return self.counts[address]
        if 0x440 <= address <= 0x448:
            return 0x800000
        return self.registers.get(address, 0)

    def write(self, address, value):
        self.writes.append((address, value))
        self.registers[address] = value & 1 if address in (0x400, 0x480) else value
        if 0x200 <= address <= 0x23c:
            self.registers[address + 0x100] = value


class CustomerTests(unittest.TestCase):
    def run_adc(self, client, initialize=False):
        args = ['adc_input_client', '--encoding', 'signed24', '--count', '1']
        if initialize: args += ['--initialize']
        with patch.object(sys, 'argv', args), patch.object(adc, 'Client', return_value=client), \
                patch.object(adc.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()) as out, \
                contextlib.redirect_stderr(io.StringIO()):
            result = adc.main()
        return result, out.getvalue()

    def test_signed_voltage_boundaries(self):
        for raw, value in [(0, 0), (0x7fffff, 8388607), (0x800000, -8388608), (0xffffff, -1)]:
            self.assertEqual(adc.decode(raw, 'signed24'), value)
        self.assertEqual(adc.decode(0x800000, 'signed24') * 3 / 2**23, -3)
        self.assertEqual(adc.decode(0xffffff, 'unsigned24'), 16777215)
        self.assertEqual(adc.decode(0x800000, 'offset-binary'), 0)

    def test_live_input_csv_and_initialize_scope(self):
        c = FakeClient()
        result, text = self.run_adc(c, True)
        self.assertEqual(result, 0)
        self.assertIn('-3.000000000', text)
        self.assertEqual(c.writes, [(0x400, 0), (0x480, 0), (0x400, 2)])

    def test_stale_input_fails(self):
        c = FakeClient(active=False)
        self.assertEqual(self.run_adc(c)[0], 1)
        self.assertEqual(c.writes, [])

    def test_restarted_frame_fails(self):
        self.assertEqual(self.run_adc(FakeClient(restart=True))[0], 1)

    def test_dummy_mode_rejected_without_writes(self):
        c = FakeClient()
        c.registers[0x480] = 1
        self.assertEqual(self.run_adc(c)[0], 1)
        self.assertEqual(c.writes, [])

    def test_dac_voltage_boundaries_and_quantization(self):
        for text, code in [('0', 0), ('-10', 0x8000), ('10', 0x7fff),
                           ('2.5', 0x2000), ('-2.5', 0xe000),
                           ('0.000152587890625', 1), ('-0.000152587890625', 0xffff)]:
            self.assertEqual(dac.voltage_to_code(text), code)
        self.assertEqual(dac.code_to_voltage(0x8000), -10)
        self.assertEqual(dac.code_to_voltage(0x7fff), 10 - 10 / 32768)
        for code in range(65536):
            self.assertEqual(dac.voltage_to_code(str(dac.code_to_voltage(code))), code)

    def test_dac_invalid_voltage_and_connector(self):
        for text in ['nan', 'inf', '-inf', '10.001', '-10.001', 'hello']:
            with self.assertRaises(argparse.ArgumentTypeError):
                dac.voltage_to_code(text)
        for text in ['CN2', 'CN19', '0', '17', 'CNx']:
            with self.assertRaises(argparse.ArgumentTypeError):
                dac.channel_or_connector(text)
        for channel in range(1, 17):
            self.assertEqual(dac.channel_or_connector(f'CN{channel+2}'), channel)
        with patch.object(sys, 'argv', ['dac', 'set-voltage', 'CN3', '11']), \
                patch.object(dac, 'Client') as client, contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                dac.main()
            client.assert_not_called()

    def test_dac_status_readonly_and_set_scope(self):
        for command, expected in [(['status'], []), (['set', '16', '0xffff'], [(0x23c, 0xffff)]),
                                  (['set-voltage', 'CN3', '-2.5'], [(0x200, 0xe000)]),
                                  (['set-voltage', 'CN18', '10'], [(0x23c, 0x7fff)])]:
            c = FakeClient()
            with patch.object(sys, 'argv', ['dac_output_client'] + command), \
                    patch.object(dac, 'Client', return_value=c), \
                    contextlib.redirect_stdout(io.StringIO()) as out, \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(dac.main(), 0)
            self.assertEqual(c.writes, expected)
            self.assertIn('16,4,IC17,D,CN15-CN18,CN18', out.getvalue())


if __name__ == '__main__':
    unittest.main()
