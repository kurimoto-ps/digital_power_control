# JPARC rev58 試験ソフト納品資料

作成日：2026-10-25。対象：STM32H745 M7の `adc_dummy` プロファイルとMAX10 ADC/DAC統合版v1.3。
本納品は基板の通信・メモリ・入出力確認用です。電源制御ループ、PWM制御、PLC制御は実装対象に含みません。

## 1. ソフト構成

```text
お客様ホストPC（Python 3、標準ライブラリのみ）
  └ Ethernet / TCP 4244
      └ STM32H745 Cortex-M7 / Zephyr
          ├ 起動時：外部SDRAM 8 MiB試験
          ├ SPI4 master：MAX10のレジスタ読み書き
          └ SPI1 slave + DMA：MAX10のダミーADCストリーム検査
              MAX10 ADC/DAC統合版 v1.3
                ├ 光入力：O/Eボード → ADC1～3受信 → 最新値レジスタ
                ├ 内部ダミー生成 → FIFO → SPI1送信
                └ DAC1～4（各4出力）：計16チャネルの値を送信
```

|項目|納品時の構成|
|---|---|
|STM32|STM32H745XIH6、M7のみ使用。BCM7=1、BCM4=0、M4起動無効|
|OS・クロック|Zephyr、SYSCLK 400 MHz、HCLK 200 MHz、VOS1|
|ネットワーク|固定IP `192.168.100.58/24`、TCPポート4244。DHCPなし|
|SPI4|STM32 master、Mode 0、MSB first、8 bit。1 MHz指定（現クロックでは781.25 kHz）、1要求8バイト|
|SPI1|STM32 slave RX、Mode 0、20 MHz。PG11=SCLK、PA4=NSS、PB5=DATA。FPGAが駆動|
|DMA|内部AXI SRAMの32 KiB循環バッファ。ADC検査データをSDRAMへ蓄積する機能は未実装|
|SDRAM|0xD0000000～、8 MiB（32 bit整数2,097,152個）。起動ごとに破壊試験を実施|
|FPGA識別|ID `0x4A503538`、version `0x00010003`、capabilities `0x0000000F`|
|ADC|3系統、24 bit。内部ダミーは各78.125 kHz。ADC4～6は現FPGAで未実装|
|DAC|4 IC × 4出力、±10 V・符号付き16 bit（2の補数）。CN3～CN18を電圧またはrawコードで指定。実電圧の自動読戻しなし|

M7がEthernet、SPI4、SPI1 DMA、SDRAM試験を担当します。M4との処理分担はありません。
SPI4操作とダミー試験は同じサーバで直列に処理されるため、複数の試験スクリプトを同時に実行しないでください。

## 2. GitHubで管理するもの

リポジトリの `targets/jparc_rev58/` が本基板専用です。既存Nucleoは `targets/nucleo_h755zi_q/` に分離しています。

|場所|内容|
|---|---|
|`boards/`|rev58ボード定義|
|`apps/bringup/`|STM32試験アプリ、プロファイル、Ethernetサーバ、DMA受信|
|`lib/fpga_link/`|SPI4プロトコル、ADCレコード解析|
|`scripts/spi4_client.py`|通信確認、レジスタ検査、DAC基本設定|
|`scripts/adc_dummy_client.py`|FPGA内部ダミー→SPI1 DMAの検査|
|`scripts/adc_input_client.py`|実ADCの最新値・電圧をCSV表示|
|`scripts/dac_output_client.py`|実DAC電圧／コード設定、CN対応・公称電圧・FPGA送信状態をCSV表示|
|`docs/CUSTOMER_IO_TEST.md`|お客様用接続・操作・結果判定手順|
|`docs/hardware/rev58_adc_dac/`|現行FPGA統合仕様書と旧引継ぎ資料の参照コピー|
|`docs/hardware/circuit/`|母基板・DAC・O/E回路図、rev58コネクタ用途表とSHA-256|

ソースの納品識別にはGitコミットIDを使用してください。今回のホストスクリプト追加はSTM32ファームウェアを変更しません。
STM32基準実装は `edd9817`（`adc_dummy`）。実際の納品コミットは `git rev-parse HEAD` で記録します。
ビルド生成物は本リポジトリにコミットしない運用です。書込み用HEX/BINおよびFPGA POF/SOFを配布する場合はGitHub Releaseの添付ファイルとして、ソースコミットとSHA-256を一緒に管理してください。
本変更ではGitHub Release作成・バイナリ公開は行っていません。

## 3. 確認済みの範囲

|試験|これまでの実機結果|
|---|---|
|STM32起動|M4起動無効化後、`rev58_status.error=0`、tick進行を確認|
|SDRAM|8 MiB試験完了、`rev58_memtest.stage=5`、失敗アドレス0|
|Ethernet→SPI4|読書き一致、ダミーレジスタ16個・176回書込み試験PASS|
|FPGA内部ADCダミー|1秒、3秒、100 ms反復を確認。CRC・通番・値・DMAエラーなし|
|ダミー値の例|`123456/89ABCD/FEDCBA`、`000000/FFFFFF/800000`（16進）|
|単独電源起動|以前のEthernet＋SPI4構成で電源再投入後の通信を確認|

DACの個別CN対応はTEP522A回路図で確定しました。実ADC光入力、実DACアナログ出力の精度・極性・実配線、および長時間連続動作はお客様試験で確認する項目です。
今回追加のホストスクリプトのテスト結果と、実際に接続したアナログ回路での検証は区別してください。

