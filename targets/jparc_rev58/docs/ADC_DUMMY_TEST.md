# FPGA v1.3 ダミーADC → SPI1 DMA → Ethernet試験

FPGAの3系統ダミー生成をSPI4から設定し、20 MHz SPI1をSTM32 M7が
循環DMAで連続受信する。ホストには検査結果だけを返す。
SDRAM 8 MiBの起動時試験と、従来のEthernet/SPI4サービスも有効。

## ビルド・実行

FPGAにADC/DAC統合版v1.3を設定済みの状態で、リポジトリ直下から:

```sh
./scripts/build.sh jparc_rev58 adc_dummy
./scripts/flash.sh jparc_rev58 adc_dummy
python3 targets/jparc_rev58/scripts/spi4_client.py info
python3 targets/jparc_rev58/scripts/adc_dummy_client.py
```

新しい出力先は `../build-jparc-rev58-adc_dummy/zephyr/`。
M4はBCM4=0、M7はBCM7=1のまま。GDBでCPUを停止したまま試験しない。
固定IP `192.168.100.58/24`、TCP 4244。PC側の接続用IPは例 `192.168.100.1/24`。
従来の `combined` はSPI1 DMA試験を含まないため、新プロファイルを書き込むこと。

`info` の期待値はID `0x4A503538`、version `0x00010003`、capabilities `0x0000000F`。
旧v1.0〜v1.2も通常のINFO/READ/WRITEは使えるが、ADCTESTはv1.3・caps=15・3系統だけを受け付ける。

既定は1秒、値 `0x123456 / 0x89ABCD / 0xFEDCBA`。各系統約78,125件を受信する。

```sh
# 3秒間、値を変更（ADCの24 bit生ビット列。符号・電圧変換なし）
python3 targets/jparc_rev58/scripts/adc_dummy_client.py \
  --duration-ms 3000 --values 0 0xffffff 0x800000

# 1秒試験を10回（各回の間には停止・再設定が入り、通番は0から再開）
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --repeat 10

# 完全な結果をJSONで表示
python3 targets/jparc_rev58/scripts/adc_dummy_client.py --json
```

`--duration-ms` は100〜3000、`--repeat` は1〜1000。
`--host` / `--port` も指定可能。Python標準ライブラリのみを使用し、
`adc_dummy_client.py` 単独でホストPCへコピーして実行できる。
成功は終了コード0と `PASS`、失敗は終了コード1と診断情報。
データそのものをEthernetへ全量転送する機能ではない。

## 開始・終了の制御

1. FPGA ID/version/capabilities/ADC_CHANNEL_COUNTを確認。
2. ADC_CONTROL=0、BUSY解除を待つ。
3. ADC_DUMMY1〜3へ指定値を書き、各値を読み戻す。
4. ADC_SOURCE_MODE=1を書き、読み戻す。
5. SPI1と循環DMAを準備する。
6. ADC_CONTROL=3（clear＋開始）を書き、bit0=1の読戻しを確認。
7. 指定時間連続受信し、CRC・値・系統別通番を検査する。
8. 送信中にFPGAのDROP/RESTARTを読み、ADC_CONTROL=0、BUSY解除を待つ。
9. DMAの最終端数を回収し、安定したFPGA_TX_COUNTと全受信数を比較する。
10. ADC_SOURCE_MODE=0に戻して結果を返す。

終了時には正常／異常を問わず停止・ダミーモード解除を試みる。ホスト切断時も
サーバーの有限時間試験は最後まで実行し、終了処理を行う。SPI4自体が故障して
停止命令を届けられない場合は `cleanup_error` を返すため、基板の接続状態を確認する。
ダミー値の設定は残るが、モードは実ADC=0、送信許可は0になる。
DAC設定には触れない。電源再投入後もホストから毎回この手順で再設定する。

**停止後もFPGAはダミーを生成し、約3.28 msでFIFOが満杯になる。**
そのためDROP/RESTARTは「停止直前、送信中」の非同時スナップショットであり、
停止後の蓄積は試験中の欠落として扱わない。停止直前の読出しから実際の停止までの
短い末尾期間を含め、STM32での欠落は通番・CRC・FPGA_TX_COUNTとの一致でも検査する。
これらのカウンタはSOURCE_MODE変更でリセットされるため、結果はSTM32内へ保存してからモードを戻す。

## DMAと検査内容

- SPI1: PG11=SCLK、PA4=NSS、PB5=DATA、AF5。FPGAが全3本を駆動。
- Slave、Mode 0、MSB first、8 bit、ハードウェアNSS。MISOなし。
- 100 MHzのSPI1 kernel clock、入力SCLKはFPGAの20 MHz。
- SPI1はこのモジュールが専有。STM32 LLで受信専用・TSIZE=0の連続受信を設定し、
  Zephyr DMA APIで32 KiBの循環転送を開始する。1レコードごとのSPI API再設定は行わない。
