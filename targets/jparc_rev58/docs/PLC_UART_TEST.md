# PLC 16入力・16出力とRS232C：配線対応の実機試験

FPGA v2.0.2にも対応しました。[ACC入力診断・同軸配線試験](ACC_INPUT_TEST.md)で0x0024とSTM32 PA0/PD13、および0x0028/0x002Cによる12 MHz検出を確認できます。J16/J17は96 kHz／TRIG、12 MHzは別のJ6光経路です。実機試験は未実施です。


対象：rev58＋マザーボードTEP521C、STM32 `mixed_adc` イメージ。2026-11-07追加。
[English](PLC_UART_TEST_en.md)／[ADC・DACを含む全体構成](MIXED_ADC_V2.md)

**本機能はビルド・ホスト試験まで確認済みです。今回実機書込み・PLC接続試験・RS232C試験はしていません。** 以下は試験者が実機で実施する手順です。

## 1. 今回追加したもの

- M7からPLC用GPIOを16入力・16出力として使用。出力は初期化時に全点Low。
- Ethernetで入力値読取り、出力値指定、32点の対話式試験・JSON記録。
- UART8を**115200 bps、8 data bits、パリティなし、1 stop bit、フロー制御なし**で使用。
- UART受信は割込みで1,024バイトのリングへ保存。Ethernetから最大128バイトずつ取り出し、送信は1コマンド最大32バイト。受信欠落数・エラーフラグを確認可能。

Ethernet、SPI4、v2 ADC受信試験、DAC操作、SDRAM 8 MiB起動試験は同じイメージに残しています。PLC/UARTコマンドはFPGAのCONF_DONEやバージョン判定に依存しません。これはPLC信号のマッピング試験用で、運転シーケンスや制御ロジックは実装していません。

出力は**次に変更するまで保持**します。TCP切断ではゼロへ戻りません。対話式試験は正常終了・通常の中断時に全出力をLowに戻しますが、通信断・PC強制終了では確認できない場合があります。試験ではPLCを試験モードにし、対象信号で機械や電源が運転しない状態にしてください。これは起動前の浮遊状態や故障時まで含む安全出力機能ではありません。

## 2. 準備

STM32には今回の `mixed_adc` をビルドして書き込んでください。以前の同名イメージにはPLC/UARTコマンドがありません。以下は開発環境で実行するコマンドで、今回こちらでは書込みを実行していません。

```sh
./scripts/build.sh jparc_rev58 mixed_adc
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-mixed_adc
./scripts/flash.sh jparc_rev58 mixed_adc
```

試験者のホストPCはPython 3のみ必要です。追加pipパッケージは不要です。

```sh
git clone https://github.com/kurimoto-ps/digital_power_control.git
cd digital_power_control
git switch feature/jparc-rev58-zephyr-layout
# 既存cloneの場合はこのブランチで git pull --ff-only
cd targets/jparc_rev58/scripts
python3 board_io_client.py plc-read
```

ホスト有線LANは `192.168.100.1/24`、STM32は `192.168.100.58:4244/TCP`。別IPならサブコマンドの前に `--host IP` を指定します。GPIO値はJSON内では10進数、対話式のマスク入力・比較表示は16進数です。bit0は最下位です。

試験中は他のホストスクリプトによる同時操作を止めます。特にADC capture中はTCPサーバがその試験を処理するため、PLC/UARTの応答が遅れます。UART受信割込みは動きますが、1,024バイトを超えて未読データを送ればあふれます。本機能は大容量のシリアル収録ではありません。

## 3. PLC配線表

「PLC IN/OUT」はPLC側から見た名称です。STM32入力＝PLC出力、STM32出力＝PLC入力です。rev58 J8と母基板CN1Aは同じピン番号です。PLC側の実際のデバイスアドレスは資料にないため、試験前に下表のbit0〜15とPLCモニタの表示アドレスを対応付けて記録してください。

