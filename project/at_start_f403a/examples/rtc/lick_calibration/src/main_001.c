/**
  **************************************************************************
  * @file     main.c
  * @brief    main program - PTPd phs2syc Dynamic Clock Calibration
  **************************************************************************
  *                       Copyright notice & Disclaimer
  *
  * The software Board Support Package (BSP) that is made available to
  * download from Artery official website is the copyrighted work of Artery.
  * Artery authorizes customers to use, copy, and distribute the BSP
  * software and its related documentation for the purpose of design and
  * development in conjunction with Artery microcontrollers. Use of the
  * software is governed by this copyright notice and the following disclaimer.
  *
  * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
  * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
  * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
  * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
  * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
  *
  **************************************************************************
  */

#include <stdint.h>
#include <stdbool.h>
#include "at32f403a_407_board.h"
#include "at32f403a_407_clock.h"
#include "at32f403a_407_emac.h"
#include "at32f403a_407_crm.h"
#include "at32f403a_407_rtc.h"
#include "at32f403a_407_tmr.h"
#include "at32f403a_407_gpio.h"
#include "at32f403a_407_pwc.h"
#include "at32f403a_407_bpr.h"
#include "at32f403a_407_exint.h"

/** @addtogroup AT32F403A_periph_examples
  * @{
  */

/** @addtogroup 403A_RTC_lick_calibration RTC_lick_calibration
  * @{
  */

crm_clocks_freq_type crm_clocks;
__IO uint32_t periodvalue = 0;
__IO uint32_t lickfreq = 0;
__IO uint32_t operationcomplete = 0;

/* PTPd phs2syc dynamic calibration variables */
#define PTP_SYNC_INTERVAL_MS    1000    /* PTP sync message interval (1 second) */
#define PHASE_ADJUSTMENT_RATE   1000    /* Maximum phase adjustment per second (ppm) */
#define RTC_BASE_FREQUENCY      32768   /* RTC base frequency when using LEXT */

typedef struct {
    uint32_t ptp_seconds;
    uint32_t ptp_nanoseconds;
    uint32_t local_seconds;
    uint32_t local_nanoseconds;
    int64_t phase_offset;        /* Phase offset in nanoseconds */
    int64_t frequency_offset;    /* Frequency offset in ppm */
    uint32_t rtc_divider_current;
    uint32_t calibration_count;
} ptp_calibration_data_t;

ptp_calibration_data_t ptp_calibration;
__IO uint32_t ptp_sync_received = 0;

/**
  * @brief  configures the nested vectored interrupt controller.
  * @param  none
  * @retval none
  */
void nvic_configuration(void)
{
  /* configure one bit for preemption priority */
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);

  /* enable the rtc interrupt */
  nvic_irq_enable(RTC_IRQn, 0, 0);

  /* enable the tmr5 interrupt */
  nvic_irq_enable(TMR5_GLOBAL_IRQn, 0, 0);
}

/**
  * @brief  configures the rtc.
  * @param  none
  * @retval none
  */
void rtc_configuration(void)
{
  /* enable pwc and bpr clocks */
  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_BPR_PERIPH_CLOCK, TRUE);

  /* allow access to bpr domain */
  pwc_battery_powered_domain_access(TRUE);

  /* reset backup domain */
  bpr_reset();

  /* enable the lick osc */
  crm_clock_source_enable(CRM_CLOCK_SOURCE_LICK, TRUE);
  /* wait till lick is ready */
  while(crm_flag_get(CRM_LICK_STABLE_FLAG) == RESET);
  /* select the rtc clock source */
  crm_rtc_clock_select(CRM_RTC_CLOCK_LICK);

  /* enable rtc clock */
  crm_rtc_clock_enable(TRUE);

  /* wait for rtc registers update */
  rtc_wait_update_finish();

  /* wait for the register write to complete */
  rtc_wait_config_finish();

  /* enable the rtc second */
  rtc_interrupt_enable(RTC_TS_INT, TRUE);

  /* wait for the register write to complete */
  rtc_wait_config_finish();

  /* set rtc divider: set rtc period to 1sec */
  rtc_divider_set(40000);

  /* wait for the register write to complete */
  rtc_wait_config_finish();

  /* tamper pin disabled */
  bpr_tamper_pin_enable(FALSE);

  /* enable the rtc second output on tamper pin */
  bpr_rtc_output_select(BPR_RTC_OUTPUT_SECOND);
}

