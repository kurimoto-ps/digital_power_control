# rev58 混在ADC／DACファームウェア仕様 v2.0

作成日：2026-11-07。対象：MAX10 `10M16DCF484I7G`、プロジェクト`spi4_mixed_adc_dac`。
**24 bit ADCは3系統をすべて維持し、ADS8568の16 bit×8CHを追加する。計11CHを10 kHzで集約する。**
旧v1.3プロジェクトは保持。本版は実機に書き込まない。SPI1形式は非互換なので、末尾のSTM32更新手順に従うこと。

## 1. 動作の要点

- 従来のOE側ADC1〜3は非同期20 MHz入力として、120 MHz・3段同期で各24 bitを受信する。
- ADS8568は同一PLLの40 MHz／20 MHz／20 MHz(+12.5 ns)で制御し、8CHを1本のシリアル入力から取得する。
- ADS8568の1回の受信完了を契機に、その8値と、従来3系統の**その時点の最新値**を一括でレコード化する。
- 初期の取得周期は100 µs（10 kHz）。24 bit ADCが各78.125 kHzで来ても全件転送せず、最新値へ間引く。通番・経過時間・更新フラグを付ける。
- 48バイトレコードをSPI1の20 MHz／Mode0でSTM32へ送る。FIFOでワード全体を渡し、データ途中で取り合わせが変わらないようにする。
- DAC CH1〜8へADS8568、CH9〜11へ24 bit ADCの上位16 bitを出せる。各CHをSPI4の手動値へ戻す選択マスクを持つ。

8CHのADS8568内部の同時変換と、11CH全体の同時サンプリングは別である。3系統の24 bit ADCは独立・非同期で、最新値の集約であり時刻をそろえて取得し直すものではない。

## 2. 配線と現場確認

### 2.1 新ADS8568の4信号

ユーザーの「未使用OE光ICを選んでよい」との指定に基づき、受信をADC4用だったDATA光レーンに割り当てる。**資料上のIC番号／接点対応であり、現物の光ファイバ番号・ICシルクは現場確認対象。**

| 信号 | FPGA方向 | 光IC（資料上） | rev58接点 | MAX10球 |
|---|---|---|---|---|
| SCLK | 出力 | EO TEP526A IC24、DOUT15 | J9-58 | R2 |
| FS | 出力、Low有効 | EO TEP526A IC25、DOUT16 | J9-60 | R1 |
| CONVSTART | 出力、Highパルス | EO TEP526A IC26、DOUT17 | J9-62 | AA15 |
| ADDATA16 | **1 bit入力** | **OE TEP523A IC11、CH4-2** | **J10-44** | **V13** |

「ADDATA16」は16 bitパラレルバスではない。8CH×16 bit＝128 bitを受ける1本の線。OEのIC26は資料では電源IC TPS62091RGTTなので使用しない。既存ADC3のDATA（OE IC8／AB10）は転用しない。

### 2.2 維持する既存配線

| 系統 | DRDY：J10 / 球 | DATA：J10 / 球 | SCLK：J10 / 球 |
|---|---|---|---|
| ADC24-1 | 18 / E19 | 20 / F18 | 22 / E21 |
| ADC24-2 | 26 / G17 | 28 / F21 | 30 / F22 |
| ADC24-3 | 34 / H22 | 36 / AB10 | 38 / J22 |

SPI4はSCLK PE2/L14、NSS PE4/A12（GPIO CS）、MOSI PE6/E20、MISO PE5/J21。STM32がマスター。
SPI1はSCLK **PG11/C21**、NSS PA4/C20、DATA PB5/C22。FPGAがマスター、STM32がスレーブ受信。全て3.3-V LVTTL。
DACの物理配線はv1.3と同じ。DAC SDO4本は引き続き入力とし、FPGAから固定値を駆動しない。

## 3. ADS8568制御と設定適用

添付`adc-single-max10.zip`の`ads8568_single.sv`を無改変で組み込む。制御40 MHz、連続SCLK 20 MHz、位相基準20 MHz(+12.5 ns)。BUSYはADCの端子ではなく制御FSMの状態で、外部ADC BUSYは入力しない。

