# JPARC rev58 FPGAファームウェア仕様 v1.3

更新日：2026-10-25。現行RTLと生成済み書き込みファイルに基づく統合仕様。
本書だけでSPI4の設定、ADC／ダミーのSPI1受信、DACのDC出力を確認できる。

## 1. 対象と実装範囲

| 項目 | 内容 |
|---|---|
| 基板 | JPARC rev58 |
| FPGA | MAX10 `10M16DCF484I7G` |
| プロジェクト | `quartus/projects/spi4_adc_dac` |
| トップ | `jparc_adc_dac_top` |
| DEVICE_ID | `0x4A503538`（ASCII JP58） |
| PROTOCOL_VERSION | `0x00010003`（major=1、minor=3） |
| CAPABILITIES | `0x0000000F` |
| ツール | Quartus Lite 25.1std.0 Build 1129 |
| 書き込み状態 | 本セッションでv1.3のPOFを内蔵フラッシュへ書き込み・検証成功、0エラー・0警告 |

実装機能は以下のとおり。

- SPI4：STM32からFPGAの設定・状態レジスタへアクセス。
- ADC：非同期のSCLK／DRDY／DATAを持つ3系統から各24 bitを受信。
- SPI1：ADCの受信値、または内蔵ダミー値をSTM32へ連続送信。
- DAC：AD5764×4個の合計16チャンネルへ16 bitのDCコードを設定。
- 汎用DUMMY：通信確認用32 bitレジスタ16個。

ADC4〜6、フィードバック演算、周期波形生成、DDR2、PLC、DutyOut、SPI2／3、READY／FAULT／IRQによる通知、DACのコマンド読戻しは未実装。既存のZephyrソース／開発コンテナは変更していない。

## 2. 全体構成と動作の区別

```text
STM32 ── SPI4（STM32 master）──→ FPGAレジスタ ──→ DAC 16CH
                                     │
ADC 3系統 ── 24bit受信 ──────────────┤ ソース選択
内蔵ダミー3値 ── 各78.125kHz ────────┘
                                     ↓
                           系統別DCFIFO ×3
                                     ↓
                       SPI1（FPGA master）──→ STM32 DMA受信
```

| 経路 | 動作 |
|---|---|
| DAC | SPI4のDAC設定レジスタへWRITEするたびに対象ICへ一発送信。その後DACがアナログ値を保持する。周期再送なし |
| ADC／ダミー → SPI1 | 送信許可中、データがある限り連続送信。実ADCは到着レートに従い、ダミーは各78.125 kHz |
| 汎用DUMMY | 値を保持してREADで返すだけ。DAC／SPI1には出力しない |

基板発振器50 MHzからPLLで120 MHzと80 MHzを生成する。SPI4、ADC取得、DAC制御、ダミー生成は120 MHz領域、SPI1送信器は80 MHz領域。SPI1は80 MHz÷4、DAC SCLKは120 MHz÷6で、どちらも20 MHz。

## 3. ピン接続

すべてFPGA球番号。I/Oは3.3-V LVTTL、実装した出力の駆動強度は4 mA。未使用ユーザーピンは入力トライステート。

### 3.1 STM32とFPGA

| 経路 | 信号 | STM32 | FPGA | 方向 |
|---|---|---|---|---|
| SPI4 | SCLK | PE2 AF5 | L14 | STM32 → FPGA |
| SPI4 | NSS | PE4 GPIO、Low有効 | A12 | STM32 → FPGA |
| SPI4 | MOSI | PE6 AF5 | E20 | STM32 → FPGA |
| SPI4 | MISO | PE5 AF5 | J21 | FPGA → STM32 |
| SPI1 | SCLK | **PG11 AF5** | C21 | FPGA → STM32 |
| SPI1 | NSS | PA4 AF5、Low有効 | C20 | FPGA → STM32 |
| SPI1 | DATA（配線名MOSI） | PB5 AF5 | C22 | FPGA → STM32 |
| 発振器 | CLK50 | ― | N5 | 基板50 MHz → FPGA |

