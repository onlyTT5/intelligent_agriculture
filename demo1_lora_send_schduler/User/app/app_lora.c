/**
 * @file    app_lora.c
 * @brief   LoRa发送业务层实现
 *
 * 职责：
 *   1. 初始化 LLCC68 LoRa 模块 → 进入待机模式准备发送
 *   2. 初始化各传感器（DHT22/CO2/光敏/土壤）
 *   3. 调度周期调用 app_lora_send_poll()，每 5 秒读取传感器 → LoRa 发送
 *   4. 发送成功时 LED2 闪烁指示（非阻塞式：时间戳控制熄灭）
 *
 * 设计要点（与 demo1_lora_recv_schduler 的 app_lora 风格一致，教学衔接）：
 *   - app_lora_send_poll() 内绝对禁止 HAL_Delay / while 等待等阻塞操作
 *   - 5 秒周期基于 HAL_GetTick() 时间戳判断，未到则立即返回
 *   - 传感器读取失败时填 0，不阻塞发送流程
 *   - 数据打包为 10 字节（5个 int16_t），与 recv 端解析逻辑对称
 *   - LED2 闪烁采用时间戳方式（点亮后记录熄灭时刻，poll 中检查超时熄灭），
 *     避免阻塞调度器
 *
 * 数据帧格式（与 recv 端 sensor_data.h 一一对应）：
 *   tx_data[0] = 温度 ×10（int16_t，如 25.5℃ → 255）
 *   tx_data[1] = 湿度 ×10（int16_t，如 60.0% → 600）
 *   tx_data[2] = CO2 浓度 PPM（int16_t 原值）
 *   tx_data[3] = 光照强度 lux（int16_t 原值）
 *   tx_data[4] = 土壤湿度 %（int16_t 原值，0~100）
 *
 * 字节序说明：
 *   STM32 为小端架构，int16_t 数组直接强转 uint8_t* 发送，
 *   recv 端也用 int16_t* 直接强转还原（保持一致）
 */
#include "main.h"
#include "app_lora.h"
#include "llcc68_p2p.h"  /* llcc68_init / llcc68_lora_send / llcc68_ctx */
#include "sensor_data.h" /* g_sensor_data：本地传感器数据存储 */
#include "dht22.h"       /* dht22_init / dht22_read_data */
#include "co2.h"         /* co2_init / co2_read */
#include "photores.h"    /* photores_init / photores_get_lux */
#include "soil.h"        /* soil_init / soil_get_humi_percent */
#include "led.h"         /* led_ctrl：LED2 闪烁指示发送成功 */
#include <stdio.h>       /* printf（调试用） */

/* ============================ 全局共享变量定义 ============================ */

/* 本地传感器数据（此处定义，其他模块 extern 引用） */
sensor_data_t g_sensor_data = {0};

/* ============================ 内部状态 ============================ */

/* 上次发送时间戳：用于 5 秒周期判断 */
static uint32_t s_last_send_tick = 0;

/* LED2 闪烁熄灭时间戳：发送成功时点亮，到期后熄灭（非阻塞式闪烁） */
static uint32_t s_led2_off_tick = 0;

#define LORA_SEND_PERIOD_MS 5000 /* LoRa 发送周期：5 秒 */
#define LED2_BLINK_MS 100        /* LED2 闪烁时长（ms）：100ms 短闪一下 */

/* ============================ 接口函数实现 ============================ */

/**
 * @brief  初始化 LoRa 发送业务模块
 *
 * 流程：
 *   [1] 循环调用 llcc68_init(&llcc68_ctx)，失败则每秒重试
 *   [2] 初始化各传感器（DHT22/CO2/光敏/土壤）
 *   [3] 清空 g_sensor_data.valid_flag，等待首次读取
 *   [4] 记录当前 tick 作为发送周期起点
 *
 * @note   含 HAL_Delay(1000) 阻塞，仅在初始化阶段调用
 */
void app_lora_send_init(void)
{
    llcc68_status_t status;

    printf("[LoRa] 初始化 LLCC68 模块...\r\n");

    /* [1] 初始化 LLCC68，失败则每秒重试 */
    while (1)
    {
        status = llcc68_init(&llcc68_ctx);
        if (status != LLCC68_STATUS_OK)
        {
            printf("[LoRa] 初始化失败，1秒后重试...\r\n");
            HAL_Delay(1000);
        }
        else
        {
            printf("[LoRa] 初始化成功！\r\n");
            break;
        }
    }

    /* [2] 初始化各传感器
     *     DHT22   : 温湿度（单总线，GPIO 输入）
     *     CO2     : MH-Z14A（USART2 串口接收，空闲中断）
     *     photores: 光敏（ADC1 通道0）
     *     soil    : 土壤湿度（ADC1 通道1 DMA采样 → 百分比）
     */
    printf("[LoRa] 初始化传感器...\r\n");
    dht22_init();
    co2_init();
    photores_init();
    soil_init();
    printf("[LoRa] 传感器初始化完成\r\n");

    /* [3] 清空共享数据有效标志 */
    g_sensor_data.valid_flag = 0;
    g_sensor_data.update_tick = 0;

    /* [4] 记录当前 tick，作为 5 秒发送周期的起点 */
    s_last_send_tick = HAL_GetTick();

    printf("[LoRa] 进入待机模式，准备每 %dms 发送一次数据\r\n", LORA_SEND_PERIOD_MS);
}