| パラメータ | 初期シャドー値 | 意味 |
|---|---|---|
| DELAY D | 9 | 戻りデータの先頭ビットを取り込む位置。1カウント25 ns |
| CONVERSION C | 68 | 変換待ちの比較値。1カウント25 ns |
| PHASE | 3 | 出力SCLKの位相選択 |
| NEGATE | **0** | 0＝生16 bit、1＝各CHの16 bit算術負号。ビット反転ではない |
| PERIOD | 4000 | 40 MHz制御クロック単位の取得間隔、4000＝100 µs＝10 kHz |
| ENABLE | 0 | 初期停止。APPLYして実適用するまで取得しない |

初期値は校正開始候補であって、実機光リンクでの保証値ではない。特にDとPHASEを現場で調整する。

| PHASE | SCLK出力 |
|---|---|
| 0 | 20 MHz、0度 |
| 1 | 20 MHz反転、180度 |
| 2 | 20 MHz(+12.5 ns)、90度 |
| 3 | 20 MHz(+12.5 ns)反転、270度 |

CONVSTARTのHigh幅は50 ns。start受付からFS下降まで`(C+4)×25 ns`、FS下降から最初の取得まで`(D+3)×25 ns`。取得は50 nsごとに128 bit、完了はstartから`(C+D+262)×25 ns`。C=68/D=9なら8.475 µs。

チャネル順はA0、A1、B0、B1、C0、C1、D0、D1。raw128の上位から16 bitずつ。NEGATE=1でも16 bit幅のため、0x8000の負号は0x8000に循環し、飽和処理はしない。STM32へ送る16 bit値とDACへ回す16 bit値には、同じNEGATE設定を適用する。

### 3.1 シャドー／APPLY方式

1. SPI4でD、C、PHASE、NEGATE、PERIODをシャドーへ書く。
2. `ADS_CONTROL`へENABLEとAPPLYをまとめて書く。開始は3、停止は2。
3. FPGAは設定全体を保持し、120→40 MHzのreq/ackハンドシェイクでまとめて渡す。個々のbitを独立同期して取り合わせない。
4. 取得回路をresetし、FS Highで1 µs待って位相を変え、さらに1 µs待ってreset解除。開始位相を20 MHz立ち上がり側へそろえる。
5. 実適用完了後にAPPLIED_IDを更新する。STM32はPENDING解除、ERROR、APPLIED_IDと実適用値を確認する。

運転中にPHASEを直接変えない。SPI4への書き込みだけでは実適用せず、必ずAPPLYする。ENABLEだけを書いてもAPPLYしなければ実稼働状態は変わらない。

PERIODは偶数、2以上、かつ`C+D+270`以上を要求する。不正値やPENDING中の追加APPLYは拒否し、元の実適用設定を保つ。CONFIG_ERRORとCONFIG_REJECTEDを更新する。**SPI4の応答status=0はレジスタWRITE受理であり、設定の実適用成功ではない。**
この検査はFSMの整合性検査であり、任意のC/D/PHASEがADC実機の変換時間・光遅延に適合する保証ではない。

## 4. SPI1：48バイトの11CHレコード

**旧v1.3の8バイト形式とは互換性がない。** Mode0、MSB first、8 bit、20 MHz。NSS Lowの間に48バイト＝384クロックを送る。DATAは通常立ち下がりで更新、STM32は立ち上がりで取得する。
NSS選択から最初の立ち上がり125 ns、最後の立ち上がりからNSS解除125 ns。データ待ち中はNSS High、SCLK Low。

| byte | 内容 |
|---|---|
| 0〜1 | magic＝A5 5A |
| 2 | format＝02 |
| 3 | FLAGS |
| 4〜7 | フレーム通番32 bit、ビッグエンディアン |
| 8〜9 / 10〜11 / 12〜13 | ADC24-1/2/3の受信通番、各16 bit BE |
| 14〜15 / 16〜17 / 18〜19 | ADC24-1/2/3の受信後経過時間、各16 bit BE、µs |
| 20〜22 / 23〜25 / 26〜28 | ADC24-1/2/3、各24 bit BE |
| 29〜30 | ADS A0、16 bit BE |
| 31〜32 | ADS A1 |
| 33〜34 | ADS B0 |
| 35〜36 | ADS B1 |
| 37〜38 | ADS C0 |
| 39〜40 | ADS C1 |
| 41〜42 | ADS D0 |
| 43〜44 | ADS D1 |
| 45 | bit0＝24 bit ADCがダミーモード、bit1〜7＝0 |
| 46〜47 | CRC16、BE |

