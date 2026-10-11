顧客編集部分のコード説明
########################

この資料は、ソフトを触り始めたばかりの方向けに、顧客が主に変更する3つの場所を説明します。
対象は ``control_core/customer_code`` フォルダ内の次の3項目です。

* ``pwm``: PWM出力を作る部分
* ``adc``: ADCで読んだ値を電圧などへ変換する部分
* ``feedback``: ADCの値を見ながらPWMを調整する部分

基板との通信、ADCの10 kHz周期、DMA、割り込み、2つのCPU間のやり取りは
``control_core/platform`` 側にまとめています。最初は変更しなくて大丈夫です。
まずは、この資料に書かれている3項目だけを見てください。

変更してから動かすまで
**********************

コードを変更した後は、次の順番で確認します。

#. VS Codeでファイルを編集します。
#. ``Ctrl+S`` で保存します。
#. ``./scripts/build.sh`` で書き込み用プログラムを作成します。
#. ``./scripts/flash.sh`` で基板へ書き込みます。
#. ``nc 192.168.100.2 4242`` で基板へ命令を送り、動作を確認します。

動かなくなった場合は、最後に変更した値を元へ戻し、もう一度保存、ビルド、書き込みを行います。

1. PWM出力: ``pwm``
*******************

PWMで見るファイル
=================

* ``control_core/customer_code/pwm/pwm_control.c``
* ``control_core/customer_code/pwm/pwm_control.h``

PWMは、電源回路のスイッチをON/OFFするための信号です。このお試し版では、PE9とPE8から
相補PWMを出します。

基板には、次のような命令を送ります。

.. code-block:: text

   MODE FEEDFORWARD
   SET 20000 50 500

これは「20 kHz、50%、デッドタイム500 nsでPWMを出す」という意味です。

PWMでよく変更する値
=====================

``pwm_control.c`` の先頭付近に、次の値があります。

.. code-block:: c

   #define MIN_FREQUENCY_HZ 20U
   #define MAX_FREQUENCY_HZ 20000U
   #define MAX_DEADTIME_NS 4000U

意味は次のとおりです。

* ``MIN_FREQUENCY_HZ``: 受け付けるPWM周波数の最小値
* ``MAX_FREQUENCY_HZ``: 受け付けるPWM周波数の最大値
* ``MAX_DEADTIME_NS``: 受け付けるデッドタイムの最大値

例えば、最大周波数を10 kHzまでにしたい場合は、次のように変更します。

.. code-block:: c

   #define MAX_FREQUENCY_HZ 10000U

PWMの主な関数
===============

``pwm_control_set()``
   周波数、出力割合、デッドタイムをまとめて設定します。

``pwm_control_set_duty_percent()``
   フィードバック制御中に、出力割合だけを変更します。

``pwm_control_off()``
   PWMを停止します。

最初に触るなら、まず ``MAX_FREQUENCY_HZ`` を変えるのが分かりやすいです。
変更後に ``SET 20000 50 500`` や ``SET 10000 50 500`` を試すと、変更が反映されたか確認できます。

2. ADC入力: ``adc``
*******************

ADCで見るファイル
=================

* ``control_core/customer_code/adc/adc_input.c``
* ``control_core/customer_code/adc/adc_input.h``

ADCは、アナログ電圧を数字に変換する機能です。このお試し版では、A0端子の電圧を読みます。
読み取り周期は10 kHz、つまり100 usに1回です。

顧客編集部分では、「ADCで読んだ数字を何mVとして扱うか」を決めます。

ADCでよく変更する値
=====================

``adc_input.h`` に次の値があります。

.. code-block:: c

   #define ADC_INPUT_FULL_SCALE_MV 3300U
   #define ADC_INPUT_MAX_RAW 65535U

意味は次のとおりです。

* ``ADC_INPUT_FULL_SCALE_MV``: ADC最大値を何mVとして扱うか
* ``ADC_INPUT_MAX_RAW``: ADCの最大生値。16 bitなので65535です

初期値では、A0の0 Vを0 mV、約3.3 Vを3300 mVとして扱います。

ADCの主な関数
===============

``adc_input_raw_to_millivolts()``
   ADCの生値 ``raw`` をmVへ変換します。

