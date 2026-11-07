# ACC 12 MHz / 96 kHz / TRIG wiring checks: FPGA and STM32

For FPGA **v2.0.2**, STM32 **mixed_adc**, rev58 and TEP521C motherboard.
[日本語](ACC_INPUT_TEST.md) / [Archived FPGA designer specification](hardware/acc_v202/ACC_INPUT_MONITOR_STM32.md).

**Build and host-model validation only. Neither image was programmed during this work; the physical tests below have not been performed.**

## Signal paths and coax-test coverage

| Signal | Motherboard source | Motherboard path | CN1B / rev58 J7 | MAX10 | Register 0x0024 | STM32 |
|---|---|---|---:|---|---:|---|
| ACC_12MHZ | **J6 optical receiver** | HFBR-24E6Z → AC coupling / AD8561 → U27 → EXT_CLK_OPT | 75 | M9 | bit0 | No direct connection |
| ACC_96KHZ | **J16 LEMO coax** | RA48 → U27 → EXT_CLK_LEMO | 77 | A7 | bit1 | **PA0** |
| ACC_TRIG | **J17 LEMO coax** | RA48 → U27 → EXT_TRG_LEMO | 79 | A8 | bit2 | **PD13** |

J7 and CN1B contacts use identical numbering. Test points: TP1=12 MHz, TP2=96 kHz, TP3=TRIG.

**J16/J17 verify only the 96 kHz and TRIG nets. The 12 MHz net originates at optical receiver J6 and cannot be validated by those coax inputs.** v2.0.2 diagnoses its optical clock using the new count/status registers; instantaneous levels remain in the log.

Sources: [motherboard sheets 3/6 and 6/6](hardware/circuit/フィードバック基板_TEP521C回路図.pdf), [connector usage p.2](hardware/circuit/Connector_usage_rev58.pdf), [rev58 pin table](hardware/rev58/rev58_pin_tables.md).

## Implementation and coexistence

PA0 and PD13 initialize as GPIO inputs without internal pulls or edge interrupts. They are not driven by STM32 or assigned to timers. Incoming 96 kHz does not generate per-edge software interrupts. Existing peripheral pins do not overlap; future owners of these pins must preserve or explicitly revise this arrangement.

`ACC READ` verifies device ID, VERSION=`0x00020001` or `0x00020002` and capability bit6 before reading 0x0024. Old firmware is rejected without accessing that register. It uses the existing eight-byte SPI4 read handler, checking response status, big-endian data and zero reserved bits31:3. Diagnostics do not write FPGA registers or change PLC outputs.

The server reads STM32 GPIO, then FPGA levels, then STM32 GPIO again. All SPI4 activity is serialized by the existing single server thread. ADC 48-byte circular DMA and ADC/DAC control are unchanged; v2.0, v2.0.1 and v2.0.2 remain supported for those functions. Instantaneous ACC levels require v2.0.1 or v2.0.2; clock detection requires v2.0.2 and capability bit7. Unsupported clock registers are never accessed. PLC GPIO, UART8, Ethernet and the 8 MiB SDRAM startup test remain included.

**Samples are sequential, not simultaneous.** FPGA inputs pass through three synchronization stages at 120 MHz and are captured around SPI4 address acceptance. The two STM32 GPIOs are also read in sequence. Running clocks can produce different readings without a fault. Even equal before/after values do not prove absence of transitions between observations.

GPIO and 0x0024 measure instantaneous levels only. There is no 96 kHz frequency/pulse-width measurement or trigger history. v2.0.2 separately measures 12 MHz rising edges over completed 1 ms windows. Short TRIG pulses may be missed and repeated clock reads can appear constant because of sampling phase. Use an oscilloscope or equivalent for clock presence/frequency verification.

## Preparation

Program FPGA v2.0.2 supplied by the designer and the current STM32 mixed_adc image. The flash command below is for the later hardware operator and was not executed in this work.

