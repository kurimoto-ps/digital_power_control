# FPGA v2.0：11 CH受信・光リンク調整手順

試験 3/3 — [追加確認試験の表紙](ADDITIONAL_TESTS.md) / [English](MIXED_ADC_V2_en.md) / [前の試験](ACC_INPUT_TEST.md)

表紙の共通準備を完了してから実施してください。以下のコマンドは、既存Docker環境の `targets/jparc_rev58/scripts` で実行します。3試験を通してFPGA v2.0.2と今回のSTM32 `mixed_adc` イメージを使用します。今回の追加機能はビルド・ホスト試験まで確認済みで、以下の実機試験は試験者に実施していただくものです。

## 試験開始時の確認

ACC試験を終了し、その信号源・繰返し読出しを停止します。今回はv2の48バイト受信を確認します。従来実施したv1.3の8バイトADC試験とは異なります。M7がEthernet・SPI4・SPI1 DMAを担当し、M4は起動しません（BCM4無効、BCM7有効）。SDRAM 8 MiB起動試験も含みます。SPI1リング自体は内蔵AXI SRAMに配置します。

```sh
python3 mixed_adc_client.py status
```

クライアントは1つずつ操作し、APPLY途中に他クライアントからレジスタを書き換えないでください。失敗した書込みの自動再試行はしません。[提供されたv2仕様の保存版](hardware/mixed_v2/FIRMWARE_SPEC_v2.md)も参照できます。

## 受信する内容と検査

FPGAがSPI1マスター、STM32がスレーブです。PG11=SCLK、PA4=NSS、PB5=DATA。Mode 0、20 MHz、MSB first、8 bit単位、NSS Low中に48バイトです。

初期周期4000 × 25 ns = 100 µs（10 kHz）で、1フレームにADC24×3とADC16×8を載せます。CRC16/CCITT-FALSEは先頭46バイトを対象とし、末尾2バイトと照合します。ADC16順序はA0、A1、B0、B1、C0、C1、D0、D1です。

`capture` は100〜3000 msの**有限時間試験**です。その間はNSSごとにDMAを起動し直さず、30,720バイトの循環バッファへ連続受信します。半バッファ15,360バイト（320フレーム、10 kHz時32 ms）ごとに解析し、最終端数も回収します。DMAエラー・追い越し・SPIエラーも検出します。ホストへ返すものは区間の統計と最後の値であり、全フレームのEthernet配信や常設の無期限データ収集ではありません。

検査対象：

- マジック、形式、予約ビット、CRC、フレーム通番の連続性（32 bit周回対応）、先頭通番0、送信数との一致、設定周期から見た受信数（約±10%の余裕）。
- ADC24のknown/fresh・通番・age。隣接フレームでfresh=0なのに通番や値が変わる、ageが戻る、既知から未知へ戻る、といった不整合を検出します。**ADC24は78.125 kHzから間引くので通番差7〜8程度が正常です。差1は要求しません。** 16 bit通番の周回も許容し、fresh=1と同じ通番の組合せも丸一周の可能性があるためそれだけでは異常としません。
- `--required24` のビット0〜2に対応する系統について、区間内に既知の値があり、既知になった後のageが `--max-age-us`（既定100 µs）を超えず、受信再開カウンタが増えていないこと。起動直後、最初に既知となるまでの未知値は許容します。未接続系統を許容する場合はマスクを外します（2系統なら例 `--required24 3`）。
- `source_drop`、`frame_drop`、`missed` は0を要求します。`replaced` はADC24の最新値が集約前に更新された回数であり、通常増えます。旧版のDROPと混同しないでください。
- ADC16には各フレームの最小・最大値、最後のraw・signedを返します。`--expect16` 使用時は、全取得値が指定コード±許容幅に入ることも要求します。

`pass: true` は上記デジタル検査の結果です。ADC16にはデータ受信ACKがなく、flags bit3はFPGA側の取得窓の完了を表します。**CRCが正しくても、光入力が未接続・ビットずれ・チャンネルずれである可能性は残ります。** CRCはFPGA→STM32の転送検査であり、ADC→FPGA光リンクの正しさは既知入力で確認します。

ADC24は24 bitの2の補数としてsigned化し、`signed × 3 / 2^23` を `nominal_volts` に表示します。値の有効性はknown/ageと併せて判断してください。ADC16は16 bitの2の補数表示までで、アナログ入力レンジが確定していないため電圧換算を行いません。

## まずADC24ダミーでSPI1経路を試験

DACを全CH手動にする操作は、保持されていた手動値を実際に出力し得ます。接続負荷を確認してから明示的に実行します。`capture` 自体はDACマスクを勝手に変更せず、マスクが0以外なら拒否します。

