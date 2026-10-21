# rev58 M7のビルドと初回立ち上げ

## 現在の範囲

専用ボード `jparc_rev58/stm32h745xx/m7` とM7単体の試験アプリを用意した。
基板なしでのビルド確認用であり、起動・通信・SDRAM性能の実機合格を意味しない。
M4アプリ、運用用FPGAプロトコル、周期制御、PLC出力、UART8、ADC3、HRTIMは未実装。

ソースは `apps/bringup/`、ボード定義は `boards/jparc/jparc_rev58/`。
NucleoのLED、UART、PWM、Ethernetピン割当は継承していない。

## ビルド

リポジトリ直下で実行する。各プロファイルは一つずつ独立したビルドになる。

```sh
./scripts/build.sh jparc_rev58
./scripts/build.sh jparc_rev58 sdram
./scripts/build.sh jparc_rev58 ethernet
./scripts/build.sh jparc_rev58 spi
./scripts/build.sh jparc_rev58 spi4_regs
./scripts/build.sh jparc_rev58 combined
```

| プロファイル | 有効にする機能と実行内容 |
|---|---|
| bringup（省略時） | 内蔵Flash/RAM、RTT、クロック・電源レジスタ確認、READY/FAULT/IRQの入力ポーリング |
| sdram | 上記＋FMC SDRAM初期化、8 MiB全域を破壊するメモリ試験 |
| ethernet | 上記＋RMII MAC・LAN8742 PHY、PHY ID読出し、リンク状態表示 |
| spi | 上記＋SPI1〜4のドライバ初期化・ready確認。転送は行わない |
| combined | SDRAM 8 MiB全域の起動時試験＋Ethernet/SPI4。SDRAM合格後にTCPサーバー起動 |
| spi4_regs | 上記＋Ethernet TCPコマンドからSPI4レジスタ書込み／読戻し。[操作手順](SPI4_TEST.md) |

出力先はwest workspace内の `build-jparc-rev58-<プロファイル>/zephyr/`。
`zephyr.elf`（デバッグ用）、`zephyr.bin`、`zephyr.hex` を生成する。
`BUILD_DIR` による出力先指定にも対応する。build/flashで同じ指定を使うこと。

直接westを使う例（リポジトリ直下）:

```sh
west build -p always -b jparc_rev58/stm32h745xx/m7 \
  targets/jparc_rev58/apps/bringup -d ../build-jparc-rev58-bringup
```

別のアプリでこのボードを使う場合は、find_package(Zephyr)より前に
`targets/jparc_rev58` を `BOARD_ROOT` と `DTS_ROOT` に追加する。

## 電源・クロックとZephyrへの局所修正

- HSE: PH0外部発振器25 MHz、bypass。
- PLL1: M=5、N=160、P=2、Q=8、R=2。VCO 800 MHz、SYSCLK 400 MHz。
- AHB: 200 MHz。APB1/2/3/4: 100 MHz。
- Direct SMPS、VOS1。LSE/RTCは未有効化。
- M7 Flashは0x08000000から1 MiB。残りのFlash bankは将来のM4用に予約。
- 通常データ・BSS・スタックはAXI SRAM 0x24000000の512 KiB。
- 起動確認ではD-cacheを無効にし、デバッガとSDRAM試験が実メモリを参照する。
  これは運用時の性能・cache整合性設計ではない。

採用Zephyr commit `d9c3a39174fdc96b5177faa41c62b056c224512b` の
`drivers/clock_control/clock_stm32_ll_h7.c` は、prepare/optimizeでVOS0を指定する。
H745向けHALマクロはSYSCFG.PWRCR.ODENをセットするため、周波数設定だけでは
Direct SMPS条件を満たさない。

ボードのCMake処理で元ファイルのSHA256を確認し、VOS0指定をVOS1に置換した
ソースを**ビルドディレクトリ内**に生成してコンパイルする。
HALのVOS1経路はODENをクリアする。Flash latencyは既存LL処理がVOS1とHCLKから設定する。
元のZephyrやNucleoのビルドには修正を適用しない。
将来Zephyrを更新しハッシュが変わった場合はビルドを停止する。
その際は電源・クロック処理を再確認して修正の必要性を判断する。

## 初回書込み前とデバッグ

1. J4のSWDIO/SWCLK/NRST/GNDとターゲット電圧参照を外付けデバッガへ接続する。
   初期runnerはST-LINK＋OpenOCD、SWD 1 MHz、connect-under-reset。
2. STM32の識別、実装電源、Flash起動設定を確認する。
3. **M4の自動起動をoption byteのBCM4で無効にする。**
   `CONFIG_STM32H7_DUAL_CORE=n` はM7からの起動処理を外すだけで、
   option byteで起動したM4を停止するものではない。
   アプリはBCM4を確認して不一致を記録するが、それは起動後の診断である。
   option byteを自動変更する処理は入れていない。
4. 最初は `bringup` イメージを使う。

```sh
./scripts/flash.sh jparc_rev58
west debug -d ../build-jparc-rev58-bringup
```

OpenOCD/ST-LINKでの実接続は未確認。STM32CubeProgrammerを使う場合は
`west flash -d ../build-jparc-rev58-bringup -r stm32cubeprogrammer`。

