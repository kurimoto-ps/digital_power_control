ADC input scaling
#################

このディレクトリは、顧客がADC値の意味付けや電圧・電流への換算を変更する場所です。
ADCハードウェア設定、固定10 kHzタイマトリガ、DMA、DMA完了割り込みは
``control_core/platform/synchronized_adc.c`` が所有するため、ここでは扱いません。

Feedback入力には、NUCLEO-H755ZI-QのArduinoアナログヘッダ ``A0`` を使います。
MCUピンは ``PA3``、ADC入力は ``ADC1_INP15`` です。
外部電圧源を接続する場合は、外部電圧源のGND/COMとNucleoのGNDを必ず共通接続します。
GNDを共通接続しない浮いた入力は、ADC値とfeedback dutyが0..100%付近まで不規則に
変動する危険があります。

Electrical range
****************

* ADC resolution: 16 bit
* Raw range: 0..65535
* Reference/full scale: VDDA、ボードの通常構成では約3.3 V
* 許容する入力: 0..3.3 V

3.3 Vを超える電圧や負電圧をA0へ直接入力しないでください。電源回路の電圧・電流を
測定する場合は、必ず分圧、絶縁、電流検出アンプ、クランプ等の保護回路を使用します。
実際の電圧換算精度はVDDA、ADC誤差、配線、信号源インピーダンスに依存します。
