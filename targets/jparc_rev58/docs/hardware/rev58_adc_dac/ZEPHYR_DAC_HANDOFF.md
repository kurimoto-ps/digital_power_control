# rev58 SPI4 → AD5764 DC出力：STM32 / Zephyr引き継ぎ仕様

作成日：2026-10-24。対象FPGAプロジェクト：`quartus/projects/spi4_dac_dc`。
旧ダミーレジスタ版とは別のSOF/POF。**この版を書き込んで初めてDAC設定機能が使える。**
既存の`zephyrproject/`には変更していない。以下のZephyrコードは担当者向け実装例であり、STM32でのビルド・通信・電圧測定は未実施。

## 実現する機能

STM32がSPI4経由で16個のDAC設定レジスタの一つに値を書き込むと、その**下位16 bit**を対応するAD5764チャンネルに自動送信する。上位16 bitは無視し、設定値のREADではゼロを返す。単発設定でDC値を保持し、周期波形生成やフィードバック演算は行わない。

AD5764×4個、各4チャンネル（A/B/C/D）、合計16チャンネル。ソフトの配列添字は0〜15、資料上のCH番号は1〜16とする。従来のダミーレジスタ16個は従来どおり32 bitで読み書きでき、DAC出力に影響しない。

**値は電圧ではなく16 bit生コード。** BIN/2sCOMPピン、外部基準電圧、ゲイン／オフセット設定により電圧への対応が変わる。コード0x0000を0 Vと仮定しない。このRTLはデータレジスタ0x10〜0x13だけを書き、ゲイン、オフセット、機能レジスタを変更しない。資料にあるLDAC固定LowのDAC基板を前提とする。電圧対応は実機測定で確認する。

## SPI4の設定

既存SPI4通信形式を維持する。Mode 0（CPOL=0、CPHA=0）、MSB first、8 bit、全二重、推奨1 MHz／最大2 MHz。

| SPI4信号 | STM32 | MAX10 |
|---|---|---|
| SCLK | PE2 AF5 | L14 |
| NSS | PE4 GPIO、Low有効 | A12 |
| MISO | PE5 AF5 | J21 |
| MOSI | PE6 AF5 | E20 |

1回の`spi_transceive_dt()`で8バイトを送受信し、その間ずっとNSSをLowに保つ。PE4をGPIO CSにし、NSSのAF5設定は使用しない。

- SCLK High/Low各250 ns以上、MOSIは立ち上がり前後100 ns以上安定。
- NSS選択から最初の立ち上がり、最終立ち上がりからNSS解除、フレーム間NSS High：各100 ns以上、**各1 µs推奨**。
- クロックはフレーム間Low。NSS HighでMISOは直ちにHi-Z。
- FPGAの50 MHz発振器から新規MAX10用PLLで120 MHzを生成し、SPI4受信とDAC送信を同じクロック領域で処理する。PLLロック後にリセットを解除する。
- FPGA設定完了後10 ms待ってDEVICE_ID/versionを確認する。READY/IRQ/FAULTは未実装。

### フレーム（アドレスはバイト単位、4バイト境界）

| byte | MOSI READ | MOSI WRITE | MISO |
|---|---|---|---|
| 0 | 0x03 | 0x02 | 0 |
| 1 | address[15:8] | address[15:8] | 0 |
| 2 | address[7:0] | address[7:0] | 0 |
| 3 | 0（無視） | 0（無視） | status |
| 4 | 0 | data[31:24] | READ data[31:24]、その他0 |
| 5 | 0 | data[23:16] | READ data[23:16]、その他0 |
| 6 | 0 | data[15:8] | READ data[15:8]、その他0 |
| 7 | 0 | data[7:0] | READ data[7:0]、その他0 |

16 bitコードでも**転送長は8バイト**。WRITEは64クロック目で一括反映し、途中NSS解除は破棄する。65クロック目以降はNSS解除まで無視する。READはアドレス受信完了時の値を返す。バースト、自動アドレス加算、CRCはない。

statusはビットOR：0x00正常、0x01不正opcode、0x02非整列、0x04未割り当て、0x08 ROへのWRITE。異常時はデータ0、変更・DAC送信なし。statusはヘッダーの受理結果であり、DACへの送信完了通知ではない。

