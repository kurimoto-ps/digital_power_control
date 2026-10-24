# rev58開発コマンド

`build.sh [bringup|sdram|ethernet|spi|spi4_regs|combined|adc_dummy]` と `flash.sh [同プロファイル]`。
リポジトリ直下の `scripts/build.sh jparc_rev58 [プロファイル]` からも実行できます。
出力はwest workspaceの `build-jparc-rev58-<プロファイル>`。
詳細は[立ち上げ手順](../docs/BRINGUP.md)を参照してください。

Ethernet経由のMAX10 SPI4書込み／読戻し試験は `spi4_regs` プロファイル。
[操作手順](../docs/SPI4_TEST.md)。

SDRAMとEthernet/SPI4の同時有効化は `./scripts/build.sh jparc_rev58 combined`（リポジトリ直下）。
起動時のSDRAM全域試験に合格するとSPI4用TCPサーバーを開始します。

FPGA v1.3の3系統ダミーADC受信は `adc_dummy` プロファイルと `scripts/adc_dummy_client.py` を使用します。[手順](../docs/ADC_DUMMY_TEST.md)。
