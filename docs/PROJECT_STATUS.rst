Project status
##############

Last updated: 2026-06-07

Purpose
*******

This project is a customer-facing development starter for digital power-control
applications on the NUCLEO-H755ZI-Q. It separates reusable communication and
safety infrastructure from customer-editable PWM, ADC, and feedback code.

Current implementation
**********************

Dual-core architecture
======================

* Cortex-M7 runs Ethernet, a TCP command server, RPMsg client, and heartbeat.
* Cortex-M4 runs complementary PWM, ADC acquisition, feedback pass-through, and
  communication/ADC-failure shutdown behavior.
* RPMsg uses shared SRAM4 and the STM32 HSEM mailbox.
* The shared protocol version is 2.

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
  Uses frequency and dead time from ``SET``. The duty argument is retained as
  the feedforward value but ignored while feedback mode is active. Duty is calculated by the customer-editable pass-through function after
  each fixed-rate 10 kHz ADC DMA sample.

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
* Mapping: ADC raw 0..65535 maps linearly to high-side duty 0..100%
* Examples: 0 V -> 0%, 1.65 V -> approximately 50%, 3.3 V -> 100%
* ADC DMA failure stops PWM and sets fault bit 1.
* TIM6 update triggers ADC1 at a fixed 10 kHz rate, independent of PWM
  frequency. The nominal ADC and feedback period is 100 us.
* DMA completion interrupt handling is contained in ``control_core/platform``.
  A dedicated M4 feedback thread calls customer code and updates PWM duty.
* The feedback implementation is currently a pass-through mapping, not a closed
  loop controller.

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
* M4 image builds with STM32 ADC, DMA/DMAMUX, TIM1-synchronized sampling,
  complementary PWM, RPMsg remote endpoint, and feedback-mode logic.
* A fresh local Git clone can initialize a west workspace using
  ``west init -l digital_power_control``.

Not yet verified on hardware
****************************

* ADC voltage accuracy and calibration across 0..3.3 V.
* Duty response to ADC input while feedback mode is active.
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
* Feedback control is only ADC-to-duty pass-through; no PI/PID or regulation is
  implemented.
* Commands and RPMsg protocol have no authentication or encryption.
* TCP server handles one client at a time.
* The demonstration accepts 0% and 100% high-side duty. Real hardware may need
  narrower duty limits and additional interlocks.

Recommended next work
*********************

#. Verify A0 voltage-to-duty mapping and complementary PWM waveforms on hardware.
#. Add TIM1 Break input and independent over-current protection before connecting
   a power stage.
#. Define product-specific safe minimum/maximum duty and dead-time limits.
#. Measure ADC trigger phase, DMA-to-duty latency, jitter, and CPU load on hardware.
#. Define measured-value scaling and calibration interfaces for voltage/current.
#. Implement a bounded PI/PID feedback controller with saturation and anti-windup.
#. Add automated tests for command parsing, protocol compatibility, mapping, and
   fault transitions.
