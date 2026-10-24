# Ethernet経由のMAX10 SPI4レジスタ試験

`spi4_regs` は、PC → Ethernet/TCP → STM32H745 M7 → SPI4 → MAX10 の試験用プロファイル。
`quartus/projects/spi4_regs/SPI4_PROTOCOL.md` v1.0 に合わせた実装。
[仕様書の保存コピー](hardware/rev58/SPI4_PROTOCOL_v1.0.md)を同梱した（本文内の相対パスは元のQuartus構成）。
2026-10-21に実機でpingとINFO／DEVICE_ID読出しを確認した。
単独プロファイルで176回の書込み・読戻し試験も合格した。電気的タイミング測定は未実施。PC側はPython 3の標準ライブラリのみを使用する。

## 準備・ビルド・書込み

1. [初回立ち上げ](BRINGUP.md)のDirect SMPS/VOS1、M4のBCM4無効化、SWD接続条件を満たす。
2. MAX10へ `quartus/projects/spi4_regs` のレジスタ試験用ビットストリームを設定する。
   FPGAの設定・リセットはこのアプリから行わない。STM32より後にFPGAを設定してもよい。
3. リポジトリ直下で実行する。

```sh
./scripts/build.sh jparc_rev58 spi4_regs
./scripts/flash.sh jparc_rev58 spi4_regs
```

出力: `../build-jparc-rev58-spi4_regs/zephyr/zephyr.{elf,bin,hex}`。
このプロファイルはEthernet、SPI4、CONF_DONE入力を有効にする。SPI1〜3、SDRAMは無効。
通常の `ethernet` / `spi` プロファイルは従来の周辺確認専用で、TCPサーバーを起動しない。

## PCから操作

PCの基板接続用Ethernetを、例として **192.168.100.1 / 255.255.255.0** に設定する。
基板は **192.168.100.58/24、TCP 4244**。同じネットワークへ直接接続し、DHCP・ゲートウェイは使わない。
MACは起動時ランダム生成。IP変更は `CONFIG_JPARC_SPI4_TEST_IP`、ポート変更は
`CONFIG_JPARC_SPI4_TEST_PORT` を `apps/bringup/profiles/spi4_regs.conf` に設定して再ビルドする。
この無認証の試験サーバーは基板試験用LANで使う。

**次のコマンドは基板へ到達できるホストPC側で実行する。** コンテナから使う場合は、
そのコンテナから上記IPへの経路が必要。PCへ `spi4_client.py` 一つだけコピーしても動作する。

```sh
python3 targets/jparc_rev58/scripts/spi4_client.py info
python3 targets/jparc_rev58/scripts/spi4_client.py write 0x0100 0x12345678
python3 targets/jparc_rev58/scripts/spi4_client.py read 0x0100
python3 targets/jparc_rev58/scripts/spi4_client.py test
```

書込み成功時は `WRITE/readback OK: 0x0100 = 0x12345678`、読出しは
`0x0100: 0x12345678` と表示する。`write` はSTM32側で別SPIフレームによる読戻しを行い、
一致して初めて成功を返す。値は10進または `0x` 付き16進、アドレスは16 bit、値は32 bit。
ホストやポートを変更した場合はサブコマンドの前へ `--host ... --port ...` を付ける。

`test` は**全16個のDUMMYを上書きする**。6種類のパターン、DUMMY[0]のwalking-one/zero、
16アドレスへの異なる値の書込み後の一括読出し、RO書込み・未定義／非整列アクセスの拒否と値保持、
WRITE_COUNTの増分176（32 bit循環）を検査する。成功時は `PASS: ... 176 verified writes ...`。
最後のDUMMY[i]は `0xA5000000 | (0x0100 + 4*i)`。元値の復元は行わない。
この試験中は別のマスターからSPI4を操作しないこと。

FPGA再設定直後の全ゼロ初期値も検査するには、他の書込みを行う前に:

```sh
python3 targets/jparc_rev58/scripts/spi4_client.py test --expect-reset
```

## 実装とSPIタイミング

- STM32 master、Mode 0、8 bit、MSB first、全二重。
- PE2=SCK、PE5=MISO、PE6=MOSIはAF5。**PE4=NSSはGPIO active-low**。
- 要求1 MHz、HSE 25 MHzカーネルクロック／32で計算上 **781.25 kHz**。
  本RTLの上限2 MHzを超えない。基板資料の20 MHz設定は使用しない。
- 1フレーム8バイトを1回の `spi_transceive_dt()` で送受信。
  CS setup/holdはGPIO CSのdelayでそれぞれ1 µs、フレーム間にもbusy-wait 1 µsを入れる。
- CONF_DONE=PI13がHighでなければ `ERR NOT_READY`。
  コマンドごとに10 ms待ってからDEVICE_IDと版を読んで照合する。
  各フレーム直前にもCONF_DONEを確認。READY/FAULT/IRQはこのRTLでは駆動されず、成功判定に使わない。
- SPI4はサーバーの単一スレッドだけが使用。書込みと読戻しの間へ他の要求は入らない。
- 通信エラーを自動再試行しない。TCP切断などでは書込みが完了済みの場合があるため、
  再操作前に対象値とWRITE_COUNTを読む。CRCはなく、読戻しでも全ての電気的異常を保証はしない。

## TCPコマンド形式

ASCII、改行LF終端（CRLFも可）、1コマンド1応答。接続を保持して複数コマンドを送れる。
一度に処理するクライアントは1つ。行は改行を除き最大95バイト、数値の符号は受け付けない。
不正バイト／長過ぎる行はエラーを返して切断し、末尾を別コマンドとして実行しない。
受信・送信タイムアウト5秒。長時間かけて送る未完の行も切断する。

