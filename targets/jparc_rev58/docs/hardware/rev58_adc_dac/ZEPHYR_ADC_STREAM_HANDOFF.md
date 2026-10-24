# rev58：ADC 3系統 → FPGA → STM32 SPI1 受信仕様 v1.3

2026-10-25更新。対象プロジェクト：`quartus/projects/spi4_adc_dac`。
ADC1〜3の各24 bit値を、各78.125 kHzで受け取り、STM32へ送る。ADC4〜6は未実装。
既存SPI4レジスタアクセス、DUMMY16個、DAC16CHのDC出力を維持する。

**新しいSOF/POFをロードする必要がある。今回の版はまだ実機に書き込んでいない。**
Zephyr環境は変更していない。以下はSTM32担当者への通信契約であり、STM32 DMAの動作保証・実機波形確認結果ではない。

## 経路とクロック

```text
ADC1 SCLK/DRDY/DATA ─→ 各ピン3段同期・フレーム受信 ─→ DCFIFO 256ワード ─┐
ADC2 SCLK/DRDY/DATA ─→ 各ピン3段同期・フレーム受信 ─→ DCFIFO 256ワード ─┼→ 公平な順送り → SPI1 → STM32
ADC3 SCLK/DRDY/DATA ─→ 各ピン3段同期・フレーム受信 ─→ DCFIFO 256ワード ─┘
                        120 MHz領域                  書込120 / 読出80 MHz     80 MHz領域で20 MHz SCLK生成
```

ADCのSCLKは20 MHz、FPGAの50 MHz発振器／PLLに対して非同期。**ADCピンを内部クロック配線として使わない。** 提供例と同じ120 MHzオーバーサンプリング方式で3本を同期化し、24 bitワードを完成してからvendor DCFIFOで送信領域へ渡す。24 bitを単純な多ビット2FFで渡す処理は行わない。

PLLは50 MHzから120 MHzと80 MHzを生成する。FIFOは両クロック間のCDC、速度差、バーストを吸収する。Gray pointerにnet delay／bus skew制約を設け、同期器内部の段間は通常タイミングを解析する。FIFOのclearは両側共通、解除は各領域で同期化する。

## ADC入力の形式と前提

提供ZIPの`OPTIN_KC.v` / `OPTIN_KCtop.v` / 再利用アダプタに基づく。

- DRDYの**立ち上がり**がフレーム開始。CS Low型の通常SPIではない。
- 以降のSCLK **24回の立ち下がり**でDATAをMSB firstに取得する。フレーム完了後の余分なクロックは無視する。
- 次のDRDYが24 bit未完了のうちに来たら途中値を破棄し、フレームを再開する。RESTART_COUNTを加算する。
- 3系統は独立。フレーム間にSCLKが止まっていてもDRDYを検出する。
- 24 bitは生のビット列として保持する。ADCの型番／符号形式が不明なので符号・スケールを変換しない。

120 MHzで非同期信号を同期化するため、FPGA入力端子で次の条件を満たすこと：

| 項目 | 条件 |
|---|---|
| SCLK最大 | 20 MHz、High/Low各25 ns以上 |
| DATA setup/hold | SCLK立ち下がりの前後各 **16.7 ns以上** |
| DRDY High/Low | 各25 ns以上 |
| DRDY立ち上がり→最初のデータ立ち下がり | **50 ns以上** |
| 通常フレーム周期 | 各系統12.8 µs（78.125 kHz） |

DATAがSCLKの立ち上がりで変わる想定なら、20 MHzの半周期25 nsで上記を満たす。これらは**今回の受信器が要求する外部タイミング条件**であり、未提示のADC回路が満たすと実測したものではない。特にDATAを立ち下がりと同時に変更する信号には、このオーバーサンプリング方式をそのまま適用できない。

## 配線

J10はrev58新基板のコネクタの電気接点番号。旧ACM205球番号／母基板外部の同名J10とは別。

| 系統 | DRDY J10 / MAX10 | DATA J10 / MAX10 | SCLK J10 / MAX10 |
|---|---|---|---|
| ADC1 | 18 / E19 | 20 / F18 | 22 / E21 |
| ADC2 | 26 / G17 | 28 / F21 | 30 / F22 |
| ADC3 | 34 / H22 | 36 / AB10 | 38 / J22 |