/**
  * @brief  increments operationcomplete variable and return its value
  *         before increment operation.
  * @param  none
  * @retval operationcomplete value before increment
  */
uint32_t incrementvar_operationcomplete(void)
{
  operationcomplete++;

  return (uint32_t)(operationcomplete - 1);
}

/**
  * @brief  PTP timestamp interrupt handler
  * @param  none
  * @retval none
  */
void PTP_TS_IRQHandler(void)
{
  /* Handle PTP timestamp events */
  if(emac_ptp_flag_get(EMAC_PTP_TU_FLAG))
  {
    /* PTP timestamp updated */
    ptp_sync_received = 1;

    /* Get PTP system time */
    ptp_calibration.ptp_seconds = emac_ptp_system_second_get();
    ptp_calibration.ptp_nanoseconds = emac_ptp_system_subsecond_get();

    /* Get local RTC time for comparison */
    ptp_calibration.local_seconds = rtc_second_get();
    /* Note: For nanosecond precision, would need additional RTC subsecond counter */
    ptp_calibration.local_nanoseconds = 0; /* Placeholder */
  }
}

/**
  * @brief  Initialize PTP for phs2syc calibration
  * @param  none
  * @retval none
  */
void ptp_phs2syc_init(void)
{
  /* Enable PTP timestamp functionality */
  emac_ptp_timestamp_enable(TRUE);

  /* Configure PTP clock node */
  emac_ptp_clock_node_set(EMAC_PTP_BOUNDARY_CLOCK);

  /* Enable PTP event message snapshot */
  emac_ptp_snapshot_event_message_enable(TRUE);

  /* Enable PTP timestamp fine update */
  emac_ptp_timestamp_fine_update_enable(TRUE);

  /* Initialize calibration data */
  ptp_calibration.phase_offset = 0;
  ptp_calibration.frequency_offset = 0;
  ptp_calibration.rtc_divider_current = 32767; /* Default for 32.768kHz */
  ptp_calibration.calibration_count = 0;

  printf("PTP phs2syc calibration initialized\r\n");
}

/**
  * @brief  Calculate phase offset between PTP time and local time
  * @param  none
  * @retval phase offset in nanoseconds
  */
int64_t ptp_calculate_phase_offset(void)
{
  int64_t ptp_time_ns = (int64_t)ptp_calibration.ptp_seconds * 1000000000LL +
                       (int64_t)ptp_calibration.ptp_nanoseconds;

  int64_t local_time_ns = (int64_t)ptp_calibration.local_seconds * 1000000000LL +
                         (int64_t)ptp_calibration.local_nanoseconds;

  return ptp_time_ns - local_time_ns;
}

/**
  * @brief  Calculate frequency offset based on phase measurements
  * @param  phase_offset: current phase offset in nanoseconds
  * @retval frequency offset in ppm
  */
