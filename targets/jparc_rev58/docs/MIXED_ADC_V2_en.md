# FPGA v2.0: eleven-channel reception and optical-link calibration

Test 3/3 — [Additional tests overview](ADDITIONAL_TESTS_en.md) / [日本語](MIXED_ADC_V2.md) / [Previous](ACC_INPUT_TEST_en.md)

Complete the common preparation on the overview page first. All commands below run in `targets/jparc_rev58/scripts` inside the existing Docker environment. Use FPGA v2.0.2 and the current STM32 `mixed_adc` image throughout all three tests. These additional hardware tests remain to be performed by the operator.

## Start the test

Finish ACC testing and stop its signal generators and polling clients. This test uses the v2 48-byte format, not the earlier v1.3 eight-byte ADC procedure. M7 handles Ethernet, SPI4 and SPI1 DMA; M4 remains disabled (BCM4 off, BCM7 on). The 8 MiB SDRAM startup test remains included; the SPI1 ring uses internal AXI SRAM.

```sh
python3 mixed_adc_client.py status
```

Use one client at a time. Do not change registers from another client during APPLY. Failed writes are not retried automatically. See the [archived v2 specification](hardware/mixed_v2/FIRMWARE_SPEC_v2.md).

## Reception and validation

FPGA is the SPI1 master; STM32 is the slave. PG11=SCLK, PA4=NSS, PB5=DATA. Mode 0, 20 MHz, MSB first, eight-bit words; each NSS-low interval contains 48 bytes. At the default period of 4000 × 25 ns = 100 µs (10 kHz), each frame contains three 24-bit samples and eight 16-bit samples. ADC16 order: A0, A1, B0, B1, C0, C1, D0, D1.

`capture` runs a **bounded 100–3000 ms acquisition**. During this interval DMA runs continuously across NSS boundaries, without restarting per frame. The 30,720-byte circular buffer is processed in 15,360-byte halves (320 frames, 32 ms at 10 kHz); the final partial half is also consumed. DMA errors, buffer overruns and SPI errors are checked. Ethernet returns interval statistics and the latest values, not every frame or an indefinite live stream.

Validation includes:

- Magic, format, reserved bits, CRC16/CCITT-FALSE over the first 46 bytes, consecutive 32-bit frame numbers including wrap, first frame number zero, received/TX count equality and approximately ±10% rate tolerance.
- ADC24 known/fresh, per-input sequence and age consistency. Across adjacent frames, fresh=0 must retain sequence/value and must not reduce age; a known input must not become unknown. **ADC24 samples are downsampled from 78.125 kHz, so sequence increments near seven or eight are normal. An increment of one is not required.** Sixteen-bit wrap is permitted; fresh=1 with unchanged sequence is not alone an error because an exact full wrap is possible.
- Inputs selected by `--required24` bits 0–2 must become known, remain within `--max-age-us` (default 100 µs) after first becoming known, and have no input restart count. Initial unknown samples are allowed until first known. Use e.g. `--required24 3` when only inputs 1 and 2 are connected; mask zero permits all three to be absent.
- `source_drop`, `frame_drop`, `missed` must be zero. ADC24 `replaced` normally increases: it counts latest-value replacement before aggregation, not the old firmware's FIFO DROP condition.
- ADC16 signed minimum/maximum across all frames and latest raw/signed values. With `--expect16`, every value must be inside the specified signed reference ± tolerance.

`pass: true` describes these digital checks. ADC16 has no external receipt ACK; flags bit3 means the FPGA capture window completed. **CRC success does not establish correct ADC-to-FPGA optical reception:** disconnected input, bit shift or channel misalignment can still produce CRC-valid frames. Known analog inputs are essential.

ADC24 is displayed as two's-complement signed data and `nominal_volts = signed × 3 / 2^23`. Consult known/age before using its value. ADC16 is displayed as two's-complement signed codes; its analog input range has not been established here, so no ADC16 voltage conversion is assumed.

## Start with ADC24 dummy testing