9本ともFPGA入力。FPGAからADCクロックは出さない。

| SPI1信号 | MAX10出力 | STM32入力 |
|---|---|---|
| SCLK | C21 | **PG11 AF5** |
| NSS、Low有効 | C20 | PA4 AF5 |
| DATA（配線名SPI1_MOSI） | C22 | PB5 AF5 |

SCLKのネットラベルにPA5表記が残るが、実配線はPG11。FPGAがSPI1マスター、STM32はSPI1スレーブ受信。MISOはない。3.3-V LVTTL、出力4 mA。他の未使用ユーザーピンは入力トライステート。

## SPI1の送信形式

**Mode 0、20 MHz、MSB first、8 bitワード**。SCLKのHigh/Lowは各25 ns。
NSS Lowの間に8バイト＝64クロックを出す。DATAは通常立ち下がりで更新し、STM32は立ち上がりで読む。

| byte | 内容 |
|---|---|
| 0 | **0xA1 / 0xA2 / 0xA3**：ADC1 / ADC2 / ADC3 |
| 1 | 系統別通番[15:8] |
| 2 | 系統別通番[7:0] |
| 3 | ADC生値[23:16] |
| 4 | ADC生値[15:8] |
| 5 | ADC生値[7:0] |
| 6 | CRC16[15:8] |
| 7 | CRC16[7:0] |

通番は系統ごとに0開始、**完成した24 bitフレームごとに増加**し、65535→0で循環する。FIFO満杯で破棄した完成フレームも通番を進める。途中で再開した不完全フレームは進めず、別のRESTART_COUNTで検出する。通番は絶対時刻ではなく、3系統の同時サンプリングを保証するものではない。

CRCは**CRC-16/CCITT-FALSE**：poly=0x1021、init=0xFFFF、refin=false、refout=false、xorout=0、対象byte 0〜5。CRC自身はビッグエンディアン。

```text
ADC1 / seq=0 / raw=0x123456 : A1 00 00 12 34 56 74 F9
ADC2 / seq=0 / raw=0x89ABCD : A2 00 00 89 AB CD 08 D6
ADC3 / seq=0 / raw=0xFFFFFF : A3 00 00 FF FF FF F3 B4
ADC1 / seq=65535 / raw=1   : A1 FF FF 00 00 01 AB B5
```

NSS選択→最初の立ち上がり125 ns、最終立ち上がり→NSS解除125 ns。最終立ち下がり→解除100 ns。
固定のHigh待ちは200 ns、ヘッダー作成等を含め、連続データがあるときのNSS Highは約287.5 ns、パケット開始間隔は約**3.6875 µs**。データがなければNSS High、SCLK Low、DATA Lowで待つ。バイト間の追加の待ちはない。

3系統はround-robinで公平に選び、空のFIFOを飛ばす。到着位相・FIFO状態により順序は変わるため、A1→A2→A3固定と仮定せず、必ずbyte 0で識別する。

## データ量、FIFO、欠落

- 生データ：24 bit × 3 × 78.125 kHz = **5.625 Mbit/s**。
- 8バイトレコード：64 bit × 3 × 78.125 kHz = **15 Mbit/s＝1.875 MB/s**。
- 固定待ちを含む送信能力：約271,186レコード/s。要求234,375レコード/sに対し約86.4%使用。
- FIFOは各系統**40 bit×256ワード**（16 bit通番＋24 bitデータ）、合計768ワード。1系統あたり約**3.2768 ms**分の一時停止を吸収する。これは無期限の欠落防止ではない。
- FULL時は**新着ワードを破棄**し、古いワードの順序を保持する。DROP_COUNTを増やし、以降の通番に欠落が現れる。
- STM32からのready／ACK線はない。FPGAはSTM32のDMAオーバーランを検出できないので、STM32側のSPI overrun／DMAエラーとCRC／通番も監視する。

## SPI4からの制御・監視

従来のSPI4 Mode0、1 MHz推奨／最大2 MHz、8バイト全二重フレームを維持する。
SPI4の形式は [DAC版引き継ぎ仕様](../spi4_dac_dc/ZEPHYR_DAC_HANDOFF.md) のフレーム・共通C関数を参照。

