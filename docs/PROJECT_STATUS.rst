Project status
##############

Last updated: 2026-10-25

Purpose
*******

This project is a customer-facing development starter for digital power-control
applications on the NUCLEO-H755ZI-Q. It separates reusable communication and
safety infrastructure from customer-editable PWM, ADC, and feedback code.

Current implementation (NUCLEO-H755ZI-Q)
****************************************

Dual-core architecture
======================

* Cortex-M7 runs Ethernet, a TCP command server, RPMsg client, and heartbeat.
* Cortex-M4 runs complementary PWM, ADC acquisition, PI feedback control, and
  communication/ADC-failure shutdown behavior.
* RPMsg uses shared SRAM4 and the STM32 HSEM mailbox.
* The shared protocol version is 3.

Network interface
=================

* Static IPv4 address: ``192.168.100.2``
* TCP port: ``4242``
* Commands: ``SET``, ``MODE FEEDFORWARD``, ``MODE FEEDBACK``, ``GET``,
  ``STATUS``, ``OFF``, and ``HELP``

Control modes
=============

``FEEDFORWARD``
  Uses frequency, duty, and dead time from the latest ``SET`` command.

``FEEDBACK``
  Uses frequency and dead time from ``SET``. The second argument is the ADC target
  percentage. A bounded customer-editable PI controller calculates duty after each
  fixed-rate 10 kHz ADC DMA sample.

PWM
===

* Timer: TIM1
* High-side output: TIM1_CH1 on PE9
* Complementary low-side output: TIM1_CH1N on PE8
* Demonstration range: 20..20000 Hz, 0..100% duty, 0..4000 ns dead time
* PWM starts disabled.
* M4 stops PWM if M7 heartbeat is absent for more than 2 seconds.

ADC and feedback demonstration
==============================

* Connector: Arduino analog header A0
* MCU input: PA3 / ADC1_INP15
* ADC resolution: 16 bit
* Reference: VDDA, configured/documented as approximately 3.3 V on the board
* Target mapping: 0..100% maps to ADC raw 0..65535 and approximately 0..3.3 V
* Feedback plant: PWM output through two cascaded 4.7 kOhm / 1 uF RC sections to A0
* ADC DMA failure stops PWM and sets fault bit 1.
* TIM6 update triggers ADC1 at a fixed 10 kHz rate, independent of PWM
  frequency. The nominal ADC and feedback period is 100 us.
* ADC1 channel 15 preselection and 810.5-cycle sampling time are configured
  explicitly. DMAMUX1 channel 0 routes ADC1 requests to a 16-bit DMA buffer.
* DMA completion interrupt handling is contained in ``targets/nucleo_h755zi_q/control_core/platform``.
  A dedicated M4 feedback thread calls customer code and updates PWM duty.
* The feedback implementation is a bounded PI controller with Kp=0.5, Ki=50 /s,
  output saturation, and conditional-integration anti-windup.

Fault bits
==========

* Bit 0: M7 communication heartbeat timeout
* Bit 1: ADC DMA failure

Build and recovery status
*************************

* Repository remote: ``https://github.com/kurimoto-ps/digital_power_control.git``
* Default branch: ``main``
* Zephyr is pinned in ``west.yml`` to commit
  ``d9c3a39174fdc96b5177faa41c62b056c224512b``.
* ``scripts/setup-workspace.sh`` restores Zephyr and modules.
* ``scripts/build.sh`` performs a clean dual-core build.
* The devcontainer bind-mounts the host workspace at ``/workdir``.

Verified
********

* M7 and M4 sysbuild configuration succeeds.
* M7 image builds with Ethernet, TCP server, and RPMsg client.
* M4 image builds with STM32 ADC, DMA/DMAMUX, TIM6-triggered sampling,
  complementary PWM, RPMsg remote endpoint, and feedback-mode logic.
* A fresh local Git clone can initialize a west workspace using
  ``west init -l digital_power_control``.
* A0 ADC acquisition and the former pass-through behavior were verified on hardware
  with an external DC source sharing Nucleo GND. The new PI loop with the two-stage
  RC feedback plant has not yet been verified on hardware.