## 4. 起動・再書込み

通常の試験にはホスト側Python 3とEthernetだけを使用します。ST-LINK、USB Blaster、Docker、Zephyr SDKは不要です。
STM32内蔵FlashとFPGAの不揮発領域に書込み済みなら、電源再投入で起動します。デバッガからの `continue` は不要です。
SDRAM検査後にTCPサーバを起動します。FPGAのダミー値・送信許可・DAC設定は電源再投入で失われます。必要な値はホストから再設定してください。DACの電源投入時の実電圧は別途測定してください。

開発用Zephyr環境で再ビルド・書込みする場合（リポジトリルート）：

```sh
./scripts/build.sh jparc_rev58 adc_dummy
./scripts/flash.sh jparc_rev58 adc_dummy
```

ビルド出力はワークスペースの `build-jparc-rev58-adc_dummy/zephyr/`。
ボード名は `jparc_rev58/stm32h745xx/m7`。環境構築・電源設定・BCM4設定は [BRINGUP.md](BRINGUP.md)、詳細試験は [ADC_DUMMY_TEST.md](ADC_DUMMY_TEST.md)、[SPI4_TEST.md](SPI4_TEST.md) を参照してください。

## 5. FPGA仕様・書込みファイルの識別

現行の通信・レジスタ・初期値・動作は、Quartus担当者作成の [FIRMWARE_SPEC_v1.3.md](hardware/rev58_adc_dac/FIRMWARE_SPEC_v1.3.md) を基準とします。
取得元は `quartus/projects/spi4_adc_dac/FIRMWARE_SPEC_v1.3.md`。旧 `ZEPHYR_*_HANDOFF.md` より現行統合仕様を優先します。
原本は内容を変更せず保存しており、原本内の相対リンクは元のQuartusディレクトリ構成を指します。

|項目|Quartus側の記録|
|---|---|
|プロジェクト／トップ|`spi4_adc_dac` / `jparc_adc_dac_top`|
|対象FPGA|MAX10 `10M16DCF484I7G`|
|ツール|Quartus Lite 25.1std.0 Build 1129|
|不揮発書込みファイル|`output_files/spi4_adc_dac.pof`|
|SRAM書込みファイル|`output_files/spi4_adc_dac.sof`|
|JTAG ID / POF checksum|`0x031030DD` / `0x04677A1D`|
|書込み状態|仕様書でPOF書込み・検証成功（0エラー・0警告）を報告|
|設計検証|仕様書で807,670項目の機能シミュレーションとタイミング／CDC検証を報告|

```text
POF SHA-256: 4ca165e56663aa9a05f5071a914ff46301c3afedabf953dc62b4baed441eed7d
SOF SHA-256: b725f3cf1fa165119b086da43c951653b69b25f60eb13905e2d6bef29f99e1f9
```

本資料作成時に提供されたPOF/SOFのSHA-256を計算し、上記と一致することを確認しました。
FPGA仕様書末尾の「STM32との実機通信未検証」はQuartus担当者側の確認範囲です。本資料3章のZephyr側試験実績と合わせて評価します。
ただし過去のZephyr試験はID/version/capabilitiesで識別しており、そのときのビットストリームのハッシュとの突合せ記録はありません。今回識別したPOFでの結合受入れは [試験手順](CUSTOMER_IO_TEST.md) の通信・ダミー試験を再実施して記録してください。

## 6. 回路と電圧仕様の出典

[回路図一覧と読取り根拠](hardware/circuit/README.md) に図面番号・版・参照ページをまとめています。
DACはTEP522AのIC11/13/15/17のA/B/C/D（23/22/19/18番ピン）を各LEMO中心端子まで追跡し、**CH1～16＝CN3～CN18**と確定しました。LEMO外側はAGNDです。
O/EはTEP523A、母基板の配線はTEP521C、rev58との接続はConnector_usage_rev58.pdfを参照します。

DACの電圧指令はユーザー指定の**±10 V、符号付き16 bit（2の補数）**を採用し、`V = signed_code × 10 / 32768` とします。FPGA仕様の「16 bit生コード」は伝送方式の説明であり、ホストで電圧をコードへ換算してから送る構成と両立します。
1 LSBは約305.176 µV、最大正電圧は+9.999694824 Vです。+10 V指令は正側最大コードに制限し、変換後の公称電圧を表示します。±10 Vの範囲外は拒否します。
FPGAはDACのデータレジスタだけを書き、校正や機能レジスタを初期化しません。電圧表示は公称値で、実測値ではありません。

ADCの符号形式は、24 bitコードを数値へ解釈する方法です。2の補数なら `0xFFFFFF=-1`、符号なしなら `0xFFFFFF=16777215` です。
FPGAは符号変換をせず、O/Eボードは光をデジタル信号へ変換するだけです。今回の回路図には光の送信元ADCが含まれないため、ADCの符号形式はこの資料だけでは確定しません。
ホストは `--encoding signed24 / unsigned24 / offset-binary` を選択し、指定された `3 / 2^23` の係数で電圧を表示します。送信元ADCまたは外部ダミーの仕様に合わせて選んでください。

追加情報として必要なのは、送信元ADCのコード形式と、将来の再ビルドに備えたQuartusソースのGitコミットです。CN対応とDAC電圧仕様の確認依頼は解消しました。
