NUCLEO-H755ZI-Q dual-core digital power-control starter
#######################################################

Development environment
***********************

Git、west workspace、コンテナ再構築後の復旧方法は ``docs/DEVELOPMENT.rst`` を参照してください。
VS Code devcontainer内でのビルド、書き込み、動作確認、PWM・ADCコード変更演習は
``docs/TRIAL_GUIDE.rst`` を参照してください。

電源メーカーがSTM32H755のデュアルコアを使ってデジタル電源制御を開発するための開始環境です。

* Cortex-M7: Ethernet、TCPコマンド、RPMsgクライアント、ハートビート
* Cortex-M4: PWM、固定10 kHz ADC/DMA、フィードバック制御、安全停止
* コア間通信: OpenAMP/RPMsg、共有SRAM4、HSEM mailbox

Directory structure
*******************

.. code-block:: text

   digital_power_control/
   |-- network_core/             M7ネットワークサーバー、RPMsgクライアント
   |-- shared/                   M7/M4共通コマンドプロトコル
   |-- boards/                   M7ボード設定
   |-- control_core/             M4リアルタイムZephyrアプリ
   |   |-- platform/             IPC、ADC DMA・割り込み、コマンド処理、安全監視（基盤コード）
   |   |-- boards/               M4 TIM1、RPMsg設定
   |   `-- customer_code/        顧客が変更するリアルタイム制御
   |       |-- pwm/              PWM、デッドタイム
   |       |-- adc/              ADC値の型・スケーリング
   |       `-- feedback/         PIフィードバック制御
   `-- sysbuild.cmake            両コアのビルド定義

Customer-editable area
**********************

顧客が製品ごとに修正するコードは、原則として ``control_core/customer_code`` 内に置きます。
現在動作しているPWM処理は ``customer_code/pwm``、ADC値のスケーリングは
``customer_code/adc``、PI制御とゲイン調整は ``customer_code/feedback`` にあります。
固定10 kHz ADCトリガ、DMA、割り込みは基盤コード ``control_core/platform`` に分離されています。

通常は ``network_core``、``shared``、``control_core/platform`` を変更しません。

Safety behavior
***************

M4はTIM1_CH1/CH1Nを所有し、PWM停止状態で起動します。M7は500 msごとにハートビートを送り、
PWM動作中に2秒間通信が途絶えるとM4がPWMを停止します。実機電力回路では、TIM1 Break入力等を
使った独立したハードウェア保護を追加してください。

PWM outputs
***********

* High-side: TIM1_CH1 on PE9
* Low-side: TIM1_CH1N on PE8
* Hardware dead time: M4で設定

Build and flash
***************

.. code-block:: console

   west build -p always --sysbuild \
     -b nucleo_h755zi_q/stm32h755xx/m7 digital_power_control \
     -d build-digital-power-control
   west flash -d build-digital-power-control

* M7: ``build-digital-power-control/digital_power_control/zephyr/zephyr.bin`` at ``0x08000000``
* M4: ``build-digital-power-control/control_core_m4/zephyr/zephyr.bin`` at ``0x08100000``

Network commands
****************

固定IPv4アドレスは ``192.168.100.2``、TCPポートは4242です。

.. code-block:: console

   nc 192.168.100.2 4242

.. code-block:: text

   MODE FEEDFORWARD
   SET 20000 50 500
   MODE FEEDBACK
   GET
   STATUS
   OFF
   HELP

デモ範囲は20..20000 Hz、0..100 percent、0..4000 nsです。

ADC feedback demonstration
**************************

Feedback入力はArduinoアナログヘッダ ``A0``、MCUピン ``PA3 / ADC1_INP15`` です。
16-bit ADCを使用し、通常の3.3 V VDDAをフルスケールとしてPI制御の目標値と測定値を扱います。
PWM出力は2段RCフィルタ（R1=R2=4.7 kOhm、C1=C2=1 uF）経由でA0へ接続します。

* 0.000 V -> 0%
* 0.825 V -> 25%
* 1.650 V -> 50%
* 2.475 V -> 75%
* 3.300 V -> 100%

A0入力は必ず0..3.3 Vに制限してください。高電圧を直接入力してはいけません。
外部電圧源のGND/COMとNucleoのGNDは必ず共通接続してください。共通GNDがない入力は
ADC値とfeedback dutyが不規則に変動する危険があります。
相補PWMのため、high-side 0%時にはlow-side出力が反対状態になります。実電力回路への接続前に、
ゲートドライバ、Break入力、起動停止シーケンスを含めた安全設計が必要です。

.. code-block:: text

   MODE FEEDFORWARD
   SET 20000 50 500
   MODE FEEDBACK
   GET

ADCはPWMから独立したTIM6 updateで10 kHz固定トリガされ、DMA完了割り込みから専用feedbackスレッドへ通知されます。
ADCサンプリング周期とfeedback処理の公称周期は100 usです。
``SET`` は周波数、第2引数、デッドタイムを設定します。FEEDFORWARDでは第2引数がPWM duty、
FEEDBACKではADC目標値です。例えば50%は約1.65 Vを意味し、PI制御がPWM dutyを調整します。ADC DMAでエラーが発生するとM4はPWMを停止し、
``GET`` のfault bit 1をセットします。
