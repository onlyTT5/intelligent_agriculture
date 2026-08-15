/**
 * @file    app_main.c
 * @brief   应用层主程序（项目整合版 - 调度器调度 / LoRa发送端）
 *
 * 架构说明（与 18-调度器\裸机编程调度器_demo1 的 app_main 风格一致，教学衔接）：
 *   本文件仅负责"模块初始化 + 任务注册 + 启动调度器"，不含任何业务逻辑。
 *   业务逻辑已下沉到独立 app_xxx.c 模块，通过时间片轮询调度器周期执行。
 *
 * 模块组成（读取传感器 → LoRa发送）：
 *   - app_lora : LoRa发送业务（初始化/轮询）  → 读取传感器 + LoRa发送 + LED2闪烁
 *   - sensor_data.h : 本地传感器数据结构（与 recv 端对称，便于教学对照）
 *   - scheduler : 任务调度器（时间片轮询 + 空闲低功耗 __wfi）
 *
 * 任务调度周期（注册顺序即执行顺序，优先级依次降低）：
 *   | 任务                | 周期  | 说明                                      |
 *   |---------------------|-------|-------------------------------------------|
 *   | app_lora_send_poll  | 10ms  | 5秒周期判断 + LED2闪烁熄灭检查 + 传感器发送 |
 *
 * 执行流程（与调度器demo1完全一致）：
 *   1. 初始化中间件(scheduler_init)
 *   2. 初始化业务模块(app_lora_send_init：LLCC68 + 传感器)
 *   3. 向调度器注册周期任务（scheduler_add_task）
 *   4. 启动调度器（scheduler_run → 永不返回，空闲时 __wfi 低功耗）
 *
 * LED 指示说明：
 *   - LED2 (GPIO)：LoRa发送成功闪烁（100ms 短闪）
 *   - LED1/LED3  ：预留（未使用，初始化时熄灭）
 *
 * 扩展新业务：
 *   新建 app_xxx.c/.h，实现 xxx_init() 与 xxx_poll()，
 *   在此 app_main() 中添加一行 app_xxx_init() 与 scheduler_add_task() 即可。
 */

#include "main.h"       /* HAL_Init、硬件句柄等主头文件 */
#include "usart.h"      /* MX_USART1_UART_Init（CubeMX生成） */
#include "app_main.h"   /* app_main() 声明 */
#include "led.h"        /* led_init / led_ctrl */
#include "adc.h"

/* ===== 业务层头文件（与调度器demo1的include结构一致） ===== */
#include "app_lora.h"   /* app_lora_send_init / app_lora_send_poll —— LoRa发送业务 */

/* ===== 中间件头文件 ===== */
#include "scheduler.h"  /* scheduler_init / scheduler_add_task / scheduler_run */

/* ============================ 全局变量（与原工程保持兼容） ============================ */

/* 串口1（调试串口）接收缓冲区，保留用于调试 */
uint8_t  g_uart1_rx_buf[32] = {0};   /* 串口1接收缓冲区，容量 32 字节 */
uint32_t g_uart1_rx_cnt      = 0;   /* 串口1接收计数器 */
uint32_t g_uart1_rx_end      = 0;   /* 串口1接收完成标志 */

/* ADC 采样表（预留兼容） */
uint16_t g_adc_value_tbl[2] = {0};

/* ============================ 接口函数实现 ============================ */

/**
 * @brief  应用层主函数
 *
 * @note   由 main() 调用，内部启动调度器后永不返回。
 *         仅做初始化与任务注册，不包含任何业务处理代码
 *         （业务逻辑全部下沉到 app_lora.c 模块）。
 */
void app_main(void)
{
	/* ========== [0] 启动串口1空闲中断接收（调试用，接收上位机数据） ========== */
	HAL_UARTEx_ReceiveToIdle_IT(&huart1, g_uart1_rx_buf, sizeof(g_uart1_rx_buf));

	
	// 启动ADC与DMA
	HAL_ADC_Start_DMA(&hadc1,(uint32_t *)g_adc_value_tbl,2);
	/* ========== [1] 初始化中间件（与调度器demo1保持一致） ========== */
	scheduler_init();     /* 调度器任务表清空 */

	printf("\r\n========================================\r\n");
	printf(  " 项目整合：LoRa发送端（传感器采集 + LoRa发送）\r\n");
	printf(  " 调度器版（时间片轮询 + 空闲低功耗）\r\n");
	printf(  "========================================\r\n\r\n");

	/* ========== [2] 初始化硬件与业务模块 ==========
	 *   注意：led_init() 必须在 app_lora_send_init() 之前调用！
	 *         因为 LED1 是 PWM 模式，led_init() 内部调用 HAL_TIM_PWM_Start() 启动 PWM 通道，
	 *         否则 app_lora_send_poll() 中调用 led_ctrl(LED2, ...) 时 GPIO 已配置但 LED1 PWM 未启动。
	 *         （LED2 是普通 GPIO 模式，led_init() 同时配置 LED1/LED2/LED3 引脚）
	 * ========================================================================== */
	led_init();           /* 先初始化 LED 硬件（启动 TIM1 CH1 PWM 输出 + 配置 LED2/LED3 GPIO） */
	led_ctrl(LED1, LED_OFF);   /* 初始熄灭 LED1（未使用） */
	led_ctrl(LED2, LED_OFF);   /* 初始熄灭 LED2 */
	led_ctrl(LED3, LED_OFF);   /* 初始熄灭 LED3（未使用） */

	app_lora_send_init(); /* [1/1] LLCC68 初始化 + 传感器初始化 + 进入待机模式 */

	printf("\r\n========================================\r\n");
	printf(  " 所有模块初始化完成，启动调度器...\r\n");
	printf(  "========================================\r\n\r\n");

	/* ========== [3] 向调度器注册周期任务（与调度器demo1格式一致）
	 *        任务顺序 = 优先级（先注册先执行）
	 *        LoRa发送 10ms：检查 5s 周期 + LED2 闪烁熄灭检查
	 * ========================================================================== */
	scheduler_add_task(app_lora_send_poll, 10);   /* LoRa 发送轮询：10ms 周期 */

	/* ========== [4] 启动调度器（永不返回，空闲时 __wfi 进入低功耗） ========== */
	scheduler_run();
}