| FLAGS bit | 意味 |
|---|---|
| 0〜2 | ADC24-1〜3について一度以上受信済み（KNOWN） |
| 3 | ADS8568の128 bit取得窓が完了した。外部ADCのACKではない |
| 4〜6 | ADC24-1〜3が前回の**生成フレーム**以後に更新された（FRESH） |
| 7 | ADS8568の16 bit値にNEGATEを適用した |

ADC24が未受信なら値0、KNOWN=0、age=0xFFFF。入力が途絶えても全体は止めず、最後の値を保持してFRESH=0、ageを増加させる。ageは65535 µsで飽和する。3系統のADC24が一緒に来ることを前提にしない。

CRCはCRC-16/CCITT-FALSE、poly=0x1021、init=0xFFFF、refin/refout=false、xorout=0、対象byte 0〜45。CRCはFPGA→STM32の転送を検査し、光ADC入力の誤受信を検出するものではない。

通番32 bitはADS8568の取得完了ごとに進み、FIFO破棄分も進む。clearで0へ戻る。ADC24通番16 bitは各受信ごとに進む。10 kHz集約ではADC24通番の差が約7〜8になるのは通常で、差1を要求しない。各通番は幅に応じて循環する。

例（通番0x01020304、24 bit通番17/18/19、age 1/2/3 µs、NEGATEなし）：

```text
A5 5A 02 7F 01 02 03 04 00 11 00 12 00 13 00 01
00 02 00 03 12 34 56 FE DC BA 65 43 21 00 01 80
00 FF FF 12 34 56 78 9A BC DE F0 01 02 00 25 F5
```

### 4.1 バッファと遅延

- ADS受信完了データは289 bit×16のDCFIFO（40→120 MHz）で渡す。raw128と実使用128、NEGATE状態、通番を一括して扱う。
- 集約済みレコードは368 bit×32のDCFIFO（120→80 MHz）。CRCは送信器で付加する。
- FULL時は**新しいレコードを丸ごと破棄**して古い順序を保つ。通番ギャップとDROPカウンタで検出する。
- 10 kHz×48 byte＝**480 kB/s、3.84 Mbit/s**。連続キュー時の開始間隔は約20.1875 µs、20 MHzで十分な帯域がある。
- 通常のストリーム停止中もADS取得・集約は続く。32レコードは約3.2 ms分なので、長く停止すればDROPする。
- ageは**FPGAで集約した時点**の値。FIFO待ち時間・SPI1転送時間は加えていない。再開時に古いフレームを制御へ入れないよう、通常はclear付き開始を使う。
- STM32からのready/ACK線はない。継続DMA受信とSTM32側のoverrun監視が必要。

## 5. DAC：手動と11CHモニタの切替

| DAC CH | 自動ソース | 変換 |
|---|---|---|
| 1〜8 | ADS A0、A1、B0、B1、C0、C1、D0、D1 | 実使用16 bitをそのまま。NEGATE設定はSPI1と共通 |
| 9 | ADC24-1 | bits[23:8] |
| 10 | ADC24-2 | bits[23:8] |
| 11 | ADC24-3 | bits[23:8] |
| 12〜16 | 自動ソースなし | 常にSPI4手動値 |

`DAC_SOURCE_MASK=0x0060`のbit0〜10をそれぞれCH1〜11に対応させる。0＝従来の手動値、1＝ADC自動値。初期0、全11CHをモニタするなら**0x07FF**。上位bitは無視する。

