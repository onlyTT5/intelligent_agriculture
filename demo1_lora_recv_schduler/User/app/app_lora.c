/**
 * @file    app_lora.c
 * @brief   LoRa接收业务层实现
 *
 * 职责：
 *   1. 初始化 LLCC68 LoRa 模块 → 进入连续接收模式
 *   2. 调度周期调用 app_lora_poll()，非阻塞式检查接收状态
 *   3. 收到数据后解析 10 字节 → 写入 g_sensor_data → 投递 EVT_LORA_DATA_READY 事件
 *   4. 收到数据时 LED2 闪烁指示（非阻塞式：时间戳控制熄灭）
 *
 * 设计要点（与调度器版本保持一致）：
 *   - app_lora_poll() 内绝对禁止 HAL_Delay / while 等待等阻塞操作
 *   - rx_done/rx_timeout 由 DIO1 中断置位，app_lora_poll() 只做读标志与解析
 *   - 每次解析后调用 llcc68_lora_receive_mode(ctx, 0) 重新进入连续接收模式
 *   - 数据共享：写入 g_sensor_data 后，valid_flag 置 1，OLED/MQTT 即可读取
 *   - LED2 闪烁采用时间戳方式（点亮后记录熄灭时刻，poll 中检查超时熄灭），
 *     避免阻塞调度器（禁止 HAL_Delay）
 */
#include "main.h"
#include "app_lora.h"
#include "llcc68_p2p.h"  /* llcc68_init / llcc68_lora_receive_mode / llcc68_lora_receive_data */
#include "sensor_data.h" /* g_sensor_data：写入共享传感器数据 */
#include "event.h"       /* event_post：投递数据就绪事件 */
#include "led.h"         /* led_ctrl：LED2 闪烁指示接收成功 */
#include <stdio.h>       /* printf（调试用） */

/* ============================ 全局共享变量定义 ============================ */

/* 跨模块共享的传感器数据（此处定义，其他模块 extern 引用） */
sensor_data_t g_sensor_data = {0};

/* ============================ 内部状态 ============================ */

/* LED2 闪烁熄灭时间戳：收到数据时置为 当前tick+闪烁时长，
 * app_lora_poll 检查到超时后熄灭LED2并清零（非阻塞式闪烁） */
static uint32_t s_led2_off_tick = 0;

#define LED2_BLINK_MS 100 /* LED2 闪烁时长（ms）：100ms 短闪一下 */

/* ============================ 接口函数实现 ============================ */

/**
 * @brief  初始化 LoRa 业务模块
 *
 * 流程：
 *   [1] 循环调用 llcc68_init(&llcc68_ctx)，失败则每秒重试
 *   [2] 成功后调用 llcc68_lora_receive_mode(ctx, 0) 进入连续接收模式
 *   [3] 清空 g_sensor_data.valid_flag，等待首包数据
 *
 * @note   含 HAL_Delay(1000) 阻塞，仅在初始化阶段调用
 */
void app_lora_init(void)
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

    /* [2] 进入连续接收模式（timeout=0 表示持续监听） */
    llcc68_lora_receive_mode(&llcc68_ctx, 0);
    printf("[LoRa] 进入连续接收模式（470.5MHz, SF9, BW125）\r\n");

    /* [3] 清空共享数据有效标志，等待首包 LoRa 数据 */
    g_sensor_data.valid_flag = 0;
    g_sensor_data.update_tick = 0;
}

/**
 * @brief  LoRa 接收任务（由调度器周期调用，非阻塞）
 *
 * 处理流程：
 *   [0] LED2 闪烁熄灭检查：若 s_led2_off_tick 已到期 → 熄灭 LED2 并清零
 *   [1] 调用 llcc68_lora_receive_data(timeout=0) 做一次检查
 *         - 如果 DIO1 中断已置 rx_done → 解析数据包
 *         - 如果没有数据 → 立即返回 LLCC68_STATUS_ERROR，不阻塞
 *   [2] 解析成功：提取 4 个 int16_t → 写入 g_sensor_data → 点亮 LED2 + 记录熄灭时间戳
 *   [3] 投递 EVT_LORA_DATA_READY 事件，通知 OLED 刷新显示
 *   [4] 重新进入连续接收模式 llcc68_lora_receive_mode(ctx, 0)
 *
 * @note   调度器建议周期：10ms
 * @note   LED2 闪烁时长由 LED2_BLINK_MS 宏控制（默认 100ms）
 */
