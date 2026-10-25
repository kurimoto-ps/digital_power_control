# JPARC rev58 FPGA firmware specification v1.3

[Japanese original](FIRMWARE_SPEC_v1.3.md)

Updated: 2026-10-25. Integrated specification based on current RTL and generated programming files. Covers SPI4 configuration, ADC/dummy SPI1 reception and DAC DC output verification.
This is a translation of the Quartus maintainer's specification. For host voltage scaling and combined hardware test evidence, see the [delivery document](../../CUSTOMER_DELIVERY_en.md).

## 1. Target and implemented scope

|Item|Value|
|---|---|
|Board|JPARC rev58|
|FPGA|MAX10 `10M16DCF484I7G`|
|Project|`quartus/projects/spi4_adc_dac`|
|Top|`jparc_adc_dac_top`|
|DEVICE_ID|`0x4A503538` (ASCII JP58)|
|PROTOCOL_VERSION|`0x00010003` (major=1, minor=3)|
|CAPABILITIES|`0x0000000F`|
|Tool|Quartus Lite 25.1std.0 Build 1129|
|Programming status|The Quartus session successfully programmed and verified the v1.3 POF in internal Flash; zero errors/warnings|

Implemented: STM32 SPI4 register access; three asynchronous SCLK/DRDY/DATA 24-bit ADC receivers; continuous SPI1 output of ADC or internal dummy data; 16 DC DAC channels across four AD5764 ICs; sixteen general-purpose 32-bit DUMMY registers.

Not implemented: ADC4–6, feedback computation, periodic waveform generation, DDR2, PLC, DutyOut, SPI2/3, READY/FAULT/IRQ notifications, or DAC command readback. The Quartus work did not modify existing Zephyr sources or the development container.

## 2. Architecture and operating modes

```text
STM32 ── SPI4 (STM32 master) ──→ FPGA registers ──→ DAC 16CH
                                      │
ADC ×3 ── 24-bit receivers ────────────┤ source selection
Internal dummy ×3, each 78.125 kHz ────┘
                                      ↓
                               Three DCFIFOs
                                      ↓
                         SPI1 (FPGA master) → STM32 DMA
```

|Path|Behavior|
|---|---|
|DAC|Each DAC register WRITE requests one transmission to the selected IC; the DAC then holds its output. No periodic retransmission|
|ADC/dummy → SPI1|Continuously sends available data while enabled. Real ADC rate follows input arrivals; dummy rate is 78.125 kHz per input|
|General DUMMY|Stores values for READ; does not drive DAC or SPI1|

The 50 MHz oscillator feeds a PLL generating 120 and 80 MHz. SPI4, ADC capture, DAC control and dummy generation use 120 MHz; SPI1 uses 80 MHz. SPI1 SCLK=80 MHz/4, DAC SCLK=120 MHz/6, both 20 MHz.

## 3. Pin connections

FPGA pin numbers below are package balls. I/O standard is 3.3-V LVTTL; implemented outputs use 4 mA drive. Unused user pins are tri-stated inputs.

### 3.1 STM32–FPGA

|Path|Signal|STM32|FPGA|Direction|
|---|---|---|---|---|
|SPI4|SCLK|PE2 AF5|L14|STM32 → FPGA|
|SPI4|NSS|PE4 GPIO, active Low|A12|STM32 → FPGA|
|SPI4|MOSI|PE6 AF5|E20|STM32 → FPGA|
|SPI4|MISO|PE5 AF5|J21|FPGA → STM32|
|SPI1|SCLK|**PG11 AF5**|C21|FPGA → STM32|
|SPI1|NSS|PA4 AF5, active Low|C20|FPGA → STM32|
|SPI1|DATA (net named MOSI)|PB5 AF5|C22|FPGA → STM32|
|Oscillator|CLK50|—|N5|Board 50 MHz → FPGA|

The SPI1 SCLK net retains a PA5 label, but the actual connection is **PG11**. SPI1 has no MISO; configure STM32 as a receiving slave.

### 3.2 ADC inputs

J10 denotes electrical contact numbers on the new rev58 board, not the motherboard's external J10.

|Input|DRDY: J10 / FPGA|DATA: J10 / FPGA|SCLK: J10 / FPGA|
|---|---|---|---|
|ADC1|18 / E19|20 / F18|22 / E21|
|ADC2|26 / G17|28 / F21|30 / F22|
|ADC3|34 / H22|36 / AB10|38 / J22|