- 手動値は従来どおり`0x0200+4×index`（index=0〜15）。自動選択中も手動シャドー値は保持し、書き換えられる。
- `0x0300+4×index`は送信完了した値、`0x0340+4×index`は選択中の実使用値、`0x0380+4×index`は最新ADC自動値。
- マスク変更時は影響するICへ再送し、手動へ戻せば保持していた手動値を使う。
- 自動値は**集約時**に更新し、SPI1の送信許可とは独立してDACへ要求する。STM32への転送が止まっていてもADC取得が動いていれば自動DAC更新は続く。
- 対象ICのA〜Dを20 MHz・24 bit×4で送信する既存DAC送信器を使う。1 IC約5.4 µs、IC間は並列。全16CHを同時更新するLDAC機能ではない。
- 手動WRITEは自動選択中でも対象ICへの再送要求になるが、出力する値はマスクで選ばれた実使用値。
- ADC未取得時の自動値は0。ADC24が途絶えた後は最後の値になる。DAC出力だけでは鮮度・有効性を判定できない。
- flow clearではDACマスク／手動値／最後の自動値を保持し、不要な0コードを再送しない。FPGA全体のresetでは初期化する。

DACコードは電圧値ではない。0x0000が0 Vかどうかは外部DAC設定に依存する。24 bitの上位16 bit化は丸めやスケール変換ではなくビット抽出。

## 6. SPI4：レジスタとプロトコル

SPI4は従来と同じ8バイト、Mode0、MSB first、1 MHz推奨／最大2 MHz。4バイト境界の16 bitバイトアドレス、32 bitデータBE。NSSを8バイト全体で保持する。

```text
WRITE TX: 02 AH AL 00 D3 D2 D1 D0
READ  TX: 03 AH AL 00 00 00 00 00
      RX: 00 00 00 ST D3 D2 D1 D0  (WRITEのD3..D0は0)
```

STは0正常、bit0不正opcode、bit1非整列、bit2未割当、bit3 ROへのWRITE。異常時のREAD値は0で変更なし。64クロック未完了のWRITEは破棄、余分なクロックはNSS解除まで無視、バースト／CRCなし。
SCLK High/Low各250 ns以上、MOSI setup/hold各100 ns以上、NSS setup/hold/High間隔各100 ns以上（各1 µs推奨）。読み出しは2段のレジスタMUXで、動的な値には最大約16.7 ns前の値を含む。

### 6.1 共通・DAC

| address | 名前・属性 | 内容 |
|---|---|---|
| 0x0000 | DEVICE_ID RO | 0x4A503538 |
| 0x0004 | VERSION RO | **0x00020000** |
| 0x0008 | CAPABILITIES RO | **0x0000003F**。bit0 DUMMY、1 DAC、2 ADC24、3 ADC24ダミー、4 ADS8568、5 DACソース選択 |
| 0x000C | DUMMY_COUNT RO | 16 |
| 0x0010 | WRITE_COUNT RO | 正常なRWレジスタWRITE完了回数、32 bit循環 |
| 0x0014 | DAC_STATUS RO | bit0〜3 BUSY、4〜7 PENDING、8〜11 INITIALIZED（IC1〜4） |
| 0x0018 | DAC_TRANSFER_COUNT RO | IC単位の4フレーム完了数 |
| 0x001C | DAC_CHANNEL_COUNT RO | 16 |
| 0x0020 | DAC_SDO_LEVEL RO | SDO4本の同期後の論理値。コマンド読戻しではない |
| **0x0060** | DAC_SOURCE_MASK RW | 下位11 bit、自動選択1／手動0、初期0 |
| 0x0100〜0x013C、4刻み | DUMMY[16] RW32 | 汎用保持レジスタ、初期0 |
| 0x0200〜0x023C、4刻み | DAC_MANUAL[16] RW16 | 手動値、初期0 |
| 0x0300〜0x033C、4刻み | DAC_SENT[16] RO16 | 最後の送信済値 |
| 0x0340〜0x037C、4刻み | DAC_EFFECTIVE[16] RO16 | マスク選択後の実使用値 |
| 0x0380〜0x03BC、4刻み | DAC_AUTO[16] RO16 | CH1〜11の最新自動値、CH12〜16は0 |

### 6.2 ストリームと24 bit ADC

