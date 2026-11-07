# 12 MHz・96 kHz・TRIG入力：FPGA／STM32配線確認

対象：FPGA **v2.0.1**＋STM32 **mixed_adc**、rev58＋マザーボードTEP521C。
[English](ACC_INPUT_TEST_en.md) / [FPGA設計者の仕様（保存版）](hardware/acc_v201/ACC_INPUT_MONITOR_STM32.md)

**ビルドとホスト模擬試験まで確認済みです。今回実機には書き込んでおらず、以下の実機試験は未実施です。**

## 1. 同軸で確認できる範囲

| 信号 | マザーボード入力 | 母基板内の経路 | CN1B／rev58 J7 | MAX10 | 0x0024 | STM32 |
|---|---|---|---:|---|---:|---|
| ACC_12MHZ | **J6：光受信** | HFBR-24E6Z → AC結合・AD8561 → U27 → EXT_CLK_OPT | 75 | M9 | bit0 | 直結なし |
| ACC_96KHZ | **J16：LEMO同軸** | RA48 → U27 → EXT_CLK_LEMO | 77 | A7 | bit1 | **PA0** |
| ACC_TRIG | **J17：LEMO同軸** | RA48 → U27 → EXT_TRG_LEMO | 79 | A8 | bit2 | **PD13** |

J7とCN1Bは同じピン番号です。テストポイントは12 MHz＝TP1、96 kHz＝TP2、TRIG＝TP3です。

**J16・J17の同軸試験で確認できるのは96 kHz線とTRIG線です。12 MHz線は別の光入力J6なので、同軸2本の試験成功を12 MHz線の確認済みとして扱いません。** 12 MHz線の瞬時値はすべての診断ログに併記します。

根拠：[母基板回路図3/6・6/6](hardware/circuit/フィードバック基板_TEP521C回路図.pdf)、[コネクタ用途表p.2](hardware/circuit/Connector_usage_rev58.pdf)、[rev58ピン表](hardware/rev58/rev58_pin_tables.md)。

## 2. 実装内容と既存機能との併用

- PA0・PD13をGPIO入力として初期化。出力、内部プルアップ／プルダウン、エッジ割込みは使用しません。
- `ACC READ` コマンドがSPI4でID、VERSION、CAPABILITIESを読み、VERSION=`0x00020001`かつbit6有効を確認してから0x0024を読みます。旧版ではこのレジスタを読みません。
- 既存のSPI4 READ処理を使用し、応答status、ビッグエンディアン値、予約bit31:3＝0を確認します。読出し専用で、FPGA設定やPLC出力などを書き換えません。
- 同じコマンド内で「STM32 GPIO読取り → FPGAレジスタ読取り → STM32 GPIO再読取り」を行い、3つの観測結果を返します。SPI4は既存の単一サーバスレッドで直列処理します。
- ADCの48バイト循環DMA・ADC設定・DAC制御は変更しません。従来のv2.0にも引き続き対応し、今回のACC診断のみv2.0.1専用です。PLC 16入力／16出力、UART8、Ethernet、SDRAM 8 MiB起動試験も同じイメージに含まれます。

PA0・PD13は既存周辺ピンと重複しません。この版ではタイマ入力や外部トリガ処理へは割り当てていません。96 kHzを入れてもエッジごとの割込み負荷は発生しません。将来これらの端子を別用途へ変更する場合は、この診断との所有関係を見直します。

**観測は同時ではありません。** FPGAは120 MHzで3段同期し、SPI4のアドレス受付付近の値を返します。STM32の2ピンの読取りも順番に行います。動くクロックではFPGAとSTM32の値が違っても異常とは限りません。固定入力でも、前後値が一致するだけで途中の変化がなかったことは証明できません。

これは瞬時レベル診断です。周波数・パルス幅・エッジ回数・TRIG到来履歴は取得しません。短いTRIGを取りこぼす可能性があり、12 MHz/96 kHzのポーリングが同じ値に見え続ける場合もあります。クロックの存在や周波数はオシロスコープ等で確認します。

## 3. 準備

FPGA側へ設計者提供のv2.0.1、STM32側へ今回のmixed_adcをそれぞれ書き込む必要があります。以下のflashコマンドは実機作業者向けで、今回こちらでは実行していません。

```sh
# Zephyr開発環境、リポジトリ直下
./scripts/build.sh jparc_rev58 mixed_adc
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-mixed_adc
./scripts/flash.sh jparc_rev58 mixed_adc
```

試験PCはPython 3標準ライブラリのみ使用します。

```sh
git clone https://github.com/kurimoto-ps/digital_power_control.git
cd digital_power_control
git switch feature/jparc-rev58-zephyr-layout
# 既存cloneなら、このブランチで git pull --ff-only
cd targets/jparc_rev58/scripts
python3 spi4_client.py info
python3 acc_input_client.py read
```

FPGAの期待値はVERSION=`0x00020001`、CAPABILITIES=`0x0000007f`です。ホスト有線LANは `192.168.100.1/24`、STM32は `192.168.100.58:4244/TCP`。別IPならサブコマンドの前に `--host IP` を指定します。

GPIO初期化が完了してEthernetコマンドが応答する状態で試験を始めます。試験中は他のスクリプトによる操作を止めてください。ADC capture中は同じTCPサーバが占有されるため診断の応答が待たされます。機能は同じイメージ内で共存しますが、同時刻の並列測定を提供するものではありません。

## 4. 同軸入力の結線とレベル

