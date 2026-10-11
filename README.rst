Digital power-control development
#################################

既存NucleoとJ-PARC rev58を、ボードごとに並列のディレクトリで開発します。
Zephyr本体と依存モジュールは、共通のwest workspaceで管理します。

.. code-block:: text

   digital_power_control/
   |-- targets/
   |   |-- nucleo_h755zi_q/     既存H755デュアルコアアプリ・ボード設定
   |   `-- jparc_rev58/         H745 + MAX10の新基板開発
   |-- docs/                   開発環境・状態・Nucleo体験ガイド
   |-- scripts/                セットアップ・ビルド・書込み
   `-- west.yml                共通のZephyr依存関係

Nucleoの構成・配線・通信仕様は ``targets/nucleo_h755zi_q/README.rst``、
rev58の構成は ``targets/jparc_rev58/README.md`` を参照してください。
rev58はディレクトリと資料の準備段階です。ボード定義・アプリは未実装です。

Nucleoのビルドと書込みは、リポジトリ直下で実行します。

.. code-block:: console

   ./scripts/build.sh nucleo_h755zi_q
   ./scripts/flash.sh nucleo_h755zi_q

引数省略時もNucleoを選択します。ビルド先は ``../build-nucleo-h755zi-q/`` です。
rev58指定時は未実装の旨を表示して終了します。

環境構築は ``docs/DEVELOPMENT.rst``、実装状況は ``docs/PROJECT_STATUS.rst``、
Nucleoの操作演習は ``docs/TRIAL_GUIDE.rst`` を参照してください。