SPI1のSCLKネット名にPA5表記が残っているが、実接続は**PG11**。SPI1にMISOはなく、STM32をスレーブ受信にする。

### 3.2 ADC入力

J10はrev58新基板のコネクタの電気接点番号であり、母基板外部の同名J10ではない。

| 系統 | DRDY：J10 / FPGA | DATA：J10 / FPGA | SCLK：J10 / FPGA |
|---|---|---|---|
| ADC1 | 18 / E19 | 20 / F18 | 22 / E21 |
| ADC2 | 26 / G17 | 28 / F21 | 30 / F22 |
| ADC3 | 34 / H22 | 36 / AB10 | 38 / J22 |

9本すべて入力。FPGAからADCのクロックは駆動しない。

### 3.3 DAC出力

| DAC IC | SCLK：J10 / FPGA | SYNC_N：J10 / FPGA | SDIN：J10 / FPGA | SDO：J10 / FPGA |
|---|---|---|---|---|
| DAC1 / IC11 | 17 / E22 | 19 / F20 | 21 / G19 | 23 / G20 |
| DAC2 / IC13 | 27 / P13 | 29 / H21 | 31 / R13 | 33 / L18 |
| DAC3 / IC15 | 37 / M18 | 39 / M20 | 41 / M21 | 43 / M22 |
| DAC4 / IC17 | 47 / N20 | 49 / N21 | 51 / P20 | 53 / V14 |

SCLK／SYNC_N／SDINは出力、**SDO4本は入力**。SDOからのレジスタ読戻し機能はない。

## 4. SPI4のレジスタ通信

### 4.1 設定・タイミング

STM32がマスター、FPGAがスレーブ。Mode 0（CPOL=0、CPHA=0）、MSB first、8 bit、全二重。**1 MHz推奨、最大2 MHz**。SPI1／DAC側の20 MHzと混同しない。

- SCLK High／Low：各250 ns以上。
- MOSI：SCLK立ち上がりの前後各100 ns以上安定。
- NSS選択→最初の立ち上がり、最終立ち上がり→NSS解除、フレーム間NSS High：各100 ns以上、**各1 µs推奨**。
- NSSを8バイト全体でLowに保持し、バイトごとのNSSパルスは出さない。フレーム間はSCLK Low。
- NSS HighでMISOはHi-Z。PE4はGPIO CSとして使用する。

### 4.2 8バイトフレーム

アドレスは16 bitの**バイトアドレス**で、4バイト境界。データは常に32 bit枠。アドレス・データともビッグエンディアン。

| byte | MOSI READ | MOSI WRITE | MISO |
|---|---|---|---|
| 0 | `0x03` | `0x02` | 0 |
| 1 | address[15:8] | address[15:8] | 0 |
| 2 | address[7:0] | address[7:0] | 0 |
| 3 | 0（無視） | 0（無視） | status |
| 4 | 0 | data[31:24] | READ data[31:24]、WRITEは0 |
| 5 | 0 | data[23:16] | READ data[23:16]、WRITEは0 |
| 6 | 0 | data[15:8] | READ data[15:8]、WRITEは0 |
| 7 | 0 | data[7:0] | READ data[7:0]、WRITEは0 |

WRITEは64回目の立ち上がりを内部で検出した時点で一括反映する。途中NSS解除は破棄し、WRITE_COUNTも増やさない。余分な65クロック目以降はNSS解除まで無視する。バースト、自動アドレス加算、SPI4のCRCはない。

READはアドレス受信時に値を取得する。ADC診断値は読み出しパイプラインにより最大1内部周期（約8.3 ns）前の値を含む。複数レジスタの一括スナップショット機能はない。

**WRITEのstatus=0はヘッダーの受理結果**であり、後続データ受信やDAC送信の完了ACKではない。WRITE後のREADで設定を確認し、DACは送信済値も確認する。

### 4.3 status

| マスク | 意味 |
|---|---|
| `0x00` | 正常 |
| `0x01` | 不正opcode |
| `0x02` | 非整列アドレス |
| `0x04` | 未割り当てアドレス |
| `0x08` | 読み取り専用レジスタへのWRITE |

