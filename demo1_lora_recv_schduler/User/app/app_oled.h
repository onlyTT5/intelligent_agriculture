/**
 * @file    app_oled.h
 * @brief   OLED显示业务头文件
 *
 * @details
 *   本文件定义了 OLED 业务层的初始化和任务接口。
 *   业务职责：
 *     1. 初始化 OLED 硬件，上电显示欢迎页 + "等待LoRa数据"提示
 *     2. 调度周期调用 app_oled_poll()，事件驱动刷新显示
 *     3. 无有效数据时显示"等待LoRa数据..."，避免显示残留乱码
 *
 *   事件驱动刷新机制（区别于固定 500ms 周期刷新）：
 *     - app_lora 收到 LoRa 数据后投递 EVT_LORA_DATA_READY 事件
 *     - app_oled_poll 消费事件，仅在有新数据时才刷新 OLED
 *     - 无数据时不刷新，避免无意义的 I2C 通信浪费 CPU
 *     - app_mqtt 不消费事件，OLED 是唯一消费者（避免竞争）
 *
 *   显示布局（128×64 OLED，16号字 = 2页高，共4行）：
 *     第0行 (y=0) : T:xx.x  H:xx.x     （温度 / 湿度）
 *     第1行 (y=2) : CO2: xxxxppm        （CO2浓度）
 *     第2行 (y=4) : Lux: xxxx           （光照强度）
 *     第3行 (y=6) : Soil: xxx %         （土壤湿度）
 *
 *   依赖：
 *     - oled.h/.c          : OLED 硬件驱动
 *     - sensor_data.h      : 共享传感器数据结构（只读）
 *     - event.h            : 事件队列（消费 EVT_LORA_DATA_READY）
 */
#ifndef __APP_OLED_H__
#define __APP_OLED_H__

#ifdef __cplusplus
extern "C"
{
#endif

    /* ============================ 接口函数声明 ============================ */

    /**
     * @brief  初始化 OLED 业务模块
     *
     * @details
     *   调用 OLED_Init() / OLED_Clear(0) 初始化硬件，
     *   显示欢迎页"LoRa接收网关 / 项目整合版"，2秒后清屏，
     *   然后显示"等待LoRa数据..."提示（直到收到第一包数据触发事件刷新）。
     *
     * @param  无
     * @return 无
     * @note   含 HAL_Delay(2000) 阻塞，仅在初始化阶段调用
     */
    void app_oled_init(void);

    /**
     * @brief  OLED 显示任务（由调度器周期调用，事件驱动刷新）
     *
     * @details
     *   事件驱动刷新流程：
     *     1. 循环消费事件队列中所有挂起事件
     *     2. 遇到 EVT_LORA_DATA_READY → 标记需要刷新
     *     3. 无事件 → 直接返回，不刷新（收到数据才刷新）
     *     4. 有事件 → 读取 g_sensor_data 显示最新值
     *
     *   调度器建议周期：500ms（事件队列会缓存事件，500ms 内不会丢失）
     *
     * @param  无
     * @return 无
     * @note   本函数非阻塞
     */
    void app_oled_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_OLED_H__ */
