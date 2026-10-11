# rev58 implementation handoff

PDF/DOCX: 実装用ハードウェア資料。
rev58_pin_connections.json: U1/U5の全ネットリスト端子。
rev58_pin_tables.md: 接続済み端子一覧。
rev58_pins_template.qsf: 物理ピン割当ひな形。HDL仮名を実装に合わせる。
rev58_timing_template.sdc: 50 MHzのみ記入した不完全なタイミング制約ひな形。

ビルド・Fitter・実機動作は未確認。SPI、PHY、SDRAM初期化は採用Zephyr版で検証。回路・PCBは変更していません。
