# お客様ホストPCでのADC・DAC試験

[English version](CUSTOMER_IO_TEST_en.md)

対象：rev58 STM32 `adc_dummy`、FPGA統合版v1.3。構成・検証範囲は [CUSTOMER_DELIVERY.md](CUSTOMER_DELIVERY.md)。
コマンドはリポジトリルートで実行します。Python 3標準ライブラリのみ使用します。Windowsでは `python3` を `py -3` に置き換えられます。
スクリプトだけを持ち出す場合、`spi4_client.py`、`adc_dummy_client.py`、`adc_input_client.py`、`dac_output_client.py` を同じディレクトリに置き、コマンドのパスを合わせてください。

## 1. 接続と識別

PCの基板接続用Ethernetを `192.168.100.1`、ネットマスク `255.255.255.0` に設定します。基板は `192.168.100.58`、TCP4244です。
Linuxで専用インターフェースがeno1の場合の設定例（実際の名称は `ip -br addr` で確認）：

```sh
sudo ip addr add 192.168.100.1/24 dev eno1
sudo ip link set eno1 up
python3 targets/jparc_rev58/scripts/spi4_client.py info
```

既にそのアドレスが設定済みなら追加不要です。WindowsではEthernetのIPv4設定で同じ値を設定します。
期待値：device_id=`0x4a503538`、version=`0x00010003`、capabilities=`0x0000000f`。
ホストを変更した場合、各スクリプトのコマンド直後に `--host IPアドレス` を指定します。

## 2. FPGA内部ダミー試験（ADC未接続で実施可能）

```sh
python3 targets/jparc_rev58/scripts/spi4_client.py test
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --duration-ms 1000
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --duration-ms 3000 --values 0x000000 0xffffff 0x800000
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --duration-ms 1000 --values 0x555555 0xaaaaaa 0x123456 --repeat 3
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --duration-ms 1000 --json > dummy_result.json
```

既定の3値はADC1=`0x123456`、ADC2=`0x89ABCD`、ADC3=`0xFEDCBA`。
1回の試験は100～3000 msです。各レコードのチャンネル識別、24 bit値、CRC16、チャンネルごとの通番連続性、受信数、FPGA送信数、FIFO DROP/RESTART、SPI/DMAエラーを検査します。
`--repeat` は独立した試験を反復し、間に停止時間があります。途切れない長時間収録ではありません。
終了時にSPI1送信を停止し、実入力モードへ戻します。DAC値は変更しません。
ダミーはFPGA内部で作るため、光受信素子・O/E配線・実ADCの変換精度はこの試験では検証しません。

## 3. 実ADC：光受信素子と信号の対応

以下のIC番号は**O/Eドーターボード上**の番号です。各3本とも外部送信器からFPGAへ入る信号です。

|系統|FS/DRDY 光受信|DATA/DOUT 光受信|SCLK 光受信|rev58 J10ピン（DRDY / DATA / SCLK）|
|---|---|---|---|---|
|ADC1|IC1 / CH1-1|IC2 / CH1-2|IC3 / CH1-3|18 / 20 / 22|
|ADC2|IC4 / CH2-1|IC5 / CH2-2|IC6 / CH2-3|26 / 28 / 30|
|ADC3|IC7 / CH3-1|IC8 / CH3-2|IC9 / CH3-3|34 / 36 / 38|

O/E CN1のピンは、ADC1がa1/b1/a3、ADC2がa5/b5/a7、ADC3がa9/b9/a11（順にDRDY/DATA/SCLK）。
経路は O/E CN1 → 母基板J11 → 母基板CN1C → rev58 J10。rev58 J10と母基板CN1Cは同じ接点番号です。**母基板J10はDAC接続用で、rev58 J10とは別物**です。
ADC4～6に相当する光素子IC10～18は現FPGAでは使用しません。
出典：[回路図一覧](hardware/circuit/README.md) のTEP523A p.1、TEP521C p.3・6、rev58コネクタ用途表p.3、および [FPGA統合仕様](hardware/rev58_adc_dac/FIRMWARE_SPEC_v1.3.md) §3.2。以前の信号抽出表と新たに提供された回路図を照合しました。旧ACM205のFPGAボール番号はrev58へ流用していません。

