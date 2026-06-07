Feedback control
################

現在は制御演算を行わない ``feedback_control_thru()`` 実装です。16-bit ADC値を
0..100%のPWM high-side dutyへ線形変換します。

.. code-block:: text

   duty_percent = round(adc_raw * 100 / 65535)

目安:

* 0.000 V -> 0%
* 0.825 V -> 25%
* 1.650 V -> 50%
* 2.475 V -> 75%
* 3.300 V -> 100%

この関数はDMA割り込みではなく、M4の専用feedbackスレッドから呼ばれます。DMA、割り込み、
固定10 kHz ADCトリガ処理は ``control_core/platform`` に分離されています。

将来、顧客固有のPI/PID、電圧・電流制御、保護判定をこのディレクトリへ実装します。
処理は制御周期内に必ず完了させ、sleep、ネットワークアクセス、動的メモリ確保、
無期限待機を避けてください。
