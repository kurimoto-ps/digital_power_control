# J-PARC rev58 Zephyr開発

対象はSTM32H745XIH6 + MAX10 (10M16DCF484I7G)の新基板です。
このディレクトリは開発開始用の構成と設計資料を管理します。
現在はボード定義・アプリ未実装で、rev58用のビルド／書込みはまだできません。

```text
jparc_rev58/
├── apps/
│   ├── bringup/                 M7単体の起動確認（最初の実装対象）
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

既存Nucleoアプリはリポジトリ直下にあります。既存の `scripts/build.sh`
はNucleo向けです。rev58には専用ボード定義を作成します。
Zephyrと依存モジュールは既存west workspaceを共有し、この配下へコピーしません。
ボード実装時はこのディレクトリをBOARD_ROOT/DTS_ROOTに指定する構成を追加します。

[開発条件と着手順](docs/DEVELOPMENT.md)を参照してください。
提供資料はZIPから内容を変更せず展開し、技術説明書を併せて保存しています。
QSF/SDCは参考ひな形であり、Quartusプロジェクトの実装は今回の対象外です。