int64_t ptp_calculate_frequency_offset(int64_t phase_offset)
{
  /* ===========================================
   * PI 控制器頻率偏差計算原理說明
   * ===========================================
   *
   * 核心概念：
   * 相位偏差(phase_offset) → 頻率偏差(frequency_offset)
   *
   * 數學關係：
   * 如果相位偏差持續存在，說明存在頻率差異
   * 頻率偏差(ppm) = 相位偏差變化率 × 轉換係數
   *
   * PI 控制器：
   * P(比例項)：對當前相位偏差做出響應
   * I(積分項)：消除穩態誤差，積累歷史偏差
   * ===========================================
   */

  /* PI 控制器狀態變數 */
  static int64_t integral_error = 0;        /* 積分誤差累積 */
  int64_t proportional_error = phase_offset; /* 比例誤差（當前相位偏差）*/
  int64_t derivative_error = 0;            /* 微分誤差（保留以備將來使用）*/

  /* ===========================================
   * 積分項計算
   * 原理：將相位偏差積累，用於消除穩態誤差
   * 如果系統存在固有頻率偏差，積分項會不斷累積直到抵消偏差
   * ===========================================
   */
  integral_error += proportional_error;

  /* ===========================================
   * 積分風上保護
   * 原理：防止積分項過度累積導致系統振盪或不穩定
   * 限制積分項的最大值，維持系統穩定性
   * ===========================================
   */
  if(integral_error > 1000000) integral_error = 1000000;
  if(integral_error < -1000000) integral_error = -1000000;

  /* ===========================================
   * 頻率偏差計算
   *
   * 數學推導：
   * 1. 相位偏差單位：納秒(ns)
   * 2. 同步間隔：PTP_SYNC_INTERVAL_MS 毫秒
   * 3. 目標：計算每百萬分之一的頻率偏差(ppm)
   *
   * 轉換公式：
   * frequency_offset(ppm) = phase_offset(ns) × 1000 / PTP_SYNC_INTERVAL_MS
   *
   * 詳細推導：
   * - 相位偏差 rate = phase_offset(ns) / PTP_SYNC_INTERVAL_MS(ms)
   * - 轉換為 ppm：rate × 1000 × 1000000 / 1000000 = rate × 1000
   * - 最終：phase_offset × 1000 / PTP_SYNC_INTERVAL_MS
   *
   * 積分項係數 100 是經驗值，用於平衡 P 和 I 項的權重
   * ===========================================
   */
  int64_t frequency_offset = (proportional_error * 1000) / PTP_SYNC_INTERVAL_MS +
                           (integral_error * 100) / PTP_SYNC_INTERVAL_MS;

  /* ===========================================
   * 調整速率限制
   * 原理：防止劇烈調整造成時鐘跳躍或系統不穩定
   * PHASE_ADJUSTMENT_RATE 定義了最大允許的調整速率
   * ===========================================
   */
  if(frequency_offset > PHASE_ADJUSTMENT_RATE) frequency_offset = PHASE_ADJUSTMENT_RATE;
  if(frequency_offset < -PHASE_ADJUSTMENT_RATE) frequency_offset = -PHASE_ADJUSTMENT_RATE;

  return frequency_offset;
}

/**
  * @brief  Adjust RTC divider based on calculated frequency offset
  * @param  frequency_offset: frequency offset in ppm
  * @retval none
  */
void ptp_adjust_rtc_divider(int64_t frequency_offset)
{
  /* Calculate new divider value */
  /* Base frequency is 32.768kHz, target is 1Hz */
  /* frequency_offset is in ppm, so adjust by that amount */
  int64_t divider_adjustment = (RTC_BASE_FREQUENCY * frequency_offset) / 1000000LL;

  int64_t new_divider = (int64_t)RTC_BASE_FREQUENCY - 1 + divider_adjustment;

  /* Limit divider range */
  if(new_divider < 1) new_divider = 1;
  if(new_divider > 0xFFFF) new_divider = 0xFFFF;

  ptp_calibration.rtc_divider_current = (uint32_t)new_divider;

  /* Apply new divider */
  rtc_divider_set(ptp_calibration.rtc_divider_current);
  rtc_wait_config_finish();

  printf("RTC divider adjusted: %d (freq_offset: %lld ppm)\r\n",
         ptp_calibration.rtc_divider_current, frequency_offset);
}

/**
  * @brief  PTP phs2syc dynamic calibration main process
  * @param  none
  * @retval none
  */
void ptp_phs2syc_calibration_process(void)
{
  if(ptp_sync_received)
  {
    ptp_sync_received = 0;

    /* Calculate phase offset */
    ptp_calibration.phase_offset = ptp_calculate_phase_offset();

    /* Calculate frequency offset */
    ptp_calibration.frequency_offset = ptp_calculate_frequency_offset(ptp_calibration.phase_offset);

    /* Adjust RTC divider */
    ptp_adjust_rtc_divider(ptp_calibration.frequency_offset);

    /* Update calibration statistics */
    ptp_calibration.calibration_count++;

    /* Print calibration status every 10 iterations */
    if(ptp_calibration.calibration_count % 10 == 0)
    {
      printf("PTP Calibration #%d:\r\n", ptp_calibration.calibration_count);
      printf("  Phase offset: %lld ns\r\n", ptp_calibration.phase_offset);
      printf("  Freq offset: %lld ppm\r\n", ptp_calibration.frequency_offset);
      printf("  RTC divider: %d\r\n", ptp_calibration.rtc_divider_current);
    }
  }
}