## レジスタマップ

| address | 名前 | 属性 | 意味／初期値 |
|---|---|---|---|
| 0x0000 | DEVICE_ID | RO | 0x4A503538（JP58）、旧版と同じ |
| 0x0004 | PROTOCOL_VERSION | RO | **0x00010001**（v1.1） |
| 0x0008 | CAPABILITIES | RO | **0x00000003**：bit0=DUMMY RW32、bit1=DAC16 DC |
| 0x000C | DUMMY_COUNT | RO | 16 |
| 0x0010 | WRITE_COUNT | RO | 正常なDUMMY/DAC設定WRITEの完了回数、初期0、32 bit循環 |
| 0x0014 | DAC_STATUS | RO | 下記、初期0 |
| 0x0018 | DAC_TRANSFER_COUNT | RO | DAC IC単位の4フレーム送信完了回数の合計、初期0、32 bit循環 |
| 0x001C | DAC_CHANNEL_COUNT | RO | 16 |
| 0x0020 | DAC_SDO_LEVEL | RO | bit0〜3に各SDOの2段同期後の論理値。DACレジスタ読戻しではない |
| 0x0100〜0x013C（4刻み） | DUMMY[0..15] | RW32 | 従来同様、初期0。DACと無関係 |
| **0x0200〜0x023C（4刻み）** | **DAC_SET[0..15]** | **RW16** | 下位16 bitが出力要求値、初期0。WRITEで自動送信 |
| 0x0300〜0x033C（4刻み） | DAC_SENT[0..15] | RO16 | 送信完了した最後のコード、初期0。DACからの読戻しではない |

上記以外は未割り当て。DAC_STATUSの未使用上位ビットは0。

| DAC_STATUS bit | 意味 |
|---|---|
| 0〜3 | DAC1〜4のBUSY：4フレーム送信シーケンス実行中 |
| 4〜7 | DAC1〜4のPENDING：再送要求あり |
| 8〜11 | DAC1〜4のINITIALIZED：今回のFPGA設定後に一度以上送信完了 |

DAC_SENTはICの4フレームと最後の待ち時間が完了した時に4値まとめて更新する。送信前の初期0は外部DACの実状態を保証しないので、INITIALIZEDも確認する。DONEはSDOによる受信確認ではなく、**FPGAが送信を完了したこと**を示す。外部DACが無接続でも進む。

## 16チャンネルの割当

設定address = `0x0200 + 4 * index`、送信済address = `0x0300 + 4 * index`。

| index | CH | DAC IC | IC内CH | 設定address | 送信済address | DAC command |
|---|---|---|---|---|---|---|
| 0 | 1 | DAC1 / IC11 | A | 0x0200 | 0x0300 | 0x10 |
| 1 | 2 | DAC1 / IC11 | B | 0x0204 | 0x0304 | 0x11 |
| 2 | 3 | DAC1 / IC11 | C | 0x0208 | 0x0308 | 0x12 |
| 3 | 4 | DAC1 / IC11 | D | 0x020C | 0x030C | 0x13 |
| 4 | 5 | DAC2 / IC13 | A | 0x0210 | 0x0310 | 0x10 |
| 5 | 6 | DAC2 / IC13 | B | 0x0214 | 0x0314 | 0x11 |
| 6 | 7 | DAC2 / IC13 | C | 0x0218 | 0x0318 | 0x12 |
| 7 | 8 | DAC2 / IC13 | D | 0x021C | 0x031C | 0x13 |
| 8 | 9 | DAC3 / IC15 | A | 0x0220 | 0x0320 | 0x10 |
| 9 | 10 | DAC3 / IC15 | B | 0x0224 | 0x0324 | 0x11 |
| 10 | 11 | DAC3 / IC15 | C | 0x0228 | 0x0328 | 0x12 |
| 11 | 12 | DAC3 / IC15 | D | 0x022C | 0x032C | 0x13 |
| 12 | 13 | DAC4 / IC17 | A | 0x0230 | 0x0330 | 0x10 |
| 13 | 14 | DAC4 / IC17 | B | 0x0234 | 0x0334 | 0x11 |
| 14 | 15 | DAC4 / IC17 | C | 0x0238 | 0x0338 | 0x12 |
| 15 | 16 | DAC4 / IC17 | D | 0x023C | 0x033C | 0x13 |

