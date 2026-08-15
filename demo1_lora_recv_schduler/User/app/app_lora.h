/**
 * @file    app_lora.h
 * @brief   LoRa接收业务头文件
 *
 * @details
 *   本文件定义了 LoRa 业务层的初始化和任务接口。
 *   业务职责：
 *     1. 初始化 LLCC68 LoRa 模块，进入连续接收模式
 *     2. 调度周期调用 app_lora_poll()，非阻塞式检查是否收到数据
 *     3. 收到数据后解析为传感器值，写入 g_sensor_data，并投递事件
 *     4. 收到数据时 LED2 闪烁指示（非阻塞式：时间戳控制熄灭）
 *
 *   与 demo2_lora_recv 的关键区别：
 *     demo2 使用 while(1)+HAL_Delay(1000) 阻塞等待
 *     本文件使用调度器时间片，每次 poll 只做一次超时检查，未收到立即返回
 *
 *   依赖：
 *     - llcc68_p2p.h/.c  : LLCC68 驱动层（初始化/发送/接收）
 *     - sensor_data.h    : 共享传感器数据结构（写入）
 *     - event.h          : 事件队列（投递 EVT_LORA_DATA_READY）
 *     - led.h            : LED2 闪烁指示接收成功
 */
#ifndef __APP_LORA_H__
#define __APP_LORA_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ 接口函数声明 ============================ */

/**
 * @brief  初始化 LoRa 业务模块
 *
 * @details
 *   调用 llcc68_init() 初始化硬件，成功后调用 llcc68_lora_receive_mode()
 *   进入连续接收模式。失败则每秒重试直至成功，保证上电硬件未就绪时不崩溃。
 *
 * @param  无
 * @return 无
 * @note   含阻塞 HAL_Delay(1000)，仅在 app_main() 初始化阶段调用一次
 */
void app_lora_init(void);

/**
 * @brief  LoRa 接收任务（由调度器周期调用）
 *
 * @details
 *   调用 llcc68_lora_receive_data(timeout_in_ms=0) 做一次"检查"：
 *     - 如果 rx_done/rx_timeout 被 DIO1 中断置位 → 解析数据 → 写入 g_sensor_data
 *     - 如果尚未收到 → 立即返回，不阻塞调度器
 *
 *   调度器建议周期：10ms（太短浪费，太长影响数据新鲜度）
 *
 * @param  无
 * @return 无
 * @note   本函数非阻塞：即使没有收到数据，也会立即返回
 */
void app_lora_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LORA_H__ */