デバッガから `rev58_status` を表示する。
`magic=0x52563538`、`error=0`、`ticks` が1秒ごとに増えることを確認する。
SYSCLK/HCLK、PWR.CR3、PWR.D3CR、SYSCFG.PWRCR/UR1も保存される。
RTT対応のデバッグクライアントでも起動メッセージを読める。
UARTコンソールやUSB仮想シリアルは用意していない。

READY/FAULT/IRQはraw値を読むだけで、信号の意味や正常状態は判定しない。
PD11/PJ11のEXTI11競合を避け、割込みは使用しない。
HEARTBEAT、nCONFIG要求、PLC、PWMはアプリから駆動しない。
起動前・リセット中の端子状態は基板の抵抗やMCUのリセット状態も含めて実機で確認する。

## 周辺プロファイルの確認範囲

### SDRAM

FMC BANK2、16 bit、row12/column8、4 bank、0xD0000000〜0xD07FFFFF。
HCLK 200 MHzからSDCLK 100 MHz、CL3、refresh count 1542、
タイミング `<2 8 5 7 3 2 2>` は提供資料に基づく初期案。
mode 0x230（BL1、CL3、single write）、起動待ち200 us、8回auto-refresh。

試験は先頭32 bitのwalking 1/0、全域アドレスパターンと反転パターン、
1秒保持、全halfwordの上下byte独立書込みを実施する。
`rev58_memtest.stage=5` はこの試験の合格。失敗はアドレス・期待値・実値を保存する。
DMA、温度、電源変動、長期保持、負荷時タイミングは別途試験が必要。
未使用DQM2/3の初期化条件と実装U2のJ品を確認する。

### Ethernet

TX_ENはPB11（旧ネットラベルPG11を流用しない）。LAN8742A、PHY address 0は
ストラップからの初期候補で、実機でIDを照合する。PHYリセットはNRST共通。
Ethernet DMA用SRAM3はZephyrのSTM32H7 SoC定義を使用し、AXI SRAMと分ける。

試験用のランダムMACを使用し、このプロファイルでHSI48とハードウェアRNGを有効化する。
ネットワークスタック用にUDPを有効にするが、UDPアプリや転送処理は実装していない。
この `ethernet` プロファイルには固定IP・転送アプリを設定していない。
`spi4_regs` は固定IPのTCP試験を提供する。パタン転送プロトコルと製品MAC割当は未実装。
PHY IDとリンクの確認だけで転送の合格とはしない。

採用ZephyrのLAN8742ドライバには、リセット後の広告設定が要求値と一致すると
`-EALREADY` を初期化失敗として返す問題がある。実機ではPHYアドレス0から
ID `0x0007C131` とリンク状態を読める一方、デバイスのinit_resが120となり、
アプリの `Peripheral check failed: -19` でIP設定前に終了した。
ボード限定・SHA256照合付きの生成ソースで、初期化時の `-EALREADY` だけを許容し、
通常のリンク監視開始まで進める。他のエラー処理と共有Zephyrソースは変更しない。

### SPI

SPI1: FPGA master→MCU slave受信用の配線。
SPI2/3: MCU master送信用。SPI4: 双方向、レジスタ試験v1はMCU master。
SPI1〜3にはMISOを追加しない。NSSはAFで定義し、cs-gpiosは指定しない。
採用版では `CONFIG_SPI_SLAVE` と `spi_config.operation` でslaveを選択し、
cs-gpios/st,soft-nssがなければハードウェアNSSを使う。
資料の旧版用 `CONFIG_SPI_STM32_USE_HW_SS` は採用版には存在しない。

`spi` プロファイルはドライバとピン設定の準備のみ。NSS極性、CPOL/CPHA、フレーム長、
受信armとREADY/IRQの順序を決めてから転送アプリを追加する。
SPI1〜3のkernel clockは100 MHzなので通常の2冪分周で20 MHzぴったりにはならない。
SPI4はHSE 25 MHzを選択する。採用ZephyrのH745 clock driverではPCLK2を
domain sourceとして指定すると `-ENOTSUP` となるため、対応済みのHSEを使う。
SPI1〜3の目標20 MHzへのクロック設計は別途行う。
`spi4_regs` は別のoverlayでPE4をGPIO CSに変更し、指定1 MHzで8バイト転送する。
MAX10レジスタ試験v1の上限は2 MHzであり、20 MHzを適用しない。[詳細](SPI4_TEST.md)。

## 基板なしの検証

各プロファイルをビルド後、リポジトリ直下で次を実行する。
（Zephyr環境のPython、pyelftoolsを使用。自分で生成したビルド成果物だけを入力する。）

```sh
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-bringup
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-sdram
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-ethernet
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-spi
```

生成Devicetreeの電源・クロック・SDRAM条件、ELFのRAM配置、VOS1修正のコンパイル、
有効なFMC/RMII/SPI端子のAF・重複・提供ネットリストの接続先を検査する。
この検査は実配線や電気的動作を検証するものではない。
`CONFIG_ASSERT=y` によりビルド中に表示されるassert有効化のCMake警告は意図した設定。
