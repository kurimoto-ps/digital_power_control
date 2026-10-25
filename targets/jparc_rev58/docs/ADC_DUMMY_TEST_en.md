# FPGA v1.3 dummy ADC → SPI1 DMA → Ethernet test

[日本語](ADC_DUMMY_TEST.md)

Configure three FPGA dummy sources through SPI4 and receive their 20 MHz SPI1 stream continuously on STM32 M7 using circular DMA. Only test results are returned to the host. The startup 8 MiB SDRAM test and existing Ethernet/SPI4 service remain enabled.

## Build and run

With integrated ADC/DAC FPGA v1.3 programmed, run from the repository root:

```sh
./scripts/build.sh jparc_rev58 adc_dummy
./scripts/flash.sh jparc_rev58 adc_dummy
python3 targets/jparc_rev58/scripts/spi4_client.py info
python3 targets/jparc_rev58/scripts/adc_dummy_client.py
```

Outputs: `../build-jparc-rev58-adc_dummy/zephyr/`. Keep BCM4=0 and BCM7=1. Do not leave the CPU halted in GDB during network tests.
Board: `192.168.100.58/24`, TCP 4244; example PC address: `192.168.100.1/24`.
The previous `combined` profile does not contain SPI1 DMA testing; program `adc_dummy`.
Expected FPGA ID/version/capabilities: `0x4A503538` / `0x00010003` / `0x0000000F`.
Ordinary INFO/READ/WRITE support v1.0–v1.2 as well, but ADCTEST requires v1.3, caps=15 and three inputs.

The default test lasts one second with raw values `0x123456 / 0x89ABCD / 0xFEDCBA`, receiving approximately 78,125 records per input.

```sh
# Three seconds with different raw 24-bit values; no sign/voltage conversion
python3 targets/jparc_rev58/scripts/adc_dummy_client.py \
  --duration-ms 3000 --values 0 0xffffff 0x800000

# Ten independent one-second tests; stop/reconfigure and restart sequences each time
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --repeat 10

# Full result JSON
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --json
```

`--duration-ms`: 100–3000; `--repeat`: 1–1000; `--host` and `--port` are available.
The script uses only Python's standard library and can be copied independently to a host PC.
Success: exit 0 and PASS; failure: exit 1 with diagnostics. This does not transfer all raw samples over Ethernet.

## Start and cleanup sequence

1. Check FPGA ID/version/capabilities/ADC_CHANNEL_COUNT.
2. ADC_CONTROL=0; wait for BUSY clear.
3. Write/read back ADC_DUMMY1–3.
4. Set/read back ADC_SOURCE_MODE=1.
5. Prepare SPI1 and circular DMA.
6. ADC_CONTROL=3 clears/enables the stream; verify bit 0=1.
7. Receive for the requested interval; check CRC, values and per-input sequences.
8. Read DROP/RESTART while still transmitting, then ADC_CONTROL=0; wait for BUSY clear.
9. Collect the final partial DMA buffer; compare total received records against stable FPGA_TX_COUNT.
10. Restore ADC_SOURCE_MODE=0 and return results.

Cleanup attempts to stop transmission and leave dummy mode on both success and failure. Even if the host disconnects, the finite-duration server test runs to completion and cleans up.
If SPI4 itself fails and cleanup cannot reach the FPGA, `cleanup_error` indicates the issue; inspect connections.
Dummy settings remain stored, but real-input mode=0 and TX enable=0 are restored. DAC settings are untouched. The host reconfigures the test each time, including after power cycling.

**Dummy generation continues after transmission is stopped; the FIFO fills in about 3.28 ms.** DROP/RESTART therefore represent non-atomic reads immediately before stopping, while TX is active. Later accumulation is not counted as an in-test failure.
Sequence/CRC checks and equality with FPGA_TX_COUNT also cover the short tail between those reads and actual stop. Mode changes clear counters, so the STM32 saves the results before restoring mode0.

## DMA and validation

