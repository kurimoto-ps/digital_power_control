# JPARC rev58 test software delivery

[日本語](CUSTOMER_DELIVERY.md)

Date: 2026-10-25. Target: STM32H745 M7 `adc_dummy` profile and MAX10 integrated ADC/DAC firmware v1.3.
This delivery supports board communication, memory, and I/O verification. Power-converter feedback loops, PWM control, and PLC control are outside its implementation scope.

## 1. Software architecture

```text
Customer host PC (Python 3, standard library only)
  └ Ethernet / TCP 4244
      └ STM32H745 Cortex-M7 / Zephyr
          ├ At startup: external SDRAM 8 MiB test
          ├ SPI4 master: MAX10 register access
          └ SPI1 slave + DMA: MAX10 dummy ADC stream validation
              MAX10 integrated ADC/DAC v1.3
                ├ Optical inputs: O/E board → ADC1–3 receivers → latest-value registers
                ├ Internal dummy generator → FIFOs → SPI1 transmitter
                └ DAC1–4 (four outputs each): 16 output channels
```

|Item|Delivered configuration|
|---|---|
|STM32|STM32H745XIH6; M7 only. BCM7=1, BCM4=0; M4 boot disabled|
|OS and clocks|Zephyr; SYSCLK 400 MHz, HCLK 200 MHz, VOS1|
|Network|Static `192.168.100.58/24`, TCP port 4244; no DHCP|
|SPI4|STM32 master, Mode 0, MSB first, 8 bit. Requested 1 MHz (781.25 kHz with the current clock); eight bytes per request|
|SPI1|STM32 slave RX, Mode 0, 20 MHz; PG11=SCLK, PA4=NSS, PB5=DATA, all driven by FPGA|
|DMA|32 KiB circular buffer in internal AXI SRAM. ADC capture storage in SDRAM is not implemented|
|SDRAM|8 MiB starting at 0xD0000000: 2,097,152 32-bit integers. Destructive test on every boot|
|FPGA identity|ID `0x4A503538`, version `0x00010003`, capabilities `0x0000000F`|
|ADC|Three 24-bit inputs; internal dummy generation at 78.125 kHz each. ADC4–6 are not implemented|
|DAC|Four ICs, four outputs each; ±10 V, signed 16-bit two's complement. Address CN3–CN18 by voltage or raw code. No automatic analog voltage readback|

M7 handles Ethernet, SPI4, SPI1 DMA, and SDRAM testing. There is no M4 workload.
The same server serializes SPI4 requests and dummy tests. Do not run multiple test scripts simultaneously.

## 2. Files managed in GitHub

This board is under `targets/jparc_rev58/`; the existing Nucleo target is separate under `targets/nucleo_h755zi_q/`.

|Location relative to the target|Contents|
|---|---|
|`boards/`|rev58 board definition|
|`apps/bringup/`|STM32 test application, profiles, Ethernet server, DMA receiver|
|`lib/fpga_link/`|SPI4 protocol and ADC record decoder|
|`scripts/spi4_client.py`|Communication and register tests; basic DAC setting|
|`scripts/adc_dummy_client.py`|Internal FPGA dummy → SPI1 DMA test|
|`scripts/adc_input_client.py`|Real ADC latest values and voltages as CSV|
|`scripts/dac_output_client.py`|DAC voltage/code commands; connectors, nominal voltages and FPGA transmission status as CSV|
|`docs/CUSTOMER_IO_TEST_en.md`|Customer connections, commands, and result interpretation|
|`docs/hardware/rev58_adc_dac/`|Current integrated FPGA specification and archived handoff documents|
|`docs/hardware/circuit/`|Motherboard, DAC and O/E schematics; connector usage; SHA-256 checksums|

Identify the delivered source using its Git commit ID. The host-script additions do not change STM32 firmware.
The STM32 application baseline is `edd9817` (`adc_dummy`). Record the actual delivery commit with `git rev-parse HEAD`.
Build outputs are not committed to this repository. Distribute programming HEX/BIN and FPGA POF/SOF files as GitHub Release attachments with their source commit and SHA-256. This change does not create a GitHub Release or publish binaries.

## 3. Verification scope

|Test|Previously observed hardware result|
|---|---|
|STM32 startup|After disabling M4 boot: `rev58_status.error=0`, ticks advancing|
|SDRAM|8 MiB test completed; `rev58_memtest.stage=5`, failed address 0|
|Ethernet → SPI4|Write/readback matches; 16 dummy registers and 176 verified writes PASS|
|FPGA internal ADC dummy|1 s, 3 s, and repeated 100 ms tests; no CRC, sequence, value, or DMA errors|
|Example dummy values|Hex `123456/89ABCD/FEDCBA` and `000000/FFFFFF/800000`|
|Standalone power-up|Power-cycle communication verified with the earlier Ethernet + SPI4 configuration|

