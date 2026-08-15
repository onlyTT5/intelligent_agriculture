/**
 * @file    app_mqtt.h
 * @brief   WiFi+MQTT业务头文件
 *
 * @details
 *   本文件定义了 WiFi(MQTT) 业务层的初始化和任务接口。
 *   业务职责：
 *     1. 稳定初始化 ESP12-F：退出透传→复位→关回显→WiFi→TCP→透传→MQTT CONNECT→SUBSCRIBE
 *     2. 调度周期调用 app_mqtt_poll()：
 *          - 解析透传缓冲区，处理服务器下发的 MQTT PUBLISH 消息（LED on/off 等控制指令）
 *          - 周期（60秒）发送 MQTT 心跳（PINGREQ）保持连接
 *          - 周期（30秒）按主题分别发布传感器数据到巴法云
 *          - WiFi/MQTT掉线时自动重连（15秒后重试）
 *     3. LED3 状态指示：连接成功常亮，断开/失败熄灭
 *
 *   设计原则：
 *     - 初始化阶段 app_mqtt_init() 允许阻塞（每步 delay_ms 2秒，确保模块稳定）
 *     - 稳定性关键：必须先 esp_exit_transparent_transmission() 退出透传 → esp_reset() 复位
 *     - 轮询阶段 app_mqtt_poll() 非阻塞，使用 HAL_GetTick() 做时间片判断
 *
 *   依赖：
 *     - esp.h/.c              : ESP12-F Wi-Fi AT 驱动
 *     - esp_mqtt.h/.c         : MQTT 协议实现（巴法云适配版）
 *     - sensor_data.h         : 共享传感器数据结构（只读）
 *     - led.h                 : LED3 状态指示 MQTT连接状态
 */
#ifndef __APP_MQTT_H__
#define __APP_MQTT_H__

#ifdef __cplusplus
extern "C"
{
#endif

    /* ============================ 接口函数声明 ============================ */

    /**
     * @brief  初始化 WiFi+MQTT 业务模块（允许阻塞）
     *
     * @details
     *   稳定初始化流程（与 16-mqtt/demo1_esp_mqtt 的 esp_mqtt_init 一致）：
     *     [1] esp_init()                           初始化 ESP12-F（USART2）
     *     [2] esp_exit_transparent_transmission()   退出透传模式（稳定性关键）
     *     [3] esp_reset()                          AT+RST 复位模块
     *     [4] esp_enable_echo(0)                   关闭回显
     *     [5] esp_connect_ap(SSID, PWD)            连接 WiFi 热点
     *     [6] esp_connect_server(TCP, bemfa, 9501) TCP 连接巴法云
     *     [7] esp_entry_transparent_transmission() 进入透传模式
     *     [8] mqtt_connect(CLIENTID, user, pwd)    MQTT CONNECT 握手
     *     [9] mqtt_subscribe_topic(LED)            订阅控制主题（light002）
     *     [10]成功 → LED3 常亮，重置心跳/发布时间戳
     *
     *   任何一步失败：LED3 熄灭，s_mqtt_connected=0，由 poll 自动重连
     *
     * @param  无
     * @return 无
     * @note   含 delay_ms（每步间隔2秒），仅初始化阶段调用
     */
    void app_mqtt_init(void);

    /**
     * @brief  WiFi+MQTT 周期任务（由调度器调用，非阻塞）
     *
     * @details
     *   每次执行：
     *     [A] 解析透传缓冲区 MQTT 消息（PUBLISH/CLOSED/ERROR）
     *           - PUBLISH → 处理 LED on/off 指令
     *           - CLOSED/ERROR → LED3 熄灭，s_mqtt_connected=0（触发重连）
     *     [B] 60秒计时到期 → mqtt_send_heart() 发送心跳保活
     *     [C] 30秒计时到期且数据有效 → 按主题发布传感器数据
     *     [D] 连接异常 → 15秒后自动重连（调用 app_mqtt_init）
     *
     *   调度器建议周期：50ms（透传消息解析要求响应较快）
     *
     * @param  无
     * @return 无
     */
    void app_mqtt_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MQTT_H__ */