変換の考え方は次の式です。

.. code-block:: text

   millivolts = raw * ADC_INPUT_FULL_SCALE_MV / ADC_INPUT_MAX_RAW

目安は次のとおりです。

.. list-table:: ADC入力と表示値の目安
   :header-rows: 1
   :widths: 30 30 40

   * - A0入力電圧
     - ADC生値の目安
     - 表示される電圧の目安
   * - 0.0 V
     - 0
     - 0 mV
   * - 1.65 V
     - 約32768
     - 約1650 mV
   * - 3.3 V
     - 65535
     - 3300 mV

最初に触るなら、``ADC_INPUT_FULL_SCALE_MV`` を ``2000U`` などに変えて、``GET`` の
``adc=`` 表示が変わることを確認すると分かりやすいです。確認後は ``3300U`` へ戻します。

3. フィードバック制御: ``feedback``
***********************************

フィードバックで見るファイル
============================

* ``control_core/customer_code/feedback/feedback_control.c``
* ``control_core/customer_code/feedback/feedback_control.h``

フィードバック制御は、ADCで読んだ値を見ながらPWMの出力割合を自動で変える処理です。
このお試し版では、PWM出力をRCフィルタに通してA0へ戻します。A0の電圧が目標値に近づくように、
PI制御でPWM dutyを調整します。

基板には、次のような命令を送ります。

.. code-block:: text

   MODE FEEDBACK
   SET 20000 50 0
   GET

``FEEDBACK`` モードでは、``SET`` の2番目の値はPWM dutyではなくADCの目標値です。
``50`` はフルスケールの50%、つまり約1.65 Vを目標にする、という意味です。

フィードバックでよく変更する値
=================================

``feedback_control.c`` の先頭付近に、次の値があります。

.. code-block:: c

   #define PI_KP_MILLI 500LL
   #define PI_KI_PER_SECOND_MILLI 50000LL

意味は次のとおりです。

* ``PI_KP_MILLI``: 比例ゲイン ``Kp``。``500`` は ``0.5`` を意味します
* ``PI_KI_PER_SECOND_MILLI``: 積分ゲイン ``Ki``。``50000`` は ``50 /s`` を意味します

``MILLI`` は「1000倍した整数で書いている」という意味です。
小数を直接使わず、整数で計算するためです。

例えば ``Kp = 0.8`` にしたい場合は、次のようにします。

.. code-block:: c

   #define PI_KP_MILLI 800LL

例えば ``Ki = 20 /s`` にしたい場合は、次のようにします。

.. code-block:: c

   #define PI_KI_PER_SECOND_MILLI 20000LL

フィードバックの主な関数
=========================

``feedback_control_set_target_percent()``
   目標値を設定します。``SET`` 命令の2番目の値がここに入ります。

``feedback_control_reset()``
   フィードバック制御を始めるときに、内部状態を初期化します。

``feedback_control_pi_step()``
   ADCの値を1回分受け取り、次に出すPWM dutyを計算します。10 kHz周期で呼ばれます。

PI制御の計算は、次の順番です。

#. 目標値とADC値の差を計算します。
#. 比例分 ``Kp`` を計算します。
#. 積分分 ``Ki`` を少しずつ足します。
#. PWM dutyが0..100%の範囲に収まるようにします。
#. 計算したPWM dutyを返します。

最初に触るなら、``PI_KP_MILLI`` を少し変えて応答の違いを見るのが分かりやすいです。
大きくしすぎると出力が振れやすくなり、小さくしすぎると目標値へ近づくのが遅くなります。

変更するときの考え方
********************

最初は、一度に1か所だけ変更してください。例えば、PWM最大周波数を変更したら、まずビルド、書き込み、
動作確認まで行います。その後でADCやPIゲインを変更します。

うまく動かなくなった場合は、最後に変更した値を元に戻します。元の値はこの資料や各ファイルの
READMEに書いてあります。戻した後に保存し、もう一度ビルドと書き込みを行えば確認できます。

この3項目を理解できると、次に「電圧を読む場所を変える」「電流制御を追加する」
「製品に合わせてPIゲインを調整する」といった作業へ進みやすくなります。
