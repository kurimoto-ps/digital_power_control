M4 platform infrastructure
##########################

このディレクトリは顧客編集対象ではないM4基盤コードです。

``synchronized_adc.c`` は次を所有します。

* PWMから独立したTIM6 updateによるADC1固定10 kHz外部トリガ
* ADC1_INP15の16-bit変換、channel preselection、810.5-cycle sampling time
* DMAMUX1 channel 0経由のDMA1循環転送（ADCデータ幅16 bit）
* DMA半完了・完了割り込みコールバック（10 kHz通知）
* DMA割り込みからfeedbackスレッドへの通知

DMA割り込み内では顧客コードやPWM更新を実行しません。顧客の
``feedback_control_thru()`` は専用feedbackスレッドから呼ばれます。
