# rev58 M7 build and initial bring-up

[日本語](BRINGUP.md)

## Scope

The dedicated `jparc_rev58/stm32h745xx/m7` board and M7-only startup/peripheral test application are available.
The original bring-up document describes the pre-hardware baseline: successful builds alone do not prove startup, communication or SDRAM performance. Later hardware results are recorded in the [delivery document](CUSTOMER_DELIVERY_en.md) and [measurements](measurements/README_en.md).
M4 application, production FPGA protocol, periodic control, PLC outputs, UART8, ADC3 peripheral and HRTIM remain unimplemented in that baseline.
Sources: `apps/bringup/`; board definition: `boards/jparc/jparc_rev58/`. Nucleo LED/UART/PWM/Ethernet pin assignments are not inherited.

## Build

Run from the repository root. Each profile has its own build:

```sh
./scripts/build.sh jparc_rev58
./scripts/build.sh jparc_rev58 sdram
./scripts/build.sh jparc_rev58 ethernet
./scripts/build.sh jparc_rev58 spi
./scripts/build.sh jparc_rev58 spi4_regs
./scripts/build.sh jparc_rev58 combined
./scripts/build.sh jparc_rev58 adc_dummy
```

|Profile|Enabled checks/features|
|---|---|
|bringup (default)|Internal Flash/RAM, RTT, clock/power registers, READY/FAULT/IRQ raw input polling|
|sdram|Above plus FMC initialization and destructive full 8 MiB SDRAM test|
|ethernet|Above plus RMII MAC/LAN8742 PHY, PHY ID readout and link reporting|
|spi|Above plus SPI1–4 driver initialization/ready checks; no transfers|
|spi4_regs|Ethernet TCP-triggered SPI4 register writes/readback; [procedure](SPI4_TEST_en.md)|
|combined|Startup SDRAM8 MiB test + Ethernet/SPI4; TCP server starts only after SDRAM PASS|
|adc_dummy|SDRAM + Ethernet/SPI4 + SPI1 circular DMA; [host dummy tests](ADC_DUMMY_TEST_en.md)|

Outputs: west workspace `build-jparc-rev58-<profile>/zephyr/`, containing `zephyr.elf`, `.bin`, `.hex`.
`BUILD_DIR` overrides the output directory; use the same override for build/flash.
Direct west example from the repository root:

```sh
west build -p always -b jparc_rev58/stm32h745xx/m7 \
  targets/jparc_rev58/apps/bringup -d ../build-jparc-rev58-bringup
```

Other applications must add `targets/jparc_rev58` to BOARD_ROOT/DTS_ROOT before find_package(Zephyr).

## Power, clocks and board-local Zephyr adaptations

- HSE: external 25 MHz on PH0, bypass.
- PLL1 M=5, N=160, P=2, Q=8, R=2; VCO 800 MHz, SYSCLK 400 MHz.
- AHB 200 MHz; APB1/2/3/4 all 100 MHz.
- Direct SMPS, VOS1; LSE/RTC disabled.
- M7 Flash:1 MiB at 0x08000000. Remaining bank reserved for future M4.
- Data/BSS/stacks:512 KiB AXI SRAM at 0x24000000.
- D-cache disabled so debugger and SDRAM checks access actual memory. This is not a production cache/performance design.

At Zephyr commit `d9c3a39174fdc96b5177faa41c62b056c224512b`, `drivers/clock_control/clock_stm32_ll_h7.c` selects VOS0 in prepare/optimize. The H745 HAL macro sets SYSCFG.PWRCR.ODEN, so frequency settings alone do not meet Direct SMPS requirements.
Board CMake verifies the upstream source SHA-256, generates a VOS1-adapted copy **inside the build directory**, and compiles it. The HAL VOS1 path clears ODEN. Existing LL logic sets Flash latency for VOS1/HCLK.
Upstream Zephyr and Nucleo builds are untouched. An upstream hash change stops the build; review power/clock handling before updating the adaptation.

## Before first programming and debugging

1. Connect an external debugger to J4 SWDIO/SWCLK/NRST/GND and target voltage reference. Initial runner: ST-LINK/OpenOCD, SWD1 MHz, connect under reset.
2. Check STM32 identity, implemented power configuration and Flash boot settings.
3. **Disable automatic M4 boot using option byte BCM4.** `CONFIG_STM32H7_DUAL_CORE=n` only removes M7's startup logic; it does not stop M4 if option bytes boot it. The app checks BCM4 and reports mismatches after startup, but never changes option bytes automatically.
4. Start with `bringup`.