/**
  * @brief  returns operationcomplete value.
  * @param  none
  * @retval operationcomplete value
  */
uint32_t getvar_operationcomplete(void)
{
  return (uint32_t)operationcomplete;
}

/**
  * @brief  sets the periodvalue variable with input parameter.
  * @param  value: value of periodvalue to be set.
  * @retval none
  */
void setvar_periodvalue(uint32_t value)
{
  periodvalue = (uint32_t)(value);
}

/**
  * @brief  main function.
  * @param  none
  * @retval none
  */
int main(void)
{
  tmr_input_config_type tmr_ic_init_structure;

  system_clock_config();

  at32_board_init();

  uart_print_init(115200);

  /* rtc configuration - use LEXT for better precision with PTP */
  rtc_configuration();

  /* Initialize PTP for phs2syc dynamic calibration */
  ptp_phs2syc_init();

  printf("\r\n\nPTP phs2syc dynamic calibration started\r\n\r\n");

  /* get the frequency value */
  crm_clocks_freq_get(&crm_clocks);

  /* enable tmr5 apb1 clocks */
  crm_periph_clock_enable(CRM_TMR5_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_IOMUX_PERIPH_CLOCK, TRUE);

  /* connect internally the tm5_ch4 input capture to the lick clock output */
  gpio_pin_remap_config(TMR5CH4_MUX, TRUE);

  /* tmr5 time base configuration */
  tmr_base_init(TMR5, 0xFFFF, 0);
  tmr_cnt_dir_set(TMR5, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR5, TMR_CLOCK_DIV1);

  /* tmr5 channel4 input capture mode configuration */
  tmr_input_default_para_init(&tmr_ic_init_structure);
  tmr_ic_init_structure.input_channel_select = TMR_SELECT_CHANNEL_4;
  tmr_ic_init_structure.input_polarity_select = TMR_INPUT_RISING_EDGE;
  tmr_ic_init_structure.input_mapped_select = TMR_CC_CHANNEL_MAPPED_DIRECT;
  tmr_ic_init_structure.input_filter_value = 0;
  tmr_input_channel_init(TMR5, &tmr_ic_init_structure, TMR_CHANNEL_INPUT_DIV_1);

  /* reinitialize the index for the interrupt */
  operationcomplete = 0;

  /* enable the tmr5 input capture counter */
  tmr_counter_enable(TMR5, TRUE);

  /* Reset all tmr5 flags */
  tmr_flag_get(TMR5, TMR_C4_FLAG);

  /* enable the tmr5 channel 4 */
  tmr_interrupt_enable(TMR5, TMR_C4_INT, TRUE);

  /* nvic configuration */
  nvic_configuration();

  /* Configure PTP timestamp interrupt */
  nvic_irq_enable(PTP_IRQn, 0, 0);
  emac_ptp_interrupt_trigger_enable(TRUE);

  /* wait the tmr5 measuring operation to be completed */
  while(operationcomplete != 2);

  /* compute the actual frequency of the lick. (tim5_clk = 1 * pclk1)  */
  if(periodvalue != 0)
  {
    lickfreq = (uint32_t)((uint32_t)(crm_clocks.apb1_freq * 2) / (uint32_t)periodvalue);
  }

  printf("apb1_freq    = %d\r\n", crm_clocks.apb1_freq);
  printf("period_value = %d\r\n", periodvalue);
  printf("lick_freq    = %d\r\n", lickfreq);

  /* Initial RTC divider setting based on measured frequency */
  rtc_divider_set((lickfreq - 1));
  rtc_wait_config_finish();

  /* turn on led2 to indicate initialization complete */
  at32_led_on(LED2);

  printf("Initial calibration complete. Starting PTP phs2syc dynamic calibration...\r\n");

  /* Main loop for continuous PTP phs2syc calibration */
  while(1)
  {
    /* Process PTP phs2syc calibration */
    ptp_phs2syc_calibration_process();

    /* Small delay to prevent excessive CPU usage */
    delay_ms((uint16_t)10);
  }
}

/**
  * @}
  */

/**
  * @}
  */