資料が示す外部アナログ出力コネクタのグループはDAC1=CN3〜CN6、DAC2=CN7〜CN10、DAC3=CN11〜CN14、DAC4=CN15〜CN18。グループ内の現物のA〜D対応は実機の回路図／出力測定でも確認する。

## 自動送信の動作

1. DAC_SETへの正常WRITEを受理し、下位16 bitを保持する。同じ値のWRITEでも送信を要求する。
2. 対象ICのA〜D要求値を一括スナップショットし、A→B→C→Dの順で4フレーム送信する。他のICには送信しない。
3. 各フレームは `command[7:0] + code[15:0]` の24 bit、MSB first。SCLK 20 MHz、DACは**立ち下がり**で取得する。SYNCは各24 bitフレームでLowにし、フレーム間にHighを入れる。
4. 4フレーム完了後、DAC_SENTとINITIALIZEDを更新し、DAC_TRANSFER_COUNTを1増やす。異なるICは並列動作できる。

DAC出力側のSCLKは待機時High、転送中High/Low各25 ns。SYNC選択→最初の立ち下がり25 ns、最後の立ち下がり→SYNC解除75 ns、フレーム間SYNC High 100 ns。SDINは通常立ち上がりで更新し、次の立ち下がりまで25 ns確保する。1 ICの4フレーム送信は約**5.4 µs**、要求から開始まで数クロック。

同じICの他3チャンネルも現在の要求値で再送する。**そのICへの最初のWRITEでは、まだ書いていない3チャンネルにも初期コード0x0000が送られる。** FPGA起動時だけでは自動送信しない。まだWRITEしていないICは駆動波形を出さず、外部DACの電源投入／以前の状態を保つ。16チャンネルを一斉に切り替える機能ではなく、LDAC固定Lowのため各チャンネルは各SYNC解除で順次更新される。

送信中の追加WRITEは要求値を更新してPENDINGに記録する。現在の4値スナップショットは変更せず、完了後に最新の4値を再送する。複数の追加WRITEは最新値へ集約し、中間値をすべて出力するFIFOではない。通常のSPI4最大2 MHzでは1 WRITEに32 µs以上かかるため、5.4 µsの送信より十分遅い。

FPGA再コンフィグレーションまたはPLLロック喪失によるリセットで要求値・送信済値・カウンタを初期化する。NSS HighやSTM32再起動だけではFPGA内の設定を消さない。SOF/POFに通信で設定したDC値を保存する機能はなく、電源再投入後はSTM32から再設定する。

## 実際のバイト列

```text
CH1（index 0 / DAC1-A）へ0x1234:
TX 02 02 00 00 00 00 12 34
RX 00 00 00 00 00 00 00 00

CH1要求値READ:
TX 03 02 00 00 00 00 00 00
RX 00 00 00 00 00 00 12 34

CH1送信済値READ（送信完了後）:
TX 03 03 00 00 00 00 00 00
RX 00 00 00 00 00 00 12 34

CH16（index 15 / DAC4-D）へ0xABCD:
TX 02 02 3C 00 00 00 AB CD
RX 00 00 00 00 00 00 00 00

DAC1でのCH1設定の出力フレーム（他3CHが初期値の場合）:
SYNC Low: 10 12 34 / SYNC High
SYNC Low: 11 00 00 / SYNC High
SYNC Low: 12 00 00 / SYNC High
SYNC Low: 13 00 00 / SYNC High
```

SPI4とDAC側ではサンプリングエッジが異なる。SPI4はSTM32の立ち上がり取得、DAC側はAD5764の立ち下がり取得。

## Zephyr実装例

既存のSPI4用DTS・8バイト転送関数をそのまま利用できる。SPI設定は変更不要、`CONFIG_SPI=y`、`CONFIG_GPIO=y`。新規実装用の最小構成も末尾に記載する。

