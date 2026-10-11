# JPARC rev58 MAX10 SPI4 レジスタ仕様 v1.0

Zephyr担当者への引き継ぎ用。STM32をマスター、MAX10をスレーブとしてダミーレジスタを読み書きする。既存のZephyrソースには変更を加えていない。以下のDTS/Cは組み込み用の例であり、STM32ビルド・実機試験は未実施。

## 接続

対象FPGAは **10M16DCF484I7G**。基板資料のSPI4と50 MHz発振器を使用する。

| 信号 | STM32 | FPGAピン | 方向 |
|---|---|---|---|
| SCLK | PE2 / AF5 | L14 | STM32 → FPGA |
| NSS | PE4 / GPIO | A12 | STM32 → FPGA、Low選択 |
| MISO | PE5 / AF5 | J21 | FPGA → STM32 |
| MOSI | PE6 / AF5 | E20 | STM32 → FPGA |
| CLK50 | 基板発振器 | N5 | FPGA内部クロック |

I/Oは3.3-V LVTTL、MISO駆動強度4 mA。NSSがHighならMISOはハイインピーダンス。READY/FAULT/IRQは今回のRTLで駆動しない。他の未使用ユーザーピンは入力・トライステートとする。

## SPI設定とタイミング

- Mode 0（CPOL=0、CPHA=0）、MSB first、8 bit、全二重。
- **推奨1 MHz、最大2 MHz**。SCLKのHigh/Lowはそれぞれ250 ns以上。基板資料の20 MHzはこの初版の対応範囲外。
- MOSIはSCLK立ち上がり前後100 ns以上安定させる。
- NSS Lowから最初のSCLK立ち上がりまで100 ns以上。
- 最終SCLK立ち上がりからNSS Highまで100 ns以上。
- フレーム間のNSS Highは100 ns以上。Zephyrでは上記NSSの3期間を**1 µs以上**にすることを推奨。
- NSSを8バイト全体でLowに維持する。バイトごとのNSSパルスは禁止。フレーム間はSCLKをLowに戻す。
- FPGAはCLK50で入力を同期・検出する。MISOは検出したSCLK立ち下がりで更新されるため、外部立ち下がりから即時には変化しない。最大2 MHzはこの遅延を含む設計上の上限で、実機の信号品質は別途確認する。
- FPGA再コンフィグレーションでレジスタを初期化する。内部起動リセットは約320 ns。STM32側はコンフィグレーション完了後10 ms待ち、DEVICE_IDを読んで通信開始を確認する。

## 1フレームの形式

NSSをLowにして**8バイト（64クロック）**を一度の全二重転送で送る。アドレスは16 bitの**バイトアドレス**、値は32 bit。すべてビッグエンディアン。

| バイト位置 | MOSI: READ | MOSI: WRITE | MISO |
|---|---|---|---|
| 0 | 0x03 | 0x02 | 0x00 |
| 1 | アドレス[15:8] | アドレス[15:8] | 0x00 |
| 2 | アドレス[7:0] | アドレス[7:0] | 0x00 |
| 3 | 0x00（無視） | 0x00（無視） | ステータス |
| 4 | 0x00 | 値[31:24] | READ: 値[31:24] / WRITE: 0 |
| 5 | 0x00 | 値[23:16] | READ: 値[23:16] / WRITE: 0 |
| 6 | 0x00 | 値[15:8] | READ: 値[15:8] / WRITE: 0 |
| 7 | 0x00 | 値[7:0] | READ: 値[7:0] / WRITE: 0 |

READ値はアドレス受信完了時にスナップショットする。WRITEは64番目の立ち上がりを内部で検出した時点で一括反映する。最後まで転送せずNSSを上げたWRITEは破棄し、値・WRITE_COUNTを変更しない。NSS Highは通信パーサだけをリセットし、レジスタ値は保持する。

WRITEのステータスは**ヘッダーの受理結果**であり、後続データの受信完了通知ではない。WRITE後に別フレームでREADして一致を確認する。余分な65クロック目以降はNSS Highまで無視し、複数操作・バースト・自動アドレス加算は行わない。CRCはない。

### ステータス（MISO byte 3）

| マスク | 意味 |
|---|---|
| 0x00 | 正常 |
| 0x01 | opcodeが0x02/0x03以外 |
| 0x02 | アドレスが4バイト境界にない |
| 0x04 | 未割り当てアドレス |
| 0x08 | 読み取り専用レジスタへのWRITE |

複数条件はORで返す。異常時のMISO byte 4..7はゼロ、レジスタ変更なし。例えばREAD 0x0101は0x02、READ 0x0001は0x06。WRITEのopcodeが不正なら0x08は判定しない。

## レジスタマップ

| バイトアドレス | 名前 | 属性 | 初期値 / 意味 |
|---|---|---|---|
| 0x0000 | DEVICE_ID | RO | 0x4A503538（ASCII JP58） |
| 0x0004 | PROTOCOL_VERSION | RO | 0x00010000（上位16 bit major=1、下位 minor=0） |
| 0x0008 | CAPABILITIES | RO | 0x00000001（bit 0: RW32対応） |
| 0x000C | DUMMY_COUNT | RO | 16 |
| 0x0010 | WRITE_COUNT | RO | 0、正常なダミーWRITEの完了ごとに+1、32 bitで循環 |
| 0x0100 + 4×i、i=0..15 | DUMMY[i] | RW | 各0、全32 bit読み書き可 |

DUMMY[15]は0x013C。その他のアドレスは未割り当て。同値のWRITEもWRITE_COUNTを増やす。ソフトウェアリセットはない。再初期化にはFPGAの再コンフィグレーションを使用する。

## テストベクトル

