#include "synchronized_adc.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <stm32h7xx_ll_adc.h>
#include <stm32h7xx_ll_dmamux.h>
#include <stm32h7xx_ll_tim.h>

LOG_MODULE_REGISTER(synchronized_adc, LOG_LEVEL_INF);

#define ADC_INSTANCE ((ADC_TypeDef *)DT_REG_ADDR(DT_NODELABEL(adc1)))
#define ADC_TRIGGER_TIMER ((TIM_TypeDef *)DT_REG_ADDR(DT_NODELABEL(timers6)))
#define ADC_TRIGGER_COUNTER_NODE DT_CHILD(DT_NODELABEL(timers6), counter)
#define DMA_CHANNEL 0U
#define DMA_SAMPLE_COUNT 2U
#define ADC_READY_TIMEOUT_US 1000U
#define ADC_SAMPLE_RATE_HZ 10000U

static const struct adc_dt_spec feedback_adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
static const struct device *const dma_device = DEVICE_DT_GET(DT_NODELABEL(dmamux1));
static const struct device *const trigger_counter = DEVICE_DT_GET(ADC_TRIGGER_COUNTER_NODE);
static uint16_t dma_samples[DMA_SAMPLE_COUNT];
static struct adc_input_sample latest_sample;
static struct k_spinlock sample_lock;
static struct k_sem sample_ready;
static atomic_t dma_error;

static void dma_callback(const struct device *dev, void *user_data,
			 uint32_t channel, int status)
{
	struct adc_input_sample sample;
	k_spinlock_key_t key;

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	ARG_UNUSED(channel);

	if (status < 0) {
		atomic_set(&dma_error, status);
		k_sem_give(&sample_ready);
		return;
	}

	uint32_t sample_index;

	if (status == DMA_STATUS_BLOCK) {
		sample_index = 0U;
	} else if (status == DMA_STATUS_COMPLETE) {
		sample_index = 1U;
	} else {
		atomic_set(&dma_error, -EIO);
		k_sem_give(&sample_ready);
		return;
	}

	sample.raw = MIN(dma_samples[sample_index], ADC_INPUT_MAX_RAW);
	sample.millivolts = adc_input_raw_to_millivolts(sample.raw);

	key = k_spin_lock(&sample_lock);
	latest_sample = sample;
	k_spin_unlock(&sample_lock, key);
	k_sem_give(&sample_ready);
}

static int configure_trigger_timer(void)
{
	struct counter_top_cfg top_cfg = { 0 };
	uint32_t timer_frequency_hz;

	if (!device_is_ready(trigger_counter)) {
		LOG_ERR("TIM6 counter device is not ready");
		return -ENODEV;
	}
	timer_frequency_hz = counter_get_frequency(trigger_counter);
	if ((timer_frequency_hz < ADC_SAMPLE_RATE_HZ) ||
	    ((timer_frequency_hz % ADC_SAMPLE_RATE_HZ) != 0U)) {
		LOG_ERR("TIM6 counter frequency %u cannot generate %u Hz",
			timer_frequency_hz, ADC_SAMPLE_RATE_HZ);
		return -ERANGE;
	}

	top_cfg.ticks = (timer_frequency_hz / ADC_SAMPLE_RATE_HZ) - 1U;
	int ret = counter_set_top_value(trigger_counter, &top_cfg);

	if (ret != 0) {
		LOG_ERR("TIM6 top configuration failed (%d)", ret);
		return ret;
	}
	LL_TIM_SetTriggerOutput(ADC_TRIGGER_TIMER, LL_TIM_TRGO_UPDATE);
	ret = counter_start(trigger_counter);
	if (ret != 0) {
		LOG_ERR("TIM6 start failed (%d)", ret);
	} else {
		LOG_INF("TIM6 ADC trigger started at %u Hz", ADC_SAMPLE_RATE_HZ);
	}
	return ret;
}

