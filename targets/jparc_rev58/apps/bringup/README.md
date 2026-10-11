# M7 bring-up

rev58専用の最小起動・周辺確認アプリです。
[ビルド・実機立ち上げ手順](../../docs/BRINGUP.md)を参照してください。

全プロファイルのスタックとBSSは内蔵RAMを使います。SDRAM試験は全8 MiBを上書きします。

Ethernet経由のMAX10 SPI4書込み／読戻し試験は `spi4_regs` プロファイル。
[操作手順](../../docs/SPI4_TEST.md)。