Selecting manual DAC mode can immediately restore previously stored manual output codes. Check the connected load before explicitly selecting it. Capture refuses nonzero DAC source masks and never changes DAC selection itself.

```sh
python3 mixed_adc_client.py dac-source 0
python3 mixed_adc_client.py capture --dummy24 > dummy24.json
python3 mixed_adc_client.py capture --dummy24 --values24 0 0x7fffff 0x800000 > dummy24_limits.json
python3 mixed_adc_client.py capture --dummy24 --values24 0x555555 0xaaaaaa 0xffffff > dummy24_bits.json
```

Only the three ADC24 inputs are synthetic. All eight ADC16 inputs remain physical. A dummy-test pass with disconnected ADC16 does not validate that optical link.

Capture applies and verifies configuration, selects ADC24 source, enables acquisition with APPLY, arms DMA, then clears FIFOs and starts SPI1 transmission. The STM32 finishes by applying acquisition disable, draining queued frames, stopping transmission/DMA and restoring physical ADC24 input. The final source change clears FPGA flow counters; preserve the returned JSON as the test record. Cleanup runs even if the host disconnects. A SPI4 failure can prevent confirmed stopping; inspect `status` and use `stop` after communication recovers.

## Calibrate the new ADC optical link

| Signal | OE/EO component and net | rev58 connector | MAX10 |
|---|---|---|---|
| SCLK output | EO TEP526A IC24, DOUT15 | J9-58 | R2 |
| FS output, active low | EO TEP526A IC25, DOUT16 | J9-60 | R1 |
| CONVSTART output, 50 ns high | EO TEP526A IC26, DOUT17 | J9-62 | AA15 |
| ADDATA16 serial input, one wire | **OE TEP523A IC11, CH4-2** | J10-44 | V13 |

OE IC26 is a power IC, not this receiver. Do not confuse this link with existing ADC3 IC8/AB10. Check physical fiber ends, board markings and ADC settings (DCEN, PAR/SER, etc.). See the FPGA specification and [existing connector guide](CUSTOMER_IO_TEST_en.md).

Apply distinguishable, stable, known inputs to all eight channels. All-zero inputs alone cannot detect bit or channel shifts. Include positive/negative values, different magnitudes and changes affecting MSB and low bits. Derive reference codes and tolerances from the actual ADC range and source accuracy.

| Option | Shadow register | Meaning | Default |
|---|---|---|---|
| `--period` | 0x0514 | Acquisition period in 25 ns ticks; even and at least `conversion + delay + 270` | 4000 |
| `--conversion` | 0x0508 | Conversion wait C, 25 ns ticks | 68 |
| `--delay` | 0x0504 | Receive delay D, 25 ns ticks | 9 |
| `--phase` | 0x050C | 0=0°, 1=180°, 2=90° (12.5 ns delayed), 3=270° | 3 |
| `--negate` | 0x0510 | 0=unchanged, 1=16-bit arithmetic negation, not bitwise inversion | 0 |

CONVSTART to FS low is `(C+4)×25 ns`; FS low to first sample is `(D+3)×25 ns`; completion occurs around `(C+D+262)×25 ns`. There are 128 bits sampled at 50 ns intervals. Negation affects both SPI1 and automatic DAC data; `0x8000` remains `0x8000` under 16-bit arithmetic.

Defaults are starting points, not measured calibration. Optical delays in the specification are assumptions; simulation settings do not guarantee physical reception.

1. Use manual DAC mode, negate=0, period=4000 and conversion=68. Use `--required24 0` if ADC24 is disconnected.
2. Sweep phases 0–3 and receive delay, comparing one-second raw/min/max records with known inputs. The following D=4–16 range is an **example**, not a guaranteed valid range; adapt it to actual conversion and optical delays.

```sh
mkdir -p optical_tuning
for phase in 0 1 2 3; do
  for delay in 4 5 6 7 8 9 10 11 12 13 14 15 16; do
    python3 mixed_adc_client.py capture --required24 0 --phase "$phase" --delay "$delay" \
      > "optical_tuning/p${phase}_d${delay}.json" || echo "FAIL p=$phase d=$delay"
  done
done
```