- DEVICE_ID（0x0000）：従来どおり0x4A503538。
- PROTOCOL_VERSION（0x0004）：**0x00010003（v1.3）**。
- CAPABILITIES（0x0008）：**0x0000000F**。bit0=DUMMY、bit1=DAC、bit2=ADC→SPI1、bit3=ADC未接続のダミー送信。
- DUMMY、DAC_SET（0x0200〜）、DAC_SENT（0x0300〜）、DAC状態は従来どおり。
- WRITE_COUNTは正常なDUMMY／DAC／ADC制御・ダミー設定WRITEを数える。

| address | 名前 | 属性 | 意味 |
|---|---|---|---|
| **0x0400** | ADC_CONTROL | RW | bit0=SPI1送信許可（初期0）、bit1=受信・FIFO・送信のclearパルス。bit1のREADは0、他bit無視 |
| 0x0404 | ADC_STATUS | RO | bit0〜2=ADC1〜3 FIFO FULL、bit8〜10=各FIFO EMPTY、bit16=SPI1送信BUSY（同期後） |
| 0x0408 | ADC_TX_COUNT | RO | 完了したSPI1レコード数、32 bit、Gray同期後の値 |
| 0x040C | ADC_CHANNEL_COUNT | RO | 3 |
| 0x0410 / 14 / 18 | ADC_RX_COUNT[0..2] | RO | 各系統の完成24 bitフレーム数、FIFO破棄も含む |
| 0x0420 / 24 / 28 | ADC_DROP_COUNT[0..2] | RO | FIFO満杯で破棄したワード数 |
| 0x0430 / 34 / 38 | ADC_RESTART_COUNT[0..2] | RO | 未完了フレーム中の次DRDYで再開した回数 |
| 0x0440 / 44 / 48 | ADC_LAST[0..2] | RO | 最後に受信完了した24 bit値（上位8 bit=0）、FIFO破棄時も更新 |
| 0x0450 / 54 / 58 | ADC_FIFO_LEVEL[0..2] | RO | 書込領域から見た格納数0〜256。反対側の読出し反映には同期遅延あり |
| **0x0480** | **ADC_SOURCE_MODE** | **RW** | **0=実ADC、1=ダミー生成。初期0、bit0のみ保持** |
| **0x0484** | **ADC_DUMMY1** | **RW24** | ADC1相当の生値。下位24 bit、初期0 |
| **0x0488** | **ADC_DUMMY2** | **RW24** | ADC2相当の生値。下位24 bit、初期0 |
| **0x048C** | **ADC_DUMMY3** | **RW24** | ADC3相当の生値。下位24 bit、初期0 |

カウンタは32 bit循環、clearで0。STATUSは各信号の同期遅延がある診断情報で、レジスタ間の一括スナップショットではない。診断値の読み出しMUXを1周期パイプライン化しており、アドレス受信時より最大1内部周期（約8.3 ns）前の値を返す場合がある。

**送信禁止でもADC受信とFIFO蓄積は継続する。** 起動から3.2768 ms以上受信側を待たせるとFIFO満杯になり得る。通常はSTM32受信準備後にclearしてから開始する。

操作例（SPI4）：

```text
送信禁止＋ADC経路clear（DAC/DUMMYは保持）:
TX 02 04 00 00 00 00 00 02
RX 00 00 00 00 00 00 00 00

SPI1送信開始:
TX 02 04 00 00 00 00 00 01

SPI1送信停止（現在のレコードは最後まで送る）:
TX 02 04 00 00 00 00 00 00
```

clearは送信途中でも直ちにFIFO／経路をリセットし、NSS解除で不完全レコードが出ることがある。正常停止後にclearするか、受信側で最後の短いレコードを破棄し、clear後に通番の期待値を初期化する。clearはDAC要求値／送信済値やDUMMY、グローバルWRITE_COUNTをリセットしない。

### STM32側の開始順序

1. SPI4でID/version/capabilities/channel countを確認する。
2. ADC_CONTROL=0で停止し、現在の送信完了を待つ。必要ならclear=2でFIFO・通番を初期化する。
3. STM32 SPI1を**スレーブ受信／Mode0／8 bit／MSB first／ハードウェアNSS入力**で設定し、DMAを開始する。
4. ADC_CONTROL=1でFPGA送信を有効にする。以後のデータはFIFOから自動転送される。
5. CRCと系統別通番を確認し、SPI4でDROP/RESTART、STM32でSPI/DMAエラーを監視する。

