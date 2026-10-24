#!/usr/bin/env python3
"""Check a trusted local rev58 build against clock, RAM and supplied netlist constraints."""
import argparse
import json
import pickle
import re
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build_dir', type=Path)
parser.add_argument('--zephyr-base', type=Path,
                    default=Path(__file__).resolve().parents[4] / 'zephyr')
args = parser.parse_args()
sys.path.insert(0, str(args.zephyr_base / 'scripts/dts/python-devicetree/src'))
edt = pickle.loads((args.build_dir / 'zephyr/edt.pickle').read_bytes())
config = (args.build_dir / 'zephyr/.config').read_text()
node = edt.label2node

def require(condition, message):
    if not condition:
        raise SystemExit('FAIL: ' + message)

def value(label, prop):
    return node[label].props[prop].val

require('CONFIG_BOARD_JPARC_REV58=y' in config, 'wrong board')
require('CONFIG_STM32H7_DUAL_CORE=y' not in config, 'M4 wakeup enabled')
require('CONFIG_DCACHE=y' not in config, 'bring-up D-cache must be disabled')
require(value('pwr', 'power-supply') == 'smps-direct', 'supply')
require(value('clk_hse', 'clock-frequency') == 25000000 and
        value('clk_hse', 'hse-bypass'), 'HSE oscillator')
require(value('rcc', 'clock-frequency') == 400000000, 'SYSCLK')
require(value('rcc', 'hpre') == 2, 'HCLK')
for divider in ('d1ppre', 'd2ppre1', 'd2ppre2', 'd3ppre'):
    require(value('rcc', divider) == 2, 'APB divider ' + divider)