Not yet verified on hardware
****************************

* ADC voltage accuracy and calibration across the complete 0..3.3 V range.
* Complementary PWM behavior at 0% and 100% duty with the intended gate driver.
* Dead-time accuracy on an oscilloscope.
* ADC input behavior under noisy power-electronics conditions.
* Hardware Break input and independent over-current shutdown.
* Complete power-stage startup, shutdown, and fault-recovery sequences.

Known limitations
*****************

* ADC sampling is hardware-triggered at 10 kHz, but feedback-thread latency,
  missed notifications, jitter, and CPU load have not yet been measured on hardware.
* Every 10 kHz ADC sample produces a DMA interrupt and wakes the feedback thread. At
  high PWM frequencies this may need batching or a lower interrupt rate.
* Initial PI gains are demonstration values and require plant-specific tuning and
  hardware stability verification.
* Commands and RPMsg protocol have no authentication or encryption.
* TCP server handles one client at a time.
* The demonstration accepts 0% and 100% high-side duty. Real hardware may need
  narrower duty limits and additional interlocks.

Recommended next work
*********************

#. Characterize A0 voltage accuracy across 0..3.3 V and verify complementary
   PWM waveforms on an oscilloscope.
#. Add TIM1 Break input and independent over-current protection before connecting
   a power stage.
#. Define product-specific safe minimum/maximum duty and dead-time limits.
#. Measure ADC trigger phase, DMA-to-duty latency, jitter, and CPU load on hardware.
#. Define measured-value scaling and calibration interfaces for voltage/current.
#. Tune and validate the PI controller against the intended power stage.
#. Add automated tests for command parsing, protocol compatibility, mapping, and
   fault transitions.

J-PARC rev58 preparation (2026-10-11)
************************************************************

* Added ``targets/jparc_rev58/`` for STM32H745XIH6 + MAX10 development.
* Imported provided rev58 hardware documents and pin data with SHA256 checksums.
* Prepared separate board, bring-up, M7/M4, library, and test directories.
* Board support, firmware, core ownership, protocols, and hardware verification
  are pending. See ``targets/jparc_rev58/docs/DEVELOPMENT.md``.

Parallel target layout (2026-10-11)
************************************************************

* Moved Nucleo and rev58 development under sibling ``targets/`` directories.
* Scoped the existing board architecture instructions to the Nucleo target.
* Build and flash scripts accept a target name and default to Nucleo.
* Default build output is now ``build-nucleo-h755zi-q`` in the west workspace.
* The rev58 target remains a development scaffold and has no build or flash entry yet.
* Verified a clean Nucleo M7/M4 sysbuild from the new source path using
  Zephyr commit ``d9c3a39174fdc96b5177faa41c62b056c224512b`` and SDK 1.0.1.
* Verified build/flash argument routing without hardware access. No flashing or
  new hardware validation was performed for this directory migration.

J-PARC rev58 M7 bring-up (2026-10-11)
************************************************************

* Added the out-of-tree ``jparc_rev58/stm32h745xx/m7`` board for STM32H745XIH6.
* HSE is 25 MHz bypass; SYSCLK/HCLK/APB are 400/200/100 MHz with Direct SMPS.
* A board-local, SHA256-guarded build adaptation uses VOS1 in the STM32H7 clock
  driver. The shared Zephyr checkout and Nucleo clock driver remain unchanged.
* M7 uses internal Flash and AXI SRAM; D-cache is disabled for bring-up visibility.
  M4 is not started by software. BCM4 option bytes must disable M4 hardware boot.
* Added minimal bring-up, SDRAM, Ethernet, and SPI profiles, with isolated outputs.
  SDRAM tests overwrite 8 MiB. Ethernet reads PHY ID/link. SPI initializes drivers
  without transferring unspecified FPGA frames.
* Added build-artifact checks for clock/power, RAM placement, and peripheral pins
  against the supplied netlist. An altered upstream clock source was rejected by
  the VOS1 guard in a configuration test.
* See ``targets/jparc_rev58/docs/BRINGUP.md`` for commands and initial hardware checks.
* M4, operating protocols, pattern management, control, and all hardware validation
  remain pending. No board programming or option-byte writes have been performed.
