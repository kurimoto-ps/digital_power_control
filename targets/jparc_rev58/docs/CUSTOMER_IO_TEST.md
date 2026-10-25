# お客様ホストPCでのADC・DAC試験

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
出典：提供資料 `ACM205_signal_extract/signal_map.json` のO/E物理経路（必要部分の保存コピー：[daughter_adc_mapping.json](hardware/rev58_adc_dac/daughter_adc_mapping.json)）、rev58コネクタ用途表とADC引継ぎ仕様。旧ACM205のFPGAボール番号はrev58へ流用していません。

外部ダミー送信器を使う場合も、DRDY立上りでフレーム開始、SCLK立下りでDATAを取り込む24 bit MSB firstの信号を送ります。SCLK/DATAだけではフレームが開始しません。
現仕様では20 MHz、DRDY立上りから最初のサンプルクロックまで50 ns以上、DATA setup/hold各16.7 ns以上です。詳細は [ADC引継ぎ仕様](hardware/rev58_adc_dac/ZEPHYR_ADC_STREAM_HANDOFF.md) に合わせてください。

### 最新値の表示と保存

ADCデータ形式を確認して `--encoding` を必ず指定します。

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

## 4. 実DAC：コード設定と電圧測定

|ホストCH|DACボードIC|DAC出力|アナログ出力コネクタ群|
|---|---|---|---|
|1 / 2 / 3 / 4|IC11（DAC1）|A / B / C / D|CN3～CN6|
|5 / 6 / 7 / 8|IC13（DAC2）|A / B / C / D|CN7～CN10|
|9 / 10 / 11 / 12|IC15（DAC3）|A / B / C / D|CN11～CN14|
|13 / 14 / 15 / 16|IC17（DAC4）|A / B / C / D|CN15～CN18|

**各群のA/B/C/Dと個別CN番号の対応は未確定です。CH1=CN3等とは断定できません。**
出典：[DAC引継ぎ仕様](hardware/rev58_adc_dac/ZEPHYR_DAC_HANDOFF.md)、提供信号解析資料。
確定図面を受領するまでスクリプトは `exact_connector=UNCONFIRMED` と表示します。個別CNでの正式な対応判定は、この情報を補ってから行ってください。

```sh
python3 targets/jparc_rev58/scripts/dac_output_client.py status
python3 targets/jparc_rev58/scripts/dac_output_client.py set 1 0x8000 > dac_ch1_8000.csv
python3 targets/jparc_rev58/scripts/dac_output_client.py status > dac_status.csv
```

上はCH1へ生コード`0x8000`を書き込む例で、0 V指定ではありません。DACの換算係数・極性・基準電圧が未確定なので、ADCの換算式をDACには使いません。
外部負荷に適したコードを選び、対象CNと基準GND間を電圧計またはオシロスコープで測定してください。
**1チャネルを書くだけでも同一ICの4出力が現在の設定値で再送信されます。未設定の他3チャネルは0コードです。**
プログラム終了後も出力を保持し、自動でゼロへ戻しません。終了時に必要なコードは明示的に設定してください。0コードが0 Vとは限りません。

`set_hex` はFPGA設定値、`sent_hex` はFPGAが送信した値、`initialized` はFPGA側の送信済み状態です。
一致と送信完了はDAC実チップによる受理やアナログ電圧の一致を保証しません。DACのSDOによるデバイスレジスタ読戻しは実装していません。
エラー時に書込みを自動再試行しません。応答が失われても送信済みの場合があるので、先に `status` と実電圧を確認してください。

試験記録には「Gitコミット、FPGA識別、基板番号、IC/出力、実測CN番号、設定コード、実測V、期待Vと許容差、判定」を残してください。
DAC全16出力のコネクタ対応・複数コードでの応答は、図面・換算仕様確定後にこの形式で記録します。

## 5. 問題の切分け

- 接続できない：PCの専用NICアドレス、配線、リンクLED、TCP4244への接続、電源投入後のSDRAM試験完了を確認。
- FPGA識別が違う：納品v1.3のビットストリームか確認。
- ダミーはPASSだが実ADCが更新されない：光3本の対応、光送信、DRDY、クロックとDATAタイミングを確認。
- 実ADCの値だけ違う：符号形式、ビット順、入力電圧、上流回路のゲインを確認。
- DAC送信完了でも出力が違う：DAC電源、基準電圧、個別CN対応、コード形式、周辺回路を確認。