複数条件はOR。異常時のMISO byte 4〜7は0、レジスタ変更・出力要求なし。例：READ `0x0101`は0x02、READ `0x0001`は0x06。不正opcodeの場合、RO WRITE判定はしない。

## 5. SPI4全レジスタマップ

RW16／RW24も8バイトフレームの32 bitデータ枠で転送する。未使用上位bitへのWRITEは無視し、READは0。ROは読み取り専用。

| address | 名前 | 属性 | 初期値／説明 |
|---|---|---|---|
| `0x0000` | DEVICE_ID | RO | `0x4A503538` |
| `0x0004` | PROTOCOL_VERSION | RO | `0x00010003` |
| `0x0008` | CAPABILITIES | RO | `0x0000000F`：bit0汎用DUMMY、bit1 DAC、bit2 ADC→SPI1、bit3ダミー送信 |
| `0x000C` | DUMMY_COUNT | RO | 16 |
| `0x0010` | WRITE_COUNT | RO | 初期0、すべての正常なRWレジスタWRITE完了ごとに+1。同値の再WRITEも数える |
| `0x0014` | DAC_STATUS | RO | 初期0、詳細後述 |
| `0x0018` | DAC_TRANSFER_COUNT | RO | 初期0、DAC IC単位の4フレーム送信完了回数の合計 |
| `0x001C` | DAC_CHANNEL_COUNT | RO | 16 |
| `0x0020` | DAC_SDO_LEVEL | RO | bit0〜3にSDO1〜4の同期後の論理値。DACの値の読戻しではない |
| `0x0100 + 4×i`、i=0〜15 | DUMMY[i] | RW32 | 各0、通信確認専用。最終アドレス0x013C |
| `0x0200 + 4×i`、i=0〜15 | DAC_SET[i] | RW16 | 各0、WRITEで対象ICに送信要求。最終0x023C |
| `0x0300 + 4×i`、i=0〜15 | DAC_SENT[i] | RO16 | 各0、最後に送信完了したコード。最終0x033C |
| `0x0400` | ADC_CONTROL | RW | bit0送信許可（初期0）、bit1 clearパルス（READは0）。その他bit無視 |
| `0x0404` | ADC_STATUS | RO | FIFO FULL／EMPTY、SPI1 BUSY。詳細後述 |
| `0x0408` | ADC_TX_COUNT | RO | 初期0、SPI1レコード送信完了回数の合計、Gray同期後 |
| `0x040C` | ADC_CHANNEL_COUNT | RO | 3 |
| `0x0410 / 0x0414 / 0x0418` | ADC_RX_COUNT[0..2] | RO | 各0、完成した実ADCフレーム／生成したダミーの数。FIFO破棄分も含む |
| `0x0420 / 0x0424 / 0x0428` | ADC_DROP_COUNT[0..2] | RO | 各0、FIFO満杯で破棄したワード数 |
| `0x0430 / 0x0434 / 0x0438` | ADC_RESTART_COUNT[0..2] | RO | 各0、実ADCの未完了フレーム中の次DRDYで再開した回数 |
| `0x0440 / 0x0444 / 0x0448` | ADC_LAST[0..2] | RO24 | 各0、最後の実ADC完成値／ダミー生成値。FIFO破棄時も更新 |
| `0x0450 / 0x0454 / 0x0458` | ADC_FIFO_LEVEL[0..2] | RO | 各0、格納数0〜256。書込側からの見積もりで同期遅延あり |
| `0x0480` | ADC_SOURCE_MODE | RW | 初期0、bit0のみ保持。0＝実ADC、1＝ダミー |
| `0x0484` | ADC_DUMMY1 | RW24 | 0、ADC1相当のダミー生値 |
| `0x0488` | ADC_DUMMY2 | RW24 | 0、ADC2相当のダミー生値 |
| `0x048C` | ADC_DUMMY3 | RW24 | 0、ADC3相当のダミー生値 |

