/**
 * @file    sensor_data.h
 * @brief   传感器数据共享头文件
 *
 * @details
 *   本文件定义了跨模块共享的传感器数据结构。
 *   数据流向：
 *     app_lora （LoRa接收） → 写入 g_sensor_data
 *     app_oled （OLED显示） → 读取 g_sensor_data 显示
 *     app_mqtt （WiFi上传） → 读取 g_sensor_data 发布到巴法云
 *
 *   使用原则：
 *     - 写入端：只有 app_lora 负责写入 g_sensor_data
 *     - 读取端：app_oled、app_mqtt 只读访问，不修改
 *     - 新鲜度：valid_flag=1 表示数据有效（收到了LoRa数据）
 *
 * 数据格式说明（与发送端 demo1_lora_send_schduler 一一对应）：
 *   发送端打包为 10 字节（5个 int16_t）：
 *     [0] 温度    × 10（如 25.5℃ → 255）
 *     [1] 湿度    × 10（如 60.0% → 600）
 *     [2] CO2浓度 PPM（原值）
 *     [3] 光照    lux（原值）
 *     [4] 土壤湿度 %（原值，0~100）
 */
#ifndef __SENSOR_DATA_H__
#define __SENSOR_DATA_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ============================ 数据结构定义 ============================ */

    /**
     * @brief 传感器数据结构体
     *
     * 所有数据由 app_lora 写入，app_oled/app_mqtt 读取。
     */
    typedef struct
    {
        /* ==== 各传感器值 ==== */
        int16_t temp_x10;  /* 温度    × 10（25.5℃ 存 255） */
        int16_t humi_x10;  /* 湿度    × 10（60.0% 存 600） */
        int16_t co2_ppm;   /* CO2浓度 PPM（原值） */
        int16_t lux;       /* 光照强度 lux（原值） */
        int16_t soil_humi; /* 土壤湿度 %（原值，0~100） */

        /* ==== LoRa链路信息（用于显示/调试） ==== */
        int16_t rssi_dbm; /* 接收信号强度（dBm） */
        int16_t snr_db;   /* 接收信噪比（dB） */
        uint16_t rx_len;  /* 本次接收数据包长度（字节） */

        /* ==== 数据新鲜度 ==== */
        uint8_t valid_flag;   /* 数据有效标志：1=有效，0=尚未收到数据 */
        uint32_t update_tick; /* 上次更新时间戳（HAL_GetTick） */
    } sensor_data_t;

    /* ============================ 全局共享变量声明 ============================ */

    /**
     * @brief 全局传感器数据（跨模块共享）
     *
     * 写入者：app_lora.c（收到LoRa数据时）
     * 读取者：app_oled.c / app_mqtt.c
     */
    extern sensor_data_t g_sensor_data;

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_DATA_H__ */