| bit | PLC→STM32：母J3ピン／信号 | J8・CN1A | STM32入力 | STM32→PLC：母J4ピン／信号 | J8・CN1A | STM32出力 |
|---:|---|---:|---|---|---:|---|
| 0 | 1／PDI0 | 49 | PD6（GPIO16） | 1／PDO0 | 17 | PE3（GPIO0） |
| 1 | 2／PDI1 | 51 | PD3（GPIO17） | 2／PDO1 | 19 | PG12（GPIO1） |
| 2 | 3／PDI2 | 53 | PF10（GPIO18） | 3／PDO2 | 21 | PK2（GPIO2） |
| 3 | 4／PDI3 | 55 | PC11（GPIO19） | 4／PDO3 | 23 | PA3（GPIO3） |
| 4 | 5／PDI4 | 57 | PB7（GPIO20） | 5／PDO4 | 25 | PF8（GPIO4） |
| 5 | 6／PDI5 | 59 | PC9（GPIO21） | 6／PDO5 | 27 | PH2（GPIO5） |
| 6 | 7／PDI6 | 61 | PH10（GPIO22） | 7／PDO6 | 29 | PI12（GPIO6） |
| 7 | 8／PDI7 | 63 | PG3（GPIO23） | 8／PDO7 | 31 | PH14（GPIO7） |
| 8 | 9／PDI8 | 65 | PH12（GPIO24） | 9／PDO8 | 33 | PH15（GPIO8） |
| 9 | 10／PDI9 | 67 | PG2（GPIO25） | 10／PDO9 | 35 | PI4（GPIO9） |
| 10 | 11／PDI10 | 69 | PH9（GPIO26） | 11／PDO10 | 37 | PK5（GPIO10） |
| 11 | 12／PDI11 | 71 | PI3（GPIO27） | 12／PDO11 | 39 | PG9（GPIO11） |
| 12 | 13／PDI12 | 73 | PI2（GPIO28） | 13／PDO12 | 41 | PB9（GPIO12） |
| 13 | 14／PDI13 | 75 | PJ7（GPIO29） | 14／PDO13 | 43 | PD5（GPIO13） |
| 14 | 15／PDI14 | 77 | PI1（GPIO30） | 15／PDO14 | 45 | PD7（GPIO14） |
| 15 | 16／PDI15 | 79 | PI0（GPIO31） | 16／PDO15 | 47 | PD4（GPIO15） |

PLC DO → CB25 → 母J3 → フォトカプラ・反転バッファ → PDI → STM32。
STM32 → PDO → 非反転バッファ・フォトカプラ → 母J4 → CB26 → PLC DI。

母回路から、PLC出力で入力フォトカプラをONにするとPDIはHigh、PDOをHighにすると出力フォトカプラがONになります。スクリプトは**STM32端子の生のHigh/Low**を扱い、PLC側の論理反転やデバイス名は推測しません。全OFF時に0000でない場合は、PLCの表示極性、配線、電源・コモンを確認してから再開します。PLCの24 V配線は母基板の指定端子へ接続し、STM32のGPIOへ直接接続しないでください。

根拠：[Connector_usage_rev58.pdf p.1](hardware/circuit/Connector_usage_rev58.pdf)、[母基板回路図 p.1/6・6/6](hardware/circuit/フィードバック基板_TEP521C回路図.pdf)、[rev58ピン表](hardware/rev58/rev58_pin_tables.md)。

## 4. 32点を順番に試験する

```sh
python3 board_io_client.py plc-test --report board01_plc_mapping.json
```

最初にSTM32出力を全Lowにします。その後、以下を案内します。プロンプトは英語ですが、bit番号、信号名、STM32ピンを表示します。操作待ち中はTCPを開きっぱなしにしないため、作業に時間がかかっても構いません。

**前半16回：PLCから出力し、STM32で読む**

1. PLCの対象16出力をすべてOFFにし、Enter。STM32入力が0000であることを確認。
2. 指定されたbitだけをPLC側でONにし、出力が安定してからEnter。
3. STM32の16入力全体を読み、`1 << bit` と一致するか検査。
4. PLC出力をすべてOFFへ戻してEnter。STM32入力が0000に戻るか検査。
5. bit0〜15を繰り返す。これで入力16点。

**後半16回：STM32から出力し、PLCで読む**

1. STM32全Low時のPLC入力16点を読み、実際の16進マスクを入力（通常 `0000`）。
2. スクリプトが指定bitだけをHighにする。PLCの16入力を読み、**実際の値**を入力。期待値をそのまま転記しない。
3. スクリプトが全Lowに戻す。PLC入力を再度読み、実際のマスクを入力。
4. bit0〜15を繰り返す。これで出力16点。

例：bit0だけON＝`0001`、bit3だけON＝`0008`、bit15だけON＝`8000`。意図せずbit0とbit1がONなら `0003` と入力します。全16ビットの一致を要求するため、入替・複数点同時反応もFAILになります。1点の試験はONとOFFの確認を含み、合計32点です。

JSONの `complete: true`、`passed: true`、`outputs_cleared: true` と、`tests` が32件あることを確認します。FAILでも残りの点を試験でき、結果を保存します。Ctrl+C、出力マスク入力時の `q` で中断した場合も途中結果を保存し、全出力Lowへの復帰を試みます。`outputs_cleared: false` の場合は出力復帰が未確認です。

分割実施も可能です。

```sh
python3 board_io_client.py plc-test --direction in --report board01_input.json
python3 board_io_client.py plc-test --direction out --report board01_output.json
```

個別の確認用：