Individual DAC CN assignments are confirmed from TEP522A. Real ADC optical inputs, actual DAC accuracy/polarity/wiring, and long-duration operation require customer testing. Host-script tests are distinct from testing the attached analog circuits.

For the subsequent replacement-board acceptance, including standalone boot with customer firmware, see the [hardware record](measurements/2026-10-25-board-00250028-35335115-39383331/README_en.md).

## 4. Startup and programming

Normal testing requires only Python 3 on the host and Ethernet. ST-LINK, USB Blaster, Docker and the Zephyr SDK are not required.
Once STM32 internal Flash and FPGA nonvolatile configuration are programmed, power cycling starts the system; no debugger `continue` is needed.
The TCP server starts after the SDRAM test. Power cycling loses FPGA dummy values, transmission enable, and DAC register settings. Reapply required settings from the host. Measure the DAC's actual power-up output separately.

To rebuild and program in the Zephyr development environment, from the repository root:

```sh
./scripts/build.sh jparc_rev58 adc_dummy
./scripts/flash.sh jparc_rev58 adc_dummy
```

Outputs: workspace `build-jparc-rev58-adc_dummy/zephyr/`. Board: `jparc_rev58/stm32h745xx/m7`.
See [bring-up](BRINGUP_en.md) for environment, power and BCM4 configuration; [ADC dummy testing](ADC_DUMMY_TEST_en.md) and [SPI4 testing](SPI4_TEST_en.md) for details.

## 5. FPGA specification and programming-file identity

The current protocol, registers, reset values and behavior are defined by the Quartus maintainer's [FIRMWARE_SPEC_v1.3_en.md](hardware/rev58_adc_dac/FIRMWARE_SPEC_v1.3_en.md).
The Japanese original was obtained from `quartus/projects/spi4_adc_dac/FIRMWARE_SPEC_v1.3.md` and takes precedence over older `ZEPHYR_*_HANDOFF.md` files. Archived originals are unmodified; their relative links refer to the original Quartus directory layout.

|Item|Quartus record|
|---|---|
|Project / top|`spi4_adc_dac` / `jparc_adc_dac_top`|
|Device|MAX10 `10M16DCF484I7G`|
|Tool|Quartus Lite 25.1std.0 Build 1129|
|Nonvolatile programming file|`output_files/spi4_adc_dac.pof`|
|SRAM programming file|`output_files/spi4_adc_dac.sof`|
|JTAG ID / POF checksum|`0x031030DD` / `0x04677A1D`|
|Programming|Specification reports POF programming and verification successful, zero errors/warnings|
|Design verification|Specification reports 807,670 simulation checks and timing/CDC verification|

```text
POF SHA-256: 4ca165e56663aa9a05f5071a914ff46301c3afedabf953dc62b4baed441eed7d
SOF SHA-256: b725f3cf1fa165119b086da43c951653b69b25f60eb13905e2d6bef29f99e1f9
```

The supplied POF/SOF hashes were calculated and matched these values during documentation preparation.
The FPGA specification's statement that STM32 communication remains unverified describes the Quartus maintainer's verification scope. Read it together with the Zephyr test results in section 3.
Historical Zephyr tests recorded ID/version/capabilities, but did not record the bitstream hash. Repeat and record the communication and dummy tests in the [customer procedure](CUSTOMER_IO_TEST_en.md) for acceptance of the identified POF.

## 6. Circuit and voltage specification sources

The [schematic index](hardware/circuit/README_en.md) identifies drawing numbers, revisions and pages.
Tracing IC11/13/15/17 outputs A/B/C/D (pins 23/22/19/18) to the LEMO center contacts in TEP522A confirms **CH1–16 = CN3–CN18**. LEMO shells connect to AGND.
Use TEP523A for O/E, TEP521C for motherboard routing, and Connector_usage_rev58.pdf for rev58 connections.

DAC voltage commands implement the user-specified **±10 V, signed 16-bit two's complement** scale: `V = signed_code × 10 / 32768`.
The FPGA's raw 16-bit interface is compatible with converting volts into codes on the host.
One LSB is approximately 305.176 µV; maximum positive voltage is +9.999694824 V. A +10 V request is limited to the maximum positive code and the resulting nominal voltage is displayed. Requests outside ±10 V are rejected.
The FPGA only writes DAC data registers; it does not initialize calibration or function registers. Displayed voltages are nominal, not measurements.

ADC encoding defines how the 24-bit word represents a number: `0xFFFFFF=-1` in two's complement, but `16777215` unsigned.
The FPGA does not convert the encoding, and the O/E board only converts optical signals into digital signals. The upstream transmitting ADC is absent from the supplied schematics, so its encoding cannot be established from these documents.
Select `--encoding signed24`, `unsigned24`, or `offset-binary` to match the source ADC or external dummy generator. The host applies the requested `3 / 2^23` voltage factor.

Remaining information: upstream ADC encoding and the Quartus source Git commit for reproducible builds. Connector assignments and DAC voltage scaling are resolved.
