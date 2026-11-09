# 追加の確認試験

J-PARC rev58／マザーボードTEP521C — PLC・UART、ACC入力、混在ADC／DAC

[English](ADDITIONAL_TESTS_en.md)

**MAX10のファームウェアをv2.0.2へ更新した後に実施してください。** MAX10の更新方法は別途提供する資料を参照してください。本書にはその書込み手順を含めません。

以前のADC・DAC試験を実施済みで、Git clone済みのリポジトリと同じDocker開発環境をお持ちの試験者向けです。共通準備はこのページで一度だけ行い、以下の順に進めてください。各試験の間でSTM32を書き直す必要はありません。

## 試験の概要と順番

| 順番・手順へのリンク | 確認すること | 使用する接続・機材 |
|---|---|---|
| **1. [PLC_UART_TEST：PLC・UART](PLC_UART_TEST.md)** | PLC→STM32の16入力、STM32→PLCの16出力、RS232Cの双方向通信 | PLC、母基板J3/J4、J5 RS232C接続と外部端末（115200 bps、8N1） |
| **2. [ACC_INPUT_TEST：ACC入力](ACC_INPUT_TEST.md)** | 96 kHz／TRIG線の固定High/LowとSTM32・MAX10の対応、12 MHzの開始／停止検出 | J16/J17同軸への0/3.3 V信号源、**12 MHzは別のJ6光入力**、波形確認用計測器 |
| **3. [MIXED_ADC_V2：混在ADCとDAC連動](MIXED_ADC_V2.md)** | 48バイト・11 CHの受信、ADC24ダミー検査、新ADCの光リンク調整、APPLYとDAC連動 | ADC／O/E・E/O系統、既知のアナログ入力、DAC出力計測器 |

12 MHzはJ16/J17の同軸入力だけでは試験できません。ACC試験の96 kHz／TRIGは配線の瞬時値確認であり、周波数測定やトリガー履歴の検査ではありません。

今回のSTM32 `mixed_adc` には、上記機能に加えEthernet、SPI4、SPI1循環DMA、SDRAM 8 MiB起動試験が含まれます。M7で動作し、M4は引き続き無効です。今回の追加機能はビルド・ホスト試験まで確認済みで、本書はこれから行う実機確認の手順です。

## 共通準備1：Gitから最新版を取得する

既存Docker開発環境のターミナルで実行します。以下は既存の配置を前提とします。

```sh
cd /workdir/zephyrproject/digital_power_control
git status --short
```

**何も表示されなければ**、続けて次を実行します。`M`、`??`などの行が表示された場合は、編集・追加したファイルがあります。先に下の「ローカル編集がある場合」に進んでください。

```sh
git switch feature/jparc-rev58-zephyr-layout
git pull --ff-only origin feature/jparc-rev58-zephyr-layout
git rev-parse --short HEAD
```

1行ずつ実行し、エラーが出たら次へ進まないでください。`Already up to date.` は更新不要という正常な結果です。最後に表示されるコミット番号を試験記録に残します。

### ローカル編集がある場合／編集が原因でpullできない場合

`Your local changes ... would be overwritten` は、手元の編集を上書きしないためにGitが更新を止めた状態です。ファイルを削除する必要はありません。

1. 大切な編集ファイル・試験ログを、まずリポジトリ外の保存先へコピーしてください。特にGit管理対象外のログも含めて保管します。
2. 同じリポジトリ直下で、手元の編集を一時退避します。

   ```sh
   git stash push -u -m "before-additional-tests"
   git stash list
   git status --short
   ```

   `stash` は編集内容を手元に保存して作業ファイルを元に戻す操作です。`-u` は新規ファイルも含めますが、Gitが無視するファイルは含みません。`stash list` に `before-additional-tests` が表示され、`status --short` が空になったことを確認してください。退避内容はGitHubへ送られないため、保存した環境を削除しないでください。
3. 上の `git switch`、`git pull --ff-only`、`git rev-parse` を実行します。**今回の試験が終わるまでは編集を戻さず、配布版の状態で準備・試験してください。**
4. 試験後に編集を戻したい場合は、まず `git stash list` で対象を確認します。`before-additional-tests` が `stash@{0}` にある場合に限り、次で戻せます。

   ```sh
   git stash apply 'stash@{0}'
   ```

   `apply` は退避内容を消さずに戻します。別の番号にある場合や `CONFLICT` と表示された場合は、そこで止めて担当者へ相談してください。編集を戻した後のソフトは、今回試験した配布版とは異なります。

`Not possible to fast-forward`、ブランチの分岐、競合などが表示された場合は、自分でコミットした履歴などの確認が必要です。stashだけでは解決しません。エラー全文と次の結果を担当者へ送ってください。強制更新やファイル削除は行わないでください。

```sh
git status
git log --oneline -5
```

認証・通信エラーの場合も、エラー全文を連絡してください。ローカル編集の退避では認証・通信エラーは直りません。

## 共通準備2：STM32用ソフトを一度だけ更新する

MAX10 v2.0.2への更新が済んでいることを確認します。既存のST-LINK接続・電源条件で、リポジトリ直下から次を**1行ずつ、成功を確認して**実行してください。

```sh
./scripts/build.sh jparc_rev58 mixed_adc
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-mixed_adc
./scripts/flash.sh jparc_rev58 mixed_adc
```

以前の同名イメージではなく、今回取得したソースから生成したイメージを使用します。出力は `../build-jparc-rev58-mixed_adc/zephyr/zephyr.hex` です。M4/BCM4無効、M7/BCM7有効の従来設定を前提とします。エラーがあれば先に進まず、そのログを保存してください。

## 共通準備3：Ethernet接続とバージョンの確認

以前の試験と同じ有線LAN接続を使用します。ホスト側は `192.168.100.1/24`、STM32は `192.168.100.58`、TCPポートは `4244` です。Python 3標準ライブラリのみ使用し、追加pipパッケージは不要です。

```sh
cd /workdir/zephyrproject/digital_power_control/targets/jparc_rev58/scripts
python3 spi4_client.py info
```

次の値を確認してください。

| 項目 | 期待値 |
|---|---|
| device_id | `0x4a503538` |
| version | `0x00020002`（v2.0.2） |
| capabilities | `0x000000ff` |

応答がない、または値が異なる場合は試験を開始せず、接続・書込み版を確認してください。別IPを使う場合は各スクリプトのサブコマンドより前に `--host IP` を付けます。

**以降の各試験ページのコマンドは、この `scripts` ディレクトリで実行します。** ターミナルを開き直したときは上記の `cd` で戻ってください。ホスト操作は1つずつ実行し、別ターミナルから同時操作しないでください。

準備ができたら **[1. PLC・UART試験へ進む](PLC_UART_TEST.md)**。各ページ末尾の「次の試験」で順番に進めます。

## 試験記録のまとめ

基板識別、実施日、担当者、Gitコミット番号、FPGAバージョンを記録し、各手順で生成したJSONと外部端末・計測器の結果を保管してください。JSONだけで外部のPLC表示やDACアナログ電圧まで合格と判断しないでください。

- PLC：16入力・16出力の対応、終了時の全出力Low確認。UART：受信データと外部端末での送信データ確認。
- ACC：J16/J17の対応と、J6光12 MHzの停止→開始→停止結果を別々に記録。機材がなく未実施の場合は未実施と明記。
- 混在ADC：ダミー検査、各入力とCHの対応、光リンク調整値、APPLY結果、DAC連動時の実測電圧。

ログは次回のGit操作前にリポジトリ外にもコピーしてください。不合格・未実施を含めて記録し、終了時は各試験ページの手順に従って出力を確認してください。