| address | 名前・属性 | 内容 |
|---|---|---|
| 0x0400 | STREAM_CONTROL RW | bit0 SPI1送信許可、bit1 flow clearパルス。初期0、bit1はREADで0 |
| 0x0404 | STREAM_STATUS RO | bit0集約FIFO FULL、bit8集約FIFO EMPTY、bit16 SPI1 BUSY。v1.3の系統別FULL/EMPTYとは異なる |
| 0x0408 | TX_COUNT RO | 48バイトレコードの送信完了数、同期後 |
| 0x040C | ADC24_CHANNEL_COUNT RO | 3 |
| 0x0410/14/18 | ADC24_RX_COUNT[3] RO | 完成／ダミー生成数 |
| **0x0420/24/28** | **ADC24_REPLACED[3] RO** | 最新値を集約する前に上書きした数。10 kHzへの意図した間引きで増える。旧FIFO DROPとは意味が違う |
| 0x0430/34/38 | ADC24_RESTART[3] RO | 24 bit未完了で次DRDYが来た回数 |
| 0x0440/44/48 | ADC24_LAST[3] RO24 | 最新値 |
| 0x0450/54/58 | ADC24_PENDING[3] RO | 未集約の最新値があれば1。旧FIFO LEVELとは異なる |
| 0x0460/64/68 | ADC24_AGE_US[3] RO16 | 経過µs、未取得／飽和は65535 |
| 0x0480 | ADC24_SOURCE RW | 0実ADC、1内部ダミー。変更時flow clear |
| 0x0484/88/8C | ADC24_DUMMY[3] RW24 | 各78.125 kHzで生成する3値、初期0 |

24 bitダミー設定は維持するが、**フレーム生成の契機はADS8568の受信完了**なのでADS8568制御も開始する。ダミーは24 bitの3値だけを置換し、16 bitの8値を捏造しない。

### 6.3 ADS8568と集約

| address | 名前・属性 | 内容／初期値 |
|---|---|---|
| **0x0500** | ADS_CONTROL RW | bit0要求ENABLE、bit1 APPLYパルス。READはbit0。初期0 |
| 0x0504 | ADS_DELAY RW16 | D、9 |
| 0x0508 | ADS_CONVERSION RW16 | C、68 |
| 0x050C | ADS_PHASE RW2 | 3 |
| 0x0510 | ADS_NEGATE RW1 | 0 |
| 0x0514 | ADS_PERIOD RW32 | 4000、40 MHzクロック数 |
| 0x0518 | ADS_STATUS RO | bit定義は下記 |
| 0x051C | APPLIED_ID RO | 実適用済み設定ID、32 bit |
| 0x0520 | ADS_RX_COUNT RO | 40 MHz側の取得完了数、Gray同期後 |
| 0x0524 | ADS_SOURCE_DROP RO | 40→120 MHz FIFOが満杯で破棄した数 |
| 0x0528 | ADS_MISSED RO | 取得要求の時点で制御FSMがbusyだった回数 |
| 0x052C | CONFIG_REJECTED RO | 不正／busy APPLYの拒否数 |
| 0x0530 | SUBMITTED_ID RO | 検査に合格して転送した最新設定ID |
| 0x0534 | FRAME_CAPTURE_COUNT RO | 120 MHz側で集約した数 |
| 0x0538 | FRAME_DROP_COUNT RO | 送信FIFO FULLで破棄した完全レコード数 |
| 0x053C | FRAME_FIFO_LEVEL RO | 送信FIFO格納数0〜32、書込側同期済み見積もり |
| 0x0540 | APPLIED_DELAY RO16 | 実適用D |
| 0x0544 | APPLIED_CONVERSION RO16 | 実適用C |
| 0x0548 | APPLIED_PHASE RO2 | 実適用PHASE |
| 0x054C | APPLIED_NEGATE RO1 | 実適用NEGATE |
| 0x0550 | APPLIED_PERIOD RO32 | 実適用PERIOD |
| 0x0554 | APPLIED_ENABLE RO1 | 実稼働ENABLE |
| 0x0558 | LAST_FRAME_SEQUENCE RO | 最後に集約した通番 |
| 0x0560 | ADS_CHANNEL_COUNT RO | 8 |
| 0x0580〜0x059C、4刻み | ADS_VALUE[8] RO16 | 最新の実使用値、A0〜D1 |
| 0x05A0〜0x05BC、4刻み | ADS_RAW[8] RO16 | 同じ取得の符号反転前の値、A0〜D1 |

