# rev58の開発条件と着手順

[English version](DEVELOPMENT_en.md)

2026-10-11: M7専用ボード定義と起動・周辺確認アプリを実装。実機動作は未確認。
ビルドと初回立ち上げは [BRINGUP.md](BRINGUP.md) を参照。
出典は `hardware/rev58/` 内の実装用ハードウェア資料と技術説明書v1。

## 基板条件

| 項目 | 提供資料の条件 |
|---|---|
| MCU | STM32H745XIH6、M7/M4 |
| 電源 | Direct SMPS、VOS1、M7最大400 MHz／M4最大200 MHz |
| HSE | 外部25 MHz、PH0、bypass |
| SDRAM | FMC BANK2、16 bit、row12/column8、4 bank、8 MiB、0xD0000000 |
| Ethernet | LAN8742A、RMII、PHYから50 MHz供給、PHY address 0は初期候補 |
| SPI | SPI1はFPGA master→MCU slave受信、SPI2/3はMCU送信、SPI4は双方向 |
| 同期 | PA0外部周期、PD13トリガ。実周波数・更新周期は未確定 |
| FPGA通知 | PD11 READY、PJ11 FAULT、PD12 IRQ、PK0 HEARTBEAT |

100 MHz SDRAM／20 MHz SPIは初期の設計目標であり、実機確認済み値ではありません。
MAX10 SPI4レジスタ試験v1は上限2 MHzです。[Ethernet経由の試験](SPI4_TEST.md)では1 MHzを要求します。
SDRAM初期化前のスタック・BSS・ヒープは内蔵RAMに置きます。
PD11とPJ11はEXTI11が競合します。PC3 SPI2とPC3_C ADCを区別します。
UART8 PJ8/PJ9は送受信方向の入替確認が必要です。

## 進め方

1. 採用版H745・SMPS処理を確認済み。rev58ビルドにVOS1修正を適用。Zephyr更新時は再確認する。
2. 専用ボード定義と `apps/bringup` を実装済み。次は実機でM7単体、内蔵RAM、SWD、電源・HSE・GPIO状態を確認する。
3. SDRAM初期化、8 MiB全域、byte lane、保持、cache/DMA整合性を検証する。
4. EthernetのPHY ID／リンクとSPI各系統を低速から確認する。
5. パタン形式、通信プロトコル、検証・有効化の状態遷移を定義する。
6. M7/M4/FPGAの所有資源、IPC、メモリ配置、周期制御の分担を決め、M4を導入する。
7. 模擬I/Oで同期・異常動作・遅延とジッタを測定する。

## 未確定事項

- チャネル数、点数、数値形式・単位、更新周期、Ethernet転送プロトコル。
- 運用用SPIのmode、bit順、フレーム長、CRC、timeout、READY/IRQと受信armの順序。
  SPI4レジスタ試験v1のフレームは確定し実装済み、実機確認待ちです。
- 旧パタン入力とSDRAMの選択、切替可能な状態、異常時出力・復旧条件。
- SDRAMの実クロック・タイミング、DQM初期化条件、MPU/cacheとDMAバッファ配置。
- M4の起動方法、共有RAM、周辺所有コア、FPGA通信形式の版管理。

NucleoのPA3 ADC、PE8/PE9 PWM、固定10 kHz、TCP 4242やRPMsgプロトコルは
rev58の確定仕様ではありません。新基板への採用はそれぞれ仕様を確認して決定します。