上記以外は未割り当て。32 bitカウンタはすべて循環する。SDO_LEVELは外部信号によるため初期論理値を固定しない。ADC_STATUSのEMPTYはFIFO初期化後に1、FULLとBUSYは0となる。

### 5.1 状態bit

| レジスタ | bit | 意味 |
|---|---|---|
| DAC_STATUS | 0〜3 | DAC1〜4のBUSY |
| DAC_STATUS | 4〜7 | DAC1〜4のPENDING（再送要求あり） |
| DAC_STATUS | 8〜11 | DAC1〜4のINITIALIZED（一度以上4フレーム送信完了） |
| ADC_STATUS | 0〜2 | ADC1〜3のFIFO FULL |
| ADC_STATUS | 8〜10 | ADC1〜3のFIFO EMPTY（書込側の同期済み状態） |
| ADC_STATUS | 16 | SPI1 BUSY（CRC準備〜送信終了、同期後）。IDLE／フレーム間GAPは0 |

その他bitは0。各状態には同期遅延がある。

## 6. DAC 16CHのDC設定

### 6.1 チャンネル割当

| CH | 配列index | DAC IC / 出力 | DAC_SET | DAC_SENT | DAC command |
|---|---|---|---|---|---|
| 1 | 0 | DAC1 IC11 / A | 0x0200 | 0x0300 | 0x10 |
| 2 | 1 | DAC1 IC11 / B | 0x0204 | 0x0304 | 0x11 |
| 3 | 2 | DAC1 IC11 / C | 0x0208 | 0x0308 | 0x12 |
| 4 | 3 | DAC1 IC11 / D | 0x020C | 0x030C | 0x13 |
| 5 | 4 | DAC2 IC13 / A | 0x0210 | 0x0310 | 0x10 |
| 6 | 5 | DAC2 IC13 / B | 0x0214 | 0x0314 | 0x11 |
| 7 | 6 | DAC2 IC13 / C | 0x0218 | 0x0318 | 0x12 |
| 8 | 7 | DAC2 IC13 / D | 0x021C | 0x031C | 0x13 |
| 9 | 8 | DAC3 IC15 / A | 0x0220 | 0x0320 | 0x10 |
| 10 | 9 | DAC3 IC15 / B | 0x0224 | 0x0324 | 0x11 |
| 11 | 10 | DAC3 IC15 / C | 0x0228 | 0x0328 | 0x12 |
| 12 | 11 | DAC3 IC15 / D | 0x022C | 0x032C | 0x13 |
| 13 | 12 | DAC4 IC17 / A | 0x0230 | 0x0330 | 0x10 |
| 14 | 13 | DAC4 IC17 / B | 0x0234 | 0x0334 | 0x11 |
| 15 | 14 | DAC4 IC17 / C | 0x0238 | 0x0338 | 0x12 |
| 16 | 15 | DAC4 IC17 / D | 0x023C | 0x033C | 0x13 |

### 6.2 送信動作

DAC_SETへの正常WRITEで下位16 bitを保持し、対象ICのA〜D要求値を一括取得する。A→B→C→Dの4フレームを送り、他のICには送信しない。同値のWRITEでも送信する。

- DAC SCLK＝20 MHz、High／Low各25 ns、待機時High。
- 1フレーム＝8 bit command＋16 bit code、MSB first。**DACは立ち下がり取得**。
- SYNC_Nは各24 bitフレームでLowにし、フレーム間に100 ns以上High。
- SYNC選択→最初の立ち下がり25 ns、最終立ち下がり→SYNC解除75 ns。
- 1 ICの4フレーム送信は約5.4 µs。
- 完了時、4個のDAC_SENTをまとめて更新し、INITIALIZEDを立て、DAC_TRANSFER_COUNTを1増やす。
- 送信中の追加WRITEはPENDINGで保持し、完了後に最新4値を再送する。中間値をすべて保存するFIFOではない。

**初回WRITEでは、同じICの未設定3チャンネルにも初期コード0を送る。** 16CHの同時更新機能ではない。LDACが基板上Lowに固定される構成を前提とし、各CHは各SYNC解除時に順次更新される。

