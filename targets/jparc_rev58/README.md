# J-PARC rev58 Zephyr開発

対象はSTM32H745XIH6 + MAX10 (10M16DCF484I7G)の新基板です。
専用ボード定義とM7単体の起動・周辺確認アプリを管理します。
ビルド検証は可能ですが、実機動作は未確認です。M4と製品アプリは未実装です。

```text
targets/jparc_rev58/
├── apps/
│   ├── bringup/                 M7起動確認＋SDRAM/Ethernet/SPIプロファイル
│   ├── m7/                      Ethernet・パタン管理の予定領域
│   └── m4/                      役割決定後のM4アプリ
├── boards/jparc/jparc_rev58/     独自ボード定義
├── lib/
│   ├── pattern/                 パタン検証・保存・有効化
│   ├── fpga_link/               MAX10 SPI・状態通知
│   ├── synchronization/         外部同期・サイクル境界
│   ├── plc/                     PLC入出力
│   └── control/                 設備固有の制御演算
├── include/jparc/               共通API・通信形式
├── dts/bindings/                独自binding
├── scripts/                     rev58用開発コマンド
├── tests/{unit,hardware}/       ロジック試験・実機試験
└── docs/
    ├── hardware/rev58/          提供資料とSHA256SUMS
    ├── measurements/            実機測定記録
    └── DEVELOPMENT.md           条件・未確定事項・着手順
```

既存Nucleoアプリは隣の `../nucleo_h755zi_q/` にあります。
リポジトリ直下の `scripts/build.sh` は引数省略時にNucleoをビルドします。rev58は `./scripts/build.sh jparc_rev58` でビルドします。
Zephyrと依存モジュールは既存west workspaceを共有し、この配下へコピーしません。
起動確認アプリはこのディレクトリをBOARD_ROOT/DTS_ROOTに指定しています。

[開発条件と着手順](docs/DEVELOPMENT.md)を参照してください。
提供資料はZIPから内容を変更せず展開し、技術説明書を併せて保存しています。
QSF/SDCは参考ひな形であり、Quartusプロジェクトの実装は今回の対象外です。

[ビルド・初回立ち上げ手順](docs/BRINGUP.md)に、電源初期化の修正と実機確認事項を記載しています。

Ethernet経由のMAX10 SPI4書込み／読戻し試験は `spi4_regs` プロファイル。
[操作手順](docs/SPI4_TEST.md)。

SDRAMとEthernet/SPI4の同時有効化は `./scripts/build.sh jparc_rev58 combined`（リポジトリ直下）。
起動時のSDRAM全域試験に合格するとSPI4用TCPサーバーを開始します。
