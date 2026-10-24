# ホスト単体・通信試験

リポジトリ直下で `python3 -m unittest discover -s targets/jparc_rev58/tests/unit -v`。
Python標準ライブラリとホストCコンパイラ `cc`（`CC`で変更可）を使用する。
実際のCコマンド処理を一時ディレクトリへコンパイルし、SPI仕様モデルと接続する。
テストベクトル、入力拒否、SPI失敗、読戻し不一致、FPGAエラー、TCP分割応答、
PC自動試験、WRITE_COUNT周回を確認する。Zephyrネットワーク／実機／RTL試験ではない。

ADC検査は仕様のCRC既知ベクトル、3系統の順序入替、通番周回、分割入力、再同期、
値違い／重複／欠落と、ホストの結果検査・TCP分割JSON応答を含む。
SPI4にはv1.3互換、CONTROL clear bit、DAC完了ポーリングの模擬試験を追加した。
