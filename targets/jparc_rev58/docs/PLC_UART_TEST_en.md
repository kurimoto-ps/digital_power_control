# PLC 16-input/16-output mapping and motherboard RS232 tests

FPGA v2.0.2 is supported, including the 12 MHz count/status monitor. See [ACC input diagnosis and coax wiring tests](ACC_INPUT_TEST_en.md) for the 0x0024 monitor and STM32 PA0/PD13 comparison. J16/J17 test 96 kHz/TRIG; 12 MHz is the separate J6 optical path. Hardware tests remain pending.


Added 2026-11-07 for rev58 + TEP521C motherboard, STM32 **mixed_adc** profile.
[日本語・配線表](PLC_UART_TEST.md) / [ADC/DAC system guide](MIXED_ADC_V2_en.md).

**Build and host validation only. Programming, PLC wiring verification and physical RS232 tests have not been performed in this change.** This document describes the tests to perform on hardware.

## Features and preparation

M7 now provides sixteen PLC inputs, sixteen outputs and UART8 at **115200 baud, 8 data bits, no parity, one stop bit, no flow control**. Ethernet, SPI4, FPGA v2 ADC capture, DAC control and the 8 MiB SDRAM startup test remain in the same image. GPIO/UART commands do not require FPGA CONF_DONE or a matching FPGA version. UART receives through interrupts into a 1024-byte ring; each Ethernet read drains up to 128 bytes and each send accepts up to 32 bytes. This is a wiring test interface, not a PLC operating sequence or a high-volume serial recorder.

Outputs initialize low and then **hold until explicitly changed**. TCP disconnection does not clear outputs. The guided test attempts to restore all outputs low on completion or ordinary interruption; communication loss or forced host termination can prevent confirmation. Put the PLC and connected equipment in a test state where these signals cannot start machinery or power equipment. This is not a safety-output function covering reset-time floating pins or faults.

In the Zephyr development environment, build and, when hardware is available, program:

```sh
./scripts/build.sh jparc_rev58 mixed_adc
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-mixed_adc
./scripts/flash.sh jparc_rev58 mixed_adc
```

Programming above was not executed during this work. Older images named mixed_adc do not contain these new commands.

The operator PC only needs Python 3 standard library:

```sh
git clone https://github.com/kurimoto-ps/digital_power_control.git
cd digital_power_control
git switch feature/jparc-rev58-zephyr-layout
# Existing clones: git pull --ff-only on this branch
cd targets/jparc_rev58/scripts
python3 board_io_client.py plc-read
```

Host wired address: `192.168.100.1/24`; STM32: `192.168.100.58:4244/TCP`. Specify `--host IP` before the subcommand if needed. JSON GPIO numbers are decimal; interactive masks are hexadecimal, with bit0 least significant. Do not operate other clients concurrently. ADC capture occupies the TCP server while running; UART interrupts still receive, but more than 1024 unread bytes will overflow.

## PLC signal mapping

PLC IN/OUT labels refer to the **PLC's perspective**. PLC DO drives STM32 inputs through motherboard J3; STM32 outputs drive PLC DI through motherboard J4. Rev58 J8 and motherboard CN1A use the same contact numbers. Actual PLC device addresses are not supplied; establish and record the association with bits 0–15 before testing.