設定完了の確認は、対象ICのBUSY／PENDING解除とINITIALIZEDを確認し、DAC_SENTと要求値を比較する。DAC_SENTはFPGAの送信履歴であり、SDOによるDAC受信ACKではない。

値は**16 bitの生コード**。0x0000＝0 Vとは限らない。BIN/2sCOMP、基準電圧、ゲイン／オフセット等の外部条件で出力電圧が決まる。現行RTLはデータレジスタだけを書き、アナログ校正や機能レジスタ初期化は行わない。

## 7. 実ADCの受信

ADC_SOURCE_MODE=0で選択する。SCLKはFPGAと非同期、通常SPIのCS形式ではなく**DRDY立ち上がりが開始**。

1. DRDY立ち上がりで新しいフレームを開始する。
2. SCLK立ち下がり24回でDATAをMSB firstに取得する。
3. 完成値を系統別FIFOに入れる。24 bitの符号・スケールは変換しない。
4. 余分なクロックは次のDRDYまで無視する。途中DRDY再開は不完全値を破棄しRESTART_COUNTを増やす。

入力端子で要求するタイミング：SCLK最大20 MHz、High／Low各25 ns以上、DATA setup／holdは立ち下がり前後各16.7 ns以上、DRDY High／Low各25 ns以上、DRDY立ち上がり→最初のデータ立ち下がり50 ns以上。実ADCの通常フレーム周期は各12.8 µs（78.125 kHz）を想定する。

9本を120 MHzで各3段同期し、エッジを検出する。入力SCLKを内部クロックとして使わない。完成ワードはDCFIFOの書込120 MHz→読出80 MHzで送信側へ渡す。SCLK停止中のDRDYも検出する。

## 8. ダミーソース

ADC_SOURCE_MODE=1で選択する。ADC_DUMMY1〜3の下位24 bitを、120 MHz÷1536＝**各78.125 kHz**で同時生成する。実ADCピンは無視し、ADC未接続でも動く。

実ADCと同じFIFO、SPI1形式、通番、CRCを使う。レコード内にダミーを示すフラグはなく、A1／A2／A3の識別子も同じ。RX_COUNT／DROP_COUNT／LASTは選択中ソースの診断として動き、ダミー時のRESTART_COUNTは0。

- 送信許可はADC_CONTROL.bit0で別途指定する。モード1だけでは送信しない。
- モードの**変更時だけ**ADC経路・FIFO・通番・診断カウンタを自動clearする。送信許可とダミー設定値は保持する。
- 同じモード値の再WRITEはclearしない。
- ダミー値のWRITEは次の生成タイミング以降に反映し、FIFO内の既存データは変更しない。
- 3レジスタを順次WRITEする間にも生成されるので、3値をまとめて変更したい場合は停止・設定・clear付き再開とする。

## 9. SPI1の連続送信形式

FPGAがマスター、STM32がスレーブ受信。**Mode 0、20 MHz、MSB first、8 bit**。NSS Lowの1区間に8バイト＝64クロックを送る。

| byte | 内容 |
|---|---|
| 0 | 0xA1＝ADC1相当、0xA2＝ADC2相当、0xA3＝ADC3相当 |
| 1 | 系統別通番[15:8] |
| 2 | 系統別通番[7:0] |
| 3 | 生値[23:16] |
| 4 | 生値[15:8] |
| 5 | 生値[7:0] |
| 6 | CRC16[15:8] |
| 7 | CRC16[7:0] |

通番は系統別に0開始、完成／生成ごとに+1、65535→0で循環する。FIFOで破棄した分も進む。不完全な実ADCフレームは進まず、RESTART_COUNTで確認する。3系統の同時取得や絶対時刻を表す番号ではない。

CRCは**CRC-16/CCITT-FALSE**。対象byte 0〜5、poly=0x1021、init=0xFFFF、refin=false、refout=false、xorout=0。CRCもビッグエンディアン。

```text
ADC1相当 / seq=0 / raw=0x123456 : A1 00 00 12 34 56 74 F9
ADC2相当 / seq=0 / raw=0x89ABCD : A2 00 00 89 AB CD 08 D6
ADC3相当 / seq=0 / raw=0xFFFFFF : A3 00 00 FF FF FF F3 B4
```