pll_hz = value('clk_hse', 'clock-frequency') // value('pll', 'div-m') * value('pll', 'mul-n')
require(pll_hz // value('pll', 'div-p') == 400000000, 'PLL P calculation')
require(pll_hz // value('pll', 'div-q') == 100000000, 'PLL Q calculation')
require(edt.chosen_node('zephyr,sram') == node['sram0'], 'startup SRAM')
# stm32h7_clock.h: HSE=4, SPI45 selector at D2CCIP1R[18:16], selection 5.
spi4_clocks = value('spi4', 'clocks')
require(len(spi4_clocks) == 2 and spi4_clocks[1].data ==
        {'bus': 4, 'bits': (5 << 24) | (2 << 21) | (16 << 16) | 0x50},
        'SPI4 must use the supported HSE 25 MHz domain')

require(node['sdram2'].regs[0].addr == 0xd0000000 and
        node['sdram2'].regs[0].size == 8 * 1024 * 1024, 'SDRAM region')
require(value('sdram', 'refresh-rate') == 1542, 'refresh count at 100 MHz')
bank = node['sdram'].children['bank@1']
require(bank.regs[0].addr == 1, 'SDRAM BANK2')
require(bank.props['st,sdram-timing'].val == [2, 8, 5, 7, 3, 2, 2], 'SDRAM timing')

netlist = json.loads((Path(__file__).resolve().parents[1] /
                     'docs/hardware/rev58/rev58_pin_connections.json').read_text())
pins = {p['pin_function'].rsplit('_', 1)[0]: p for p in netlist['pins'] if p['device'] == 'U1'}
seen = set()
for label in ('fmc', 'mac', 'mdio', 'spi1', 'spi2', 'spi3', 'spi4'):
    n = node[label]
    if n.status != 'okay':
        continue
    require(bool(n.pinctrls), 'missing pinctrl for ' + label)
    for conf in n.pinctrls[0].conf_nodes:
        mux = conf.props['pinmux'].val
        physical = 'P' + chr(ord('A') + ((mux >> 9) & 31)) + str((mux >> 5) & 15)
        require(physical not in seen, 'pin collision: ' + physical)
        seen.add(physical)
        require(physical in pins, 'pin missing from hardware netlist: ' + physical)
        pin = pins[physical]
        connections = pin['direct_connections'] + ' ' + ' '.join(
            c['connections'] for c in pin['series_connections'])
        expected_device = 'U2.' if label == 'fmc' else 'U3.' if label in ('mac', 'mdio') else 'U5.'
        require(expected_device in connections, physical + ' does not connect to ' + expected_device)
        require(mux & 31 == (12 if label == 'fmc' else 11 if label in ('mac', 'mdio') else
                            6 if label == 'spi3' else 5), 'wrong alternate function: ' + physical)
        if label.startswith('spi'):
            signal = conf.name.rsplit('_', 1)[0].upper().replace('_SCK', '_SCLK')
            require(any(c['net'].endswith('/' + signal) for c in pin['series_connections']),
                    'SPI net mismatch: ' + signal + ' on ' + physical)
    if label.startswith('spi'):
        if label == 'spi4' and 'cs-gpios' in n.props:
            cs = n.props['cs-gpios'].val
            require(len(cs) == 1 and cs[0].controller == node['gpioe'] and
                    cs[0].data == {'pin': 4, 'flags': 1}, 'SPI4 active-low GPIO CS PE4')
            require('PE4' not in seen, 'SPI4 NSS must not also use alternate function')
            seen.add('PE4')
            require(any(c['net'].endswith('/SPI4_NSS') and 'U5.A12' in c['connections']
                        for c in pins['PE4']['series_connections']), 'SPI4 CS FPGA connection')
            fpga = node['max10_spi4']
            require(fpga.regs[0].addr == 0, 'SPI4 CS index')
            require(fpga.props['spi-max-frequency'].val == 1000000, 'SPI4 requested clock')
            for prop in ('spi-cs-setup-delay-ns', 'spi-cs-hold-delay-ns'):
                require(fpga.props[prop].val >= 1000, prop)
            require('CONFIG_JPARC_SPI4_REGS_TEST=y' in config, 'SPI4 server enabled')
        else:
            require('cs-gpios' not in n.props and not n.props['st,soft-nss'].val, 'hardware NSS')

if 'CONFIG_JPARC_ADC_DUMMY_TEST=y' in config:
    require(node['spi1'].status == 'okay', 'SPI1 RX enabled')
    require(node['spi2'].status == node['spi3'].status == 'disabled', 'SPI2/3 remain disabled')
    require('CONFIG_DMA=y' in config and 'CONFIG_SPI_STM32_DMA=y' not in config,
            'application owns circular SPI1 DMA; SPI4 remains polling')
    rx = value('spi1', 'dmas')
    require(len(rx) == 1 and rx[0].controller == node['dmamux1'] and
            rx[0].data['channel'] == 0 and rx[0].data['slot'] == 37,
            'SPI1_RX reserves DMAMUX1 channel 0 / DMA1 stream 0 / request 37')

patched = args.build_dir / 'jparc_rev58/clock_stm32_ll_h7_vos1.c'
source = patched.read_text()
require('PWR_REGULATOR_VOLTAGE_SCALE0' not in source and
        'LL_PWR_REGU_VOLTAGE_SCALE0' not in source, 'VOS0 reference remains')
build_ninja = (args.build_dir / 'build.ninja').read_text()
require(re.search(r'^build .*clock_stm32_ll_h7_vos1[.]c[.]obj:', build_ninja, re.M), 'patched clock not compiled')
require(not re.search(r'^build .*[/]clock_stm32_ll_h7[.]c[.]obj:', build_ninja, re.M), 'upstream VOS0 clock compiled')
if 'CONFIG_PHY_MICROCHIP_LAN8742=y' in config:
    phy_source = (args.build_dir / 'jparc_rev58/phy_microchip_lan8742_init.c').read_text()
    require('if (ret < 0 && ret != -EALREADY)' in phy_source, 'LAN8742 init EALREADY fix')
    require(re.search(r'^build .*phy_microchip_lan8742_init[.]c[.]obj:', build_ninja, re.M),
            'patched LAN8742 driver not compiled')
    require(not re.search(r'^build .*[/]phy_microchip_lan8742[.]c[.]obj:', build_ninja, re.M),
            'original LAN8742 driver compiled')
with (args.build_dir / 'zephyr/zephyr.elf').open('rb') as f:
    elf = ELFFile(f)
    for section in elf.iter_sections():
        # TLS sections describe per-thread offsets, not fixed RAM addresses.
        if section['sh_flags'] & 3 == 3 and not section['sh_flags'] & 0x400 and section['sh_size']:
            start = section['sh_addr']
            end = start + section['sh_size']
            require((0x20000000 <= start < end <= 0x40000000) or
                    (section.name == '.last_section' and 0x08000000 <= start < end <= 0x08100000),
                    'writable section outside internal RAM: ' + section.name)
    if 'CONFIG_JPARC_ADC_DUMMY_TEST=y' in config:
        dma_buffer = elf.get_section_by_name('.symtab').get_symbol_by_name('adc_dma_ring')
        require(dma_buffer and dma_buffer[0]['st_size'] == 32768 and
                dma_buffer[0]['st_value'] % 32 == 0 and
                0x24000000 <= dma_buffer[0]['st_value'] <= 0x24080000 - 32768,
                'circular RX buffer must be aligned in DMA-accessible AXI SRAM')
    symbols = elf.get_section_by_name('.symtab').get_symbol_by_name('rev58_status')
    require(symbols and 0x24000000 <= symbols[0]['st_value'] < 0x24080000,
            'debug status not in AXI SRAM')
print(f'PASS {args.build_dir.name}: clock/VOS1, internal RAM, {len(seen)} active peripheral pins vs netlist')
