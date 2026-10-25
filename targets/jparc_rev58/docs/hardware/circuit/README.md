# 回路図と接続対応の根拠

2026-10-25に `/workdir/circuit/` で提供された原本を変更せず保存しました。SHA-256は `SHA256SUMS`。
ページ番号はこのディレクトリのPDFの1始まりです（旧「完成図書」のページ番号とは異なります）。

|資料|図面・版|参照位置|
|---|---|---|
|[フィードバック基板](フィードバック基板_TEP521C回路図.pdf)|TEP521C、3667-C3X、2019-10-16|p.3（3/6）：J10/J11、p.6（6/6）：CN1C|
|[16ch DAC基板](16chD_A基板_TEP522A回路図.pdf)|TEP522A、3668-C1X、2017-08-21|p.1：IC11/13/15/17、CN3～CN18|
|[6ch O/E基板](6chO_E基板_TEP523A回路図.pdf)|TEP523A、3669-C1X、2017-05-17|p.1：光受信IC1～18、IC20～22、CN1|
|[rev58コネクタ用途表](Connector_usage_rev58.pdf)|rev58、2026-09-17|p.3：rev58 J10と母基板CN1Cの同番号接続|

## DAC経路

TEP522Aでは、各AD5764のDOUTA（23番）、DOUTB（22番）、DOUTC（19番）、DOUTD（18番）が順番に次のLEMO中心端子へ接続されます。各LEMO外側はAGNDです。

|IC|A|B|C|D|
|---|---|---|---|---|
|IC11|CN3|CN4|CN5|CN6|
|IC13|CN7|CN8|CN9|CN10|
|IC15|CN11|CN12|CN13|CN14|
|IC17|CN15|CN16|CN17|CN18|

デジタル送信経路：rev58 J10 → 母基板CN1C → DF信号・抵抗 → 母基板J10 → DAC CN1 → バッファ／アイソレータ → DAC IC。
各ICのSCLK/SYNC/SDINは順に、rev58 J10の17/19/21、27/29/31、37/39/41、47/49/51番です。
DACのホストCHとIC内出力の関係は [FPGA統合仕様 §6](../rev58_adc_dac/FIRMWARE_SPEC_v1.3.md) と照合しました。
電圧スケールはユーザー指定の±10 V・符号付き16 bitを採用しています。

## ADC経路

O/E IC1～3はCH1-1/2/3 → OE0/1/2、IC4～6はCH2-1/2/3 → OE3/4/5、IC7～9はCH3-1/2/3 → OE6/7/8です。
O/E CN1 → 母基板J11 → DG信号・抵抗 → 母基板CN1C → rev58 J10に接続されます。
DRDY/DATA/SCLKの順に、ADC1はJ10の18/20/22、ADC2は26/28/30、ADC3は34/36/38番です。
信号の役割は [FPGA統合仕様 §3・7](../rev58_adc_dac/FIRMWARE_SPEC_v1.3.md) と照合しました。
O/E基板は受光・論理バッファ回路で、上流ADCの符号形式を規定する回路は含まれません。

お客様向けの操作・全CH一覧は [CUSTOMER_IO_TEST.md](../../CUSTOMER_IO_TEST.md) を参照してください。
