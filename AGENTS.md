# Codex Repository Instructions

## Project Purpose

This repository provides a reusable STM32H755 dual-core digital power-control
starter environment for small and medium-sized power-supply manufacturers.
Customers should be able to modify PWM, ADC acquisition, and feedback-control
code without needing to understand the network and inter-core infrastructure.

Read `docs/PROJECT_STATUS.rst`, `README.rst`, and `docs/DEVELOPMENT.rst` before
making substantial changes.

## Architecture Boundaries

- Cortex-M7 owns Ethernet, the TCP command server, RPMsg client, and heartbeat.
- Cortex-M4 owns PWM, ADC acquisition, feedback processing, and safety shutdown.
- M7 must never directly operate PWM timers or ADC feedback control.
- Inter-core commands use the versioned structures in `shared/control_protocol.h`.
- Increment `CONTROL_PROTOCOL_VERSION` whenever an incompatible protocol layout
  or meaning changes.

Customer-editable real-time code belongs under:

- `control_core/customer_code/pwm`
- `control_core/customer_code/adc`
- `control_core/customer_code/feedback`

Infrastructure code belongs under:

- `network_core`
- `shared`
- `control_core/platform`

Keep these boundaries obvious. Do not move product-specific control algorithms
into platform or network code.

## Real-Time Rules

- M4 control-path code must have bounded execution time.
- Do not add network access, sleeps, dynamic allocation, or unbounded waits to
  customer real-time code.
- Keep network parsing and logging off the real-time control path.
- ADC1 is triggered by the TIM6 update event at a fixed 10 kHz rate and
  transferred by DMA. DMA and interrupt handling belong in ``control_core/platform``.
- The customer feedback function runs in a dedicated M4 thread after each 10 kHz DMA
  sample. Its latency, jitter, and CPU load must be verified on hardware.

## Safety Rules

This repository is a development starter, not a certified power-control product.

- Never describe software heartbeat shutdown as sufficient hardware protection.
- Preserve the M4 behavior that stops PWM on M7 heartbeat timeout.
- Preserve PWM shutdown on ADC read failure while feedback mode is active.
- Inputs connected to Arduino A0 / PA3 / ADC1_INP15 must remain within 0..VDDA,
  normally approximately 0..3.3 V on the Nucleo board.
- Never connect a power-stage voltage directly to the ADC input. Require external
  scaling, isolation where needed, filtering, and input protection.
- Before use with a real power stage, require independent hardware protection,
  such as TIM1 Break input, over-current shutdown, gate-driver interlock, and a
  reviewed startup/shutdown sequence.
- Complementary PWM edge cases at 0% and 100% duty require hardware validation.

## Current Interfaces

TCP server:

- Address: `192.168.100.2`
- Port: `4242`
- Commands: `SET`, `MODE FEEDFORWARD`, `MODE FEEDBACK`, `GET`, `STATUS`, `OFF`,
  and `HELP`

Current ADC demonstration mapping:

- Input: Arduino A0, PA3, ADC1_INP15
- ADC resolution: 16 bit
- 0 V maps to 0% high-side duty
- VDDA, normally about 3.3 V, maps to 100% high-side duty

## Build and Verification

Run the repository build script after any source, Devicetree, Kconfig, CMake, or
protocol change:

```sh
./scripts/build.sh
```

This must build both images successfully:

- `build-digital-power-control/digital_power_control/zephyr/zephyr.bin`
- `build-digital-power-control/control_core_m4/zephyr/zephyr.bin`

For changes affecting ADC, PWM, RPMsg, networking, pin assignments, or safety
behavior, state clearly that hardware verification is still required unless it
was actually performed. Do not claim physical behavior based only on a build.

## Repository and Workspace Rules

- This repository is the source of truth.
- `west.yml` pins Zephyr and imports its dependencies.
- Do not commit `zephyr/`, `modules/`, `.west/`, or `build-*` output.
- Update `docs/PROJECT_STATUS.rst` when capabilities, known limitations, hardware
  assignments, or next priorities change.
- Update user-facing README files when commands, wiring, or build procedures
  change.
- Keep commits scoped and descriptive. Push completed work to the configured
  remote so it survives host loss and Codex reinstallation.
