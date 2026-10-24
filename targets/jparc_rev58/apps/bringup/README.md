# M7 bring-up

rev58専用の最小起動・周辺確認アプリです。
[ビルド・実機立ち上げ手順](../../docs/BRINGUP.md)を参照してください。

全プロファイルのスタックとBSSは内蔵RAMを使います。SDRAM試験は全8 MiBを上書きします。

Ethernet経由のMAX10 SPI4書込み／読戻し試験は `spi4_regs` プロファイル。
[操作手順](../../docs/SPI4_TEST.md)。

SDRAMとEthernet/SPI4の同時有効化は `./scripts/build.sh jparc_rev58 combined`（リポジトリ直下）。
起動時のSDRAM全域試験に合格するとSPI4用TCPサーバーを開始します。

FPGA v1.3の3系統ダミーADC受信は `adc_dummy` プロファイルと `scripts/adc_dummy_client.py` を使用します。[手順](../../docs/ADC_DUMMY_TEST.md)。