```text
DEVICE_ID READ:
TX 03 00 00 00 00 00 00 00
RX 00 00 00 00 4A 50 35 38

DUMMY[0] = 0x12345678 WRITE:
TX 02 01 00 00 12 34 56 78
RX 00 00 00 00 00 00 00 00

DUMMY[0] READ（上のWRITE完了後）:
TX 03 01 00 00 00 00 00 00
RX 00 00 00 00 12 34 56 78

DEVICE_IDへのWRITE（変更されない）:
TX 02 00 00 00 FF FF FF FF
RX 00 00 00 08 00 00 00 00
```

## Zephyr設定例

PE4はGPIO CSとして使い、SPI4のNSS AF5ピン設定を含めない。PE2/PE5/PE6のAF5ラベルはローカルSTM32H745のpinctrl定義で確認済み。使用中のボードoverlayとSPI4の競合をZephyr担当側で確認する。

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

テストアプリ内に `dts/bindings/spi/altr,max10-spi4-regs.yaml` を作る例：

```yaml
description: JPARC MAX10 SPI4 register test interface
compatible: "altr,max10-spi4-regs"
include: spi-device.yaml
```

`prj.conf`には `CONFIG_SPI=y`、`CONFIG_GPIO=y` を設定する。STM32クロックから選ばれる実際のSPI分周周波数も確認する。

### Cのアクセス例

既存ドライバを追加せず、子ノードからSPI設定を取得する例。呼び出しは単一スレッドを想定。同じデバイスを複数スレッドから使う場合はフレーム操作をmutexで直列化する。

```c
#include <errno.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/byteorder.h>

static const struct spi_dt_spec fpga = SPI_DT_SPEC_GET(
    DT_NODELABEL(max10_spi4),
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);

/* statusにはFPGAの応答を返す。負の戻り値は通信/FPGAエラー。 */
static int fpga_frame(uint8_t op, uint16_t addr, uint32_t value,
                      uint32_t *result, uint8_t *status)
{
    uint8_t tx[8] = {op, (uint8_t)(addr >> 8), (uint8_t)addr, 0};
    uint8_t rx[8] = {0};
    sys_put_be32(value, &tx[4]);
    struct spi_buf tb = {.buf = tx, .len = sizeof(tx)};
    struct spi_buf rb = {.buf = rx, .len = sizeof(rx)};
    const struct spi_buf_set ts = {.buffers = &tb, .count = 1};
    const struct spi_buf_set rs = {.buffers = &rb, .count = 1};

    *status = 0xff;  /* transport失敗時にはFPGA応答未取得 */
    k_busy_wait(1);  /* 前回のCS解除から1 us以上あける */
    int ret = spi_transceive_dt(&fpga, &ts, &rs);
    if (ret < 0) {
        return ret;
    }
    *status = rx[3];
    if (rx[3] != 0) {
        return -EIO;
    }
    if (result != NULL) {
        *result = sys_get_be32(&rx[4]);
    }
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

/* FPGA設定完了後に呼ぶ最小スモークテスト。 */
static int fpga_smoke_test(void)
{
    uint32_t value;
    uint8_t status;
    int ret;
    if (!spi_is_ready_dt(&fpga)) {
        return -ENODEV;
    }
    k_msleep(10);
    ret = fpga_read(0x0000, &value, &status);
    if (ret != 0) return ret;
    if (value != 0x4A503538) return -EIO;
    ret = fpga_write(0x0100, 0x12345678, &status);
    if (ret != 0) return ret;
    ret = fpga_read(0x0100, &value, &status);
    if (ret != 0) return ret;
    return value == 0x12345678 ? 0 : -EIO;
}
```

ヘッダー送信とデータ受信を別のSPI呼び出しに分割しない。1回の `spi_transceive_dt()` で8バイトを送受信する。CS保持フラグは不要。GPIO CSのsetup/hold delayを指定する現在のAPIは、ローカルZephyr 4.4.99のヘッダーでも確認した。

## Zephyr側の受け入れテスト

1. 再コンフィグレーション直後、ID/version/capabilities/count、16個のDUMMYが全ゼロ、WRITE_COUNTがゼロであることを読む。
2. 各DUMMYに0、FFFFFFFF、AAAAAAAA、55555555、12345678、87654321を順番に書き、毎回READ一致を確認する。
3. 16個に異なる値を書いてから全て読み直し、アドレス間の独立性を確認する。
4. walking-oneとwalking-zeroを32 bit全ビットで試す。
5. WRITE_COUNTを開始時に読み、正常WRITEの実行数と差分が一致することを32 bit循環演算で確認する。
6. RO WRITE、未割り当てREAD/WRITE、不正opcode、非整列アドレスでstatusと値不変を確認する。
7. 必要なら短い転送（例7バイトWRITE）でNSSを解除し、対象値・WRITE_COUNTが変化しないことを確認する。
8. まず1 MHz、次に2 MHzで同じ試験を行う。ロジックアナライザで64クロック、NSS保持、setup/hold、高/低期間を確認する。

RTLシミュレーションでは1/2 MHz、タイミングジッタ、全64種類の途中中断、エラー応答、余分なクロック、MISOトライステートを含む7,512項目を通過済み。実機試験は未実施。

## 参照

- ボード資料：`../../board/rev58/rev58_pin_connections.json`、`JPARC_rev58_技術説明書_v1.docx`。
- [Zephyr SPI API](https://docs.zephyrproject.org/latest/doxygen/html/group__spi__interface.html)
- [Zephyr GPIO CS制御](https://docs.zephyrproject.org/latest/doxygen/html/structspi__cs__control.html)
- [Zephyr spi-device binding](https://github.com/zephyrproject-rtos/zephyr/blob/main/dts/bindings/spi/spi-device.yaml)