/**
 * @brief  LoRa 发送任务（由调度器周期调用，非阻塞）
 *
 * 处理流程：
 *   [0] LED2 闪烁熄灭检查：若 s_led2_off_tick 已到期 → 熄灭 LED2 并清零
 *   [1] 检查是否距上次发送已满 5 秒，未到则立即返回
 *   [2] 读取 DHT22 温湿度 → temp_x10 / humi_x10
 *   [3] 读取 CO2 浓度 → co2_ppm
 *   [4] 读取光照强度 → lux
 *   [5] 读取土壤湿度 → soil_humi（百分比 0~100）
 *   [6] 写入 g_sensor_data + 打包为 10 字节
 *   [7] 调用 llcc68_lora_send() 发送
 *   [8] 发送成功 → 点亮 LED2 + 记录熄灭时间戳
 *
 * @note   调度器建议周期：10ms（用于 LED2 熄灭检查 + 5s 周期判断）
 * @note   LED2 闪烁时长由 LED2_BLINK_MS 宏控制（默认 100ms）
 */
void app_lora_send_poll(void)
{
    int16_t tx_data[5]; /* 发送缓冲区：5个int16_t = 10字节 */

    /* [0] LED2 闪烁的非阻塞式熄灭检查 */
    if (s_led2_off_tick && (HAL_GetTick() >= s_led2_off_tick))
    {
        led_ctrl(LED2, LED_OFF);
        s_led2_off_tick = 0;
    }

    /* [1] 5 秒周期判断：未到则立即返回，不阻塞调度器 */
    if ((HAL_GetTick() - s_last_send_tick) < LORA_SEND_PERIOD_MS)
    {
        return;
    }
    s_last_send_tick = HAL_GetTick();

    /* [2] 读取 DHT22 温湿度（温度×10、湿度×10，避免浮点传输）
     *     读取失败时填 0，不阻塞发送流程 */
    {
        float humi = 0, temp = 0;
        if (dht22_read_data(&humi, &temp) == 0)
        {
            tx_data[0] = (int16_t)(temp * 10); /* 温度 ×10（如 25.5℃ → 255） */
            tx_data[1] = (int16_t)(humi * 10); /* 湿度 ×10（如 60.0% → 600） */
        }
        else
        {
            tx_data[0] = 0;
            tx_data[1] = 0;
            printf("[LoRa] DHT22 读取失败\r\n");
        }
    }

    /* [3] 读取 CO2 浓度（PPM） */
    {
        struct co2_t co2_data;
        int co2_ret = co2_read(&co2_data);
        if (co2_ret == CO2_OK)
        {
            tx_data[2] = (int16_t)co2_data.concentration;
        }
        else
        {
            tx_data[2] = 0;
            printf("[LoRa] CO2 读取失败 (ret=%d)\r\n", co2_ret);
        }
    }

    /* [4] 读取光照强度（lux） */
    tx_data[3] = (int16_t)photores_get_lux();

    /* [5] 读取土壤湿度（百分比 0~100） */
    tx_data[4] = (int16_t)soil_get_humi_percent();

    /* [6] 写入共享数据结构（便于其他模块读取，如 OLED 显示） */
    g_sensor_data.temp_x10 = tx_data[0];
    g_sensor_data.humi_x10 = tx_data[1];
    g_sensor_data.co2_ppm = tx_data[2];
    g_sensor_data.lux = tx_data[3];
    g_sensor_data.soil_humi = tx_data[4];
    g_sensor_data.valid_flag = 1;
    g_sensor_data.update_tick = HAL_GetTick();

    /* 串口调试打印 */
    printf("[LoRa] 发送: T=%.1f℃  H=%.1f%%  CO2=%dppm  Lux=%d  Soil=%d%%\r\n",
           tx_data[0] / 10.0f, tx_data[1] / 10.0f, tx_data[2], tx_data[3], tx_data[4]);

    /* [7] 通过 LoRa 发送 10 字节数据（超时 1000ms）
     *     int16_t 数组直接强转 uint8_t* 发送，STM32 小端架构 */
    if (llcc68_lora_send(&llcc68_ctx, (uint8_t *)tx_data, 10, 1000) == LLCC68_STATUS_OK)
    {
        /* [7] 发送成功 → LED2 闪烁指示 */
        led_ctrl(LED2, LED_ON);
        s_led2_off_tick = HAL_GetTick() + LED2_BLINK_MS;
    }
    else
    {
        printf("[LoRa] 发送失败！\r\n");
    }
}