```sh
python3 mixed_adc_client.py dac-source 0
python3 mixed_adc_client.py capture --dummy24 > dummy24.json
python3 mixed_adc_client.py capture --dummy24 --values24 0 0x7fffff 0x800000 > dummy24_limits.json
python3 mixed_adc_client.py capture --dummy24 --values24 0x555555 0xaaaaaa 0xffffff > dummy24_bits.json
```

ダミーになるのは**ADC24の3系統だけ**です。ADC16の8 CHは常に実入力です。ADC16未接続でもダミー試験の `pass` は成立し得ます。この結果をADC16光リンクの検証としないでください。

各試験は、設定を書いてAPPLY/適用内容を確認→ADC24ソース設定→ADC取得有効化APPLY→DMA準備→FIFOクリア・SPI1送信開始、の順です。終了時はSTM32自身がADC取得停止をAPPLYし、送信キューを排出して送信とDMAを止め、ADC24ソースを実入力へ戻します。最後のソース切替でFPGAのフローカウンタがクリアされるため、試験結果はJSONに保存した値を使用してください。ホスト切断時もSTM32の試験と終了処理は続きます。SPI4通信異常時は停止を保証できないため、復旧後に `status` / `stop` で確認してください。

## 新ADCの光リンクを調整する

### 配線と入力信号

| 新ADCの信号 | OE/EO基板の素子・系統 | rev58コネクタ | MAX10ピン |
|---|---|---|---|
| SCLK出力 | EO TEP526A IC24、DOUT15 | J9-58 | R2 |
| FS出力（Low有効） | EO TEP526A IC25、DOUT16 | J9-60 | R1 |
| CONVSTART出力（High 50 ns） | EO TEP526A IC26、DOUT17 | J9-62 | AA15 |
| ADDATA16入力（1本のシリアル信号） | **OE TEP523A IC11、CH4-2** | J10-44 | V13 |

OE基板のIC26は電源ICであり、この入力ではありません。既存ADC3のIC8/AB10と取り違えないでください。実配線・光ファイバ端・基板シルクを照合し、新ADC側の外部設定（DCEN、PAR/SER等）も確認します。詳細は保存版FPGA仕様と[既存コネクタ資料](CUSTOMER_IO_TEST.md)を参照してください。

8 CHに識別できる既知の安定信号を与えます。全CHゼロだけではビットずれやチャンネルずれを発見できません。正負、大小、MSBと下位bitの変化が分かる入力を含めます。入力の期待rawコードと許容差は使用ADCのレンジと入力源精度に基づいて決めてください。

### 設定値の意味

| 引数 | SPI4 shadow | 単位・意味 | 初期値 |
|---|---|---|---|
| `--period` | 0x0514 | 25 ns、取得周期。偶数かつ `conversion + delay + 270` 以上 | 4000 |
| `--conversion` | 0x0508 | 25 ns、変換完了を待つ設定C | 68 |
| `--delay` | 0x0504 | 25 ns、受信開始遅延D | 9 |
| `--phase` | 0x050C | 0=0°、1=180°、2=90°（12.5 ns遅延）、3=270° | 3 |
| `--negate` | 0x0510 | 0=なし、1=16 bit算術符号反転。bit反転ではない | 0 |

CONVSTART→FS Lowは `(C+4)×25 ns`、FS Low→最初のサンプルは `(D+3)×25 ns`、取得完了は概ね `(C+D+262)×25 ns`。サンプル間隔50 ns、128 bitを受信します。`negate=1` はSPI1とDAC自動値の両方に作用し、`0x8000` は16 bit周回により `0x8000` のままです。

初期値は調整開始点です。仕様の光遅延値は測定結果ではなく仮定で、シミュレーションの成功条件も実機での成功を保証しません。

1. 全DAC手動、`negate=0`、周期4000、変換待ち68から開始します。ADC24が未接続なら `--required24 0` を指定します。
2. 位相0〜3それぞれについて遅延Dを少しずつ変え、1秒区間のraw/min/maxと既知入力を比較します。以下は探索の**例**であり、D=4〜16が必ず適正範囲とは限りません。ADC変換時間や光往復遅延に応じて探索幅を調整します。

```sh
mkdir -p optical_tuning
for phase in 0 1 2 3; do
  for delay in 4 5 6 7 8 9 10 11 12 13 14 15 16; do
    python3 mixed_adc_client.py capture --required24 0 --phase "$phase" --delay "$delay" \
      > "optical_tuning/p${phase}_d${delay}.json" || echo "FAIL p=$phase d=$delay"
  done
done
```

