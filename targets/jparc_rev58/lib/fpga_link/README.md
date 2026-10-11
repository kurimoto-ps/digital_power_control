# MAX10 SPI4試験プロトコル

`spi4_protocol.c/.h` は8バイトフレームの構築・応答確認とTCPテキストコマンドの処理。
Zephyrに依存しない転送callbackを使い、ホスト側でも同じCコードを試験する。
SPIアクセスとCONF_DONE待ちは `apps/bringup/src/spi4_test_server.c` が担当し、単一スレッドで所有する。
SPI1〜3、運用プロトコルと状態通知は未実装。[手順](../../docs/SPI4_TEST.md)。