```sh
python3 board_io_client.py plc-read
python3 board_io_client.py plc-write 0x0001
python3 board_io_client.py plc-write 0x8000
python3 board_io_client.py plc-write 0
```

`output_latch` は指定値、`output_raw` はSTM32端子の読戻しです。どちらもPLC側の観測の代わりにはなりません。複数GPIOポートへ順番に書くため、16ビット同時更新のタイミング試験には使えません。

## 5. RS232C配線・端末設定

使用するのは**マザーボードJ5のRS232C端子**です。rev58 J8へRS232C電圧を直接接続しません。

| 方向 | 母J5 | 母基板内 | CN1A／rev58 J8 | STM32 |
|---|---|---|---|---|
| 外部端末→STM32 | pin2（基板受信） | ADM3202 R1IN→R1OUT、RS232C2 | pin70 | **PJ8：UART8 RX（入替後）** |
| STM32→外部端末 | pin3（基板送信） | RS232C3、ADM3202 T1IN→T1OUT | pin72 | **PJ9：UART8 TX（入替後）** |
| 信号GND | pin5 | DGND | — | — |

通常のUART8ではPJ8=TX、PJ9=RXですが、この母基板との組合せでは逆方向なので `tx-rx-swap` を有効にしています。rev58の信号ラベル `T2_UART/R2_UART` だけを見て通常割当へ戻さないでください。

PCのUSB–RS232C変換器等を使用します。相手が一般的なDTE配列（pin3送信・pin2受信）なら、**相手pin3→母J5 pin2、母J5 pin3→相手pin2、pin5同士**のクロス接続になります。DTE/DCE・ケーブル内部結線を確認し、オス／メス形状だけで判断しないでください。TTL UART変換器をJ5へつなぐ試験ではありません。

端末は115200/8N1、ハード・ソフトフロー制御なし、ローカルエコーなし。RTS/CTSは使用せず、母基板のフロー制御経路はrev58 J8の66/68短絡による折返しです。UARTは試験データ専用で、起動ログ・Zephyrコンソール・コマンドシェルを出力しません。外部端末から文字を入力しても、自動エコーはしません。

## 6. RS232Cの受信・送信試験

**外部端末→STM32（受信）**

Ethernet側PCで次を実行した後、30秒以内に外部RS232C端末からASCII `RX_TEST` にCR LFを付けて**1回だけ**送信します。可能なら端末のHEX送信で下記9バイトを送ってください。

```sh
python3 board_io_client.py uart-receive --seconds 30 --hex "52 58 5f 54 45 53 54 0d 0a" > board01_uart_rx.json
```

このコマンドは最初にソフトウェア受信バッファと統計をクリアしてから待ちます。表示 `RX buffer cleared` の後に送信します。自動改行設定でCRだけ、LFだけ、CR LF二重送信にならないよう注意してください。

`received_hex` が `52585f544553540d0a`、`passed: true`、`rx_dropped: 0`、`error_flags: 0` なら合格です。エラーフラグはZephyr UARTのエラービット（overrun/parity/framing等）の累積です。別の文字・余計な改行・重複送信・欠落があればFAILになります。

**STM32→外部端末（送信）**

外部RS232C端末の受信表示を開いてから、Ethernet側PCで実行します。

```sh
python3 board_io_client.py uart-send --hex "54 58 5f 54 45 53 54 0d 0a"
```

外部端末で `TX_TEST` ＋ CR LFが1回届くことを確認し、端末ログを保存します。Ethernetの成功応答や `tx_total` だけでは外部端末への到達を証明しません。バイナリ対応端末なら `00 55 aa ff` でも同様に両方向を試験すると、文字表示だけでは分からないbit不良も確認できます。

手動調査用：

```sh
python3 board_io_client.py uart-clear
# この後に外部端末から短いデータを送る
python3 board_io_client.py uart-status
python3 board_io_client.py uart-read
python3 board_io_client.py uart-send --text "STM32_UART_OK"
```

`uart-read` は読んだデータをバッファから取り除きます。`buffered` が残っていたら再実行します。`uart-clear` はソフトウェア内の未読データと統計を捨てるため、送信中には実行しません。`--text` はASCIIで、改行を勝手に付けません。送信は最大32バイト、受信比較の期待データも最大32バイトです。

## 7. 終了と記録

PLC側の対象出力をすべてOFFに戻し、PLCの強制出力設定も解除します。STM32側は下記で全Lowと読戻しを確認し、PLC側でも全OFFを確認します。

```sh
python3 board_io_client.py plc-write 0
python3 board_io_client.py plc-read
python3 board_io_client.py uart-status
```

基板番号、ソフトのGitコミット、PLC機種・アドレス対応、ケーブル、32点のJSON、UART受信JSON、外部端末の送信受信ログを保存してください。ホスト試験の成功を、この実機配線試験の成功として記録しないでください。
