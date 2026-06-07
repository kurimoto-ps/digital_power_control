VS Code devcontainer tutorial
#############################

.. note::

   このチュートリアルは、VS Codeでdevcontainerを開いた後から開始します。
   VS Codeの起動方法とdevcontainerへ入るまでの手順は、利用環境に合わせて
   この文書の前へ追記してください。

目的
****

このチュートリアルでは、NUCLEO-H755ZI-Q向けデュアルコア・デジタル電源制御環境を使い、
ソース確認、ビルド、書き込み、動作確認、PWM・ADCコード変更までを行います。

この環境では、Cortex-M7がEthernetとTCPサーバーを担当し、Cortex-M4がPWM、ADC、
feedback処理を担当します。通常、顧客が変更するコードは
``control_core/customer_code`` 内にあります。

安全上の注意
************

このチュートリアル中は、PWM出力を実際の電力変換回路やゲートドライバへ接続しないでください。
オシロスコープ等の測定器だけを接続して確認します。

* A0へ入力できる電圧は0..3.3 Vです。
* 外部電圧源のGND/COMとNucleoのGNDを必ず共通接続してください。
* A0へ負電圧、3.3 Vを超える電圧、電源回路の高電圧を直接入力しないでください。
* PE9とPE8は相補PWMです。0%または100% dutyでも片側出力がHighになる場合があります。
* 実電力回路には、TIM1 Break入力、過電流保護、ゲートドライバのインターロック等が必要です。

.. figure:: images/tutorial/01-board-overview.jpg
   :alt: NUCLEO-H755ZI-Q全体
   :width: 100%

   NUCLEO-H755ZI-Q全体。A0、GND、Ethernet、ST-LINK USB、PE9、PE8の位置を確認します。

1. ワークスペースを確認する
***************************

VS Codeでdevcontainerを開いた状態で、メニューから
``Terminal`` -> ``New Terminal`` を選択します。

.. figure:: images/tutorial/02-vscode-devcontainer.png
   :alt: VS Codeでdevcontainerを開いた画面
   :width: 100%

   VS Codeでdevcontainerを開き、下部にターミナルを表示した状態。

ターミナルで次を実行します。

.. code-block:: console

   cd /workdir/zephyrproject/digital_power_control
   west list -f "{name}: {path} @ {revision}" | head

チュートリアル開始時の標準設定は、PWM最大周波数が20000 Hz、
ADC表示フルスケールが3300 mVです。演習を最初から行う場合は、次の値になっていることを
指導担当者が確認してください。

.. code-block:: c

   #define MAX_FREQUENCY_HZ 20000U
   #define ADC_INPUT_FULL_SCALE_MV 3300U

主なディレクトリは次のとおりです。

