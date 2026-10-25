# Customer host-PC ADC and DAC tests

[日本語](CUSTOMER_IO_TEST.md)

Target: rev58 STM32 `adc_dummy` and integrated FPGA v1.3. See [delivery configuration and scope](CUSTOMER_DELIVERY_en.md).
Run commands from the repository root. Python 3 standard library only; on Windows, replace `python3` with `py -3`.
If copying only scripts, keep `spi4_client.py`, `adc_dummy_client.py`, `adc_input_client.py`, and `dac_output_client.py` together and adjust command paths.

## 1. Connection and identification

Configure the PC's board-facing Ethernet interface as `192.168.100.1`, netmask `255.255.255.0`. The board uses `192.168.100.58`, TCP 4244.
Example for a dedicated Linux interface named eno1 (check the name with `ip -br addr`):

```sh
sudo ip addr add 192.168.100.1/24 dev eno1
sudo ip link set eno1 up
python3 targets/jparc_rev58/scripts/spi4_client.py info
```

Do not add the address again if it is already configured. On Windows use the same values in Ethernet IPv4 settings.
Expected: device_id=`0x4a503538`, version=`0x00010003`, capabilities=`0x0000000f`.
For a different board address, place `--host IP_ADDRESS` immediately after the script name.

## 2. Internal FPGA dummy test (no ADC connection required)

```sh
python3 targets/jparc_rev58/scripts/spi4_client.py test
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --duration-ms 1000
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --duration-ms 3000 --values 0x000000 0xffffff 0x800000
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --duration-ms 1000 --values 0x555555 0xaaaaaa 0x123456 --repeat 3
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --duration-ms 1000 --json > dummy_result.json
```

Defaults: ADC1=`0x123456`, ADC2=`0x89ABCD`, ADC3=`0xFEDCBA`. Each test lasts 100–3000 ms.
Checks cover channel identifiers, 24-bit values, CRC16, per-channel sequence continuity, received and FPGA transmitted record counts, FIFO DROP/RESTART, and SPI/DMA errors.
`--repeat` runs independent tests with stops between them, not an uninterrupted long capture.
On completion, SPI1 transmission stops and real-input mode is restored. DAC values are unchanged.
Internal dummy generation does not test optical receivers, O/E wiring or real ADC conversion accuracy.

## 3. Real ADC: optical receiver assignments

IC numbers below belong to the **O/E daughterboard**. All three signals per input are driven from the external transmitter toward the FPGA.

|Input|FS/DRDY receiver|DATA/DOUT receiver|SCLK receiver|rev58 J10 pins (DRDY / DATA / SCLK)|
|---|---|---|---|---|
|ADC1|IC1 / CH1-1|IC2 / CH1-2|IC3 / CH1-3|18 / 20 / 22|
|ADC2|IC4 / CH2-1|IC5 / CH2-2|IC6 / CH2-3|26 / 28 / 30|
|ADC3|IC7 / CH3-1|IC8 / CH3-2|IC9 / CH3-3|34 / 36 / 38|

O/E CN1 pins, in DRDY/DATA/SCLK order: ADC1=a1/b1/a3, ADC2=a5/b5/a7, ADC3=a9/b9/a11.
Route: O/E CN1 → motherboard J11 → motherboard CN1C → rev58 J10. rev58 J10 and motherboard CN1C use identical contact numbers.
**Motherboard J10 is the DAC connector and is distinct from rev58 J10.** Optical IC10–18 (ADC4–6) are unused in the current FPGA.
Sources: [schematics](hardware/circuit/README_en.md), TEP523A p.1, TEP521C pp.3/6, rev58 connector usage p.3, and [FPGA specification](hardware/rev58_adc_dac/FIRMWARE_SPEC_v1.3_en.md) §3.2. The prior extracted routing was cross-checked against the supplied schematics. Legacy ACM205 FPGA ball assignments are not reused for rev58.