static int configure_dma(void)
{
	struct dma_block_config block = {
		.source_address = (uint32_t)&ADC_INSTANCE->DR,
		.dest_address = (uint32_t)dma_samples,
		.block_size = sizeof(dma_samples),
		.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE,
		.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT,
		.source_reload_en = 1U,
		.dest_reload_en = 1U,
	};
	struct dma_config config = {
		.dma_slot = LL_DMAMUX1_REQ_ADC1,
		.channel_direction = PERIPHERAL_TO_MEMORY,
		.channel_priority = 3U,
		.source_data_size = sizeof(dma_samples[0]),
		.dest_data_size = sizeof(dma_samples[0]),
		.block_count = 1U,
		.head_block = &block,
		.dma_callback = dma_callback,
		.complete_callback_en = 1U,
		.cyclic = 1U,
	};
	int ret;

	ret = dma_config(dma_device, DMA_CHANNEL, &config);
	if (ret != 0) {
		LOG_ERR("ADC DMAMUX configuration failed (%d)", ret);
		return ret;
	}
	ret = dma_start(dma_device, DMA_CHANNEL);
	if (ret != 0) {
		LOG_ERR("ADC DMA start failed (%d)", ret);
	}
	return ret;
}

int synchronized_adc_init(void)
{
	int ret;

	k_sem_init(&sample_ready, 0, 1);
	if (!adc_is_ready_dt(&feedback_adc)) {
		LOG_ERR("ADC1 device is not ready");
		return -ENODEV;
	}
	if (!device_is_ready(dma_device)) {
		LOG_ERR("DMAMUX1 device is not ready");
		return -ENODEV;
	}

	ret = adc_channel_setup_dt(&feedback_adc);
	if (ret != 0) {
		LOG_ERR("ADC1 channel setup failed (%d)", ret);
		return ret;
	}
	ret = configure_dma();
	if (ret != 0) {
		return ret;
	}

	LL_ADC_SetResolution(ADC_INSTANCE, LL_ADC_RESOLUTION_16B);
	LL_ADC_SetChannelPreselection(ADC_INSTANCE, LL_ADC_CHANNEL_15);
	LL_ADC_SetChannelSamplingTime(ADC_INSTANCE, LL_ADC_CHANNEL_15,
				      LL_ADC_SAMPLINGTIME_810CYCLES_5);
	LL_ADC_REG_SetSequencerLength(ADC_INSTANCE, LL_ADC_REG_SEQ_SCAN_DISABLE);
	LL_ADC_REG_SetSequencerRanks(ADC_INSTANCE, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_15);
	LL_ADC_REG_SetContinuousMode(ADC_INSTANCE, LL_ADC_REG_CONV_SINGLE);
	LL_ADC_REG_SetDataTransferMode(ADC_INSTANCE, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);
	LL_ADC_REG_SetOverrun(ADC_INSTANCE, LL_ADC_REG_OVR_DATA_OVERWRITTEN);
	LL_ADC_REG_SetTriggerSource(ADC_INSTANCE, LL_ADC_REG_TRIG_EXT_TIM6_TRGO);

	LL_ADC_ClearFlag_ADRDY(ADC_INSTANCE);
	LL_ADC_Enable(ADC_INSTANCE);
	for (uint32_t elapsed_us = 0U;
	     LL_ADC_IsActiveFlag_ADRDY(ADC_INSTANCE) == 0U; elapsed_us++) {
		if (elapsed_us >= ADC_READY_TIMEOUT_US) {
			LOG_ERR("ADC1 ready timeout");
			return -ETIMEDOUT;
		}
		k_busy_wait(1U);
	}
	LL_ADC_REG_StartConversion(ADC_INSTANCE);
	LOG_INF("ADC1 configured: PCSEL=0x%08x SQR1=0x%08x SMPR2=0x%08x CFGR=0x%08x",
		ADC_INSTANCE->PCSEL, ADC_INSTANCE->SQR1, ADC_INSTANCE->SMPR2,
		ADC_INSTANCE->CFGR);
	return configure_trigger_timer();
}

int synchronized_adc_wait_sample(struct adc_input_sample *sample, k_timeout_t timeout)
{
	k_spinlock_key_t key;
	int ret;

	if (sample == NULL) {
		return -EINVAL;
	}
	ret = k_sem_take(&sample_ready, timeout);
	if (ret != 0) {
		return ret;
	}
	ret = atomic_get(&dma_error);
	if (ret != 0) {
		return ret;
	}

	key = k_spin_lock(&sample_lock);
	*sample = latest_sample;
	k_spin_unlock(&sample_lock, key);
	return 0;
}
