/**
 * @file    sensor_data.h
 * @brief   传感器数据本地存储头文件（send 端）
 *
 * @details
 *   本文件定义了 send 端本地传感器数据结构，与 recv 端的 sensor_data.h
 *   保持完全一致，便于教学衔接（学生对照 send/recv 两端数据结构）。
 *
 *   数据流向：
 *     app_lora_send_poll （读取传感器） → 写入 g_sensor_data → LoRa 发送
 *
 *   使用原则：
 *     - 写入端：只有 app_lora_send_poll 负责写入
 *     - 读取端：可扩展（如 OLED 显示、串口打印等）
 *     - 新鲜度：valid_flag=1 表示数据有效（已成功读取过一次）
 *
 * 数据格式说明（与 recv 端 demo1_lora_recv_schduler 一一对应）：
 *   打包为 10 字节（5个 int16_t）通过 LoRa 发送：
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
     * @brief 传感器数据结构体（与 recv 端完全一致）
     *
     * send 端由 app_lora_send_poll 写入，LoRa 发送时打包为 10 字节。
     */
    typedef struct
    {
        /* ==== 各传感器值 ==== */
        int16_t temp_x10;  /* 温度    × 10（25.5℃ 存 255） */
        int16_t humi_x10;  /* 湿度    × 10（60.0% 存 600） */
        int16_t co2_ppm;   /* CO2浓度 PPM（原值） */
        int16_t lux;       /* 光照强度 lux（原值） */
        int16_t soil_humi; /* 土壤湿度 %（原值，0~100） */

        /* ==== LoRa链路信息（send 端预留，保持结构一致） ==== */
        int16_t rssi_dbm; /* 接收信号强度（dBm） - send端未用 */
        int16_t snr_db;   /* 接收信噪比（dB）   - send端未用 */
        uint16_t rx_len;  /* 数据包长度（字节）  - send端未用 */

        /* ==== 数据新鲜度 ==== */
        uint8_t valid_flag;   /* 数据有效标志：1=有效，0=尚未读取过 */
        uint32_t update_tick; /* 上次更新时间戳（HAL_GetTick） */
    } sensor_data_t;

    /* ============================ 全局共享变量声明 ============================ */

    /**
     * @brief 全局传感器数据（send 端本地存储）
     *
     * 写入者：app_lora.c（读取传感器时）
     * 读取者：LoRa 发送逻辑（打包发送）
     */
    extern sensor_data_t g_sensor_data;

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_DATA_H__ */