下記は既存の`fpga_read(uint16_t,uint32_t *,uint8_t *)`と`fpga_write(uint16_t,uint32_t,uint8_t *)`を使う追加関数。複数スレッド／他の制御処理から同じDACを変更しない条件で使用する。共有時はこの一連の操作をmutexで保護する。

```c
#define FPGA_DAC_SET_BASE  0x0200u
#define FPGA_DAC_SENT_BASE 0x0300u
#define FPGA_DAC_STATUS    0x0014u

/* indexは0..15、codeは変換・符号拡張せず16bitのビット列を渡す。 */
static int fpga_dac_set(unsigned int index, uint16_t code)
{
    uint8_t status;
    uint32_t value;
    int ret;

    if (index >= 16) return -EINVAL;
    ret = fpga_write(FPGA_DAC_SET_BASE + 4u * index, code, &status);
    if (ret != 0) return ret;

    /* 要求値の読み戻し。FPGAが書き込みを受けたか確認する。 */
    ret = fpga_read(FPGA_DAC_SET_BASE + 4u * index, &value, &status);
    if (ret != 0) return ret;
    if (value != code) return -EIO;

    unsigned int chip = index / 4u;
    uint32_t waiting = (1u << chip) | (1u << (chip + 4u));
    uint32_t initialized = 1u << (chip + 8u);
    int64_t deadline = k_uptime_get() + 20; /* 通信込み20ms */
    do {
        ret = fpga_read(FPGA_DAC_STATUS, &value, &status);
        if (ret != 0) return ret;
        if ((value & waiting) == 0 && (value & initialized) != 0) {
            ret = fpga_read(FPGA_DAC_SENT_BASE + 4u * index,
                            &value, &status);
            if (ret != 0) return ret;
            if (value == code) return 0; /* FPGAの送信完了 */
        }
        k_usleep(10);
    } while (k_uptime_get() < deadline);
    return -ETIMEDOUT;
}
```

最初にDEVICE_ID=0x4A503538、version>=0x00010001、CAPABILITIES bit1=1、DAC_CHANNEL_COUNT=16を確認してから使用する。旧版もIDは同じなので、**IDだけでDAC対応を判定しない**。例：`fpga_dac_set(0, 0x1234)`はCH1、`fpga_dac_set(15, 0xABCD)`はCH16。

## 実機受け入れテスト

1. 新版SOFをSRAMへロードし、ID/version/capabilities/channel countを確認する。
2. 初回はDAC1の4CHがすべて再送されることを踏まえ、測定用接続でCH1へコードを設定する。
3. 要求値、送信済値、BUSY/PENDING解除、INITIALIZEDを確認する。
4. ロジックアナライザで対象ICだけに4回のSYNC Low、各24立ち下がり、20 MHz、0x10〜0x13と16 bitコードが出ることを確認する。SDO4本はFPGA入力。
5. 各16CHへ異なる値を設定し、他CHの要求値保持と実際の出力先を確認する。テストコードと電圧の関係を測定して対応表を作る。
6. 境界値・walking bit、上位16 bit無視、RO WRITE、非整列、未定義アドレス、途中中断を確認する。SPI4はまず1 MHzで試す。
7. SRAM版で動作確認後、常用する場合は同じ版のPOFをフラッシュに書く。電源再投入後の回路起動とSTM32によるDC値の再設定を確認する。

今回の開発ではADC、DutyOut、PLC、DDR2、SDOコマンド読戻し、アナログ校正、実機書き込み／電圧測定は行っていない。

## 新基板のDAC端子

ここでいうJ10は**rev58新基板の100接点コネクタ**。母基板外部の同名J10ではない。番号は図面上の電気接点番号で、物理方向図ではない。

| IC | SCLK J10 / 球 | SYNC_N J10 / 球 | SDIN J10 / 球 | SDO J10 / 球（入力） |
|---|---|---|---|---|
| DAC1 IC11 | 17 / E22 | 19 / F20 | 21 / G19 | 23 / G20 |
| DAC2 IC13 | 27 / P13 | 29 / H21 | 31 / R13 | 33 / L18 |
| DAC3 IC15 | 37 / M18 | 39 / M20 | 41 / M21 | 43 / M22 |
| DAC4 IC17 | 47 / N20 | 49 / N21 | 51 / P20 | 53 / V14 |

