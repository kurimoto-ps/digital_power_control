# PLC / board IO test protocol

`board_io_protocol.c` validates the Ethernet commands for PLC GPIO and motherboard UART tests.
The GPIO mapping and UART8 pin swap are in `apps/bringup/profiles/plc_uart.overlay`;
Zephyr hardware access is in `apps/bringup/src/board_io_test.c`.

Commands: `PLC READ`, `PLC WRITE <unsigned16>`, `UART STATUS`, `UART CLEAR`,
`UART READ`, `UART SEND <1..32 bytes of contiguous hex>`.
These commands are enabled in `mixed_adc`, independently of FPGA readiness.
They do not implement PLC operating sequence logic.

[日本語試験手順](../../docs/PLC_UART_TEST.md) / [English](../../docs/PLC_UART_TEST_en.md).
Build and host validation are available; hardware tests remain pending.