3. Compare against references, not merely CRC. The numbers below illustrate syntax only: **replace them with codes corresponding to your actual applied inputs**. This requires every sample within ±8 LSB.

```sh
python3 mixed_adc_client.py capture --required24 0 --phase 3 --delay 9 \
  --expect16 -24000 -16000 -8000 -1000 1000 8000 16000 24000 --tolerance 8 \
  > known_input.json
```

4. Find a contiguous stable delay window and select near its center, rather than a single passing boundary point. Repeat with different inputs. If no stable window exists, check wiring, fiber direction, serial-mode settings, FS/CONVSTART polarity and conversion time. Increase C if needed, maintaining the period constraint, and repeat.
5. Repeat `--ms 3000` captures at the selected settings. Verify actual cable lengths, supply and temperature conditions. Save JSON, configuration, reference input, cable and board identification.
6. Enable `--negate 1` only when needed; recheck positive/negative values and endpoints. Negation cannot correct bit alignment. Finally connect physical ADC24 and verify known values, fresh and age with `--required24 7`.

Every capture reapplies its supplied options; omitted options use defaults, not previously calibrated values. Put selected settings explicitly in your command or experiment script. `status` is read-only.

## APPLY and DAC linkage

```sh
python3 mixed_adc_client.py configure --phase 3 --delay 9 --conversion 68 --period 4000 --negate 0
python3 mixed_adc_client.py status
```

Configure defaults to acquisition disabled. A successful SPI4 WRITE alone is not sufficient: the host verifies PENDING clear, ERROR absent, unchanged REJECTED, updated SUBMITTED_ID, matching APPLIED_ID, and every applied setting at 0x0540–0x0554. Reconfigure after power cycling.

Register 0x0060 selects automatic DAC channels; reset value zero means all manual. Bits 0–7 select CH1–8 from ADC16 A0–D1; bits 8–10 select CH9–11 from ADC24 CH1–3 bits23:8. CH12–16 remain manual. Mask `0x7ff` selects all eleven automatic channels. DAC CHn maps to daughter-board analog connector CN(n+2).

```sh
# After successful physical-input calibration, explicitly enable the selected settings
python3 mixed_adc_client.py configure --phase 3 --delay 9 --conversion 68 --period 4000 --enable 1
python3 mixed_adc_client.py dac-source 0x7ff
python3 dac_output_client.py status
# Stop acquisition; outputs hold, they do not become zero
python3 mixed_adc_client.py stop
# Returning to manual mode restores stored manual codes
python3 mixed_adc_client.py dac-source 0
python3 dac_output_client.py set-voltage CN3 1.0
```

Automatic DAC updates continue with SPI1 transmission disabled. Acquisition without SPI1 TX fills the frame FIFO and increases DROP; this is expected for the DAC-only example. Subsequent capture clears flow state before testing. Unknown ADC24 automatic values are zero; lost inputs hold their latest value. Manual voltage commands refuse ADC-linked channels. Manual shadows, effective codes and sent codes are distinct; measure actual analog outputs externally.

`stop` disables acquisition via APPLY. Neither STREAM_CONTROL=0 nor ADS_CONTROL=0 alone stops new ADC acquisition. Neither stopping nor disconnecting TCP is a DAC zero-output or safety-stop operation.

## Finish and retain evidence

Record the board ID, Git commit, FPGA version, capture JSON, applied phase/delay/conversion/period settings, input voltages, ADC readings and measured DAC voltages. Record untested items and failures explicitly. ADC16 range and optical-link margins require measurements; earlier v1.3 results do not validate this version.

Stop acquisition, return DAC channels to manual mode and explicitly set the agreed final output voltages. `stop` alone does not zero outputs; the example above leaves CN3 at 1 V. Confirm the final voltages with an external instrument.

---

[Additional tests overview](ADDITIONAL_TESTS_en.md) / [日本語](MIXED_ADC_V2.md) / [Previous](ACC_INPUT_TEST_en.md)