- 2チャンネルの信号源等を、J16（96 kHz線）とJ17（TRIG線）へ接続します。LEMO中心が信号、外側が母基板DGNDです。
- まずクロック・パルス出力を止め、DCのLow/Highを保持できる設定にします。Lowは0 V、Highは3.3 Vを使用し、コネクタで実測してください。
- 信号源の50 Ω負荷換算表示と実際の高インピーダンス負荷で、電圧が倍になる設定に注意してください。負電圧を含む±波形やPLCの24 Vを入力しません。STM32／MAX10側の信号は3.3 Vロジックです。
- 2本とも能動的にLowへ固定し、片方を外して浮かせた状態での比較は避けます。他の信号源と同時駆動しません。
- High/Lowを切り替えて安定してから読出します。仕様上1 ms程度の待ちで十分な余裕があり、対話式試験はEnter後にも10 ms待ちます。

## 5. J16・J17の対話式マッピング試験

```sh
python3 acc_input_client.py coax-test --report board01_acc_coax.json
```

画面の指示に従って以下の7状態を設定し、安定後にEnterを押します。各状態を3回読んで判定します。

| 手順 | J16 | J17 | FPGAのbit2:1（値 & 0x6） | STM32 PA0 | STM32 PD13 |
|---|---|---|---:|---:|---:|
| 初期Low | Low | Low | 0x0 | 0 | 0 |
| 96 kHz線のみHigh | High | Low | 0x2 | 1 | 0 |
| 96 kHz線をLowへ戻す | Low | Low | 0x0 | 0 | 0 |
| TRIG線のみHigh | Low | High | 0x4 | 0 | 1 |
| TRIG線をLowへ戻す | Low | Low | 0x0 | 0 | 0 |
| 両方High | High | High | 0x6 | 1 | 1 |
| 最後に両方Low | Low | Low | 0x0 | 0 | 0 |

対応するピンだけ変わり、FPGAとSTM32の前後2回がすべて期待値と一致すれば合格です。ビット入替や意図しない他方の変化もFAILになります。

JSONの `complete: true`、`passed: true`、7件の `steps` を確認します。`tested_fpga_mask` と `tested_stm32_mask` はともに `6`、`not_tested` に12 MHz光経路が残ります。FPGAのbit0は観測ログには残りますが、同軸試験の合否から除外します。

途中で `q` またはCtrl+Cを入力すると、それまでの結果を保存します。**スクリプトは信号発生器を操作しません。** 中断時も含め、試験者が外部信号をLowへ戻してください。

## 6. 個別読出しと診断ログ

```sh
# 1回観測。signals以下に信号名・HIGH/LOW・STM32ピン名が表示される
python3 acc_input_client.py read
# 繰返し観測を保存。周波数測定ではない
python3 acc_input_client.py read --count 10 --interval 0.2 --report board01_acc_levels.json
# J16のみ固定High、J17固定Lowを期待して5回確認
python3 acc_input_client.py read --count 5 --expect 0x2 --mask 0x6 --report board01_acc_j16.json
```

`--expect` なしでは `passed: null` です。不一致を自動的に故障と判定しません。`shared_levels_equal` は観測値同士の一致だけを示します。`--expect` を使う場合は入力を固定してください。`--mask` の既定値は0x6です。`sampled_utc` はホストの記録時刻で、ハードウェアのエッジ時刻ではありません。

送受信例は `03 00 24 00 00 00 00 00` → `00 00 00 00 00 00 00 05`。このFPGA応答は12 MHz線High、96 kHz線Low、TRIG線Highです。STM32が固定状態を読んだ場合はPA0=0、PD13=1になります。

## 7. 12 MHz経路の別途確認

12 MHz線の入力元は光受信J6で、AC結合と比較器を通ります。**光を固定ON/OFFにしただけでFPGA入力を固定High/Lowにできるとは限りません。** J16へ12 MHzを入れてもbit1の経路を試すだけで、bit0の12 MHz経路の検証にはなりません。

光経路を試験する場合は設計者と信号条件を合わせ、J6へ光クロックを与え、オシロスコープ等でEXT_CLK_OPT／TP1の波形と周波数を確認し、診断ログのbit0を補助的に観測します。bit0が常に同じ値でも、瞬時値ポーリングだけでは未接続と断定できません。

設計者が、既存のU27出力との競合をなくした試験治具等でFPGA入力3本の固定レベルを保証できる場合に限り、`read --expect 0x1 --mask 0x7` 等でbit0を含む静的比較ができます。**通常の実装状態のTP1へ外部出力を直結しないでください。** 12 MHzについてはSTM32側の比較先はありません。

## 8. 結果保存と異常時

基板番号、両ファームウェアの版、STM32のGitコミット、同軸接続先、信号源・実測電圧、JSON、測定器の波形を保存します。

- `ERR ACC_REQUIRES_V2_0_1_CAP_BIT6`：FPGA版・機能bitを確認。旧版では0x0024を読んでいません。
- `ERR NOT_READY`：FPGAの設定完了状態を確認。
- `ERR FPGA ...` / SPI通信エラー：レジスタ応答異常。High/Lowの結果として扱わない。
- `ERR ACC_RESERVED_BITS`：予約bitが非ゼロ。仕様・版・通信を確認。
- 固定入力でFPGAとSTM32が不一致：PA0／PD13共有ネットの配線、接触、レベルを確認。
- クロック／短いTRIGでの不一致や変化なし：固定レベルに切り替えて配線試験を行うか、測定器で確認。

今回のホスト試験はSPI4応答・GPIO値のモデルでプロトコルと判定を検証したもので、物理配線や実際の入力電圧・波形を検証した結果ではありません。