* Verified all four rev58 profile builds and the existing Nucleo M7/M4 build
  with the pinned Zephyr commit and SDK 1.0.1. Artifact verification passed for
  clock/VOS1, internal RAM placement, and 38 SDRAM / 9 Ethernet / 13 SPI pins.
* Build/flash target/profile routing was checked without accessing hardware;
  OpenOCD and STM32CubeProgrammer runner configuration was generated successfully.

J-PARC rev58 Ethernet to SPI4 register test (2026-10-11)
************************************************************

* Added ``spi4_regs`` bring-up profile: static IPv4 192.168.100.58/24,
  TCP 4244, INFO/READ/WRITE commands, and a standalone Python host client.
* Implements MAX10 SPI4 register protocol v1.0 from
  ``quartus/projects/spi4_regs/SPI4_PROTOCOL.md``. An unchanged reference copy
  and its checksum are stored in rev58 hardware documents.
* SPI4 uses Mode 0, MSB-first, single 8-byte full-duplex frames and active-low
  GPIO CS PE4, with 1 us setup/hold/inter-frame delays. Requested 1 MHz gives
  a calculated 781.25 kHz from the current 100 MHz kernel clock; measure on hardware.
* Checks CONF_DONE, waits 10 ms per command, validates FPGA ID/version,
  and verifies writes with a separate read frame. FPGA errors and transport
  failures are distinct. Writes are never automatically retried.
* Host ``test`` overwrites all 16 dummy registers and checks 176 writes,
  patterns, walking bits, address independence, FPGA error responses,
  preserved values after invalid requests, and wrapping WRITE_COUNT.
* Passed 12 host tests using the actual portable firmware C command handler,
  an independent register model, and loopback TCP for the Python client.
  Includes dropped write replies, invalid inputs, wrong identity, transport
  failure, readback mismatch, fragmented replies, and counter wrap.
* Built and verified all five rev58 profiles (bringup, sdram, ethernet, spi,
  spi4_regs), including VOS1/internal RAM and netlist checks. New profile has
  13 active peripheral pins including GPIO CS. Flash routing was checked with
  a mock west executable; no hardware was accessed.
* See ``targets/jparc_rev58/docs/SPI4_TEST.md`` for programming prerequisites,
  host addressing, commands, expected responses, and remaining measurements.
* Hardware operation, Zephyr TCP runtime, SPI signal timing, FPGA reconfiguration
  during traffic, and 2 MHz operation remain unverified. M4 and operating control
  protocols remain pending. Quartus sources and Nucleo code were not modified.

J-PARC rev58 first hardware bring-up (2026-10-21)
************************************************************

* User confirmed BCM4=0 / BCM7=1. Minimal bring-up reached error=0, ticks=6,
  SYSCLK=400 MHz and HCLK=200 MHz with Direct SMPS/VOS1.
* Host eno1 had 192.168.100.1/24 and the correct route, but ARP failed because
  the firmware exited before assigning its IP address.
* Diagnosed LAN8742 init_res=120 (EALREADY), despite MDIO responding at address 0
  with PHY ID 0x0007c131 and BMSR link/autonegotiation-complete bits set.
  Added a board-local, SHA256-guarded PHY driver adaptation accepting EALREADY
  only during initialization; other errors still abort initialization.
* Diagnosed SPI4 init_res=134 (ENOTSUP): the pinned H745 clock driver does not
  support PCLK2 as a domain source. Selected supported HSE 25 MHz, SPI45 mux 5;
  requesting 1 MHz still yields a calculated 781.25 kHz (25 MHz / 32).
* Enabled NET_CONTEXT_RCVTIMEO/SNDTIMEO for the TCP server. Without these,
  setsockopt failed and each accepted connection was immediately closed.
  Added a compile-time requirement and diagnostic for socket setup failures.
* Programmed updated test firmware over SWD. Ping from the container through
  the host succeeded, including two steady-state responses around 0.23 ms
  with no packet loss. No option-byte or FPGA configuration writes were made
  during this Ethernet diagnosis.
