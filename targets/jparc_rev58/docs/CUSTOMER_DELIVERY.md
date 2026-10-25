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
|DAC|4 IC × 4出力、16 bit rawコード指定。実電圧の自動読戻しなし|

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
|`scripts/dac_output_client.py`|実DACコード設定、FPGA送信状態をCSV表示|
|`docs/CUSTOMER_IO_TEST.md`|お客様用接続・操作・結果判定手順|
|`docs/hardware/rev58_adc_dac/`|FPGA仕様書の参照コピー|

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

実ADC光入力、実DACアナログ出力の精度・極性・個別コネクタ対応、および長時間連続動作はお客様試験で確認する項目です。
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

## 5. Quartus担当者への納品情報の依頼

FPGAに実際に書いた内容の保証はQuartus側で記録してください。レジスタのversion値だけではビットストリームを一意に特定できません。
以下をQuartusリポジトリのMarkdownに記載し、Zephyr側納品コミットと相互リンクしてください。

- 書込み済みプロジェクト名・Gitコミット・Quartus版・対象デバイス、ビルド日。
- 実際に書いたPOF/SOFのファイル名とSHA-256、Flash書込み手順と電源再投入確認結果。
- v1.3レジスタ仕様、初期値、SPI4/SPI1タイミング、ADC光入力条件、DAC更新の副作用。
- O/E・DACドーターボードの図面番号と版、光受信IC対応、DAC A/B/C/D→各CN番号の確定表と出典ページ。
- ADCの符号形式、DACのコード→電圧換算・初期化条件・電源投入時出力。
- 実機確認済み項目と未確認項目、ピン配置・タイミング制約・ビルド検証結果。

現在の資料ではDACのIC単位のCN範囲は判明していますが、A/B/C/Dと個別CNの対応が未確定です。[お客様用手順](CUSTOMER_IO_TEST.md)にも未確定として記載しています。