ADS_STATUS：bit0設定PENDING、bit1 CONFIG_ERROR、bit2取得FSM BUSY（同期後）、bit3実適用ENABLE、bit4 source FIFO FULL（同期後）、bit5 source FIFO EMPTY、bit6送信FIFO FULL、bit7 APPLIED_ID非0。その他0。
APPLIED値は初回適用まで0で、シャドー初期値と区別する。カウンタは32 bit循環、診断値は複数レジスタ間で一括スナップショットしない。

## 7. clearと保持状態

- STREAM_CONTROL=0はSPI1だけを停止し、ADS取得・集約・自動DAC更新は継続する。
- STREAM_CONTROL=2は送信禁止＋flow clear、=3はclear＋送信許可。
- flow clearはADC24の取得状態／通番／age／間引き数、2個のデータFIFO、ADS取得数／source DROP／MISSED、集約数／frame DROP／TX数を初期化し、ADS制御を短時間resetして再アームする。
- **ADSの実適用パラメータ／要求シャドー／設定ID／設定拒否数、DACマスク／手動値／最後の自動値、汎用DUMMY／WRITE_COUNTはflow clearで保持する。**
- flow clearはSPI1の送信中レコードを中断し得る。通常は送信停止とBUSY解除後に行い、DMA側も通番状態を初期化する。
- FPGA再設定／PLLロック喪失では全内部状態を初期化する。回路をPOFで保存しても、通信で設定した値は電源断で失われる。
- ADC基板のRESET端子は制御しない。物理ADCの電源投入／DCEN=0／PAR-SER等の設定は基板側で確認する。

## 8. ソフト検証と実機に残る事項

原本ADCテスト、光遅延付き統合テスト、設定mailboxテスト、STM32用Cデコーダ、Quartusの結果は[VERIFICATION.md](VERIFICATION.md)にまとめる。
48バイトレコードの復号用ヘッダーは[stm32/mixed_adc_protocol.h](stm32/mixed_adc_protocol.h)。

`constraints/optical_link_candidate.sdc`は、戻りDATAが20 MHz仮想基準の5〜18 ns後に遷移するという**未実測の候補ウィンドウ**を明示する。ADS_DATAをfalse pathにはせず、40 MHz受信レジスタへの入力遅延として解析する。内部クロックは全て関連クロックとして扱う。

この解析で閉じても、実際の光往復遅延／極性／デューティ／ADC出力遅延がその窓に入ることは実測が必要。全PHASE／Dの組み合わせで受信を保証するものではない。モデルはFS往路130 ns、SCLK往路140 ns、DATA復路130 ns等の例であり、現物の測定値ではない。統合テストのPHASE=0/D=9もモデル用の設定。

現場ではEO/OEのIC・ファイバ接続、ADS8568のDCEN=0、CONVSTART/FS極性、SCLK 20 MHzを確認し、互いに異なる既知の8電圧でPHASEとDを掃引する。先頭MSBと途中ビット、正負、LSB、温度・電源・ケーブルばらつきの余裕を確認する。全ゼロだけではビットずれを判定できない。

## 9. STM32／Zephyr担当者への更新指示

以下は実装指示と移植用ヘッダーであり、既存`zephyrproject/`を自動変更したものではない。

### 9.1 バージョン判定・受信処理

1. DEVICE_ID=0x4A503538に加え、**VERSION=0x00020000**、CAPABILITIESのADS／DACソース選択bit、ADC24_CHANNEL_COUNT=3／ADS_CHANNEL_COUNT=8を確認する。v1.3のIDだけで旧処理を動かさない。
2. SPI1は従来どおりSTM32スレーブ、PG11 SCLK／PA4 NSS／PB5 DATA、AF5、Mode0／20 MHz／8 bit。GPIO CSではなくハードウェアNSS入力。
3. **旧8バイトDMA／A1〜A3分配処理を廃止し、48バイト固定レコードに変更する。** magic A5 5A、format 02、CRC46バイト分を検査して11CHを一括更新する。
4. [mixed_adc_protocol.h](stm32/mixed_adc_protocol.h)をアプリ側へ取り込み、`mixed_adc_decode()`を使用する。ADC24は3要素、ADC16は8要素を保持する。16 bit値をsignedとして扱う場合だけ符号解釈する。
5. 24 bitの有効／更新bit・ageで制御に使えるか判断する。ADC24の通番差1やREPLACED=0を正常条件にしない。48バイトのフレーム通番差は通常1で、clear時は履歴を初期化する。

