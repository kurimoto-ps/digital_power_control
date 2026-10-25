# STM32 acceptance of a new rev58 board (2026-10-25)

[日本語](README.md)

Board identity: STM32 UID, read as three 32-bit words from 0x1FF1E800: `00250028 / 35335115 / 39383331`.
A different new board of the same type replaced the previous unit. MAX10 had already been programmed by the user.
Software source: `0a6f953`; STM32 application baseline: `edd9817`.

## Initial settings and programming

ST-LINK V3SET, SWD, target 3.27 V; STM32 Device ID 0x450, Rev V, Flash 2 MiB.
Initial BCM4=1, BCM7=1, RDP=0xAA (Level 0). STM32CubeProgrammer v2.22.0 changed only BCM4 to0 and verified programming.
Runtime SYSCFG_UR1=0x00010000 confirmed M4 disabled/M7 enabled. Other protection/power option bytes were not changed.
Profiles were programmed sequentially with OpenOCD; explicit post-program `verify` passed for sdram and subsequent profiles. Artifact power/clock/pin/RAM checks passed for every profile.

|Profile|Hardware result|
|---|---|
|bringup|error=0, ticks=6, SYSCLK 400 MHz, HCLK 200 MHz|
|sdram|error=0,8 MiB stage=5; failed address/expected/actual all 0|
|ethernet|error=0, PHY ID read succeeded, RTT reported link up|
|spi|error=0, SPI1–4 drivers ready; actual SPI2/3 transfers not tested|
|spi4_regs|FPGA v1.3/caps 15; zero-initial-state 176-write test PASS (0→176);0x0100 write/readback0x12345678 matched|
|combined|SDRAM stage 5, error 0;176-write test PASS (177→353)|
|adc_dummy (final programmed profile)|SDRAM stage 5, error 0;176-write test PASS (353→529); DMA tests below passed|

FPGA identity: ID 0x4A503538, version 0x00010003, capabilities 0x0000000F.
The hash of the FPGA file programmed onto this new board was not independently checked because the user performed that programming.

## Dummy ADC tests with customer firmware

|Hex values|Duration|Received records|Result|
|---|---|---|---|
|123456 / 89ABCD / FEDCBA|1 s|234,793|PASS|
|000000 / FFFFFF / 800000|3 s|703,783|PASS|
|Defaults, three repeats|100 ms each|23,845 /23,847 /23,846|All PASS|
|555555 / AAAAAA /123456|1 s|234,795|PASS|

All received totals matched FPGA_TX_COUNT. CRC/value/sequence/SPI/DMA errors and in-stream DROP/RESTART were zero. Raw diagnostics: [adc_results.json](adc_results.json).
A host-disconnect test closed the connection immediately after requesting a one-second ADCTEST. Two seconds later, a new connection confirmed CONTROL=0 and SOURCE_MODE=0.
DAC_TRANSFER_COUNT=0; no DAC output request was issued.
Final GDB inspection: ticks 36, error 0, SDRAM stage 5. CPU was resumed and OpenOCD exited.

The first network attempt failed because the debugger had left the CPU halted. Explicit resume restored communication; subsequent tests ran with the CPU executing.
Initial GDB locations in multi-profile logs are decoded with the new ELF before the old image is replaced: those function names/arguments are not diagnostic evidence. Acceptance uses structures read after programming/reset and the subsequent network results.

## Final Flash image

Customer image in `build-jparc-rev58-adc_dummy/zephyr/` was programmed and verified.

```text
zephyr.hex SHA-256: 169b05152db4e75fb2e9f15a7869a3f78903bbc400073d45196ae9d42e489f2c
zephyr.bin SHA-256: 588cd563cd1c7fbf0e5994d1c178e28c45b0ba60640747d613a4d47e75ffe0d0
```

## Power cycle

After the user switched power off/on, the following passed **without OpenOCD/GDB connected**, confirming cold boot:

- Ethernet read FPGA ID/version/capabilities successfully; initial WRITE_COUNT=0.
- `spi4_client.py test --expect-reset`: zero initial values and 176-write test PASS (0→176).
- Customer firmware's one-second dummy ADC test:234,794 records, matching FPGA transmitted count, all errors0, PASS.
- The TCP server starts only after the SDRAM test passes, also confirming completion of the post-power-cycle SDRAM startup test.
- Final CONTROL=0, SOURCE_MODE=0, DAC_TRANSFER_COUNT=0; customer firmware left running without a debugger.

No STM32 reprogramming was performed after the power cycle.

## Scope

Real ADC optical inputs, DAC analog outputs, long-duration operation, temperature/supply variation and waveform measurements were not tested.
READY/FAULT/IRQ raw=1 values were recorded, not interpreted as successful FPGA notification operation.
See [debug_results.txt](debug_results.txt) and the [customer procedure](../../CUSTOMER_IO_TEST_en.md).