.. code-block:: text

   digital_power_control/
   |-- network_core/                 M7: Ethernet、TCP、RPMsg
   |-- shared/                       M7/M4共通プロトコル
   |-- control_core/
   |   |-- platform/                 M4基盤: DMA、割り込み、IPC、安全監視
   |   `-- customer_code/            顧客編集領域
   |       |-- pwm/                  PWM設定
   |       |-- adc/                  ADC値の換算
   |       `-- feedback/             feedback演算
   `-- scripts/                      ビルド、書き込み、環境復旧

2. 初回ビルド
*************

リポジトリのルートで次を実行します。

.. code-block:: console

   ./scripts/build.sh

このスクリプトはビルドディレクトリをクリーンにして、M7とM4を同時にビルドします。
成功すると次のバイナリが生成されます。

.. code-block:: text

   /workdir/zephyrproject/build-digital-power-control/digital_power_control/zephyr/zephyr.bin
   /workdir/zephyrproject/build-digital-power-control/control_core_m4/zephyr/zephyr.bin

生成物を確認します。

.. code-block:: console

   ls -lh \
     /workdir/zephyrproject/build-digital-power-control/digital_power_control/zephyr/zephyr.bin \
     /workdir/zephyrproject/build-digital-power-control/control_core_m4/zephyr/zephyr.bin

.. figure:: images/tutorial/03-build-success.png
   :alt: M7とM4のビルド成功画面
   :width: 100%

   VS CodeターミナルでM7とM4のビルドが成功した状態。

3. 基板への書き込み
*******************

NUCLEO-H755ZI-QのST-LINK USB端子をPCへ接続し、次を実行します。

.. code-block:: console

   ./scripts/flash.sh

このスクリプトは、直前に生成したデュアルコア・ビルドを書き込みます。
書き込みに失敗する場合は、USB接続、USBデバイスのdevcontainerへの公開、
ST-LINKの認識状態を確認してください。

.. figure:: images/tutorial/04-flash.png
   :alt: 基板への書き込み成功画面
   :width: 100%

   ``scripts/flash.sh`` による基板への書き込み成功画面。

4. EthernetとTCPコマンドの確認
******************************

NucleoのEthernet端子をPCまたは同じネットワークへ接続します。
PC側Ethernetインターフェースには、例として ``192.168.100.1/24`` を設定します。
基板の固定IPアドレスは ``192.168.100.2`` です。

VS Codeのターミナルは、Linuxコマンドを入力するための画面です。
ここでは混乱を避けるため、ターミナルを2つに分けます。

* **ビルド用ターミナル**: ``scripts/build.sh`` や ``scripts/flash.sh`` などのLinuxコマンドを実行します。
* **基板操作用ターミナル**: ``nc`` を実行して基板へ接続し、PWM操作コマンドを送ります。

VS Codeターミナル右上の ``+`` ボタンを押して、新しいターミナルを開きます。
``nc`` はnetcatという通信確認用プログラムです。このチュートリアルでは、
VS Codeターミナルから基板のTCPサーバーへ接続するために使います。
新しいターミナルを基板操作用として使い、次を入力します。

.. code-block:: console

   nc 192.168.100.2 4242

接続に成功すると、次の案内が表示されます。

.. code-block:: text

   NUCLEO-H755ZI-Q complementary PWM server; send HELP

この表示後は、基板操作用ターミナルが ``nc`` 接続中です。
このターミナルへ入力した文字はLinuxシェルではなく、Ethernet経由で基板へ送信されます。
プロンプトが表示されなくても正常です。次の基板コマンドを1行ずつ入力し、毎回Enterを押します。

.. code-block:: text

   STATUS
   GET
   MODE FEEDFORWARD
   SET 20000 25 0
   GET
   OFF

``SET`` の引数は、順番に周波数Hz、duty percent、dead time nsです。
``OFF`` を入力するとPWMを停止します。

基板操作用ターミナルで ``Ctrl+C`` を押すと ``nc`` 接続が終了し、通常のLinuxプロンプトへ戻ります。
再接続する場合は、同じターミナルで再度 ``nc 192.168.100.2 4242`` を実行します。
ビルドや書き込みは、 ``nc`` 接続中ではないビルド用ターミナルで実行してください。

.. figure:: images/tutorial/05-network-command.png
   :alt: ncで基板へTCP接続した画面
   :width: 100%

   VS Codeターミナル内で ``nc`` を実行し、基板へコマンドを送信している状態。

5. PWM波形を確認する
********************

PWM出力は次のピンです。

* High-side: TIM1_CH1、PE9
* Complementary low-side: TIM1_CH1N、PE8

オシロスコープのGNDをNucleo GNDへ接続し、PE9とPE8を測定します。
実電力回路やゲートドライバは接続しません。

基板操作用ターミナルが ``nc`` 接続中であることを確認し、次を入力します。

.. code-block:: text

   MODE FEEDFORWARD
   SET 10000 25 500
   GET

期待する主な内容は、周波数約10 kHz、high-side duty約25%、相補出力、
dead time約500 nsです。測定後はPWMを停止します。

.. code-block:: text

   OFF

.. figure:: images/tutorial/06-pwm-waveform.jpg
   :alt: 相補PWM波形
   :width: 100%

   PE9とPE8で測定した相補PWM波形。

6. PWMコード変更演習
********************

ここでは、顧客製品で使用できる最大PWM周波数を20 kHzから10 kHzへ制限します。
VS Code Explorerから次のファイルを開きます。

.. code-block:: text

   control_core/customer_code/pwm/pwm_control.c

次の定義を探します。

.. code-block:: c

   #define MAX_FREQUENCY_HZ 20000U

これを次のように変更して保存します。

.. code-block:: c

   #define MAX_FREQUENCY_HZ 10000U

.. figure:: images/tutorial/07-edit-pwm-limit.png
   :alt: VS CodeでPWM上限を編集する画面
   :width: 100%

   顧客編集領域の ``MAX_FREQUENCY_HZ`` をVS Codeで変更します。

ファイルを保存したら、ビルド用ターミナルで再ビルド、再書き込みします。

.. code-block:: console

   ./scripts/build.sh
   ./scripts/flash.sh

書き込み後は基板操作用ターミナルで ``nc 192.168.100.2 4242`` を再実行し、変更結果を確認します。

.. code-block:: text

   MODE FEEDFORWARD
   SET 20000 25 0
   SET 10000 25 0
   GET
   OFF

最初のSET 20000は範囲外としてエラーになり、SET 10000は成功します。
製品仕様で20 kHzが必要な場合は、確認後にMAX_FREQUENCY_HZを20000Uへ戻します。

7. ADCとfeedbackの確認
**********************

ADC入力にはArduinoアナログヘッダ ``A0`` を使います。

.. code-block:: text

   外部DC電源 +   -> Nucleo A0
   外部DC電源 COM -> Nucleo GND

外部DC電源は0 Vから開始し、3.3 Vを超えないようにします。
特に、外部DC電源のCOMとNucleo GNDの共通接続を確認してください。

.. figure:: images/tutorial/08-adc-wiring.jpg
   :alt: ADC入力と共通GNDの配線
   :width: 100%

   外部DC電源をA0へ接続します。外部DC電源のCOMとNucleo GNDを必ず共通接続します。

基板操作用ターミナルが ``nc`` 接続中であることを確認し、次を入力します。

.. code-block:: text

   MODE FEEDFORWARD
   SET 10000 50 0
   MODE FEEDBACK
   GET

ADC入力電圧を変えながら ``GET`` を実行します。標準設定での期待値は次のとおりです。

.. list-table::
   :header-rows: 1

   * - A0入力
     - adc表示
     - feedback duty
   * - 0.0 V
     - 約0 mV
     - 約0%
   * - 1.4 V
     - 約1400 mV
     - 約42%
   * - 1.65 V
     - 約1650 mV
     - 約50%
   * - 3.3 V
     - 約3300 mV
     - 約100%

ADCはTIM6から10 kHzでトリガされます。DMAと割り込み処理は
``control_core/platform`` にあり、顧客のfeedbackコードとは分離されています。

8. ADCスケーリング変更演習
**************************

ここでは、ADCのraw値をmV表示へ変換するフルスケール設定を変更します。
この変更は ``GET`` の``adc``表示へ影響しますが、現在のpass-through feedback dutyは
raw値から計算するため変わりません。

VS Code Explorerから次のファイルを開きます。

.. code-block:: text

   control_core/customer_code/adc/adc_input.h

次の定義を探します。

.. code-block:: c

   #define ADC_INPUT_FULL_SCALE_MV 3300U

動作を分かりやすく確認するため、一時的に次へ変更して保存します。

.. code-block:: c

   #define ADC_INPUT_FULL_SCALE_MV 3000U

.. figure:: images/tutorial/09-edit-adc-scale.png
   :alt: VS CodeでADCスケーリングを編集する画面
   :width: 100%

   顧客編集領域の ``ADC_INPUT_FULL_SCALE_MV`` をVS Codeで変更します。

再ビルド、再書き込みします。

.. code-block:: console

   ./scripts/build.sh
   ./scripts/flash.sh

A0へ約1.4 Vを入力し、feedbackモードで ``GET`` を実行します。

.. code-block:: text

   MODE FEEDFORWARD
   SET 10000 50 0
   MODE FEEDBACK
   GET

標準の3.3 V基準で約1.4 Vに相当するraw値を入力している場合、表示される ``adc`` は
約1270 mVになります。一方、feedback dutyは引き続き約42%です。

これは、次の責務が分離されているためです。

* ``customer_code/adc`` : raw値を電圧・電流などの物理量へ換算する
* ``customer_code/feedback`` : raw値または物理量からPWM dutyを計算する
* ``customer_code/pwm`` : PWMの制限、周期、dead time、出力を扱う
* ``control_core/platform`` : 10 kHzトリガ、DMA、割り込み、IPCを扱う

確認後は、ADC_INPUT_FULL_SCALE_MVを3300Uへ戻すか、実際に測定したVDDA値へ設定します。
製品では、分圧比、電流検出アンプのゲイン、オフセット、校正値等を考慮した換算処理へ変更します。

9. 変更後の最終確認
*******************

製品で使用する設定値へ戻した、または意図した値へ変更した状態で、最終確認を行います。

.. code-block:: console

   ./scripts/build.sh
   ./scripts/flash.sh

書き込み後、基板操作用ターミナルで ``nc 192.168.100.2 4242`` を再実行します。
接続成功メッセージが表示されたら、次を入力して確認します。

.. code-block:: text

   STATUS
   MODE FEEDFORWARD
   SET 10000 25 500
   GET
   MODE FEEDBACK
   GET
   OFF

確認項目:

* M7とM4の両方がビルドできる
* ``STATUS`` で ``rpmsg_bound=1`` が表示される
* feedforwardモードでは ``SET`` のdutyが使用される
* feedbackモードではA0入力に応じてdutyが変化する
* ``OFF`` でPWMが停止する
* ``fault=0x00000000`` である

.. figure:: images/tutorial/10-final-verification.jpg
   :alt: 最終動作確認
   :width: 100%

   ADC入力とPWM出力の最終動作確認。

トラブルシューティング
**********************

``MODE FEEDBACK`` が ``ERR cannot set mode (-19)`` になる
================================================================================

M4側のADC/DMA初期化に失敗しています。最新のM4/M7バイナリを両方書き込み、
ビルドが成功していることを確認してください。

ADC値が0から3300 mV付近まで不規則に変動する
================================================================================

外部電圧源のCOM/GNDとNucleo GNDが共通接続されているか確認してください。
共通GNDがない場合、入力はNucleo基準で浮き、不規則なADC値になります。

Ethernet経由で基板へ接続できない
================================================================================

PC側Ethernetインターフェースが ``192.168.100.0/24`` に設定されていること、
ケーブル接続、基板の書き込み、devcontainerからのネットワーク到達性を確認してください。
接続できない場合は、Ethernetケーブル、PC側IPアドレス、基板への書き込み結果を順番に確認します。

書き込みできない
================================================================================

ST-LINK USBケーブル、USB権限、devcontainerへのUSBデバイス公開、他のデバッガによる
ST-LINK占有を確認してください。

次の開発ステップ
****************

このチュートリアル完了後は、次の順序で製品向け実装へ進むことを推奨します。

#. ``customer_code/adc`` へ製品の電圧・電流換算と校正を実装する
#. ``customer_code/feedback`` へ飽和処理付きのPI/PID等を実装する
#. ``customer_code/pwm`` へ製品固有の周波数、duty、dead time制限を実装する
#. TIM1 Break入力、過電流保護、起動停止シーケンスを実装する
#. オシロスコープで10 kHz ADC周期、feedback遅延、PWM、dead timeを検証する