### 9.2 DMA起動と停止

循環DMA／ダブルバッファで複数NSS区間をまたいで連続受信する。例：**32レコード×48＝1536バイト**、半分768バイトごとに処理すれば10 kHzで約1.6 ms／通知。バッファ先頭・半分境界を32バイトに合わせ、H7のDMAアクセス可能RAM／D-cache整合を管理する。
`CONFIG_SPI_SLAVE`、SPI/DMA有効化と使用ドライバを確認する。Zephyrの有限長SPI API再設定だけで連続DMAを保証しない。既存実装が転送終了のたびにDMAを再起動する場合は、取りこぼさない循環／再アーム方式へ更新する。

開始順序：

1. `STREAM_CONTROL=0`、SPI1 BUSY解除を待つ。
2. 必要なら`DAC_SOURCE_MASK=0`で手動を維持して調整する。
3. ADSのシャドー設定を5レジスタへ書く。
4. `ADS_CONTROL=3`（ENABLE＋APPLY）、CONFIG_ERROR／PENDING／SUBMITTED_ID／APPLIED_IDと実適用値を確認する。
5. ADC24の実入力／ダミーを選ぶ。モード変更はflow clearになるので、この段階で行う。
6. STM32で48バイトレコード用のDMAをアームし、ソフトの通番履歴を初期化する。
7. **STREAM_CONTROL=3**でFIFOをclearして送信開始する。
8. 各値／CRC／ageが妥当なことを確認してから、必要なDAC自動マスク（全11CHなら0x07FF）を設定する。

停止はまずSTREAM_CONTROL=0とBUSY解除。ADSの変換自体も止める場合は**ADS_CONTROL=2**（ENABLE=0＋APPLY）を送り、APPLIED_ENABLE=0を確認する。ADS_CONTROL=0だけではシャドーを書き換えるだけなので停止しない。

### 9.3 SPI4の更新例

従来の`fpga_read()`／`fpga_write()`をそのまま利用できる。以下の`wr`／`rd`は、SPI4 statusとtransport errorを確認するラッパーを想定。

```c
/* 数値は校正の開始候補。モデルのPHASE=0/D=9を実機保証値にしない。 */
wr(0x0400, 0);       /* SPI1停止、BUSY解除を待つ */
wr(0x0504, delay);   /* 16bit D */
wr(0x0508, 68);      /* 16bit C */
wr(0x050c, phase);   /* 0..3 */
wr(0x0510, 0);       /* 生16bit。1なら各CHの算術負号 */
wr(0x0514, 4000);    /* 40MHz / 4000 = 10kHz */
uint32_t next_id = rd(0x0530) + 1u;
wr(0x0500, 3);       /* ENABLE + APPLY */
/* timeout付きでPENDING解除とAPPLIED_ID == next_idを待つ。
   CONFIG_ERRORが立ったら失敗。APPLIED_*も読み戻す。 */
arm_spi1_continuous_dma();
reset_frame_sequence_tracking();
wr(0x0400, 3);       /* flow clear + SPI1送信開始 */
/* 受信と値を確認後に必要なら： */
wr(0x0060, 0x07ff);  /* DAC CH1..11をADCモニタへ */
```

APPLYが拒否されてもSPI4のWRITE statusは0になり得るので、`APPLIED_ID`とERROR確認を省かない。SPI4トランザクションと一連の設定適用はmutexで直列化し、別スレッドがシャドー値を書き換えないようにする。

既存処理から必ず変更する点は、**SPI1の48バイト化、11CH配列、CRC対象46バイト、24 bitの間引き／age判断、ADS設定APPLYの完了待ち、DAC_SOURCE_MASKの設定**である。