SPI4のスレーブはFPGA、SPI1のスレーブはSTM32で、役割が逆。SPI1のPA4はNSS AF5とし、SPI4のGPIO CSと混同しない。

```dts
/* ピンとNSSの例。DMAチャネル・所有コア・クロック設定は既存ボード側で確認。 */
&spi1 {
    status = "okay";
    pinctrl-0 = <&spi1_sck_pg11 &spi1_mosi_pb5 &spi1_nss_pa4>;
    pinctrl-names = "default";
    /* cs-gpios は指定しない。SCLKはFPGAからの20MHz。 */
};
```

`CONFIG_SPI_SLAVE=y`、SPI DMAを使用する。1レコードごとの割込み／API再設定は毎秒234,375回になるうえ、NSS Highの約287.5 nsで再アームすることは期待できない。**DMAの循環／ダブルバッファ等でNSS境界をまたいで継続受信**し、例えば8バイト×複数件をまとめて処理する。ドライバの有限長転送終了時の再設定で取りこぼす場合は、継続DMAへの対応をSTM32側で実装する必要がある。ZephyrのSPI API呼び出しだけでこの連続受信が保証されるとはしていない。H7のDMAアクセス可能RAMとD-cache整合も確認する。

## ADC未接続で行うSPI1テスト（v1.3追加）

`ADC_SOURCE_MODE=0`なら実ADC受信、`=1`なら内蔵ダミー生成。ADC_DUMMY1〜3へ24 bitの任意の値を書く。上位8 bitは無視し、READではゼロを返す。モードレジスタも上位31 bitを無視し、READは0/1だけを返す。

ダミーは**120 MHzを1536分周し、各78.125 kHz**で3個同時に生成する。外部ADCのSCLK/DRDY/DATAが停止していても生成できる。モード1では実ADC信号を無視する。ダミーは実ADCと**同じDCFIFO、round-robin、通番、CRC、20 MHz SPI1**を通り、8バイト形式も同一。ダミー専用の識別ヘッダーはなく、A1/A2/A3のままなのでSTM32の受信処理を変更せずに試験できる。

- `ADC_CONTROL.bit0`の送信許可は両モード共通。モード1にするだけでは送信を許可しない。
- モードの**変更時**はADC経路とFIFOを自動clearし、通番／診断カウンタを0から再開する。送信許可bitは保持する。同じモード値を再WRITEしてもclearしない。
- モード変更は送信中のレコードを中断し得るため、まず送信を停止してBUSY解除を確認する。途中切替をした場合は短いレコードを破棄して通番の期待値を初期化する。
- 値レジスタのWRITEではclearしない。書き込んだ値は次の生成タイミング以降に反映する。すでにFIFOに入った値は変更しない。
- 3値は同一生成tickで取り込むが、SPI4で3レジスタを順に書く間にはtickが来得る。3値を一括した組として切り替える場合は、停止→3値設定→clear付き開始の順にする。
- ダミーモードでもRX_COUNT、DROP_COUNT、LAST、FIFO_LEVELは**選択中のソース**の診断として動く。RESTART_COUNTは0。送信停止中も生成・FIFO蓄積は続き、約3.2768 msで満杯になる。明示的にテストした場合を除き、開始時にclearする。
- ADC_CONTROL clearはモードと3値を保持する。FPGA再設定／PLLロック喪失ではモード0、3値0、送信禁止へ初期化する。

推奨手順：

1. ADC_CONTROL=0で停止し、ADC_STATUS.bit16が0になるのを待つ。
2. ADC_DUMMY1=0x123456、ADC_DUMMY2=0x89ABCD、ADC_DUMMY3=0xFEDCBAを設定する。
3. ADC_SOURCE_MODE=1を書き、READで確認する。
4. STM32のSPI1スレーブDMA受信を準備する。
5. **ADC_CONTROL=3（clear＋送信許可）**を書き、受信側の通番状態も初期化する。
6. 各チャンネルで指定した生値、通番の連続性、CRC、約78.125 kHzの更新を確認する。
7. 終了時は送信停止→BUSY解除→ADC_SOURCE_MODE=0。実ADCを使う時も、DMA準備後にADC_CONTROL=3で開始できる。