An external dummy transmitter must also start frames on DRDY rising edges and supply 24 bits, MSB first, sampled on SCLK falling edges. SCLK/DATA alone cannot start a frame.
The current interface supports 20 MHz, requires at least 50 ns from DRDY rising to the first sampling edge, and DATA setup/hold of at least 16.7 ns each. Follow [FPGA specification §7](hardware/rev58_adc_dac/FIRMWARE_SPEC_v1.3_en.md).

### Displaying and saving latest values

Encoding is the rule for interpreting a 24-bit word as a number. For example, `0xFFFFFF` is −1 in two's complement or 16,777,215 unsigned.
The O/E schematics do not contain the upstream ADC and the FPGA forwards bits unchanged. Always select `--encoding` to match the source. These examples assume two's complement:

```sh
python3 targets/jparc_rev58/scripts/adc_input_client.py --initialize --encoding signed24 --count 20 --interval 0.5
python3 targets/jparc_rev58/scripts/adc_input_client.py --initialize --encoding signed24 --channels 1 --count 100 --interval 0.1 > adc1.csv
```

`--initialize` stops SPI1 transmission, selects real inputs, and clears ADC counters/FIFOs without changing the DAC. The system remains in real-input mode with SPI1 transmission disabled after exit.
Omit this option to observe an already initialized system. Do not run other tests concurrently.

|Encoding|24-bit interpretation|
|---|---|
|`signed24`|Two's complement: `raw - 2^24` when bit 23 is 1|
|`unsigned24`|Use 0–16,777,215 directly|
|`offset-binary`|Use `raw - 2^23`|

Voltage is **digital_value × 3 / 2^23 [V]**.
For `signed24`: `0x000000=0 V`, `0x400000=+1.5 V`, `0x800000=-3 V`, `0xffffff≈-0.000000358 V`.
This factor does not correct upstream gain or offset. Actual ADC encoding still requires confirmation.

CSV includes raw code, integer, volts, receive counters, freshness and frame restart counts.
`fresh=0` means a newly received value was not confirmed since the previous observation; it does not mean a measured 0 V.
Exit code 1 indicates that at least one selected channel never produced fresh input, or that RESTART increased during observation.
Exit code 0 confirms input activity, not analog accuracy. Vary a known input and check raw value, polarity and converted voltage.

This is slow polling of **real input → FPGA latest-value registers → SPI4 → Ethernet**. It does not capture all 78.125 kHz samples, precise sample times, simultaneous three-channel samples, or real-ADC data through SPI1.
Counter and value reads are not atomic; `changed_during_read=1` indicates reception advanced between reads.
FPGA reception continues with SPI1 stopped. FIFO-full and DROP increases are expected in this mode; latest values and RX_COUNT continue updating.

## 4. Real DAC: connectors, voltage commands and measurement

Assignments are traced from DOUTA/B/C/D to LEMO center contacts on TEP522A p.1. Shells connect to AGND.

|Host CH|DAC IC|Output (IC pin)|Analog CN|DAC_SET|
|---|---|---|---|---|
|1|IC11 (DAC1)|A (23)|CN3|0x0200|
|2|IC11 (DAC1)|B (22)|CN4|0x0204|
|3|IC11 (DAC1)|C (19)|CN5|0x0208|
|4|IC11 (DAC1)|D (18)|CN6|0x020C|
|5|IC13 (DAC2)|A (23)|CN7|0x0210|
|6|IC13 (DAC2)|B (22)|CN8|0x0214|
|7|IC13 (DAC2)|C (19)|CN9|0x0218|
|8|IC13 (DAC2)|D (18)|CN10|0x021C|
|9|IC15 (DAC3)|A (23)|CN11|0x0220|
|10|IC15 (DAC3)|B (22)|CN12|0x0224|
|11|IC15 (DAC3)|C (19)|CN13|0x0228|
|12|IC15 (DAC3)|D (18)|CN14|0x022C|
|13|IC17 (DAC4)|A (23)|CN15|0x0230|
|14|IC17 (DAC4)|B (22)|CN16|0x0234|
|15|IC17 (DAC4)|C (19)|CN17|0x0238|
|16|IC17 (DAC4)|D (18)|CN18|0x023C|

