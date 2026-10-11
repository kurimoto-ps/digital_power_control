# ホスト単体・通信試験

リポジトリ直下で `python3 -m unittest discover -s targets/jparc_rev58/tests/unit -v`。
Python標準ライブラリとホストCコンパイラ `cc`（`CC`で変更可）を使用する。
実際のCコマンド処理を一時ディレクトリへコンパイルし、SPI仕様モデルと接続する。
テストベクトル、入力拒否、SPI失敗、読戻し不一致、FPGAエラー、TCP分割応答、
PC自動試験、WRITE_COUNT周回を確認する。Zephyrネットワーク／実機／RTL試験ではない。