既存rev58のピンJSONと照合してQSFを作成した。FPGAのVCCIOは当該バンクも3.3 V配線、3.3-V LVTTL指定、出力4 mA。未使用ユーザーピンは入力トライステート。旧ACM205の球番号とSDO固定0出力設定は使用していない。

## 根拠資料

- `/home/demo/Downloads/rev58_ADC_DAC_designer_handoff.zip`：依頼文、コネクタ表p.3、内包README/HTML解析報告、`original/ad5764asuz.v`。
- 上記の保存先：`quartus/board/rev58_adc_dac/`。原本ソースは変更していない。今回の送信器はbusy/pendingと単一クロック処理を持つ新規実装で、旧送信器をそのままインスタンス化したものではない。
- `quartus/board/rev58/rev58_pin_connections.json`：新基板U5球番号、J10接点、VCCIO配線。
- [Analog Devices AD5764データシート Rev.F](https://www.analog.com/media/en/technical-documentation/data-sheets/AD5764.pdf)：シリアル動作、データ形式、LDAC、BIN/2sCOMP。生コードの解釈と24 bit通信条件を照合。

## 付録：新規Zephyrアプリ用の最小SPI構成

既にSPI4アクセスが動いている場合は追加不要。下記を使う場合も、使用中のボード定義とピン競合を確認する。

```dts
#include <zephyr/dt-bindings/gpio/gpio.h>
&spi4 {
    status = "okay";
    pinctrl-0 = <&spi4_sck_pe2 &spi4_miso_pe5 &spi4_mosi_pe6>;
    pinctrl-names = "default";
    cs-gpios = <&gpioe 4 GPIO_ACTIVE_LOW>;
    max10_spi4: max10@0 {
        compatible = "altr,max10-spi4-regs";
        reg = <0>;
        spi-max-frequency = <1000000>;
        spi-cs-setup-delay-ns = <1000>;
        spi-cs-hold-delay-ns = <1000>;
    };
};
```

アプリの`dts/bindings/spi/altr,max10-spi4-regs.yaml`：

```yaml
description: JPARC MAX10 SPI4 register interface
compatible: "altr,max10-spi4-regs"
include: spi-device.yaml
```

先の`fpga_dac_set()`より前に配置する共通関数：

```c
#include <errno.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/byteorder.h>

static const struct spi_dt_spec fpga = SPI_DT_SPEC_GET(
    DT_NODELABEL(max10_spi4),
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);

static int fpga_frame(uint8_t op, uint16_t addr, uint32_t data,
                      uint32_t *value, uint8_t *status)
{
    uint8_t tx[8] = {op, (uint8_t)(addr >> 8), (uint8_t)addr, 0};
    uint8_t rx[8] = {0};
    sys_put_be32(data, &tx[4]);
    struct spi_buf tb = {.buf = tx, .len = sizeof(tx)};
    struct spi_buf rb = {.buf = rx, .len = sizeof(rx)};
    const struct spi_buf_set ts = {.buffers = &tb, .count = 1};
    const struct spi_buf_set rs = {.buffers = &rb, .count = 1};
    *status = 0xff;
    k_busy_wait(1); /* フレーム間CS Highを確保 */
    int ret = spi_transceive_dt(&fpga, &ts, &rs);
    if (ret < 0) return ret;
    *status = rx[3];
    if (rx[3] != 0) return -EIO;
    if (value != NULL) *value = sys_get_be32(&rx[4]);
    return 0;
}

static int fpga_read(uint16_t addr, uint32_t *value, uint8_t *status)
{
    return fpga_frame(0x03, addr, 0, value, status);
}

static int fpga_write(uint16_t addr, uint32_t value, uint8_t *status)
{
    return fpga_frame(0x02, addr, value, NULL, status);
}
```

アプリ初期化で`spi_is_ready_dt(&fpga)`を確認し、FPGA設定完了から10 ms待つ。続いてID/version/capabilitiesを確認して`fpga_dac_set()`を呼ぶ。GPIO CSなので`SPI_HOLD_ON_CS`は指定せず、フレーム全体を単一呼び出しで転送する。
