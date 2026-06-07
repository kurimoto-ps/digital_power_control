ADC acquisition
###############

Feedback入力には、NUCLEO-H755ZI-QのArduinoアナログヘッダ ``A0`` を使います。
MCUピンは ``PA3``、ADC入力は ``ADC1_INP15`` です。

Electrical range
****************

* ADC resolution: 16 bit
* Raw range: 0..65535
* Reference/full scale: VDDA、ボードの通常構成では約3.3 V
* 許容する入力: 0..3.3 V

3.3 Vを超える電圧や負電圧をA0へ直接入力しないでください。電源回路の電圧・電流を
測定する場合は、必ず分圧、絶縁、電流検出アンプ、クランプ等の保護回路を使用します。

Pass-through mapping
********************

現在のfeedback処理はADC値をそのままPWM high-side dutyへ線形変換します。

.. code-block:: text

   duty_percent = round(adc_raw * 100 / 65535)

目安:

* 0.000 V -> 0%
* 0.825 V -> 25%
* 1.650 V -> 50%
* 2.475 V -> 75%
* 3.300 V -> 100%

実際の電圧換算精度はVDDA、ADC誤差、配線、信号源インピーダンスに依存します。