void app_lora_poll(void)
{
    uint8_t rx_raw_buf[10];              /* 原始接收缓冲区（10字节 = 5个int16） */
    uint16_t rx_len = 0;                 /* 实际接收长度 */
    llcc68_pkt_status_lora_t pkt_status; /* 包状态：RSSI/SNR */

    /* [0] LED2 闪烁的非阻塞式熄灭检查
     *     收到数据时点亮 LED2 并记录熄灭时间戳，到达时间后熄灭
     *     实现"闪烁一下"效果而不阻塞调度器 */
    if (s_led2_off_tick && (HAL_GetTick() >= s_led2_off_tick))
    {
        led_ctrl(LED2, LED_OFF);
        s_led2_off_tick = 0;
    }

    /* [1] 做一次非阻塞检查（timeout=0：不等待） */
    if (llcc68_lora_receive_data(&llcc68_ctx, rx_raw_buf, &rx_len, &pkt_status, 0) != LLCC68_STATUS_OK)
    {
        /* 没有收到数据 → 立即返回，不阻塞调度器 */
        return;
    }

    /* [2] 检查数据包长度是否合法（至少10字节 = 5个int16） */
    if (rx_len < 10)
    {
        printf("[LoRa] 收到数据包过短（%d字节，需要≥10），丢弃\r\n", rx_len);
        goto re_enter_rx;
    }

    /* [3] 解析 10 字节为 5 个 int16_t
     *     发送端是 int16_t 数组直接强转 uint8_t* 发送，STM32为小端架构，
     *     因此接收端也用 int16_t* 直接强转还原（保持与发送端一致）
     *     rx_data[0] = 温度×10
     *     rx_data[1] = 湿度×10
     *     rx_data[2] = CO2 PPM
     *     rx_data[3] = 光照 lux
     *     rx_data[4] = 土壤湿度 %（0~100）
     */
    {
        int16_t *p_data = (int16_t *)rx_raw_buf;
        int16_t temp_x10 = p_data[0];
        int16_t humi_x10 = p_data[1];
        int16_t co2_ppm = p_data[2];
        int16_t lux = p_data[3];
        int16_t soil_humi = p_data[4];

        /* 写入共享数据结构 */
        g_sensor_data.temp_x10 = temp_x10;
        g_sensor_data.humi_x10 = humi_x10;
        g_sensor_data.co2_ppm = co2_ppm;
        g_sensor_data.lux = lux;
        g_sensor_data.soil_humi = soil_humi;
        g_sensor_data.rssi_dbm = pkt_status.rssi_pkt_in_dbm;
        g_sensor_data.snr_db = pkt_status.snr_pkt_in_db;
        g_sensor_data.rx_len = rx_len;
        g_sensor_data.valid_flag = 1;              /* 数据有效 */
        g_sensor_data.update_tick = HAL_GetTick(); /* 记录更新时间戳 */

        /* 串口调试打印 */
        printf("[LoRa] 收到 %d字节  T=%.1f℃  H=%.1f%%  CO2=%dppm  Lux=%d  Soil=%d%%  RSSI=%ddBm  SNR=%ddB\r\n",
               rx_len,
               temp_x10 / 10.0f, humi_x10 / 10.0f,
               co2_ppm, lux, soil_humi,
               pkt_status.rssi_pkt_in_dbm, pkt_status.snr_pkt_in_db);

        /* LED2 闪烁指示：收到数据点亮，s_led2_off_tick 到期后熄灭
         * 闪烁时长 LED2_BLINK_MS=100ms，下次 poll 自动熄灭 */
        led_ctrl(LED2, LED_ON);
        s_led2_off_tick = HAL_GetTick() + LED2_BLINK_MS;
    }

    /* [4] 投递事件：LoRa数据已就绪，通知其他任务（可选：OLED/MQTT可选择订阅） */
    event_post(EVT_LORA_DATA_READY, 0);

re_enter_rx:
    /* [5] 处理完后重新进入连续接收模式（等待下一包） */
    llcc68_lora_receive_mode(&llcc68_ctx, 0);
}