3. rawを眺めるだけでなく、8 CHの期待コードと比較します。下記の数値は**書式の例**です。必ず実際に与えた電圧に対応する期待コードに置き換えてください。ここでは±8 LSBの範囲を全サンプルに要求しています。

```sh
python3 mixed_adc_client.py capture --required24 0 --phase 3 --delay 9 \
  --expect16 -24000 -16000 -8000 -1000 1000 8000 16000 24000 --tolerance 8 \
  > known_input.json
```

4. 連続して正しく受信できるDの範囲を見つけ、その中央付近を選びます。境界の一点だけ通る設定は避け、複数の入力パターン・反復試験で確認します。安定範囲がない場合は配線、光方向、ADCの直列設定、FS/CONVSTART極性、ADC変換時間を先に確認します。必要に応じCを増やし、周期制約を満たして再探索します。
5. 採用する位相/D/Cで `--ms 3000` を複数回実行します。実際の光ケーブル長、電源、温度条件でも安定範囲を確認し、JSON・設定・入力源・ケーブル・基板番号を保存します。
6. 符号反転が必要な場合だけ `--negate 1` にし、正負双方と端点を再確認します。符号反転でビットずれを補正しようとしないでください。最後にADC24の実入力も接続し、`--required24 7` で既知値・fresh・ageを確認します。

毎回の `capture` は指定した引数（省略時は初期値）を再設定します。前回の調整値は自動で引き継ぎません。採用した値をコマンドや実験スクリプトに明記してください。状態だけを見るには `status` を使用します。

## APPLYの確認とDAC連動

```sh
python3 mixed_adc_client.py configure --phase 3 --delay 9 --conversion 68 --period 4000 --negate 0
python3 mixed_adc_client.py status
```

`configure` は既定で取得停止状態へAPPLYします。0x0500への書込み成功だけで完了としません。PENDING解除・ERRORなし・REJECTED不変・SUBMITTED_IDの更新・APPLIED_ID一致・適用値0x0540〜0x0554の全照合を行います。電源再投入後は設定し直します。

DAC連動は0x0060の各bitで選択します。初期値0（全CH手動）。bit0〜7はCH1〜8←ADC16 A0〜D1、bit8〜10はCH9〜11←ADC24 CH1〜3の上位16 bit。CH12〜16は常に手動です。`0x7ff` で11 CHすべて連動します。DAC CHnのアナログ出力はDACドーターボードCN(n+2)です。

```sh
# 光リンクの実入力試験完了後、採用した調整値を明示して取得を有効化
python3 mixed_adc_client.py configure --phase 3 --delay 9 --conversion 68 --period 4000 --enable 1
python3 mixed_adc_client.py dac-source 0x7ff
python3 dac_output_client.py status
# 取得停止。出力はゼロにならず、最後の値を保持する
python3 mixed_adc_client.py stop
# 明示的に手動へ戻す。保存されている手動値に出力が切り替わる
python3 mixed_adc_client.py dac-source 0
python3 dac_output_client.py set-voltage CN3 1.0
```

DAC連動はSPI1送信停止中も動きます。SPI1を送信しない状態で取得を続けるとフレームFIFOは満杯になり、DROPが増えます。これはこのDAC連動例では予想される挙動で、次の `capture` はクリアしてから検査します。ADC24未取得の自動値は0、途絶後は最後の値を保持します。自動連動中のチャンネルへの手動電圧コマンドは拒否します。手動シャドー、実効値、送信済み値は別物です。アナログ電圧は外部計測器で確認してください。

`stop` は取得をAPPLYで止めます。0x0400=0だけ、または0x0500=0だけでは新ADC取得を停止できません。停止やTCP切断をDACのゼロ出力・安全停止として使用しないでください。

## 終了と記録

基板識別、Gitコミット、FPGA版、captureのJSON、採用した位相・遅延・変換待ち・周期、入力電圧とADC読出し値、DAC実測電圧を記録してください。未実施・不合格項目も区別します。ADC16入力レンジと光リンク調整の余裕は実測で確認してください。従来v1.3の実績は今回の版の合格を意味しません。

取得を停止し、DACを手動へ戻して、試験者間で決めた終了時の電圧を明示的に設定してください。`stop` だけではゼロになりません。上記の例ではCN3に1 Vが残ります。最終出力は外部計測器でも確認してください。

---

[追加確認試験の表紙](ADDITIONAL_TESTS.md) / [English](MIXED_ADC_V2_en.md) / [前の試験](ACC_INPUT_TEST.md)
