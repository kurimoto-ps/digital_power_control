Customer control code
#####################

このディレクトリが、顧客が製品ごとに変更するM4リアルタイム制御領域です。
IPC、コマンド処理、ハートビート監視、安全停止は ``../platform`` にあります。

想定する制御フロー:

.. code-block:: text

   ADC sampling -> feedback calculation -> PWM update

* ``pwm``: PWM、デッドタイム、変調方式
* ``adc``: ADC取得、校正、スケーリング、フィルタ
* ``feedback``: PI/PID、電圧・電流制御、保護判定

ここに追加する処理は実行時間を制限し、sleep、ネットワークアクセス、無期限待機を避けてください。