All nine pins are inputs. The FPGA does not supply ADC clocks.

### 3.3 DAC connections

|DAC IC|SCLK: J10 / FPGA|SYNC_N: J10 / FPGA|SDIN: J10 / FPGA|SDO: J10 / FPGA|
|---|---|---|---|---|
|DAC1 / IC11|17 / E22|19 / F20|21 / G19|23 / G20|
|DAC2 / IC13|27 / P13|29 / H21|31 / R13|33 / L18|
|DAC3 / IC15|37 / M18|39 / M20|41 / M21|43 / M22|
|DAC4 / IC17|47 / N20|49 / N21|51 / P20|53 / V14|

SCLK/SYNC_N/SDIN are outputs. **All four SDO pins are inputs**; register readback through SDO is not implemented.

## 4. SPI4 register communication

### 4.1 Configuration and timing

STM32 master, FPGA slave; Mode 0 (CPOL=0, CPHA=0), MSB first, 8-bit, full duplex. **1 MHz recommended; 2 MHz maximum.** Do not confuse this with 20 MHz SPI1/DAC clocks.

- SCLK High/Low: at least 250 ns each.
- MOSI stable at least 100 ns before and after SCLK rising.
- NSS assertion to first rising edge, last rising edge to NSS release, and inter-frame NSS High: at least 100 ns each; **1 µs each recommended**.
- Hold NSS Low for all eight bytes; no byte-wise NSS pulses. SCLK is Low between frames.
- MISO is Hi-Z with NSS High. PE4 is GPIO CS.

### 4.2 Eight-byte frame

Addresses are 16-bit **byte addresses**, aligned to four bytes. Data uses a 32-bit field. Address and data are big-endian.

|Byte|MOSI READ|MOSI WRITE|MISO|
|---|---|---|---|
|0|0x03|0x02|0|
|1|address[15:8]|address[15:8]|0|
|2|address[7:0]|address[7:0]|0|
|3|0 (ignored)|0 (ignored)|status|
|4|0|data[31:24]|READ data[31:24]; 0 for WRITE|
|5|0|data[23:16]|READ data[23:16]; 0 for WRITE|
|6|0|data[15:8]|READ data[15:8]; 0 for WRITE|
|7|0|data[7:0]|READ data[7:0]; 0 for WRITE|

WRITE commits atomically when the 64th rising edge is internally detected. Early NSS release discards the transaction without incrementing WRITE_COUNT. Additional clocks (65 onward) are ignored until NSS release. No burst mode, address auto-increment or SPI4 CRC.
READ captures a value when the address is received. ADC diagnostic reads may contain a value up to one internal clock old (about 8.3 ns) due to pipelining. There is no atomic multi-register snapshot.
**WRITE status=0 acknowledges the header only**, not completion of data reception or DAC transmission. READ back the setting and also check DAC transmission status when applicable.

### 4.3 Status

|Mask|Meaning|
|---|---|
|0x00|Normal|
|0x01|Invalid opcode|
|0x02|Unaligned address|
|0x04|Unmapped address|
|0x08|WRITE to read-only register|

Conditions are ORed. On errors, MISO bytes 4–7 are zero; no register modification or output request occurs. READ 0x0101 returns 0x02; READ 0x0001 returns 0x06. Read-only WRITE checking is not applied to an invalid opcode.

## 5. Complete SPI4 register map

RW16/RW24 still use the 32-bit data field in an eight-byte frame. Unused upper bits are ignored on writes and read as zero. RO means read-only.

