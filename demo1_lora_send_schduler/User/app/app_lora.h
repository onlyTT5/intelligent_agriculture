/**
 * @file    app_lora.h
 * @brief   LoRa发送业务头文件
 *
 * @details
 *   本文件定义了 LoRa 发送业务层的初始化和任务接口。
 *   业务职责：
 *     1. 初始化 LLCC68 LoRa 模块（进入待机模式准备发送）
 *     2. 调度周期调用 app_lora_send_poll()，每 5 秒读取传感器 → LoRa 发送
 *     3. 发送成功时 LED2 闪烁指示（非阻塞式：时间戳控制熄灭）
 *
 *   与 demo1_lora_recv_schduler 的 app_lora 关键区别：
 *     recv 端：进入连续接收模式，poll 中检查 rx_done 并解析
 *     send 端：进入待机模式，poll 中按 5s 周期读传感器并发送
 *
 *   数据帧格式（与 recv 端 sensor_data.h 一一对应，共 10 字节 = 5个 int16_t）：
 *     [0] 温度    × 10（如 25.5℃ → 255）
 *     [1] 湿度    × 10（如 60.0% → 600）
 *     [2] CO2浓度 PPM（原值）
 *     [3] 光照    lux（原值）
 *     [4] 土壤湿度 %（原值，0~100）
 *
 *   依赖：
 *     - llcc68_p2p.h/.c  : LLCC68 驱动层（初始化/发送）
 *     - dht22.h          : DHT22 温湿度传感器
 *     - co2.h            : MH-Z14A CO2 传感器
 *     - photores.h       : 光敏电阻
 *     - sensor_data.h    : 本地传感器数据结构（与 recv 端对称）
 *     - led.h            : LED2 闪烁指示发送成功
 */
#ifndef __APP_LORA_H__
#define __APP_LORA_H__

#ifdef __cplusplus
extern "C"
{
#endif

    /* ============================ 接口函数声明 ============================ */

    /**
     * @brief  初始化 LoRa 发送业务模块
     *
     * @details
     *   [1] 循环调用 llcc68_init(&llcc68_ctx)，失败则每秒重试
     *   [2] 成功后模块进入待机模式（Standby RC），准备发送
     *
     * @note   含阻塞 HAL_Delay(1000)，仅在 app_main() 初始化阶段调用一次
     */
    void app_lora_send_init(void);

    /**
     * @brief  LoRa 发送任务（由调度器周期调用，非阻塞）
     *
     * @details
     *   每 5 秒执行一次：
     *     [1] 读取 DHT22 温湿度 → temp_x10 / humi_x10
     *     [2] 读取 CO2 浓度 → co2_ppm
     *     [3] 读取光照强度 → lux
     *     [4] 读取土壤湿度 → soil_humi（百分比）
     *     [5] 打包为 10 字节，调用 llcc68_lora_send() 发送
     *     [6] 发送成功 → LED2 闪烁 100ms
     *
     *   调度器建议周期：10ms（用于 LED2 闪烁熄灭检查 + 5s 周期判断）
     *
     * @note   本函数非阻塞：基于 HAL_GetTick() 判断 5s 周期，未到则立即返回
     */
    void app_lora_send_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LORA_H__ */
