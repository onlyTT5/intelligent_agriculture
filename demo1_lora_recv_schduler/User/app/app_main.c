/**
 * @file    app_main.c
 * @brief   应用层主程序（项目整合版 - 调度器调度）
 *
 * 架构说明（与 18-调度器\裸机编程调度器_demo1 的 app_main 风格一致，教学衔接）：
 *   本文件仅负责"模块初始化 + 任务注册 + 启动调度器"，不含任何业务逻辑。
 *   业务逻辑已下沉到独立 app_xxx.c 模块，通过时间片轮询调度器周期执行，
 *   模块间通过事件队列解耦。
 *
 * 模块组成（LoRa接收 → OLED显示 → WiFi上传巴法云）：
 *   - app_lora : LoRa接收业务（初始化/轮询）  → 写入 g_sensor_data + LED2闪烁指示
 *   - app_oled : OLED显示业务（初始化/轮询）  → 事件驱动刷新显示
 *   - app_mqtt : WiFi+MQTT业务（初始化/轮询） → 读取 g_sensor_data 上传巴法云 + LED1状态指示
 *   - sensor_data.h : 跨模块共享传感器数据结构（解耦）
 *   - scheduler : 任务调度器（时间片轮询 + 空闲低功耗 __wfi）
 *   - event     : 事件队列（中断 → 主循环，如 DIO1 → EVT_LORA_DATA_READY）
 *
 * LED 指示说明：
 *   - LED1 (PWM) ：MQTT连接状态（成功常亮，断开/失败熄灭）
 *   - LED2 (GPIO)：LoRa接收成功闪烁（100ms 短闪）
 *   - LED3 (GPIO)：预留（未使用，可在 app_main 初始化时熄灭）
 *
 * 任务调度周期（注册顺序即执行顺序，优先级依次降低）：
 *   | 任务            | 周期  | 说明                                      |
 *   |-----------------|-------|-------------------------------------------|
 *   | app_lora_poll   | 10ms  | 检查LoRa DIO1接收标志，解析数据 + LED2闪烁 |
 *   | app_oled_poll   | 500ms | OLED事件驱动刷新（收到数据才刷新）        |
 *   | app_mqtt_poll   | 50ms  | MQTT心跳/发布/接收解析/掉线重连 + LED1    |
 *
 * 执行流程（与调度器demo1完全一致）：
 *   1. 初始化中间件(event_init / scheduler_init)
 *   2. 初始化各业务模块（app_xxx_init）
 *   3. 向调度器注册周期任务（scheduler_add_task）
 *   4. 启动调度器（scheduler_run → 永不返回，空闲时 __wfi 低功耗）
 *
 * 扩展新业务：
 *   新建 app_xxx.c/.h，实现 xxx_init() 与 xxx_poll()，
 *   在此 app_main() 中添加一行 app_xxx_init() 与 scheduler_add_task() 即可。
 */

#include "main.h"       /* HAL_Init、硬件句柄等主头文件 */
#include "usart.h"      /* MX_USART1_UART_Init（CubeMX生成） */
#include "app_main.h"   /* app_main() 声明 */
#include "led.h"


/* ===== 业务层头文件（与调度器demo1的include结构一致） ===== */
#include "app_lora.h"   /* app_lora_init / app_lora_poll  —— LoRa接收业务 */
#include "app_oled.h"   /* app_oled_init / app_oled_poll  —— OLED显示业务 */
#include "app_mqtt.h"   /* app_mqtt_init / app_mqtt_poll  —— WiFi+MQTT上传业务 */

/* ===== 中间件头文件 ===== */
#include "event.h"      /* event_init          —— 事件队列初始化 */
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
 *         （业务逻辑全部下沉到 app_xxx.c 模块）。
 */
void app_main(void)
{
	/* ========== [0] 启动串口1空闲中断接收（调试用，接收上位机数据） ========== */
	HAL_UARTEx_ReceiveToIdle_IT(&huart1, g_uart1_rx_buf, sizeof(g_uart1_rx_buf));

	/* ========== [1] 初始化中间件（与调度器demo1保持一致） ========== */
	event_init();         /* [1/2] 事件队列清空（环形buffer头尾指针归零） */
	scheduler_init();     /* [2/2] 调度器任务表清空 */

	printf("\r\n========================================\r\n");
	printf(  " 项目整合：LoRa接收 + OLED + MQTT上传\r\n");
	printf(  " 调度器版（时间片轮询 + 空闲低功耗）\r\n");
	printf(  "========================================\r\n\r\n");

	/* ========== [2] 初始化各业务模块（LoRa → OLED → WiFi/MQTT） ==========
	 *   注意：led_init() 必须在 app_mqtt_init() 之前调用！
	 *         因为 LED1 是 PWM 模式，led_init() 内部调用 HAL_TIM_PWM_Start() 启动 PWM 通道，
	 *         否则 app_mqtt_init() 中调用 led_ctrl(LED1, ...) 时 PWM 通道未启动，SetCompare 无效。
	 * ========================================================================== */
	led_init();           /* 先初始化 LED 硬件（启动 TIM1 CH1 PWM 输出） */
	led_ctrl(LED1, LED_OFF);   /* 初始熄灭 LED1（PWM 占空比100%=灭） */
	led_ctrl(LED2, LED_OFF);   /* 初始熄灭 LED2 */
	led_ctrl(LED3, LED_OFF);   /* 初始熄灭 LED3 */

	app_oled_init();      /* [1/3] OLED 初始化 + 欢迎页显示 */
	app_lora_init();      /* [2/3] LoRa LLCC68 初始化 + 进入连续接收模式 */
	app_mqtt_init();      /* [3/3] WiFi连接 → 巴法云MQTT → 透传 → SUBSCRIBE */

	printf("\r\n========================================\r\n");
	printf(  " 所有模块初始化完成，启动调度器...\r\n");
	printf(  "========================================\r\n\r\n");

	/* ========== [3] 向调度器注册周期任务（与调度器demo1格式一致）
	 *        任务顺序 = 优先级（先注册先执行）
	 *        LoRa 10ms 最高（丢包敏感）
	 *        MQTT 50ms 次之（心跳与发布需要及时响应）
	 *        OLED 500ms 最慢（人眼刷新频率要求低）
	 * ========================================================================== */
	scheduler_add_task(app_lora_poll, 10);   /* LoRa 接收轮询： 10ms 周期 */
	scheduler_add_task(app_mqtt_poll, 50);   /* MQTT 业务轮询： 50ms 周期 */
	scheduler_add_task(app_oled_poll, 500);  /* OLED 显示刷新： 500ms 周期 */

	/* ========== [4] 启动调度器（永不返回，空闲时 __wfi 进入低功耗） ========== */
	scheduler_run();
}