外部ダミー送信器を使う場合も、DRDY立上りでフレーム開始、SCLK立下りでDATAを取り込む24 bit MSB firstの信号を送ります。SCLK/DATAだけではフレームが開始しません。
現仕様では20 MHz、DRDY立上りから最初のサンプルクロックまで50 ns以上、DATA setup/hold各16.7 ns以上です。詳細は [FPGA統合仕様 §7](hardware/rev58_adc_dac/FIRMWARE_SPEC_v1.3.md) に合わせてください。

### 最新値の表示と保存

符号形式とは、24 bitを正負の数に解釈する規則です。例えば `0xFFFFFF` は2の補数で−1、符号なしで16,777,215です。O/E回路図には光送信元ADCの回路が含まれず、FPGAは受信ビットをそのまま転送するので、送信元の形式に合わせて `--encoding` を必ず指定します。以下は2の補数の場合の例です。

```sh
python3 targets/jparc_rev58/scripts/adc_input_client.py --initialize --encoding signed24 --count 20 --interval 0.5
python3 targets/jparc_rev58/scripts/adc_input_client.py --initialize --encoding signed24 --channels 1 --count 100 --interval 0.1 > adc1.csv
```

`--initialize` はSPI1送信を停止し、実入力モードへ切り替え、ADCカウンタとFIFOをクリアします。DACには触れません。終了時も実入力モード・SPI1送信停止のままです。
初期化済みの状態を観測する場合はこのオプションを省略できます。別の試験を並行実行しないでください。

|encoding|24 bitコードの解釈|
|---|---|
|`signed24`|2の補数。bit23が1なら `raw - 2^24`|
|`unsigned24`|0～16,777,215をそのまま使用|
|`offset-binary`|`raw - 2^23` を使用|

電圧は指定いただいた式 **digital_value × 3 / 2^23 [V]** です。
`signed24` なら `0x000000=0 V`、`0x400000=+1.5 V`、`0x800000=-3 V`、`0xffffff≈-0.000000358 V`。
換算係数は上流回路のゲイン・オフセットを補正しません。実際のADC符号形式は要確認です。

CSVは生コード・整数・電圧・受信カウンタ・更新の有無・フレーム再開始数を出力します。
`fresh=0` は前回以降の新規受信を確認できなかった値であり、0 Vを測定したという意味ではありません。
指定チャネルのどれかで一度も新規受信がない、または観測中にRESTARTが増加した場合、終了コード1になります。
終了コード0は入力更新を観測した意味で、アナログ精度合格ではありません。既知入力を変化させ、生値・極性・換算電圧が追従するか確認してください。

これは **実入力→FPGA最新値→SPI4→Ethernet** の低速ポーリングです。78.125 kHzの全データ、厳密なサンプル時刻、3系統同時サンプル、SPI1経由の実ADC収録は提供しません。
カウンタと値は別々に読むため原子的な組ではなく、`changed_during_read=1` は読取り中に受信が進んだことを示します。
SPI1を停止してもFPGA入力は受信し続けます。FIFO満杯とDROP増加はこの観測方法では想定動作であり、最新値・RX_COUNTは更新されます。

## 4. 実DAC：CN番号・電圧指令と測定

TEP522A回路図p.1のDOUTA/B/C/DからLEMO中心端子を追跡した対応です。LEMO外側はAGNDです。

|ホストCH|DACボードIC|出力（ICピン）|アナログCN|DAC_SET|
|---|---|---|---|---|
|1|IC11（DAC1）|A（23）|CN3|0x0200|
|2|IC11（DAC1）|B（22）|CN4|0x0204|
|3|IC11（DAC1）|C（19）|CN5|0x0208|
|4|IC11（DAC1）|D（18）|CN6|0x020C|
|5|IC13（DAC2）|A（23）|CN7|0x0210|
|6|IC13（DAC2）|B（22）|CN8|0x0214|
|7|IC13（DAC2）|C（19）|CN9|0x0218|
|8|IC13（DAC2）|D（18）|CN10|0x021C|
|9|IC15（DAC3）|A（23）|CN11|0x0220|
|10|IC15（DAC3）|B（22）|CN12|0x0224|
|11|IC15（DAC3）|C（19）|CN13|0x0228|
|12|IC15（DAC3）|D（18）|CN14|0x022C|
|13|IC17（DAC4）|A（23）|CN15|0x0230|
|14|IC17（DAC4）|B（22）|CN16|0x0234|
|15|IC17（DAC4）|C（19）|CN17|0x0238|
|16|IC17（DAC4）|D（18）|CN18|0x023C|

