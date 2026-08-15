/**
 * @file    app_oled.c
 * @brief   OLED显示业务层实现
 *
 * 职责：
 *   1. 初始化 OLED 硬件 + 显示欢迎页"粤嵌温工"2秒 + 清屏
 *   2. 调度周期调用 app_oled_poll()，事件驱动刷新显示
 *
 * 设计要点：
 *   - 只读取 g_sensor_data（只读，不修改），与 LoRa 写入端完全解耦
 *   - 事件驱动刷新：仅当消费到 EVT_LORA_DATA_READY 事件时才刷新显示
 *     避免无意义的 500ms 周期刷屏（OLED I2C 通信有耗时，频繁刷新浪费CPU）
 *   - 显示前先将 display_buf 全部置零，避免 sprintf 溢出后显示残留字符
 *   - valid_flag=0 时直接返回不刷新（保持当前屏幕，避免显示错误值）
 *
 * 事件消费说明：
 *   - EVT_LORA_DATA_READY 事件由 app_lora 收到数据后投递（event_post）
 *   - app_mqtt 不消费事件（其发布为周期性），OLED 是唯一事件消费者
 *     → 避免多消费者竞争导致事件丢失
 *   - 循环消费所有挂起事件，只要遇到 EVT_LORA_DATA_READY 就标记需要刷新
 *     多个事件合并为一次刷新（避免连续多包导致 OLED 频繁刷新）
 *
 * 显示布局（与 demo2_lora_recv 保持一致，具有教学衔接性）：
 *   y=0 (第0行) : "T:xx.x  H:xx.x"
 *   y=2 (第1行) : "CO2: xxxxppm"
 *   y=4 (第2行) : "PRes: xxxxLux"
 *   y=6 (第3行) : "Soil: xxx %"
 *
 * 行级增量刷新策略：
 *   - 缓存上次显示的5项数据值（temp/humi/co2/lux/soil）
 *   - 仅当对应行的数据发生变化时才执行 oled_show_string 刷新该行
 *   - 首次刷新（s_first_refresh=1）强制刷新全部4行，保证初始化后屏幕有内容
 *   - 优点：避免相同数据重复刷屏，减少 I2C 通信开销，降低 CPU 占用
 */
#include "main.h"
#include "app_oled.h"
#include "oled.h"        /* OLED_Init / OLED_Clear / oled_show_string */
#include "sensor_data.h" /* g_sensor_data：只读共享传感器数据 */
#include "event.h"       /* event_get / event_has_event / EVT_LORA_DATA_READY */
#include <stdio.h>       /* sprintf */
#include <string.h>      /* memset */

/* ============================ 私有静态变量 ============================
 * 上次显示的数据快照，用于行级增量刷新比对。
 * 仅在本文件内可见，避免外部修改。
 */
static int16_t s_last_temp_x10 = 0;  /* 上次显示的温度×10              */
static int16_t s_last_humi_x10 = 0;  /* 上次显示的湿度×10              */
static int16_t s_last_co2_ppm = 0;   /* 上次显示的CO2浓度PPM           */
static int16_t s_last_lux = 0;       /* 上次显示的光照lux              */
static int16_t s_last_soil_humi = 0; /* 上次显示的土壤湿度%            */
static uint8_t s_first_refresh = 1;  /* 首次刷新标志：1=首次,强制全刷   */

/* ============================ 接口函数实现 ============================ */

/**
 * @brief  初始化 OLED 业务模块
 *
 * 流程：
 *   [1] OLED_Init()     初始化硬件（I2C/SPI）
 *   [2] OLED_Clear(0)   清屏
 *   [3] oled_show_string 显示欢迎页"粤嵌温工"（与 demo1/demo2 教学界面风格一致）
 *   [4] HAL_Delay(2000) 停留 2 秒
 *   [5] OLED_Clear(0)   清屏，等待 LoRa 数据触发事件刷新
 *
 * @note   初始化后屏幕为空白，直到 app_oled_poll 收到 EVT_LORA_DATA_READY
 *         事件且 valid_flag=1 时才会刷新显示传感器数据
 * @note   含 HAL_Delay(2000) 阻塞，仅在初始化阶段调用
 */
void app_oled_init(void)
{
    /* [1] 初始化 OLED 硬件 */
    OLED_Init();

    /* [2] 清屏（参数0：全屏擦除） */
    OLED_Clear(0);

    oled_show_string(32, 3, (uint8_t *)"智慧农业", 16);

    /* [3] 停留 2 秒（仅初始化阶段允许阻塞） */
    HAL_Delay(2000);

    /* [4] 清屏 */
    OLED_Clear(0);
}