Sources: [TEP522A and routing references](hardware/circuit/README_en.md), [FPGA specification §6](hardware/rev58_adc_dac/FIRMWARE_SPEC_v1.3_en.md).
Digital route: rev58 J10 → motherboard CN1C → motherboard J10 → DAC board CN1.

### Voltage commands (recommended)

```sh
python3 targets/jparc_rev58/scripts/dac_output_client.py status
python3 targets/jparc_rev58/scripts/dac_output_client.py set-voltage CN3 2.5 > dac_cn3_plus2_5V.csv
python3 targets/jparc_rev58/scripts/dac_output_client.py set-voltage CN3 -2.5 > dac_cn3_minus2_5V.csv
python3 targets/jparc_rev58/scripts/dac_output_client.py set-voltage CN3 0 > dac_cn3_zeroV.csv
```

Host channel `1` can replace `CN3`; `CN18` is CH16. Raw-code commands remain available: `set CN3 0x2000` requests nominal +2.5 V.
For ±10 V signed 16-bit two's complement, `signed_code = round(V × 32768 / 10)` and the transmitted value is its low 16 bits.
Round to the nearest code, with half-LSB ties away from zero. +10 V is limited to code 32767. Out-of-range values, NaN and infinities are rejected before communication.

|Requested V|Signed code|Transmitted code|Nominal output V|
|---|---|---|---|
|−10|−32768|0x8000|−10|
|−5|−16384|0xC000|−5|
|−2.5|−8192|0xE000|−2.5|
|0|0|0x0000|0|
|+2.5|8192|0x2000|+2.5|
|+5|16384|0x4000|+5|
|+10|32767|0x7FFF|+9.999694824|

CSV `exact_connector` identifies the CN. `set_nominal_volts` and `sent_nominal_volts` are code-derived nominal voltages, not analog readback.
Choose commands appropriate for the external load. Measure between the LEMO center and its AGND shell using a voltmeter or oscilloscope. Check zero, positive and negative responses at each CN; record code, nominal and measured voltage, and tolerance.

**Writing one channel retransmits the current settings for all four outputs of that IC. Unconfigured companion channels receive code 0 (nominal 0 V).**
Outputs remain held after the script exits. Explicitly send `set-voltage CN_NUMBER 0` or another required setting after measurement. FPGA reconfiguration alone does not reset DAC analog outputs.
At power-up, a displayed unsent zero code does not establish an actual 0 V output.

`set_hex` is the FPGA setting, `sent_hex` is its last transmitted code, and `initialized` records FPGA transmission completion.
Agreement/completion does not guarantee DAC acceptance or analog accuracy. DAC register readback through SDO is not implemented.
Writes are not automatically retried on errors. A lost reply can follow a completed write: check `status` and measured voltage before another command.

Record software Git commit, FPGA POF hash, board ID, IC/output, CN, requested V, code, nominal V, measured V, tolerance and verdict.
See [delivery section 5](CUSTOMER_DELIVERY_en.md) for FPGA hash provenance and historical test limitations.

## 5. Troubleshooting

- Cannot connect: check the PC's dedicated NIC address, cabling, link LEDs, TCP 4244 access and completion of the startup SDRAM test.
- Unexpected FPGA identity: confirm the delivered v1.3 bitstream.
- Dummy PASS but no real ADC updates: check the three optical connections, optical transmitter, DRDY, clock and DATA timing.
- Incorrect ADC values: check encoding, bit order, input voltage and upstream gain.
- DAC transmission complete but wrong output: check DAC supplies, reference, connector, code format and surrounding circuits.