```text
SPI4 WRITE、順に実行（各RX status=00）:
ADC_DUMMY1=0x123456 : 02 04 84 00 00 12 34 56
ADC_DUMMY2=0x89ABCD : 02 04 88 00 00 89 AB CD
ADC_DUMMY3=0xFEDCBA : 02 04 8C 00 00 FE DC BA
ADC_SOURCE_MODE=1  : 02 04 80 00 00 00 00 01
DMA準備後の開始     : 02 04 00 00 00 00 00 03

SPI4 READ例:
ADC_SOURCE_MODE    : TX 03 04 80 00 00 00 00 00
                     RX 00 00 00 00 00 00 00 01
ADC_DUMMY1         : TX 03 04 84 00 00 00 00 00
                     RX 00 00 00 00 00 12 34 56
```

1つの系統の値を変えても、他の2系統の設定は維持する。CRCは通番も含むので、固定値でも通番の増加に従いCRCは変わる。初回seq=0のADC1/ADC2のレコードは、前述のCRCテストベクトルと一致する。

## Cデコード例

DMA受信済みの8バイトを解析する関数。転送設定・DMA開始処理は含まない。

```c
#include <errno.h>
#include <stdint.h>

struct adc_record {
    uint8_t channel;       /* 1..3 */
    uint16_t sequence;
    uint32_t raw24;        /* 符号変換なし */
};

static uint16_t adc_crc16(const uint8_t *p, unsigned int len)
{
    uint16_t crc = 0xffff;
    for (unsigned int i = 0; i < len; ++i) {
        crc ^= (uint16_t)p[i] << 8;
        for (unsigned int b = 0; b < 8; ++b) {
            crc = (uint16_t)((crc << 1) ^
                            ((crc & 0x8000) ? 0x1021 : 0));
        }
    }
    return crc;
}

static int adc_decode(const uint8_t p[8], struct adc_record *out)
{
    if (p[0] < 0xa1 || p[0] > 0xa3) return -EINVAL;
    uint16_t received_crc = ((uint16_t)p[6] << 8) | p[7];
    if (adc_crc16(p, 6) != received_crc) return -EBADMSG;
    out->channel = p[0] - 0xa0;
    out->sequence = ((uint16_t)p[1] << 8) | p[2];
    out->raw24 = ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5];
    return 0;
}
```

系統ごとに`(uint16_t)(current_sequence - previous_sequence)`を求め、通常は1。0は重複、2以上は欠落候補。65535→0も差分1になる。clearした時は前回値を無効にする。DMA開始位置が不明／CRC異常時は、A1〜A3とCRCが合う8バイト候補までバイト単位で再同期し、その後の通番も検証する。

## 実機で確認すること

- ADC入力9本のDRDY極性、24 bit、立ち下がり取得、setup/holdと20 MHz波形。
- FPGA→STM32のSPI1 3本の方向、PG11使用、64クロック／レコード、CRC。
- 3系統同時78.125 kHzで長時間DMA受信し、CRC／通番欠落、SPI/DMA overrun、FIFO DROPがないこと。
- 意図的な送信停止でFIFOがFULLになり、再開後の通番欠落とDROPが一致すること。
- ADC未接続／SCLK停止／途中DRDY再開／clear／電源再投入から復帰すること。
- ダミーモードでADCを未接続にし、3値の設定・読み戻し、連続受信、実ADCへの切替、FIFO内旧データの破棄を確認する。
- DAC16CHの既存設定とSPI4アクセスが同時に動くこと。

## 参照

- 提供`rev58_ADC_DAC_designer_handoff.zip`の実装依頼書、`OPTIN_KC.v`、`OPTIN_KCtop.v`、`reusable/acm205_adc6_dac4_core.v`。
- `quartus/board/rev58/rev58_pin_connections.json`、技術説明書第6章。
- [Intel FIFO FPGA IP User Guide](https://www.intel.com/programmable/technical-pdfs/683522.pdf)：DCFIFO、Gray pointerの遅延／skew制約。
- [既存DAC通信仕様・SPI4アクセス関数](../spi4_dac_dc/ZEPHYR_DAC_HANDOFF.md)。
