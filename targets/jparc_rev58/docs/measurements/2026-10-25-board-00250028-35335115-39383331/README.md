# 新品rev58基板のSTM32受入れ確認（2026-10-25）

[English version](README_en.md)

基板識別：STM32 UID（0x1FF1E800から32 bitずつ）`00250028 / 35335115 / 39383331`。
同型の別の新品基板へ交換後の試験。MAX10はユーザーが書込み済み。
ソフトソース：`0a6f953`、STM32アプリの基準実装は `edd9817`。

## 初期設定・書込み

ST-LINK V3SET、SWD接続、ターゲット電圧3.27 V、STM32 Device ID 0x450、Rev V、Flash 2 MiBを確認。
初期値はBCM4=1、BCM7=1、RDP=0xAA（Level 0）。STM32CubeProgrammer v2.22.0でBCM4のみ0へ変更し、書込み照合成功。
実行時SYSCFG_UR1=0x00010000によりM4無効・M7有効を確認。保護・電源等の他option byteは変更していない。

OpenOCDで各プロファイルを順に書込み。sdram以降は書込み後の `verify` 成功を確認。
既存ビルドの電源・クロック・ピン・RAM配置の生成物検査も各プロファイルで合格。

|構成|実機確認結果|
|---|---|
|bringup|error=0、ticks=6、SYSCLK=400 MHz、HCLK=200 MHz|
|sdram|error=0、8 MiB試験stage=5、失敗アドレス・期待値・実値=0|
|ethernet|error=0、PHY ID読取り成功、RTTでリンクupを確認|
|spi|error=0、SPI1～4ドライバready（SPI2/3の実転送は対象外）|
|spi4_regs|FPGA v1.3/caps=15、初期0確認付き176回試験PASS（0→176）、0x0100へ0x12345678の書込み／読戻し一致|
|combined|SDRAM stage=5、error=0、176回試験PASS（177→353）|
|adc_dummy（最終書込み）|SDRAM stage=5、error=0、176回試験PASS（353→529）、下記DMA試験PASS|

FPGA識別：ID=0x4A503538、version=0x00010003、capabilities=0x0000000F。
新基板に書いたFPGAファイル自体のハッシュはユーザー側書込みのため未照合。

## 顧客用ソフトでのADCダミー試験

|設定|時間|受信レコード数|結果|
|---|---|---|---|
|123456 / 89ABCD / FEDCBA|1秒|234,793|PASS|
|000000 / FFFFFF / 800000|3秒|703,783|PASS|
|既定値、3回反復|各100 ms|23,845 / 23,847 / 23,846|全PASS|
|555555 / AAAAAA / 123456|1秒|234,795|PASS|

全試験でFPGA_TX_COUNTと受信数が一致。CRC・値・通番・SPI・DMAエラー、送信中DROP/RESTARTは0。
生の診断結果は [adc_results.json](adc_results.json)。
1秒ADCTEST要求の直後にホスト接続を閉じる試験も実施し、2秒後に別接続からCONTROL=0、SOURCE_MODE=0を確認。
DAC_TRANSFER_COUNT=0で、DAC出力要求は行っていない。
最終GDB確認はticks=36、error=0、SDRAM stage=5。その後CPUをresumeし、OpenOCDを終了。

最初のネットワーク試験ではデバッガの停止状態が残り接続できなかった。明示的なresume後に正常通信し、以後の確認はCPU実行中に行った。
複数プロファイルのGDBログ先頭は書換え前のCPU位置を新ELFで表示するため、関数名／引数はその時点では評価しない。書換え・リセット後の構造体とネットワーク試験を判定根拠とする。

## 最終Flashイメージ

`build-jparc-rev58-adc_dummy/zephyr/` の顧客用イメージを書込み・照合済み。

```text
zephyr.hex SHA-256: 169b05152db4e75fb2e9f15a7869a3f78903bbc400073d45196ae9d42e489f2c
zephyr.bin SHA-256: 588cd563cd1c7fbf0e5994d1c178e28c45b0ba60640747d613a4d47e75ffe0d0
```

## 電源再投入

ユーザーが電源OFF/ONを実施した後、OpenOCD/GDBを接続しない状態で次を確認し、コールドブート合格。

- EthernetからFPGA ID/version/capabilitiesを正常取得。WRITE_COUNT初期値0。
- `spi4_client.py test --expect-reset`：全初期値0、176回試験PASS（0→176）。
- 顧客用ソフトの1秒ADCダミー試験：234,794レコード、FPGA送信数と一致、全エラー0、PASS。
- TCPサーバはSDRAM試験合格後のみ起動するため、再投入後のSDRAM起動試験完了も確認。
- 最終状態CONTROL=0、SOURCE_MODE=0、DAC_TRANSFER_COUNT=0。デバッガを終了したまま顧客用ソフトを運転。

再投入後にSTM32を再書込みすることなく動作確認できた。

## 確認範囲

実ADC光入力、DACアナログ出力、長期運転・温度／電源変動・波形測定は未実施。
READY/FAULT/IRQはいずれもraw=1を記録しただけで、FPGA通知機能の合格判定ではない。
デバッグ記録は [debug_results.txt](debug_results.txt)。お客様の操作は [試験手順](../../CUSTOMER_IO_TEST.md)。