出典：[TEP522A回路図と接続根拠](hardware/circuit/README.md)、[FPGA統合仕様 §6](hardware/rev58_adc_dac/FIRMWARE_SPEC_v1.3.md)。
デジタル経路は rev58 J10 → 母基板CN1C → 母基板J10 → DACボードCN1。

### 電圧で指定する（推奨）

```sh
python3 targets/jparc_rev58/scripts/dac_output_client.py status
python3 targets/jparc_rev58/scripts/dac_output_client.py set-voltage CN3 2.5 > dac_cn3_plus2_5V.csv
python3 targets/jparc_rev58/scripts/dac_output_client.py set-voltage CN3 -2.5 > dac_cn3_minus2_5V.csv
python3 targets/jparc_rev58/scripts/dac_output_client.py set-voltage CN3 0 > dac_cn3_zeroV.csv
```

`CN3` の代わりにホストCH番号 `1` も指定できます。`CN18` はCH16です。
コード指定も残しています。例えば `set CN3 0x2000` は公称+2.5 Vです。

DACは±10 V、符号付き16 bit（2の補数）として、`signed_code = round(V × 32768 / 10)`、送信値はその下位16 bitとします。
最も近いコードへ丸め、ちょうど半LSBの場合は0から遠い側へ丸めます。+10 Vはコード32767へ制限し、±10 Vを超える値やNaN/無限大は通信前に拒否します。

|指令V|符号付きコード|送信コード|公称出力V|
|---|---|---|---|
|−10|−32768|0x8000|−10|
|−5|−16384|0xC000|−5|
|−2.5|−8192|0xE000|−2.5|
|0|0|0x0000|0|
|+2.5|8192|0x2000|+2.5|
|+5|16384|0x4000|+5|
|+10|32767|0x7FFF|+9.999694824|

CSVの `exact_connector` に実CN番号、`set_nominal_volts` / `sent_nominal_volts` にコードから換算した公称電圧を出力します。実電圧の自動読戻しではありません。
外部負荷に適した指令を選び、対象LEMOの中心端子と外側AGND間を電圧計またはオシロスコープで測定してください。各CNで0 V、正電圧、負電圧への追従を確認し、コード・公称電圧・実測電圧・許容差を記録してください。

**1チャネルを書くだけでも同一ICの4出力が現在の設定値で再送信されます。未設定の他3チャネルは0コード（公称0 V）です。**
プログラム終了後も出力を保持します。測定終了後は必要なチャネルへ `set-voltage CN番号 0` 等を明示的に送ってください。FPGAの再設定だけではDAC出力をリセットしません。
DAC電源投入直後は、まだ送信していない0コードの表示だけで実出力0 Vと判断しないでください。

`set_hex` はFPGA設定値、`sent_hex` はFPGAが送信した値、`initialized` はFPGA側の送信済み状態です。
一致と送信完了はDAC実チップによる受理やアナログ電圧の一致を保証しません。DACのSDOによるデバイスレジスタ読戻しは未実装です。
エラー時は書込みを自動再試行しません。応答が失われても送信済みの場合があるので、先に `status` と実電圧を確認してください。

試験記録には「ソフトGitコミット、FPGA POFハッシュ、基板番号、IC/出力、CN番号、指令V、設定コード、公称V、実測V、許容差、判定」を残してください。
FPGAハッシュと過去試験との対応範囲は [納品資料 §5](CUSTOMER_DELIVERY.md) を参照してください。

## 5. 問題の切分け

- 接続できない：PCの専用NICアドレス、配線、リンクLED、TCP4244への接続、電源投入後のSDRAM試験完了を確認。
- FPGA識別が違う：納品v1.3のビットストリームか確認。
- ダミーはPASSだが実ADCが更新されない：光3本の対応、光送信、DRDY、クロックとDATAタイミングを確認。
- 実ADCの値だけ違う：符号形式、ビット順、入力電圧、上流回路のゲインを確認。
- DAC送信完了でも出力が違う：DAC電源、基準電圧、個別CN対応、コード形式、周辺回路を確認。