- DMAMUX1 channel 0 → DMA1 stream 0、request 37 (SPI1_RX)を予約。
  SPI4は既存のポーリング転送を使う。`CONFIG_SPI_STM32_DMA` は無効。
- DMAはAXI SRAMの32-byte整列バッファへ書く。D-cacheは無効、DTCMは使わない。
  半分の16 KiB（2048レコード）ごとの割込みで処理する。割込み中にCRC計算は行わない。
- 完成した半分を作業バッファへコピーし、循環DMAが上書きする前に回収できなければ失敗とする。
  SDRAMは起動試験後も有効だが、今回の受信バッファや保存先には使っていない。
- A1〜A3で系統を識別し、順序固定と仮定しない。CRCはCCITT-FALSE。
  破損時は1 byteずつ再同期し、エラーを保持する（回復しても試験はPASSにしない）。
- 各系統で初回seq=0、以後+1（65535→0含む）、値の一致を確認。
  受信数は要求時間×78,125 Hzの±10%以内を要求する。これは厳密な周波数測定ではない。
- CRC／ヘッダー／値／通番／DMA／SPIエラー、捨てたbyte、末尾不完全レコード、
  送信中のFPGA DROP/RESTARTがすべて0、かつFPGA_TX_COUNT=全受信数の場合だけPASS。

TCPサーバーは単一クライアント・単一所有スレッドでSPI4操作と試験を直列化する。
ADCTEST中の他クライアントのDAC設定などは同時処理しない。

## TCP形式

```text
ADCTEST 1000 0x123456 0x89abcd 0xfedcba\n
OK ADC {"pass":true,...}\n
```

JSONの主な値は `received`、`fpga_tx`、`elapsed_ms`、各エラーカウンタと
`channels` の3要素（records/expected/last/first_seq/last_seq/value_errors/
sequence_errors/fpga_drop/fpga_restart）。`control_error` はSPI4制御の失敗、
`cleanup_error` は停止／モード解除の失敗、`error` はDMA受信処理のerrno。
`spi_error_flags` はSTM32 SPI_SRのOVR/MODF/TIFREのビットマスク。
`crc_errors` / `header_errors` は再同期候補の検査失敗数で、物理的な破損レコード数とは限らない。
`elapsed_ms` は開始指示直前から停止・BUSY解除までの時間であり、指定時間より少し長くなる。

## DAC設定と既存コマンド

```sh
# CH1に16 bit生コードを設定（実行時に外部DAC出力が変わる）
python3 targets/jparc_rev58/scripts/spi4_client.py dac-set 1 0x1234
```

CHは1〜16。要求値の読戻しに加え、BUSY/PENDING解除、INITIALIZED、DAC_SENTを確認する。
表示はFPGAの送信完了であって、外部DACの受信やアナログ電圧の保証ではない。
同じICの4CHを一括再送するため、初回は他の3CHの初期コード0も送信される。
コード0を0 Vとは仮定しない。ダミーADC試験からこのコマンドを自動実行することはない。

汎用 `write` はDAC_SETの16 bit、ADCダミー値の24 bit、CONTROL/SOURCE_MODEのbit0だけを
読戻し比較する。CONTROL=3のclear bitが読出しで0になるのは正常。
既存 `spi4_client.py test` の未割当アクセスは0x0800へ変更し、DAC_SET=0x0200へ書き込まない。

## 検証記録・限界

2026-10-24、接続中の基板にこのプロファイルを書き込み、FPGA v1.3/caps=15を確認。
既定値で1秒（234,791レコード）、別の3値で3秒（703,775レコード）、100 msを3回
（各23,847レコード）の試験が合格した。全てCRC/通番/値/DMA/SPI/送信中DROPは0、
FPGA_TX_COUNTと受信数が一致。最後にCONTROL=0、SOURCE_MODE=0、DAC_TRANSFER_COUNT=0を確認した。
実ADCの入力受信、DACのアナログ出力、ロジックアナライザの波形、長時間の無停止運転は未検証。

```sh
python3 -m unittest discover -s targets/jparc_rev58/tests/unit -v
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-adc_dummy
```

ホスト試験はCRC既知ベクトル、3系統・16 bit通番周回、分割入力、破損・欠落・重複・値違い、
ホスト結果判定、TCP分割応答、SPI4旧版とv1.3、clear bitの読戻しを検査する。
生成物検査では54本の周辺端子、DMA割当とAXI SRAM配置も確認する。

参照原本のコピー（相対リンクは元のQuartus構成を指す）:
- [ADC v1.3引継ぎ](hardware/rev58_adc_dac/ZEPHYR_ADC_STREAM_HANDOFF.md)
- [DAC引継ぎ](hardware/rev58_adc_dac/ZEPHYR_DAC_HANDOFF.md)

原本はそれぞれ `quartus/projects/spi4_adc_dac/`、`quartus/projects/spi4_dac_dc/` から取得した。
ADC文書の「未書込み」記載に対し、ユーザーの書込み済み連絡と実機のv1.3読出しを優先した。
原本コピーのSHA256は同ディレクトリの `SHA256SUMS` に保存した。
