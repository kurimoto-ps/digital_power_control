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
rev58はM7単体の専用ボード定義と起動・周辺確認アプリを実装しています。実機動作は未確認です。

Nucleoのビルドと書込みは、リポジトリ直下で実行します。

.. code-block:: console

   ./scripts/build.sh nucleo_h755zi_q
   ./scripts/flash.sh nucleo_h755zi_q

引数省略時もNucleoを選択します。ビルド先は ``../build-nucleo-h755zi-q/`` です。
rev58の最小アプリは ``./scripts/build.sh jparc_rev58``、
周辺確認は末尾に ``sdram``、``ethernet``、``spi`` を指定します。
Ethernet経由のMAX10レジスタ試験は ``spi4_regs`` を指定します。
SDRAMとの同時使用は ``combined`` を指定します。
操作は ``targets/jparc_rev58/docs/SPI4_TEST.md`` を参照してください。
手順と初回書込み条件は ``targets/jparc_rev58/docs/BRINGUP.md`` を参照してください。

環境構築は ``docs/DEVELOPMENT.rst``、実装状況は ``docs/PROJECT_STATUS.rst``、
Nucleoの操作演習は ``docs/TRIAL_GUIDE.rst`` を参照してください。

FPGA v1.3のADCダミー送信をSPI1の循環DMAで受信する試験は
``./scripts/build.sh jparc_rev58 adc_dummy`` でビルドします。
ホスト操作は ``targets/jparc_rev58/docs/ADC_DUMMY_TEST.md`` を参照してください。