|Address|Name|Access|Initial value / description|
|---|---|---|---|
|0x0000|DEVICE_ID|RO|0x4A503538|
|0x0004|PROTOCOL_VERSION|RO|0x00010003|
|0x0008|CAPABILITIES|RO|0x0000000F: bit 0 general DUMMY, bit 1 DAC, bit 2 ADC→SPI1, bit 3 dummy source|
|0x000C|DUMMY_COUNT|RO|16|
|0x0010|WRITE_COUNT|RO|0; increments on every completed valid RW WRITE, including unchanged values|
|0x0014|DAC_STATUS|RO|0; see below|
|0x0018|DAC_TRANSFER_COUNT|RO|0; total completed four-frame IC transmissions|
|0x001C|DAC_CHANNEL_COUNT|RO|16|
|0x0020|DAC_SDO_LEVEL|RO|bits0–3: synchronized SDO1–4 levels, not DAC register readback|
|0x0100 + 4×i, i=0–15|DUMMY[i]|RW32|All 0; communication test only; last 0x013C|
|0x0200 + 4×i, i=0–15|DAC_SET[i]|RW16|All 0; WRITE requests transmission to the IC; last 0x023C|
|0x0300 + 4×i, i=0–15|DAC_SENT[i]|RO16|All 0; last completed transmitted codes; last 0x033C|
|0x0400|ADC_CONTROL|RW|bit 0 TX enable (initial 0); bit 1 clear pulse (reads 0); other bits ignored|
|0x0404|ADC_STATUS|RO|FIFO FULL/EMPTY and SPI1 BUSY; below|
|0x0408|ADC_TX_COUNT|RO|0; total completed SPI1 records, after Gray synchronization|
|0x040C|ADC_CHANNEL_COUNT|RO|3|
|0x0410 / 0x0414 / 0x0418|ADC_RX_COUNT[0..2]|RO|All 0; completed ADC frames/generated dummies, including FIFO drops|
|0x0420 / 0x0424 / 0x0428|ADC_DROP_COUNT[0..2]|RO|All 0; words discarded due to FIFO full|
|0x0430 / 0x0434 / 0x0438|ADC_RESTART_COUNT[0..2]|RO|All 0; DRDY restarts during incomplete real ADC frames|
|0x0440 / 0x0444 / 0x0448|ADC_LAST[0..2]|RO24|All 0; latest completed ADC/generated dummy, updated even on FIFO drops|
|0x0450 / 0x0454 / 0x0458|ADC_FIFO_LEVEL[0..2]|RO|All 0; 0–256 words, write-side estimate with synchronization latency|
|0x0480|ADC_SOURCE_MODE|RW|0; bit 0 only: real ADC=0, dummy=1|
|0x0484|ADC_DUMMY1|RW24|0; ADC1 dummy raw value|
|0x0488|ADC_DUMMY2|RW24|0; ADC2 dummy raw value|
|0x048C|ADC_DUMMY3|RW24|0; ADC3 dummy raw value|

Other addresses are unmapped. All 32-bit counters wrap. SDO_LEVEL depends on external pins and has no fixed initial logic value. ADC_STATUS EMPTY becomes 1 after FIFO initialization; FULL/BUSY are 0.

### 5.1 Status bits

|Register|Bits|Meaning|
|---|---|---|
|DAC_STATUS|0–3|DAC1–4 BUSY|
|DAC_STATUS|4–7|DAC1–4 PENDING (retransmission requested)|
|DAC_STATUS|8–11|DAC1–4 INITIALIZED (at least one completed four-frame transmission)|
|ADC_STATUS|0–2|ADC1–3 FIFO FULL|
|ADC_STATUS|8–10|ADC1–3 FIFO EMPTY (write-side synchronized state)|
|ADC_STATUS|16|SPI1 BUSY (CRC preparation through transmission completion, synchronized); 0 in IDLE/inter-frame GAP|

Other bits are zero. Status includes synchronization delays.

## 6. DC configuration of 16 DAC channels

### 6.1 Channel assignments

|CH|Index|DAC IC / output|DAC_SET|DAC_SENT|DAC command|
|---|---|---|---|---|---|
|1|0|DAC1 IC11 / A|0x0200|0x0300|0x10|
|2|1|DAC1 IC11 / B|0x0204|0x0304|0x11|
|3|2|DAC1 IC11 / C|0x0208|0x0308|0x12|
|4|3|DAC1 IC11 / D|0x020C|0x030C|0x13|
|5|4|DAC2 IC13 / A|0x0210|0x0310|0x10|
|6|5|DAC2 IC13 / B|0x0214|0x0314|0x11|
|7|6|DAC2 IC13 / C|0x0218|0x0318|0x12|
|8|7|DAC2 IC13 / D|0x021C|0x031C|0x13|
|9|8|DAC3 IC15 / A|0x0220|0x0320|0x10|
|10|9|DAC3 IC15 / B|0x0224|0x0324|0x11|
|11|10|DAC3 IC15 / C|0x0228|0x0328|0x12|
|12|11|DAC3 IC15 / D|0x022C|0x032C|0x13|
|13|12|DAC4 IC17 / A|0x0230|0x0330|0x10|
|14|13|DAC4 IC17 / B|0x0234|0x0334|0x11|
|15|14|DAC4 IC17 / C|0x0238|0x0338|0x12|
|16|15|DAC4 IC17 / D|0x023C|0x033C|0x13|

