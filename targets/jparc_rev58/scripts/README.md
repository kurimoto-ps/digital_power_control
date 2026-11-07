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

FPGA v2.0用には `build.sh mixed_adc` と `mixed_adc_client.py` を使用します。
48バイト連続DMA試験、APPLY完了確認、光リンク位相・遅延調整、DAC連動については
[日本語](../docs/MIXED_ADC_V2.md) / [English](../docs/MIXED_ADC_V2_en.md) を参照してください。
v1.3用 `adc_dummy` は維持しています。

`board_io_client.py` はPLCの32点対話式マッピング試験と母基板RS232Cの送受信試験用です。
[日本語手順](../docs/PLC_UART_TEST.md) / [English](../docs/PLC_UART_TEST_en.md)。

`acc_input_client.py` はFPGA v2.0.1のACC入力とSTM32 PA0/PD13の瞬時レベル診断用です。
[同軸試験手順](../docs/ACC_INPUT_TEST.md) / [English](../docs/ACC_INPUT_TEST_en.md)。
