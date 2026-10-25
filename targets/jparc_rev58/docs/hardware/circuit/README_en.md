# Schematics and connector-routing evidence

[日本語](README.md)

Originals supplied in `/workdir/circuit/` on2026-10-25 are preserved unchanged. Checksums: `SHA256SUMS`.
Page numbers are one-based within these PDFs, not the older complete-document bundle.

|Document|Drawing/revision|Relevant pages|
|---|---|---|
|[Feedback motherboard](フィードバック基板_TEP521C回路図.pdf)|TEP521C,3667-C3X,2019-10-16|p.3 (3/6): J10/J11; p.6 (6/6): CN1C|
|[16-channel DAC board](16chD_A基板_TEP522A回路図.pdf)|TEP522A,3668-C1X,2017-08-21|p.1: IC11/13/15/17, CN3–CN18|
|[6-channel O/E board](6chO_E基板_TEP523A回路図.pdf)|TEP523A,3669-C1X,2017-05-17|p.1: optical IC1–18, IC20–22, CN1|
|[rev58 connector usage](Connector_usage_rev58.pdf)|rev58,2026-09-17|p.3: same-number contacts between rev58 J10 and motherboard CN1C|

## DAC path

TEP522A connects AD5764 DOUTA(pin 23), DOUTB(pin 22), DOUTC(pin 19), DOUTD(pin 18) to these LEMO center contacts. Shells connect to AGND.

|IC|A|B|C|D|
|---|---|---|---|---|
|IC11|CN3|CN4|CN5|CN6|
|IC13|CN7|CN8|CN9|CN10|
|IC15|CN11|CN12|CN13|CN14|
|IC17|CN15|CN16|CN17|CN18|

Digital route: rev58 J10 → motherboard CN1C → DF signals/resistors → motherboard J10 → DAC CN1 → buffers/isolators → DAC IC.
SCLK/SYNC/SDIN for the four ICs use rev58 J10 pins 17/19/21,27/29/31,37/39/41,47/49/51 respectively.
Host-channel/output assignments were checked against [FPGA specification §6](../rev58_adc_dac/FIRMWARE_SPEC_v1.3_en.md). Voltage scaling follows the user-specified ±10 V signed16 format.

## ADC path

O/E IC1–3: CH1-1/2/3→OE0/1/2; IC4–6: CH2-1/2/3→OE3/4/5; IC7–9: CH3-1/2/3→OE6/7/8.
Route: O/E CN1 → motherboard J11 → DG signals/resistors → motherboard CN1C → rev58 J10.
DRDY/DATA/SCLK pins: ADC1=18/20/22, ADC2=26/28/30, ADC3=34/36/38.
Signal roles were checked against [FPGA specification §§3/7](../rev58_adc_dac/FIRMWARE_SPEC_v1.3_en.md).
The O/E board contains optical receivers and logic buffers, not circuitry defining upstream ADC encoding.
See [customer tests](../../CUSTOMER_IO_TEST_en.md) for commands and all channels.