- SPI1: PG11=SCLK, PA4=NSS, PB5=DATA, AF5; FPGA drives all three.
- Slave, Mode 0, MSB first, 8 bit, hardware NSS, no MISO.
- SPI1 kernel clock 100 MHz; incoming FPGA SCLK 20 MHz.
- The module exclusively owns SPI1. STM32 LL configures RX-only, TSIZE=0 continuous receive; Zephyr DMA API starts a 32 KiB circular transfer. No per-record SPI reinitialization.
- DMAMUX1 channel 0 → DMA1 stream 0, request 37 (SPI1_RX). SPI4 remains polling; `CONFIG_SPI_STM32_DMA` is disabled.
- DMA writes a 32-byte-aligned AXI SRAM buffer; D-cache disabled, no DTCM. Each half is 16 KiB/2048 records. CRC is computed outside interrupts.
- Completed halves are copied to a work buffer. Failure to collect before circular overwrite is an error. SDRAM stays available after its boot test but is not used for these buffers or sample storage.
- A1–A3 identify inputs; order is not fixed. CRC is CCITT-FALSE. On corruption, resynchronize byte by byte and retain the error; recovery does not turn the test into PASS.
- Each input must start at sequence0, then increment by one including 65535→0. Counts must lie within ±10% of requested duration ×78,125 Hz; this is not precise frequency measurement.
- PASS requires zero CRC/header/value/sequence/DMA/SPI errors, discarded bytes, trailing partial records and in-stream FPGA DROP/RESTART, plus FPGA_TX_COUNT equal to total received records.

One server thread owns SPI4 and serializes commands/tests for one client at a time. DAC requests from another client are not processed concurrently with ADCTEST.

## TCP format

```text
ADCTEST 1000 0x123456 0x89abcd 0xfedcba\n
OK ADC {"pass":true,...}\n
```

JSON includes `received`, `fpga_tx`, `elapsed_ms`, error counters and three `channels` entries: records, expected, last, first_seq, last_seq, value_errors, sequence_errors, fpga_drop, fpga_restart.
`control_error` describes SPI4 control failure; `cleanup_error` describes stop/mode-restoration failure; `error` is the DMA receive errno.
`spi_error_flags` is the STM32 SPI_SR OVR/MODF/TIFRE bit mask.
CRC/header error counts include failed resynchronization candidates, not necessarily distinct damaged physical records.
`elapsed_ms` extends from just before start through stopping/BUSY clear and is slightly longer than requested.

## DAC and existing commands

```sh
# Setting CH1 changes real external DAC outputs
python3 targets/jparc_rev58/scripts/spi4_client.py dac-set 1 0x1234
```

Channels are 1–16. Besides setting readback, this checks BUSY/PENDING clear, INITIALIZED and DAC_SENT. It confirms FPGA transmission, not DAC acceptance or analog voltage.
All four outputs in the same IC are retransmitted, including initial code 0 on previously unset companions.
For this delivery, ±10 V signed16 makes code 0 nominal 0 V. For CN/voltage commands, use `dac_output_client.py set-voltage` in the [customer guide](CUSTOMER_IO_TEST_en.md). Dummy ADC tests never invoke DAC setting automatically.
Generic `write` compares only the implemented DAC16, dummy24 and CONTROL/SOURCE_MODE bit 0 fields. CONTROL=3 reads back with the pulse bit cleared, as expected.
The legacy register test now uses unmapped 0x0800, not DAC_SET address 0x0200.

## Verification record and limitations

On 2026-10-24, the connected board was programmed with this profile and FPGA v1.3/caps 15 was confirmed.
Tests passed: default values for 1 s (234,791 records); alternative values for 3 s (703,775); three 100 ms runs (23,847 each).
CRC/sequence/value/DMA/SPI/in-stream DROP errors were zero, and received counts matched FPGA_TX_COUNT. Final CONTROL=0, SOURCE_MODE=0 and DAC_TRANSFER_COUNT=0 were confirmed.
Real ADC reception, DAC analog output, logic-analyzer waveforms and long uninterrupted operation were not verified in that record. See the [later replacement-board record](measurements/2026-10-25-board-00250028-35335115-39383331/README_en.md).

```sh
python3 -m unittest discover -s targets/jparc_rev58/tests/unit -v
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-adc_dummy
```

Host tests cover known CRC vectors, interleaved inputs, 16-bit sequence wrap, fragmented input, corruption/missing/duplicate/wrong values, result validation, fragmented TCP JSON, SPI4 legacy/v1.3 compatibility, clear-bit readback and DAC polling. Artifact checks include 54 peripheral pins, DMA assignment and AXI SRAM placement.
Current specification: [FIRMWARE_SPEC_v1.3_en.md](hardware/rev58_adc_dac/FIRMWARE_SPEC_v1.3_en.md). Programming identity and verification scope: [delivery document](CUSTOMER_DELIVERY_en.md).
Archived Japanese ADC/DAC handoffs beside the specification are preserved with SHA256SUMS; their links refer to the original Quartus tree. The old ADC handoff's “not programmed” statement is superseded by the user's programming report and hardware version readout.