* With the final firmware, the host Python INFO command succeeded on hardware:
  DEVICE_ID=0x4a503538, version=0x00010000, capabilities=1, dummy_count=16,
  write_count=0. Ethernet/TCP and SPI4 register readback are now verified together.
* Hardware TCP checks passed for fragmented commands, pipelined INFO/READ,
  oversized-line rejection/close, and a 5-second idle-client timeout.
* Default bring-up, ethernet, spi and spi4_regs builds and artifact checks passed.
  Generated artifacts now check the supported HSE SPI4 source and the compiled
  PHY adaptation. No shared Zephyr sources were edited.
* FPGA register writes, the full 176-write test, and waveform timing measurements
  remain pending. The board is left running the corrected spi4_regs image.

J-PARC rev58 combined SDRAM and Ethernet/SPI4 profile (2026-10-21)
************************************************************

* User reported standalone SPI4 test PASS: 176 verified writes, WRITE_COUNT
  1 -> 177, after successful write/readback of 0x12345678 at 0x0100.
* User reported standalone SDRAM test stage=5 with zero failure fields.
* Added ``combined`` profile, composing the existing SDRAM and SPI4/Ethernet
  overlays and configuration fragments without duplicating their settings.
* On every boot, the existing destructive 8 MiB SDRAM test runs first. After
  successful peripheral checks the TCP SPI4 service starts at 192.168.100.58:4244.
  SDRAM failure prevents server startup and preserves the existing failure record.
* SDRAM remains mapped and initialized; program stacks and ordinary buffers remain
  in internal RAM. This does not yet add SDRAM-backed SPI4 payload storage or a
  network memory-access API. D-cache remains disabled and M4 boot must stay disabled.
* Build/flash commands accept ``jparc_rev58 combined`` and use the isolated output
  directory ``build-jparc-rev58-combined``. Host SPI4 commands are unchanged.
* Combined image built successfully; artifact verification passed for all 51
  active peripheral pins without collisions, clock/VOS1, PHY adaptation, and
  internal RAM placement. Generated DT confirms SDRAM, Ethernet and SPI4 enabled.
* Regression builds/artifact checks passed for bringup, sdram and spi4_regs.
  Combined flash routing was tested with a mock west; no board programming was
  performed for this change. Simultaneous operation on hardware remains to be checked.

J-PARC rev58 FPGA v1.3 dummy ADC DMA reception (2026-10-24)
************************************************************

* User previously reported combined-profile SPI4 test PASS (176 writes,
  WRITE_COUNT 177 -> 353), and after a power cycle INFO write_count=0 followed
  by test --expect-reset PASS (0 -> 176).
* Imported unchanged ADC v1.3 and DAC handoff references with SHA256 sums.
  Despite the ADC document's stale not-programmed note, user confirmation and
  runtime INFO verified FPGA ID=0x4a503538, version=0x00010003, capabilities=15.
* Added ``adc_dummy`` profile retaining the SDRAM self-test and Ethernet/SPI4.
  SPI1 is Mode 0, hardware NSS, 8-bit slave RX on PG11/PA4/PB5. A dedicated
  STM32 LL receive setup and Zephyr circular DMA preserve reception across NSS
  boundaries, without rearming each 8-byte record.
* Reserved DMAMUX1 channel 0 / DMA1 stream 0 / SPI1_RX request 37. Uses a
  32 KiB aligned AXI SRAM ring with 16 KiB half/full callbacks; D-cache remains
  disabled. Detects DMA errors, overwritten halves, and SPI OVR/MODF/TIFRE.
* ADCTEST performs identity/capability checks, stop/wait, three dummy value writes,
  mode selection, DMA arm, clear/start, bounded capture (100..3000 ms), stop/wait,
  final-byte collection, stable TX count read, and mode=0 cleanup. No DAC writes.
* Portable decoder validates CRC-16/CCITT-FALSE, channel tags, expected raw24 values,
  first sequence zero and modulo-16-bit continuity; corrupt input is resynchronized
  bytewise while retaining failure counters. Host independently checks all results.
