# MAX10 SPI4 register tests over Ethernet

[日本語](SPI4_TEST.md)

`spi4_regs` tests PC → Ethernet/TCP → STM32H745 M7 → SPI4 → MAX10. It originated from `quartus/projects/spi4_regs/SPI4_PROTOCOL.md` v1.0; the [archived Japanese protocol](hardware/rev58/SPI4_PROTOCOL_v1.0.md) retains original Quartus relative references.
On 2026-10-21, ping, INFO/DEVICE_ID and the standalone 176-write/readback test passed on hardware. Electrical timing was not measured. Host tools require only Python 3 standard libraries.
For current integrated v1.3 operation, see the [customer guide](CUSTOMER_IO_TEST_en.md) and [FPGA specification](hardware/rev58_adc_dac/FIRMWARE_SPEC_v1.3_en.md).

## Preparation, build and programming

1. Meet [bring-up requirements](BRINGUP_en.md): Direct SMPS/VOS1, BCM4 disabled and SWD connected.
2. Program MAX10 with the register-test bitstream (`quartus/projects/spi4_regs`). This app does not configure/reset the FPGA; FPGA programming can follow STM32 programming. Current host software also supports integrated v1.3.
3. Run from the repository root:

```sh
./scripts/build.sh jparc_rev58 spi4_regs
./scripts/flash.sh jparc_rev58 spi4_regs
```

Output: `../build-jparc-rev58-spi4_regs/zephyr/zephyr.{elf,bin,hex}`.
This profile enables Ethernet, SPI4 and CONF_DONE input; SPI1–3 and SDRAM are disabled. Ordinary `ethernet`/`spi` profiles only check peripherals and do not start a TCP server.

## Host commands

Example PC board-facing address: **192.168.100.1 / 255.255.255.0**. Board: **192.168.100.58/24, TCP 4244**. Connect directly on the same subnet; no DHCP/gateway.
MAC is randomly generated at boot. Change `CONFIG_JPARC_SPI4_TEST_IP` or `CONFIG_JPARC_SPI4_TEST_PORT` in `apps/bringup/profiles/spi4_regs.conf` and rebuild to change addressing. Use this unauthenticated test server on a board-test LAN.
Run commands on a host that can reach the board. A container needs a route to the board IP. `spi4_client.py` also works when copied alone to a PC.

```sh
python3 targets/jparc_rev58/scripts/spi4_client.py info
python3 targets/jparc_rev58/scripts/spi4_client.py write 0x0100 0x12345678
python3 targets/jparc_rev58/scripts/spi4_client.py read 0x0100
python3 targets/jparc_rev58/scripts/spi4_client.py test
```

Expected messages: `WRITE/readback OK: 0x0100 = 0x12345678` and `0x0100: 0x12345678`.
WRITE succeeds only after STM32 reads the value in a separate SPI frame and finds a match.
Numbers accept decimal or `0x` hexadecimal; addresses are 16 bit, data 32 bit. Place `--host ... --port ...` before the subcommand.

`test` **overwrites all sixteen DUMMY registers**. It checks six patterns; walking-one/zero on DUMMY[0]; distinct values at all addresses followed by a separate read pass; RO/unmapped/unaligned rejection without value modification; and WRITE_COUNT increment176 with 32-bit wrap.
Success prints `PASS: ... 176 verified writes ...`. Final DUMMY[i]=`0xA5000000 | (0x0100 + 4*i)`; original values are not restored. No other master should access SPI4 during this test.
To additionally check zero initial values immediately after FPGA reconfiguration, before any writes:

```sh
python3 targets/jparc_rev58/scripts/spi4_client.py test --expect-reset
```

## Implementation and SPI timing

- STM32 master, Mode 0, 8 bit, MSB first, full duplex.
- PE2=SCK, PE5=MISO, PE6=MOSI use AF5. **PE4=NSS is active-low GPIO**.
- Requested 1 MHz; HSE 25 MHz/32 gives **781.25 kHz**. Stay below the RTL maximum 2 MHz; do not use the original board-document 20 MHz setting for SPI4.
- One `spi_transceive_dt()` call transfers all eight bytes. GPIO CS setup/hold delay 1 µs each, plus inter-frame busy-wait 1 µs.
- CONF_DONE=PI13 must be High or the command returns `ERR NOT_READY`. Each command waits10 ms, then checks DEVICE_ID/version. CONF_DONE is also checked before each frame. READY/FAULT/IRQ are not driven by this RTL and are not success criteria.
- One server thread owns SPI4; no other request can interleave WRITE and readback.
- No automatic retry. A write may complete before TCP failure; read the target value and WRITE_COUNT before retrying. No SPI4 CRC; readback cannot guarantee detection of every electrical fault.

## TCP format