SCLK待機Low、NSS待機High、DATA待機Low。DATAは通常立ち下がりで更新し、STM32は立ち上がりで取得する。NSS選択→最初の立ち上がり125 ns、最後の立ち上がり→NSS解除125 ns。連続データ時のNSS Highは約287.5 ns、パケット開始間隔は約3.6875 µs。

送信はround-robinで空のFIFOを飛ばす。A1→A2→A3固定順と仮定しない。識別子と系統別通番で分類する。

## 10. バッファ・帯域・STM32受信

各FIFOは40 bit×256ワード（16 bit通番＋24 bit値）、計3個。各78.125 kHzなら1系統あたり約3.2768 ms分。

- 生データは5.625 Mbit/s、8バイトレコードとしては**15 Mbit/s＝1.875 MB/s、毎秒234,375件**。
- 固定待ちを含む送信能力は約271,186件/sで、通常レートは約86.4%使用。
- FULL時は**新着を破棄**し、古いデータの順序を保つ。DROP_COUNTと通番欠落で検出する。
- 送信禁止中も受信／生成・FIFO蓄積は続く。無期限に停止すると欠落する。
- STM32からのready／ACKはない。FPGAはSTM32側のSPI overrunやDMAエラーを検出できない。

STM32はSPI1をハードウェアNSS入力、スレーブ受信、Mode0／8 bitで設定する。SPI1のPA4にはNSS AF5を使い、SPI4のGPIO CSと混同しない。

**循環DMA／ダブルバッファ等でNSS境界をまたいで連続受信し、複数レコードをまとめて処理する。** レコードごとのAPI再設定を約287.5 nsの隙間で行うことは想定しない。Zephyrの有限長SPI転送だけで連続受信を保証するものではない。STM32H7ではDMAアクセス可能RAMとD-cache整合も確認する。

受信側はCRC、識別子、系統別通番、SPI／DMAエラーを監視する。通番差は16 bit循環演算で求め、通常1、0は重複、2以上は欠落候補。clear／モード変更後は前回値を無効にする。受信境界を失ったら識別子とCRCが一致する8バイト候補を探し、通番も確認する。

## 11. 推奨操作手順・バイト列

### 11.1 起動確認

FPGAコンフィグレーション完了後10 ms待ち、SPI4でID／version／capabilitiesを確認する。IDだけでは旧版と区別できない。

```text
DEVICE_ID READ:
TX 03 00 00 00 00 00 00 00
RX 00 00 00 00 4A 50 35 38

VERSION READ:
TX 03 00 04 00 00 00 00 00
RX 00 00 00 00 00 01 00 03
```

### 11.2 ADC未接続のダミーテスト

まずADC_CONTROL=0で停止し、ADC_STATUS.bit16のBUSY解除を確認する。次に設定する。

```text
ADC_DUMMY1=0x123456 : 02 04 84 00 00 12 34 56
ADC_DUMMY2=0x89ABCD : 02 04 88 00 00 89 AB CD
ADC_DUMMY3=0xFEDCBA : 02 04 8C 00 00 FE DC BA
ADC_SOURCE_MODE=1  : 02 04 80 00 00 00 00 01
```

STM32のSPI1 DMAを準備してから、次のWRITEでclearと送信許可を同時に行う。

```text
ADC_CONTROL=3     : 02 04 00 00 00 00 00 03
```

3値、系統別の通番連続性、CRC、約78.125 kHz／系統の更新を確認する。固定ダミー値でも通番が変わるのでCRCは毎回変わる。

### 11.3 実ADCへ切替

1. ADC_CONTROL=0で停止し、BUSY解除を確認する。
2. ADC_SOURCE_MODE=0を書き、古いFIFOと通番を初期化する。
3. STM32の受信状態を初期化／DMA準備し、ADC_CONTROL=3で開始する。
4. RX_COUNT、DROP_COUNT、RESTART_COUNTとCRC／通番を確認する。