* DROP/RESTART are sampled while streaming just before stopping: the dummy source
  keeps producing after TX stops and fills FIFOs in about 3.28 ms. Post-stop DROP
  is therefore not treated as streaming loss. TX count is read after BUSY clears.
* Added standalone stdlib host script ``scripts/adc_dummy_client.py`` with duration,
  three values, repetitions, and JSON results. Repetitions have deliberate gaps.
* SPI4 firmware/client now support known v1.0..v1.3. Write verification respects
  DAC 16-bit, ADC 24-bit and self-clearing CONTROL masks. Moved legacy dummy-test
  invalid address from 0x0200 (now DAC) to 0x0800. Added explicit host ``dac-set``
  with SET readback, BUSY/PENDING/INITIALIZED and SENT checks; no automatic DAC use.
* Programmed the new STM32 test firmware and verified hardware captures:
  default values, 1000 ms: 234791 records; 0/0xffffff/0x800000, 3000 ms:
  703775 records; 100 ms repeated three times: 23847 records each.
  All passed: received count=FPGA_TX_COUNT, zero CRC/sequence/value/SPI/DMA errors
  and zero pre-stop DROP/RESTART. CONTROL and SOURCE_MODE read back zero afterwards;
  DAC_TRANSFER_COUNT remained zero. No FPGA or option-byte programming was performed.
* Host tests cover published CRC vectors, 65535->0 sequence wrap, fragmented and
  corrupted streams, wrong values/duplicates/gaps, fail-closed result validation,
  TCP JSON fragmentation, old/new SPI4 compatibility, and DAC completion logic.
* Real ADC inputs, DAC analog outputs, waveform timing and long uninterrupted runs
  remain unverified. Current captures retain statistics, not raw payload history;
  SDRAM is enabled but the DMA ring resides in internal AXI SRAM.
* Passed 21 host tests and all seven rev58 profile builds/artifact checks:
  bringup, sdram, ethernet, spi, spi4_regs, combined, adc_dummy. New profile
  verifies 54 active peripheral pins and the DMA ring in aligned AXI SRAM.
* Deliberately disconnected a host immediately after ADCTEST; after the bounded
  capture, CONTROL=0 and SOURCE_MODE=0 were verified through a new TCP connection.
* See ``targets/jparc_rev58/docs/ADC_DUMMY_TEST.md`` for command examples,
  ownership, cleanup, result semantics and limits. Board is left running the
  adc_dummy firmware with streaming disabled; DAC output was not exercised.


rev58 customer handoff (2026-10-25)
==================================

* Customer Markdown: ``targets/jparc_rev58/docs/CUSTOMER_DELIVERY.md`` and
  ``CUSTOMER_IO_TEST.md`` describe the M7-only adc_dummy baseline, verified
  scope, daughter wiring, dummy tests, and real I/O procedures.
* ``adc_input_client.py`` polls FPGA real-input LAST/RX_COUNT registers via
  SPI4, outputs raw24, explicitly selected encoding, volts (3 / 2^23),
  freshness and restart counters as CSV. This is not full-rate SPI1 capture.
* ``dac_output_client.py`` accepts CH/CN voltage commands (signed16 +/-10 V),
  raw16 codes, or reads FPGA SET/SENT status;
  each write resends all four channels in its IC. Analog measurement is external.
* O/E optical receiver IC1..9 mappings are recorded with source provenance.
  TEP522A confirms CH1..16 -> CN3..18; TEP521C/523A and connector drawings
  are archived with checksums. Upstream ADC coding remains unspecified.
* Firmware is unchanged. No real ADC/DAC analog test was performed for this
  handoff. FIRMWARE_SPEC_v1.3.md is archived as the current FPGA specification;
  supplied POF/SOF hashes match its programming record. Historical Zephyr tests
  did not record bitstream hashes: rerun integration acceptance on this POF.
  Quartus source commit remains to be supplied.
* Host suite: 29 tests pass, including signed conversion boundaries, stale
  input and frame restart detection, all 65536 DAC code/voltage round trips,
  voltage limits, connector selection, and absence of unintended DAC writes.
* Both bringup and adc_dummy build and artifact verification passed.