ASCII lines terminated by LF (CRLF accepted), one response per command; persistent connections supported. One client is handled at a time.
Maximum line length95 bytes excluding newline; signed numbers are rejected. Invalid bytes/oversized lines cause an error and disconnect, preventing execution of a trailing fragment. Send/receive timeout5 s; incomplete slow lines also time out.

```text
INFO
OK INFO 0x4a503538 0x00010000 0x00000001 16 0

WRITE 0x0100 0x12345678
OK WRITE 0x0100 0x12345678

READ 0x0100
OK READ 0x0100 0x12345678

WRITE 0x0000 1
ERR FPGA 0x08
```

The INFO example is v1.0; fields are DEVICE_ID, PROTOCOL_VERSION, CAPABILITIES, DUMMY_COUNT, WRITE_COUNT.
READ/WRITE pass RO/unmapped/unaligned addresses to SPI so FPGA status can be observed, provided the preliminary identity/version check succeeds. No API sends arbitrary opcodes or shortened frames.

|Response|Check|
|---|---|
|`ERR NOT_READY`|FPGA configuration and CONF_DONE wiring/level|
|`ERR ID <id> <version>`|Wrong bitstream, MISO stuck Low, wiring, SPI mode|
|`ERR FRAME`|Unexpected first three RX bytes or WRITE response data; wiring/signal integrity|
|`ERR FPGA <status>`|Mask:1 opcode,2 alignment,4 unmapped,8 RO write|
|`ERR TRANSPORT <errno>`|Zephyr SPI failure or CONF_DONE falling before a frame|
|`ERR VERIFY <addr> <expected> <actual>`|WRITE/readback mismatch after header acceptance|
|`ERR COMMAND`|Syntax, range, length or invalid bytes|
|`ERR TIMEOUT`|Incomplete-line receive deadline exceeded|

If TCP cannot connect, inspect PC IP/routes, physical link and RTT PHY/server logs. Nonzero `rev58_status.error` indicates failed startup conditions or peripheral initialization.

## Host validation and remaining hardware checks

```sh
python3 -m unittest discover -s targets/jparc_rev58/tests/unit -v
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-spi4_regs
```

The host suite compiles the actual firmware command/frame code into a shared library using `cc` (or `CC`) and connects it to an independent Python register model. PC tools call it over local TCP; tests include fragmented replies, the complete register suite and counter wrap. This does not execute the Zephyr TCP stack, STM32 SPI driver or FPGA RTL.
Artifact checks cover clocks, RAM, RMII/SPI pins, PE4 GPIO CS and timing settings.
On hardware, check startup, PHY address 0, CONF_DONE, Ethernet and commands. Use a logic analyzer for Mode 0,64 clocks/frame, uninterrupted NSS, setup/hold/gaps, SCK High/Low and MOSI setup/hold; save [measurement records](measurements/README_en.md).
Requested 2 MHz, arbitrary opcodes, partial frames, FPGA reconfiguration during communication and long-duration/temperature testing are outside this automatic suite.

## Hardware bring-up corrections, 2026-10-21

ARP FAILED despite correct host IP/routes can indicate initialization stopped. Corrections addressed LAN8742's already-configured `EALREADY` result and SPI4's unsupported clock-source selection. The LAN8742 patch is board-local and source-hash-guarded; see [bring-up](BRINGUP_en.md).
`CONFIG_NET_CONTEXT_RCVTIMEO` and `CONFIG_NET_CONTEXT_SNDTIMEO` were enabled to prevent disconnects immediately after TCP connection. Program the corrected image.

## Combined SDRAM and Ethernet/SPI4

```sh
./scripts/build.sh jparc_rev58 combined
./scripts/flash.sh jparc_rev58 combined
python3 targets/jparc_rev58/scripts/spi4_client.py info
python3 targets/jparc_rev58/scripts/spi4_client.py test
```

`combined` merges existing SDRAM and SPI4/Ethernet settings. Output: `../build-jparc-rev58-combined/zephyr/`.
Every boot overwrites/tests all 8 MiB before starting TCP. SDRAM remains initialized; IP 192.168.100.58/port4244 and commands are unchanged. Wait a few seconds after startup. SDRAM failure prevents server startup and stores diagnostics in `rev58_memtest`.
SDRAM starts at 0xD0000000. Stacks, ordinary variables and TCP use internal RAM; no SPI4 data logging to SDRAM is implemented. D-cache remains disabled.
Check `rev58_memtest.stage == 5` and `rev58_status.error == 0` with GDB, then resume: halting the CPU also halts Ethernet.
For integrated v1.3 SPI1 dummy reception use `adc_dummy`; see [DMA testing](ADC_DUMMY_TEST_en.md).
The latest client supports v1.0–v1.3 INFO and legacy dummy tests. Since v1.1 uses0x0200 for DAC, the invalid-address test moved to0x0800. Update host scripts together with firmware; do not use the old test script unchanged with a newer FPGA.