/**
 * @brief  OLED 显示任务（由调度器周期调用，非阻塞，事件驱动刷新）
 *
 * 处理流程：
 *   [1] 循环消费事件队列中所有挂起事件
 *       - 遇到 EVT_LORA_DATA_READY → 标记 need_refresh=1
 *       - 其他事件忽略（OLED 不关心）
 *       - 多个事件合并为一次刷新（避免连续多包导致频繁刷新）
 *   [2] 若 need_refresh=0 → 直接返回，不刷新（核心：收到数据才刷新）
 *   [3] 若 need_refresh=1：
 *       - valid_flag=0 → 直接返回（保持当前屏幕内容，不显示提示页）
 *       - valid_flag=1 → 逐行比对上次显示快照（s_last_*），
 *         仅当某行数据发生变化时才格式化并刷新该行（行级增量刷新）
 *
 * 显示说明：
 *   - 温度/湿度 × 10 存储，显示时需要除以 10
 *     （使用整数除法避免浮点打印：255 → "25.5"）
 *   - 每次 sprintf 之前 memset(display_buf, 0, sizeof)
 *     保证短字符串覆盖长字符串时不显示残留字符
 *   - 首次刷新由 s_first_refresh 标志强制全刷4行，之后按行增量刷新
 */
void app_oled_poll(void)
{
    uint8_t need_refresh = 0;
    char display_buf[32]; /* OLED显示临时缓冲区，最大32字符 */

    /* ===========================================================
     * [1] 事件驱动：消费事件队列，检测是否有 LoRa 新数据
     *     循环消费所有挂起事件，避免队列堆积
     *     只要遇到一个 EVT_LORA_DATA_READY 就标记需要刷新
     * =========================================================== */
    while (event_has_event())
    {
        event_t evt = event_get();
        if (evt.type == EVT_LORA_DATA_READY)
        {
            need_refresh = 1;
        }
        /* 其他事件忽略（OLED 仅关心数据就绪事件） */
    }

    /* ===========================================================
     * [2] 无新数据事件 → 不刷新，直接返回
     *     这是"收到数据才刷新"的核心：避免无意义的 500ms 周期刷屏
     * =========================================================== */
    if (!need_refresh)
    {
        return;
    }

    /* 每次格式化前清空缓冲区 → 保证短字符串无旧字符残留 */
    memset(display_buf, 0, sizeof(display_buf));

    /* =========================================
     * [3] 情况A：尚未收到有效 LoRa 数据
     * ========================================= */
    if (g_sensor_data.valid_flag == 0)
    {
        return;
    }

    /* =========================================
     * [3] 情况B：已有有效传感器数据 → 分4行显示
     *
     * 行级增量刷新：
     *   - 首次刷新（s_first_refresh=1）强制刷新全部4行
     *   - 之后仅当对应行数据发生变化时才刷新该行
     *   - 温度+湿度合并显示在同一行，任一变化即刷新该行
     * ========================================= */

    /* [1] 第0行 (y=0)：温度 + 湿度
     *     temp_x10=255 → "T:25.5"
     *     humi_x10=600 → "H:60.0"
     *     温度或湿度任一变化即刷新该行
     */
    if (s_first_refresh ||
        g_sensor_data.temp_x10 != s_last_temp_x10 ||
        g_sensor_data.humi_x10 != s_last_humi_x10)
    {
        sprintf(display_buf, "T:%d.%d H:%d.%d",
                g_sensor_data.temp_x10 / 10, g_sensor_data.temp_x10 % 10,  /* 温度整数/小数位 */
                g_sensor_data.humi_x10 / 10, g_sensor_data.humi_x10 % 10); /* 湿度整数/小数位 */
        oled_show_string(0, 0, (uint8_t *)display_buf, 16);

        s_last_temp_x10 = g_sensor_data.temp_x10;
        s_last_humi_x10 = g_sensor_data.humi_x10;
    }

    /* [2] 第1行 (y=2)：CO2 浓度 PPM
     *     仅当 co2_ppm 变化时刷新
     */
    if (s_first_refresh ||
        g_sensor_data.co2_ppm != s_last_co2_ppm)
    {
        memset(display_buf, 0, sizeof(display_buf));
        sprintf(display_buf, "CO2: %4dppm", g_sensor_data.co2_ppm);
        oled_show_string(0, 2, (uint8_t *)display_buf, 16);

        s_last_co2_ppm = g_sensor_data.co2_ppm;
    }

    /* [3] 第2行 (y=4)：光照强度 lux
     *     仅当 lux 变化时刷新
     */
    if (s_first_refresh ||
        g_sensor_data.lux != s_last_lux)
    {
        memset(display_buf, 0, sizeof(display_buf));
        sprintf(display_buf, "Lux: %4dLux", g_sensor_data.lux);
        oled_show_string(0, 4, (uint8_t *)display_buf, 16);

        s_last_lux = g_sensor_data.lux;
    }

    /* [4] 第3行 (y=6)：土壤湿度百分比（由原 RSSI 行替换而来）
     *     仅当 soil_humi 变化时刷新
     */
    if (s_first_refresh ||
        g_sensor_data.soil_humi != s_last_soil_humi)
    {
        memset(display_buf, 0, sizeof(display_buf));
        sprintf(display_buf, "Soil: %3d%%", g_sensor_data.soil_humi);
        oled_show_string(0, 6, (uint8_t *)display_buf, 16);

        s_last_soil_humi = g_sensor_data.soil_humi;
    }

    /* 首次刷新完成后清除标志，后续按行增量刷新 */
    s_first_refresh = 0;
}