```text
INFO
OK INFO 0x4a503538 0x00010000 0x00000001 16 0

WRITE 0x0100 0x12345678
OK WRITE 0x0100 0x12345678

READ 0x0100
OK READ 0x0100 0x12345678

WRITE 0x0000 1
ERR FPGA 0x08
```

INFOの値の順序はDEVICE_ID、PROTOCOL_VERSION、CAPABILITIES、DUMMY_COUNT、WRITE_COUNT。
READ/WRITEはFPGAのstatusを観測できるよう、RO・未割当・非整列アドレスもSPIへ渡す。
ただし操作前のID/版確認が成功することが条件。任意opcodeや短縮フレームの送信APIは提供しない。

| 応答 | 確認対象 |
|---|---|
| `ERR NOT_READY` | FPGA設定、CONF_DONE配線／レベル |
| `ERR ID <id> <version>` | ビットストリーム違い、MISO固定Low、配線、SPIモード |
| `ERR FRAME` | RX先頭3バイトやWRITEデータ部が仕様と異なる。配線・信号品質 |
| `ERR FPGA <status>` | 仕様のbit mask: 1=opcode、2=非整列、4=未割当、8=RO書込み |
| `ERR TRANSPORT <errno>` | Zephyr SPI転送失敗、または転送前のCONF_DONE低下 |
| `ERR VERIFY <addr> <expected> <actual>` | ヘッダー受理後の書込み／読戻し不一致 |
| `ERR COMMAND` | 書式、範囲、行長、不正バイト |
| `ERR TIMEOUT` | 未完の行の受信期限超過 |

TCP接続自体が失敗する場合は、PCのIP／経路、リンク、RTTのPHY IDとサーバー起動ログを確認する。
`rev58_status.error` が非ゼロなら起動条件または周辺初期化で停止している。

## 基板なしの検証と実機で残る確認

```sh
python3 -m unittest discover -s targets/jparc_rev58/tests/unit -v
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-spi4_regs
```

前者はホストCコンパイラ（`cc`、または `CC`）で実際のファームウェアのコマンド／フレーム処理を
共有ライブラリとしてコンパイルし、独立したPythonレジスタモデルと接続する。
PCツールはローカルTCP経由でその処理を呼び、分割応答、全試験、カウンタ周回も確認する。
ZephyrのTCPスタック・STM32 SPIドライバ・FPGA RTLの実行試験ではない。
後者は生成物のクロック、メモリ、RMII/SPI端子、PE4 GPIO CSとタイミング設定を検査する。

実機では初回起動、PHYアドレス0、CONF_DONE、Ethernet疎通、上記コマンドを順に確認する。
ロジアナでMode 0、1フレーム64クロック、NSSが途中で上がらないこと、setup/hold/間隔、
SCK High/Low時間とMOSI setup/holdを測定し、[測定記録](measurements/README.md)へ保存する。
2 MHz要求での試験、任意opcode、途中中断、再設定中の動作、長時間／温度試験は今回の自動試験の範囲外。

## 2026-10-21 実機立ち上げでの修正

ホストのIP・経路が正しくてもARPがFAILEDの場合、初期化停止の可能性がある。
今回、PHYの既設定扱い（EALREADY）とSPI4の未対応クロック源指定を修正した。
LAN8742の変更はボード限定・元ソースのハッシュ検査付き。[詳細](BRINGUP.md)。
またTCP接続直後の切断を防ぐため、`CONFIG_NET_CONTEXT_RCVTIMEO` と
`CONFIG_NET_CONTEXT_SNDTIMEO` を有効化した。修正後のイメージで再書込みすること。

## SDRAMとEthernet/SPI4を同時に使う

リポジトリ直下で:

```sh
./scripts/build.sh jparc_rev58 combined
./scripts/flash.sh jparc_rev58 combined
python3 targets/jparc_rev58/scripts/spi4_client.py info
python3 targets/jparc_rev58/scripts/spi4_client.py test
```

`combined` は既存のSDRAMとSPI4/Ethernet設定を合成する。
出力先は `../build-jparc-rev58-combined/zephyr/`。
起動のたびにSDRAM 8 MiB全域を上書きして検査し、合格後にTCPサーバーを起動する。
起動後はSDRAMを初期化済みの状態で維持し、従来と同じIP `192.168.100.58`、
TCPポート4244、PCコマンドを使用できる。接続は起動後数秒待ってから行う。
SDRAM試験に失敗するとサーバーは起動せず、`rev58_memtest` に失敗情報を保存する。

SDRAMは `0xD0000000` から8 MiB。現時点のスタック・通常変数・TCP処理は内蔵RAMを使い、
SPI4の転送内容をSDRAMへ保存する機能はまだ追加していない。D-cacheは引き続き無効。
デバッガで `rev58_memtest.stage == 5` と `rev58_status.error == 0` を確認できる。
GDBでCPUを停止するとEthernetも止まるため、PCコマンド試験時は実行状態に戻す。

FPGA ADC/DAC統合版v1.3のSPI1ダミー受信試験は、別の `adc_dummy` プロファイルを使用する。
[DMA受信とホスト操作手順](ADC_DUMMY_TEST.md)を参照。
最新クライアントはv1.0〜v1.3のINFOに対応し、旧ダミーレジスタ試験も使用できる。
v1.1以降では0x0200がDAC領域となるため、異常アドレス試験先は0x0800に変更した。
旧いホストスクリプトをそのまま新版FPGAで使わず、ファームウェアと併せて更新すること。