### 11.4 DACのCH1を0x1234へ設定

```text
WRITE DAC_SET[0]:
TX 02 02 00 00 00 00 12 34
RX 00 00 00 00 00 00 00 00

READ DAC_SET[0]:
TX 03 02 00 00 00 00 00 00
RX 00 00 00 00 00 00 12 34

READ DAC_SENT[0]（送信完了後）:
TX 03 03 00 00 00 00 00 00
RX 00 00 00 00 00 00 12 34
```

このWRITEでDAC1のA〜Dが再送される。未設定のB〜Dにもコード0が送られる。DAC_STATUSの対象ICのBUSY／PENDING解除とINITIALIZEDも確認する。

## 12. 停止・clear・電源再投入

| 操作 | ADC／SPI1への影響 | DAC／汎用DUMMYへの影響 |
|---|---|---|
| SPI4のNSS High | SPI4パーサのみ初期化 | 設定値を保持 |
| ADC_CONTROL=0 | 現在のSPI1レコードは完送し、新規送信停止。受信／生成は継続 | 影響なし |
| ADC_CONTROL=2 | 送信禁止＋ADCパーサ／FIFO／通番／診断カウンタ／TX数をclear | 影響なし |
| ADC_CONTROL=3 | 同じclear後、送信許可 | 影響なし |
| ADC_SOURCE_MODEの値変更 | ADC経路を自動clear。送信許可とダミー3値は保持 | 影響なし |
| STM32だけ再起動 | FPGAはそのまま動作継続 | FPGA設定値を保持 |
| FPGA再設定／PLLロック喪失 | 全内部状態を初期化、実ADCモード0、ダミー値0、送信禁止 | 要求値・送信履歴・DUMMY・カウンタを0へ。DACへ自動送信はしない |
| 基板電源再投入 | フラッシュから回路をロードし、内部状態を初期化 | STM32からDAC値等を再設定する |

clear／モード変更は現在のSPI1レコードを中断し得る。通常は送信停止→BUSY解除→変更の順とする。途中clearを行った場合は受信側で最後の短いレコードを破棄する。

POFで**回路は電源断後も残るが、通信で設定したDAC値・ダミー値・送信許可は保存しない**。全内部状態の初期化は外部DACのアナログ出力を直接リセットする操作ではない。外部DACの状態はその電源／制御ピンに依存するため、必要なコードを明示的に再送する。

## 13. 書き込みファイル・検証状態

現行ファイルは`output_files/spi4_adc_dac.sof`（SRAM）と`output_files/spi4_adc_dac.pof`（内蔵フラッシュ）。本セッションで以下のPOFの書き込み・検証成功を確認した。

```text
POF SHA-256:
4ca165e56663aa9a05f5071a914ff46301c3afedabf953dc62b4baed441eed7d

SOF SHA-256:
b725f3cf1fa165119b086da43c951653b69b25f60eb13905e2d6bef29f99e1f9

JTAG ID: 0x031030DD
Quartus Programmer POF checksum: 0x04677A1D
```

機能シミュレーション807,670項目通過。合成・配置配線・全3コーナーのタイミング／Gray CDC制約確認を実施し、タイミング違反なし。コンパイルの残存警告とCDC解析の詳細は [VERIFICATION.md](VERIFICATION.md) に記載。

フラッシュ書き込み成功は確認済みだが、STM32からのSPI4通信、SPI1の実機連続DMA受信、外部ADCの実タイミング、DACの出力電圧、電源再投入後の一連動作は未検証。

## 14. 関連資料

- [ビルド・書き込み・シミュレーション手順](README.md)
- [STM32向けADC／ダミー送信の実装メモ・Cデコーダ](ZEPHYR_ADC_STREAM_HANDOFF.md)
- [DAC設定とSPI4のDTS／Cアクセス例](../spi4_dac_dc/ZEPHYR_DAC_HANDOFF.md)（旧版作成時の資料。現行の版番号・全レジスタ・書き込み状態は本書を優先）
- `rtl/`：本仕様の根拠となる現行RTL。
