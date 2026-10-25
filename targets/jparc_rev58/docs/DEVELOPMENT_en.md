# rev58 development conditions and sequence

[日本語](DEVELOPMENT.md)

Historical baseline dated2026-10-11: M7-only board definition and startup/peripheral application implemented; hardware operation not yet verified at that time. See [bring-up](BRINGUP_en.md), and the [current delivery document](CUSTOMER_DELIVERY_en.md) for subsequent results. Sources are implementation hardware documents and technical description v1 in `hardware/rev58/`.

## Board conditions

|Item|Supplied specification|
|---|---|
|MCU|STM32H745XIH6, M7/M4|
|Power|Direct SMPS, VOS1; M7 maximum 400 MHz, M4 maximum 200 MHz|
|HSE|External 25 MHz, PH0, bypass|
|SDRAM|FMC BANK2,16 bit, row12/column8,4 banks,8 MiB,0xD0000000|
|Ethernet|LAN8742A, RMII, PHY supplies 50 MHz; address 0 initial candidate|
|SPI|SPI1 FPGA master→MCU slave RX; SPI2/3 MCU TX; SPI4 bidirectional|
|Synchronization|PA0 external cycle, PD13 trigger; actual frequency/update period undecided|
|FPGA notifications|PD11 READY, PJ11 FAULT, PD12 IRQ, PK0 HEARTBEAT|

100 MHz SDRAM/20 MHz SPI were initial design targets, not hardware-verified values in this baseline. MAX10 register-test SPI4 v1 is limited to 2 MHz; the [Ethernet test](SPI4_TEST_en.md) requests 1 MHz.
Stacks/BSS/heap must be internal before SDRAM initialization. PD11/PJ11 share EXTI11. Distinguish PC3 SPI2 from PC3_C ADC. UART8 PJ8/PJ9 TX/RX direction needs checking.

## Sequence

1. H745/SMPS handling reviewed; board-local VOS1 adaptation applied. Recheck on Zephyr updates.
2. Board definition and `apps/bringup` implemented. Verify M7-only startup, internal RAM, SWD, power/HSE/GPIO on hardware.
3. Verify SDRAM initialization, full 8 MiB, byte lanes, retention and cache/DMA coherence.
4. Check Ethernet PHY ID/link and SPI paths, starting at low speeds.
5. Define pattern format, transfer protocol and validation/activation state transitions.
6. Assign M7/M4/FPGA resources, IPC, memory and periodic control; then introduce M4.
7. Measure synchronization, fault behavior, latency and jitter using simulated I/O.

## Open design items in the baseline

- Channel/point counts, numerical format/units, update period and Ethernet transfer protocol.
- Production SPI mode, bit order, frame length, CRC, timeouts and ordering of READY/IRQ/receive arming. Register-test SPI4 v1 framing was defined and implemented, awaiting hardware checks at that time.
- Legacy pattern input versus SDRAM selection, legal switching states, fault outputs and recovery.
- Actual SDRAM clock/timings, DQM initialization, MPU/cache and DMA buffer placement.
- M4 boot method, shared RAM, peripheral ownership and FPGA protocol versioning.

Nucleo's PA3 ADC, PE8/PE9 PWM, fixed10 kHz, TCP 4242 and RPMsg protocol are not established rev58 requirements. Review each before adopting it.