```sh
# Zephyr development environment, repository root
./scripts/build.sh jparc_rev58 mixed_adc
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-mixed_adc
./scripts/flash.sh jparc_rev58 mixed_adc
```

The operator PC only needs Python 3 standard library:

```sh
git clone https://github.com/kurimoto-ps/digital_power_control.git
cd digital_power_control
git switch feature/jparc-rev58-zephyr-layout
# Existing clone: git pull --ff-only on this branch
cd targets/jparc_rev58/scripts
python3 spi4_client.py info
python3 acc_input_client.py read
```

Expected VERSION=`0x00020002`, CAPABILITIES=`0x000000ff`. Host wired IP: `192.168.100.1/24`; STM32: `192.168.100.58:4244/TCP`. Use `--host IP` before the subcommand if necessary.

Start after GPIO initialization and a successful Ethernet response. Stop concurrent host operations during the wiring test. An ADC capture occupies the same TCP server and delays other requests; coexistence in one image does not provide parallel simultaneous measurements.

## Coax connection and fixed levels

Connect a two-channel source to J16 and J17: center conductor is signal, shield is motherboard DGND. Stop clock/pulse generation and use held DC levels. Use Low=0 V and High=3.3 V, measured at the connectors. Generator amplitude specified into 50 Ω may double at a high-impedance load; check actual voltage. Do not use bipolar waveforms or PLC 24 V. STM32/MAX10 signal levels are 3.3 V logic.

Drive both lines explicitly, including the inactive line low. Do not leave disconnected inputs floating or drive a net from two sources. Wait for levels to settle; the specification allows about 1 ms margin. The guided script additionally waits 10 ms after Enter.

## Guided J16/J17 mapping test

```sh
python3 acc_input_client.py coax-test --report board01_acc_coax.json
```

Follow each prompt and press Enter after setting stable levels. Three reads are evaluated at every step.

| Step | J16 | J17 | FPGA value & 0x6 | PA0 | PD13 |
|---|---|---|---:|---:|---:|
| Baseline | Low | Low | 0 | 0 | 0 |
| 96 kHz net high | High | Low | 2 | 1 | 0 |
| Return low | Low | Low | 0 | 0 | 0 |
| TRIG net high | Low | High | 4 | 0 | 1 |
| Return low | Low | Low | 0 | 0 | 0 |
| Both high | High | High | 6 | 1 | 1 |
| Final low | Low | Low | 0 | 0 | 0 |

Pass requires the expected FPGA bits and both STM32 observations, detecting swapped nets and unintended changes to the other shared input. Check `complete: true`, `passed: true` and seven `steps`. Both tested masks are 6. `not_tested` explicitly retains the optical 12 MHz path; bit0 is logged but excluded from coax pass/fail.

Enter `q` or press Ctrl+C to save a partial report and abort. **The script never controls external signal generators.** Manually restore them to low, including after interruption.

## Individual reads and logs

```sh
python3 acc_input_client.py read
python3 acc_input_client.py read --count 10 --interval 0.2 --report board01_acc_levels.json
# J16 held high, J17 held low
python3 acc_input_client.py read --count 5 --expect 0x2 --mask 0x6 --report board01_acc_j16.json
```

`signals` contains signal names, HIGH/LOW and STM32 pin names. Without `--expect`, `passed` is null and mismatching observations are not declared faulty. `shared_levels_equal` reports equality of observed levels only. Use expectations only with held inputs. Default checked mask is 0x6. `sampled_utc` is the host log time, not a hardware edge timestamp.

Example: TX `03 00 24 00 00 00 00 00`, RX `00 00 00 00 00 00 00 05` means FPGA 12 MHz high, 96 kHz low, TRIG high. With stable signals, STM32 should read PA0=0 and PD13=1.

## Optical 12 MHz start/stop test (v2.0.2)

The J6 optical path is AC-coupled. **Fixed High does not detect 12 MHz.** Apply a real 12 MHz optical clock with a compatible source. Driving J16 tests the bit1 path, not the optical bit0 path. Do not attach an external driver directly to TP1.