```sh
./scripts/flash.sh jparc_rev58
west debug -d ../build-jparc-rev58-bringup
```

The original baseline had not yet verified OpenOCD/ST-LINK on hardware; later results are in [measurements](measurements/README_en.md).
Alternative CubeProgrammer runner: `west flash -d ../build-jparc-rev58-bringup -r stm32cubeprogrammer`.
Inspect `rev58_status`: magic=0x52563538, error=0, ticks incrementing each second. SYSCLK/HCLK, PWR.CR3/D3CR and SYSCFG.PWRCR/UR1 are recorded. An RTT client can read startup logs. There is no UART console or USB virtual serial port.
READY/FAULT/IRQ are raw levels only, not semantic health checks. Inputs are polled to avoid the PD11/PJ11 EXTI11 conflict. HEARTBEAT, nCONFIG request, PLC and PWM are not driven by the app.
Verify reset/pre-start pin behavior on hardware, including external resistors and MCU reset states.

## Peripheral profiles

### SDRAM

FMC BANK2,16-bit bus,12 row/8 column bits,4 banks,0xD0000000–0xD07FFFFF.
HCLK 200 MHz gives SDCLK 100 MHz; CL 3, refresh 1542, timing `<2 8 5 7 3 2 2>` are initial supplied settings.
Mode 0x230: BL1, CL 3, single write; startup delay 200 µs; eight auto-refresh cycles.
Tests: first-word walking1/0, full-address and inverse patterns, one-second retention, independent upper/lower byte writes across all halfwords.
`rev58_memtest.stage=5` means PASS. Failure records address, expected and actual value. DMA, temperature, supply variation, long retention and loaded timing need separate tests. Confirm unused DQM2/3 initialization and the assembled U2 J-grade device.

### Ethernet

TX_EN=PB11; do not reuse the old PG11 label. LAN8742A PHY address 0 is the initial strap-derived choice; compare actual ID. PHY reset shares NRST.
Ethernet DMA uses SRAM3 from Zephyr's STM32H7 definition, separate from AXI SRAM.
The test MAC is random at startup; HSI48 and hardware RNG are enabled. UDP is enabled for the stack, but no UDP application/transfer is implemented.
`ethernet` has no static-IP transfer app; `spi4_regs` provides static-IP TCP testing. Pattern-transfer protocol and production MAC assignment are not implemented. PHY ID/link alone are not proof of end-to-end transfers.
The selected LAN8742 driver incorrectly returns `-EALREADY` as initialization failure when reset advertisement already matches. Hardware could read ID 0x0007C131 and link at address 0, while init_res=120 caused `Peripheral check failed: -19` before IP setup.
A board-local, SHA256-guarded generated driver accepts initialization `-EALREADY` and proceeds to link monitoring. Other errors and shared upstream source remain unchanged.

### SPI

SPI1: FPGA master → MCU slave RX. SPI2/3: MCU TX master. SPI4: bidirectional wiring; register-test v1 uses MCU master.
No MISO is added to SPI1–3. NSS uses AF without cs-gpios. The selected Zephyr version uses `CONFIG_SPI_SLAVE` and `spi_config.operation` for slave mode; absent cs-gpios/st,soft-nss gives hardware NSS. The old `CONFIG_SPI_STM32_USE_HW_SS` option does not exist here.
`spi` only prepares drivers/pins. Define NSS polarity, CPOL/CPHA, frames, receive arming and READY/IRQ sequencing before implementing transfers.
SPI1–3 kernels run at 100 MHz; ordinary power-of-two master dividers cannot generate exactly 20 MHz. SPI4 uses HSE 25 MHz because this H745 clock driver returns `-ENOTSUP` for PCLK2 as domain source. SPI1–3 master target-clock design is separate; the DMA profile receives FPGA-generated 20 MHz.
`spi4_regs` uses a separate overlay making PE4 GPIO CS, requests 1 MHz and transfers eight-byte frames. MAX10 register-test v1 limits SCLK to 2 MHz, not 20 MHz. See [SPI4 details](SPI4_TEST_en.md).

## Validation without hardware

After each build, use the Zephyr environment's Python/pyelftools on trusted locally generated artifacts:

```sh
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-bringup
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-sdram
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-ethernet
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-spi
```

Checks cover generated Devicetree power/clocks/SDRAM, ELF RAM placement, VOS1 source compilation, FMC/RMII/SPI AFs, duplicate pins and supplied netlist connectivity. They do not validate physical wiring/electrical behavior. The CMake warning for `CONFIG_ASSERT=y` is intentional.