| bit | PLC→STM32: J3 pin / signal | J8 / CN1A | STM32 input | STM32→PLC: J4 pin / signal | J8 / CN1A | STM32 output |
|---:|---|---:|---|---|---:|---|
| 0 | 1 / PDI0 | 49 | PD6 (GPIO16) | 1 / PDO0 | 17 | PE3 (GPIO0) |
| 1 | 2 / PDI1 | 51 | PD3 (GPIO17) | 2 / PDO1 | 19 | PG12 (GPIO1) |
| 2 | 3 / PDI2 | 53 | PF10 (GPIO18) | 3 / PDO2 | 21 | PK2 (GPIO2) |
| 3 | 4 / PDI3 | 55 | PC11 (GPIO19) | 4 / PDO3 | 23 | PA3 (GPIO3) |
| 4 | 5 / PDI4 | 57 | PB7 (GPIO20) | 5 / PDO4 | 25 | PF8 (GPIO4) |
| 5 | 6 / PDI5 | 59 | PC9 (GPIO21) | 6 / PDO5 | 27 | PH2 (GPIO5) |
| 6 | 7 / PDI6 | 61 | PH10 (GPIO22) | 7 / PDO6 | 29 | PI12 (GPIO6) |
| 7 | 8 / PDI7 | 63 | PG3 (GPIO23) | 8 / PDO7 | 31 | PH14 (GPIO7) |
| 8 | 9 / PDI8 | 65 | PH12 (GPIO24) | 9 / PDO8 | 33 | PH15 (GPIO8) |
| 9 | 10 / PDI9 | 67 | PG2 (GPIO25) | 10 / PDO9 | 35 | PI4 (GPIO9) |
| 10 | 11 / PDI10 | 69 | PH9 (GPIO26) | 11 / PDO10 | 37 | PK5 (GPIO10) |
| 11 | 12 / PDI11 | 71 | PI3 (GPIO27) | 12 / PDO11 | 39 | PG9 (GPIO11) |
| 12 | 13 / PDI12 | 73 | PI2 (GPIO28) | 13 / PDO12 | 41 | PB9 (GPIO12) |
| 13 | 14 / PDI13 | 75 | PJ7 (GPIO29) | 14 / PDO13 | 43 | PD5 (GPIO13) |
| 14 | 15 / PDI14 | 77 | PI1 (GPIO30) | 15 / PDO14 | 45 | PD7 (GPIO14) |
| 15 | 16 / PDI15 | 79 | PI0 (GPIO31) | 16 / PDO15 | 47 | PD4 (GPIO15) |

PLC DO → CB25 → motherboard J3 → optocouplers/inverting buffers → PDI → STM32.
STM32 → PDO → non-inverting buffers/optocouplers → motherboard J4 → CB26 → PLC DI.

The motherboard circuit produces PDI high when its input optocoupler is on; PDO high turns on its output optocoupler. Commands report **raw STM32 pin levels**, without guessing PLC program inversion or names. If all-off does not read 0000, check PLC display polarity, wiring, supplies and commons before proceeding. Connect PLC 24 V wiring to the designated motherboard interface, never directly to STM32 GPIO.

Sources: [connector usage p.1](hardware/circuit/Connector_usage_rev58.pdf), [motherboard schematic sheets 1/6 and 6/6](hardware/circuit/フィードバック基板_TEP521C回路図.pdf), [rev58 pin table](hardware/rev58/rev58_pin_tables.md).

## Guided 32-point test

```sh
python3 board_io_client.py plc-test --report board01_plc_mapping.json
```

The script initially clears STM32 outputs. Operator prompts do not keep an idle TCP connection open, so manual work may take as long as needed.

First sixteen points, PLC → STM32:

1. Turn all sixteen PLC outputs off and press Enter; the input baseline must be 0000.
2. Turn on only the requested PLC DO bit, wait for it to settle, then press Enter.
3. The script compares all sixteen STM32 inputs against `1 << bit`.
4. Turn that PLC output off and press Enter; the script verifies return to 0000.
5. Repeat bits 0–15.

Next sixteen points, STM32 → PLC:

1. With STM32 outputs low, enter the actual PLC DI mask; baseline must be 0000.
2. The script drives one STM32 output high. Read all sixteen PLC inputs and enter the **actual observed hexadecimal mask**, not a copy of the expected value.
3. The script clears outputs. Read and enter the actual PLC DI mask again.
4. Repeat bits 0–15.

Examples: only bit0 on = `0001`; bit3 = `0008`; bit15 = `8000`; unintended bits0+1 = `0003`. Checking the entire mask detects swapped channels and multiple responses. Each point includes ON and OFF confirmation: 32 points total.

Acceptance requires `complete: true`, `passed: true`, `outputs_cleared: true` and 32 entries in `tests`. A failed point does not prevent testing remaining points. Ctrl+C, or `q` at a mask prompt, saves a partial report and attempts all-low cleanup. `outputs_cleared: false` means restoration was not confirmed. Separate directions are also supported:

```sh
python3 board_io_client.py plc-test --direction in --report board01_input.json
python3 board_io_client.py plc-test --direction out --report board01_output.json
python3 board_io_client.py plc-read
python3 board_io_client.py plc-write 0x0001
python3 board_io_client.py plc-write 0x8000
python3 board_io_client.py plc-write 0
```

`output_latch` is the requested setting; `output_raw` is MCU pin readback. Neither proves reception at the PLC. Writes span multiple GPIO ports sequentially, so this is not a simultaneous 16-bit timing test.

## RS232 wiring and terminal setup

Use **motherboard J5**, not the rev58 J8 logic-level pins.

| Direction | Motherboard J5 | Circuit | CN1A / rev58 J8 | STM32 |
|---|---|---|---|---|
| External terminal → STM32 | pin2, board receive | ADM3202 R1IN→R1OUT, RS232C2 | pin70 | PJ8, UART8 RX after swap |
| STM32 → external terminal | pin3, board transmit | RS232C3, ADM3202 T1IN→T1OUT | pin72 | PJ9, UART8 TX after swap |
| Signal ground | pin5 | DGND | — | — |

UART8 normally has PJ8=TX and PJ9=RX. This board/motherboard combination requires **tx-rx-swap**. Do not restore conventional assignment based only on rev58 T2_UART/R2_UART labels.

Use an actual USB-to-RS232 adapter or equivalent. For a typical DTE adapter with pin3=TX and pin2=RX, connect adapter pin3→J5 pin2, J5 pin3→adapter pin2, and pin5↔pin5: a crossover connection. Check DTE/DCE and cable wiring rather than relying on connector gender. This test does not use a TTL UART adapter on J5.

Set the terminal to **115200/8N1, no hardware/software flow control, local echo off**. RTS/CTS is unused; the motherboard flow-control path is looped through rev58 J8 contacts66/68. UART carries only test bytes, not boot logs, a Zephyr console or a command shell. Received bytes are not automatically echoed.

## Test both serial directions

External terminal → STM32:

```sh
python3 board_io_client.py uart-receive --seconds 30 --hex "52 58 5f 54 45 53 54 0d 0a" > board01_uart_rx.json
```

Wait for `RX buffer cleared`, then send ASCII `RX_TEST` followed by CR LF **once** from the external RS232 terminal within 30 seconds. Prefer the terminal's hex-send function for these exact nine bytes. Avoid accidental extra newlines or duplicated CR LF. The command first clears the software buffer and counters, then receives.

Pass criteria: `received_hex` equals `52585f544553540d0a`, `passed: true`, `rx_dropped: 0`, `error_flags: 0`. Error flags accumulate Zephyr UART errors such as overrun/parity/framing. Extra, missing or duplicated bytes fail the comparison.

STM32 → external terminal: open the terminal receive display, then run over Ethernet:

```sh
python3 board_io_client.py uart-send --hex "54 58 5f 54 45 53 54 0d 0a"
```

Verify one `TX_TEST` followed by CR LF at the external terminal and save its log. An Ethernet success response or `tx_total` alone does not establish physical delivery. If the terminal supports binary data, repeat both directions with `00 55 aa ff` to exercise more bit patterns.

Manual diagnostics:

```sh
python3 board_io_client.py uart-clear
# Send a short message from the external terminal now
python3 board_io_client.py uart-status
python3 board_io_client.py uart-read
python3 board_io_client.py uart-send --text "STM32_UART_OK"
```

Read removes bytes from the software buffer; repeat while `buffered` is nonzero. Clear discards unread software data and statistics, so do not clear during transmission. Text is ASCII with no implicit newline. Sends and expected receive-test payloads are limited to 32 bytes.

## Finish and retain evidence

Turn all PLC test outputs off and remove PLC force settings. Clear STM32 outputs and verify both STM32 and PLC observations:

```sh
python3 board_io_client.py plc-write 0
python3 board_io_client.py plc-read
python3 board_io_client.py uart-status
```

Record board identity, Git commit, PLC model/address mapping, cables, the 32-point JSON report, UART receive JSON and external terminal logs. Host-model success is not a substitute for these hardware tests.