| Register | Log field | Meaning |
|---|---|---|
| 0x0028 | `clock.count_per_ms` | Rising edges in the last completed 1 ms window, 17 bits; about 12,000 at 12 MHz |
| Same | `clock.estimated_hz` | Count × 1,000, approximate frequency |
| 0x002C bit0 | `clock.window_valid` | First window after reset completed; stays set after input stops |
| 0x002C bit1 | `clock.active` | At least one rising edge; not a 12 MHz verdict |
| 0x002C bit2 | `clock.near_12mhz` | Count between 10,800 and 13,200 inclusive, about ±10% |

Typical STATUS: 7 while receiving 12 MHz, 1 when stopped, 3 at 6 MHz. Zero after reset means measurement pending, not a stopped-test pass. Steady High/Low yields zero edges and no near-12MHz detection. Registers are neither cumulative nor sticky, do not clear on read, and are not reset by ADC flow clear.

**Wait at least 3 ms after starting/stopping input, then read several times.** Switching partway through a window may produce one mixed count. The guided test waits 3 ms after Enter and samples three times, 10 ms apart:

```sh
python3 acc_input_client.py clock-test --report board01_acc_12mhz.json
```

1. Stop the J6 optical clock, then Enter. Require STATUS=1 three times.
2. Apply a 12 MHz optical clock to J6, then Enter. Require STATUS=7 three times and inspect counts near 12,000 in the log.
3. Stop again, then Enter. Require STATUS=1 three times and inspect the steady count returning to zero.

Acceptance: `complete: true`, `passed: true`, three `steps`. Count and estimated Hz are saved in JSON. Run the separate `coax-test` for 96 kHz/TRIG static mapping and retain both reports.

**Automatic pass/fail uses STATUS**, whose flags are obtained together in one READ. COUNT is a separate READ and can belong to a different completed window. Strict count/status correspondence is therefore not required; `clock.paired_snapshot: false` records this limitation. Count is supplementary diagnostic evidence. Investigate persistently unexpected counts under stable input with the logs and measured waveform.

```sh
# After starting input; command waits 3 ms before reading
python3 acc_input_client.py read --count 5 --interval 0.01 --clock-expect on --report clock_on.json
# After stopping input; requires VALID=1, ACTIVE=0 and NEAR=0, not merely absence of 12 MHz
python3 acc_input_client.py read --count 5 --interval 0.01 --clock-expect off --report clock_off.json
```

`--expect` checks 0x0024 instantaneous levels; `--clock-expect` checks clock status. Do not use `--expect 1 --mask 1` for a running 12 MHz input. v2.0.1 retains level diagnostics but reports `clock.supported: false`; clock-test and clock-expect reject unsupported firmware. Update the STM32 image as well as the FPGA.

This ±10% check uses the internal 120 MHz reference, not a precision frequency standard. Narrow glitches or signals below the optical receiver threshold are not guaranteed to be counted correctly. The designer suggests at least 25 ns each for High and Low. Investigate unstable results with measurements at the MAX10 input/TP1.

The script never controls the optical source. `q` or Ctrl+C saves partial results; the operator must stop the source manually.

## Records and troubleshooting

Save board identity, both firmware versions, STM32 Git commit, coax connections, source and measured voltage, JSON reports and instrument traces.

- `ERR ACC_REQUIRES_V2_0_1_OR_V2_0_2_CAP_BIT6`: check firmware/capability; the new register was not accessed.
- `ERR NOT_READY`: inspect FPGA configuration completion.
- SPI error / `ERR FPGA ...`: failed communication, not a valid level observation.
- `ERR ACC_RESERVED_BITS` / `ERR ACC_CLOCK_RESERVED_BITS`: nonzero reserved bits; inspect version, protocol or communication.
- Fixed-level mismatch: inspect shared PA0/PD13 wiring, contacts and voltage.
- Running-clock mismatch or unchanged polling: use held-level mapping tests and instruments.

Host models validate command handling and comparisons, not physical wiring, voltage or waveforms.