### 6.2 Transmission

A valid DAC_SET WRITE stores the low 16 bits and snapshots requested A–D values for the selected IC. Four frames are sent in A→B→C→D order. Other ICs are not transmitted to. Writing the same value still triggers a transmission.

- SCLK=20 MHz, High/Low 25 ns each, idle High.
- Each frame: 8-bit command + 16-bit code, MSB first; **DAC samples falling edges**.
- SYNC_N Low for each 24-bit frame, High at least 100 ns between frames.
- SYNC assertion to first falling edge: 25 ns; last falling edge to SYNC release: 75 ns.
- Four frames for one IC take approximately 5.4 µs.
- Completion updates all four DAC_SENT values together, sets INITIALIZED and increments DAC_TRANSFER_COUNT.
- Writes during transmission set PENDING; the latest four requested values are retransmitted afterward. There is no FIFO retaining every intermediate value.

**The first WRITE also sends initial code 0 to the other three unconfigured channels of that IC.** This is not a simultaneous 16-channel update. The board is assumed to tie LDAC Low; each output changes in order on its SYNC release.
For completion, wait for the IC's BUSY/PENDING to clear, require INITIALIZED, then compare DAC_SENT with the requested code. DAC_SENT is FPGA transmission history, not an SDO acknowledgment from the DAC.

The interface carries **raw 16-bit codes**. Code 0 is not universally 0 V: BIN/2sCOMP, reference, gain/offset and other external conditions determine voltage. RTL writes only data registers, not calibration/function registers. For the delivered board's specified ±10 V signed scaling, use the host conversion in the [customer guide](../../CUSTOMER_IO_TEST_en.md).

## 7. Real ADC reception

Select ADC_SOURCE_MODE=0. SCLK is asynchronous to the FPGA. Frame start is **DRDY rising**, not ordinary SPI CS.

1. DRDY rising starts a frame.
2. Sample DATA on 24 SCLK falling edges, MSB first.
3. Enqueue the completed value in that input's FIFO; no sign/scale conversion.
4. Ignore extra clocks until the next DRDY. DRDY during an incomplete frame discards that partial value, restarts and increments RESTART_COUNT.

Required timing at FPGA input pins: SCLK at most 20 MHz; High/Low at least 25 ns each; DATA setup/hold at least 16.7 ns before/after falling edges; DRDY High/Low at least 25 ns each; DRDY rising to first data-sampling falling edge at least 50 ns. Expected normal period is 12.8 µs (78.125 kHz) per ADC.
All nine signals pass through three synchronizer stages at 120 MHz before edge detection. Input SCLK is not an internal clock. Completed words cross a DCFIFO from 120 MHz write to 80 MHz read. DRDY is detected even while SCLK is stopped.

## 8. Dummy source

ADC_SOURCE_MODE=1 selects the low 24 bits of ADC_DUMMY1–3, generated together at 120 MHz/1536=**78.125 kHz each**. Real ADC pins are ignored; no ADC connection is necessary.
Dummy and real data share FIFO, SPI1 format, sequences and CRC. Records contain no dummy flag; identifiers remain A1/A2/A3. RX_COUNT/DROP_COUNT/LAST describe the selected source. Dummy RESTART_COUNT stays 0.

- ADC_CONTROL.bit 0 separately enables transmission. Mode 1 alone does not transmit.
- Only an actual mode **change** clears the ADC path, FIFOs, sequences and diagnostics. TX enable and dummy settings are retained.
- Rewriting the same mode does not clear anything.
- Dummy writes affect subsequent generation, not records already in FIFO.
- Generation continues between sequential writes to the three settings. To change them as a set, stop, configure, then restart with clear.

## 9. Continuous SPI1 record format

FPGA master, STM32 receiving slave; **Mode 0, 20 MHz, MSB first, 8-bit words**. Each NSS Low interval contains eight bytes/64 clocks.

