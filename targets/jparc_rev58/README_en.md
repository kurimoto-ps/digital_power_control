# J-PARC rev58 Zephyr development

[日本語](README.md)

Target: STM32H745XIH6 + MAX10 (10M16DCF484I7G). This directory contains the dedicated board definition and M7-only startup/peripheral test application.
M7 startup, SDRAM, Ethernet/SPI4 and internal FPGA dummy ADC reception have been verified on hardware. M4 and the production application are not implemented.

Delivery: [software configuration](docs/CUSTOMER_DELIVERY_en.md), [customer ADC/DAC and dummy tests](docs/CUSTOMER_IO_TEST_en.md).
Real ADC inputs and DAC analog outputs require customer verification.

```text
targets/jparc_rev58/
├── apps/
│   ├── bringup/                 M7 startup + SDRAM/Ethernet/SPI profiles
│   ├── m7/                      Reserved for Ethernet/pattern management
│   └── m4/                      Future M4 app after role assignment
├── boards/jparc/jparc_rev58/     Dedicated board definition
├── lib/
│   ├── pattern/                 Pattern validation/storage/activation
│   ├── fpga_link/               MAX10 SPI/status
│   ├── synchronization/         External synchronization/cycle boundaries
│   ├── plc/                     PLC I/O
│   └── control/                 Equipment-specific control computation
├── include/jparc/               Shared API/protocol formats
├── dts/bindings/                Custom bindings
├── scripts/                     rev58 development commands
├── tests/{unit,hardware}/       Logic and hardware tests
└── docs/
    ├── hardware/rev58/          Supplied documents and SHA256SUMS
    ├── measurements/            Hardware test records
    └── DEVELOPMENT_en.md        Conditions, open items and development sequence
```

The existing Nucleo application is under `../nucleo_h755zi_q/`. Root `scripts/build.sh` defaults to Nucleo when no target is given; use `./scripts/build.sh jparc_rev58` for this board.
Zephyr and modules share the existing west workspace and are not copied here. The bring-up app uses this target directory as BOARD_ROOT/DTS_ROOT.
See [development conditions](docs/DEVELOPMENT_en.md). Supplied ZIP contents and technical documents are preserved unchanged. QSF/SDC files are reference templates; implementing a Quartus project is outside this Zephyr directory's scope.
[Bring-up](docs/BRINGUP_en.md) explains power initialization adaptations and hardware checks.

Use `spi4_regs` for Ethernet-triggered MAX10 SPI4 write/readback; see [commands](docs/SPI4_TEST_en.md).
Use `./scripts/build.sh jparc_rev58 combined` for SDRAM plus Ethernet/SPI4; its TCP server starts after the complete SDRAM test passes.
For three-input FPGA v1.3 dummy reception, use `adc_dummy` and `scripts/adc_dummy_client.py`; see [DMA procedure](docs/ADC_DUMMY_TEST_en.md).

## English document index

|Document|Purpose|
|---|---|
|[Customer delivery](docs/CUSTOMER_DELIVERY_en.md)|Configuration, scope, firmware identity|
|[Customer I/O tests](docs/CUSTOMER_IO_TEST_en.md)|Optical/DAC connectors, voltage commands, CSV and dummy tests|
|[Bring-up](docs/BRINGUP_en.md)|Build, programming, power and peripheral checks|
|[Development](docs/DEVELOPMENT_en.md)|Historical baseline and development sequence|
|[SPI4 test](docs/SPI4_TEST_en.md)|TCP commands, register test and combined profile|
|[ADC dummy test](docs/ADC_DUMMY_TEST_en.md)|DMA, validation, cleanup and diagnostics|
|[FPGA v1.3 specification](docs/hardware/rev58_adc_dac/FIRMWARE_SPEC_v1.3_en.md)|Complete current FPGA interface specification|
|[Circuit references](docs/hardware/circuit/README_en.md)|Drawing revisions and routing evidence|
|[Measurement index](docs/measurements/README_en.md)|Board-specific acceptance records|
|[New-board acceptance](docs/measurements/2026-10-25-board-00250028-35335115-39383331/README_en.md)|Programming, tests and cold-boot results|

English versions cover the current customer handoff and the supporting documents above. Original schematic PDFs, older archived FPGA handoffs/v1.0 protocol, pin-table exports and internal directory placeholders remain in their original form. Use the integrated v1.3 specification for the current FPGA interface.