|Byte|Contents|
|---|---|
|0|0xA1=ADC1, 0xA2=ADC2, 0xA3=ADC3|
|1|Per-input sequence[15:8]|
|2|Sequence[7:0]|
|3|Raw value[23:16]|
|4|Raw value[15:8]|
|5|Raw value[7:0]|
|6|CRC16[15:8]|
|7|CRC16[7:0]|

Sequences start at 0 per input, increment for every completed/generated word, and wrap 65535→0. FIFO drops still advance the sequence. Incomplete real ADC frames do not; monitor RESTART_COUNT. Sequences do not indicate simultaneous acquisition across channels or absolute time.
CRC is **CRC-16/CCITT-FALSE**, over bytes 0–5: poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0. CRC is big-endian.

```text
ADC1 / seq=0 / raw=0x123456 : A1 00 00 12 34 56 74 F9
ADC2 / seq=0 / raw=0x89ABCD : A2 00 00 89 AB CD 08 D6
ADC3 / seq=0 / raw=0xFFFFFF : A3 00 00 FF FF FF F3 B4
```

Idle: SCLK Low, NSS High, DATA Low. DATA normally changes on falling edges; STM32 samples rising edges. NSS assertion to first rising edge and last rising edge to release: 125 ns each. With continuous data, NSS High is about 287.5 ns; record start interval about 3.6875 µs.
Round-robin arbitration skips empty FIFOs. Do not assume fixed A1→A2→A3 order; classify by identifier and per-input sequence.

## 10. Buffers, bandwidth and STM32 reception

Each of three FIFOs stores 256 × 40-bit words (16-bit sequence + 24-bit value), about 3.2768 ms per input at 78.125 kHz.

- Raw data: 5.625 Mbit/s. Eight-byte records: **15 Mbit/s = 1.875 MB/s, 234,375 records/s**.
- Capacity including fixed gaps: approximately 271,186 records/s, about 86.4% utilization at the normal rate.
- When FULL, **discard new arrivals**, preserving queued order. Detect using DROP_COUNT and sequence gaps.
- Reception/generation and FIFO filling continue while TX is disabled; indefinite stops cause drops.
- No STM32 ready/ACK mechanism. The FPGA cannot detect STM32 SPI overruns or DMA errors.

Configure STM32 SPI1 as slave RX, hardware NSS input, Mode 0, 8 bit. PA4 uses NSS AF5, unlike SPI4's GPIO CS.
**Use circular DMA/double buffering across NSS boundaries and process batches of records.** Reinitializing an API per record within the approximately 287.5 ns gap is not intended. Finite-length Zephyr SPI transfers alone do not guarantee continuous reception. Check DMA-accessible RAM and D-cache coherence on STM32H7.
Monitor CRC, identifiers, per-input sequences, SPI and DMA errors. Sequence differences use 16-bit modular arithmetic: normally 1; 0 suggests duplicates, ≥2 missing records. Invalidate previous sequence state after clear/mode changes. For resynchronization, find an eight-byte candidate with valid identifier and CRC, then verify sequence continuity.

## 11. Recommended operations and byte sequences

### 11.1 Identification

Wait 10 ms after FPGA configuration completion, then read ID/version/capabilities via SPI4. ID alone cannot distinguish older versions.

```text
DEVICE_ID READ:
TX 03 00 00 00 00 00 00 00
RX 00 00 00 00 4A 50 35 38

VERSION READ:
TX 03 00 04 00 00 00 00 00
RX 00 00 00 00 00 01 00 03
```

### 11.2 Dummy test without ADC hardware

First write ADC_CONTROL=0 and wait for ADC_STATUS.bit 16 BUSY to clear. Then configure:

```text
ADC_DUMMY1=0x123456 : 02 04 84 00 00 12 34 56
ADC_DUMMY2=0x89ABCD : 02 04 88 00 00 89 AB CD
ADC_DUMMY3=0xFEDCBA : 02 04 8C 00 00 FE DC BA
ADC_SOURCE_MODE=1  : 02 04 80 00 00 00 00 01
```

Prepare STM32 SPI1 DMA before clearing and enabling transmission together:

```text
ADC_CONTROL=3     : 02 04 00 00 00 00 00 03
```

Check the three values, per-input sequence continuity, CRC and approximately 78.125 kHz per input. Sequence changes make CRC change even with constant dummy values.

### 11.3 Switch to real ADC

1. Write ADC_CONTROL=0 and wait for BUSY to clear.
2. Write ADC_SOURCE_MODE=0, clearing old FIFO/sequence state on a mode change.
3. Initialize STM32 receive state and DMA, then write ADC_CONTROL=3.
4. Monitor RX_COUNT, DROP_COUNT, RESTART_COUNT, CRC and sequences.

### 11.4 Set DAC CH1 to 0x1234

```text
WRITE DAC_SET[0]:
TX 02 02 00 00 00 00 12 34
RX 00 00 00 00 00 00 00 00

READ DAC_SET[0]:
TX 03 02 00 00 00 00 00 00
RX 00 00 00 00 00 00 12 34

READ DAC_SENT[0] after completion:
TX 03 03 00 00 00 00 00 00
RX 00 00 00 00 00 00 12 34
```

This retransmits DAC1 A–D; unconfigured B–D receive code 0. Also check BUSY/PENDING clear and INITIALIZED for that IC.

## 12. Stop, clear and power cycling

|Operation|ADC/SPI1 effect|DAC/general DUMMY effect|
|---|---|---|
|SPI4 NSS High|Resets SPI4 parser only|Settings retained|
|ADC_CONTROL=0|Completes current SPI1 record, stops new transmissions; reception/generation continues|None|
|ADC_CONTROL=2|TX disabled; clears ADC parsers/FIFOs/sequences/diagnostics/TX count|None|
|ADC_CONTROL=3|Same clear, then TX enabled|None|
|ADC_SOURCE_MODE changes|Clears ADC path; retains TX enable and three dummy settings|None|
|STM32-only restart|FPGA keeps operating|FPGA settings retained|
|FPGA reconfiguration / PLL lock loss|All internal state reset: real mode0, dummy values0, TX disabled|Requested/sent/DUMMY/counters reset to0; no automatic DAC transmission|
|Board power cycle|Loads configuration from Flash; initializes internal state|Reapply DAC settings etc. from STM32|

Clear/mode changes can interrupt a current SPI1 record. Normally stop → wait BUSY clear → change. If clearing mid-record, discard the final short record on the receiver.
A POF preserves the **circuit across power loss, not runtime DAC/dummy/TX settings**. Resetting all internal state does not directly reset external DAC analog outputs; those depend on its supplies/control pins. Explicitly resend required codes.

## 13. Programming files and verification status

Current files: `output_files/spi4_adc_dac.sof` (SRAM) and `output_files/spi4_adc_dac.pof` (internal Flash). The Quartus session reported successful programming/verification of this POF:

```text
POF SHA-256:
4ca165e56663aa9a05f5071a914ff46301c3afedabf953dc62b4baed441eed7d
SOF SHA-256:
b725f3cf1fa165119b086da43c951653b69b25f60eb13905e2d6bef29f99e1f9
JTAG ID: 0x031030DD
Quartus Programmer POF checksum: 0x04677A1D
```

807,670 functional simulation checks passed. Synthesis, fitting, all three timing corners and Gray CDC constraints were checked without timing violations. Remaining compilation warnings and CDC details are in the Quartus project's `VERIFICATION.md`.
Within the original Quartus report, Flash programming was verified but STM32 SPI4 communication, continuous hardware SPI1 DMA, real ADC timing, DAC analog voltages and the complete power-cycle sequence remained unverified. Later Zephyr-side evidence is recorded separately in the [delivery documentation](../../CUSTOMER_DELIVERY_en.md) and [board acceptance record](../../measurements/2026-10-25-board-00250028-35335115-39383331/README_en.md).

## 14. Related sources

In the original Quartus tree:

- `quartus/projects/spi4_adc_dac/README.md`: build/programming/simulation procedures.
- `quartus/projects/spi4_adc_dac/ZEPHYR_ADC_STREAM_HANDOFF.md`: STM32 ADC/dummy implementation notes and C decoder.
- `quartus/projects/spi4_dac_dc/ZEPHYR_DAC_HANDOFF.md`: legacy DAC/SPI4 DTS/C examples. This integrated specification takes precedence for current version, complete register map and programming status.
- `quartus/projects/spi4_adc_dac/rtl/`: implementation underlying this specification.

Those external-tree paths are source references, not local links. Archived Japanese handoffs remain available alongside this file.
