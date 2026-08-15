/**
 * @file    app_mqtt.c
 * @brief   WiFi+MQTT业务层实现
 *
 * 职责：
 *   1. app_mqtt_init()：稳定初始化流程（与 16-mqtt/demo1_esp_mqtt 的 esp_mqtt_init 一致）
 *        退出透传 → 复位 → 关闭回显 → WiFi → TCP → 透传 → MQTT CONNECT → SUBSCRIBE
 *   2. app_mqtt_poll()：非阻塞式
 *        a) 解析透传缓冲区，处理 light002 主题下发的 on/off 指令 → 控制 LED3 亮灭
 *        b) 周期（60秒）发送 MQTT PINGREQ 心跳保活
 *        c) 周期（30秒）按主题分别发布传感器数据到巴法云
 *        d) 连接异常时自动重连（15秒重试间隔）
 *
 * 设计要点：
 *   - 初始化阶段（app_mqtt_init）可含阻塞 delay_ms（每步间隔2秒，确保模块稳定）
 *   - 稳定性关键：必须先 esp_exit_transparent_transmission() 退出透传 → esp_reset() 复位
 *     否则模块若处于透传模式，AT 指令会被当普通数据发出去，初始化失败
 *   - 轮询阶段（app_mqtt_poll）使用 HAL_GetTick() 做时间片判断，**无阻塞等待**
 *   - 仅读取 g_sensor_data（只读），不修改；publish 时将传感器格式化为巴法云字符串
 *   - 连接丢失判断：g_esp_rx_end 中若检测到 "CLOSED" 或 TCP 失败，触发重连状态机
 *   - 不消费事件队列（EVT_LORA_DATA_READY 由 app_oled 消费），发布为周期性
 *
 * LED 指示说明：
 *   - LED1 (PWM) ：MQTT连接状态（app_mqtt_init 成功常亮，失败/断开熄灭）
 *   - LED3 (GPIO)：light002 主题下发 on/off 控制（on→点亮，off→熄灭）
 *
 * 巴法云上传格式（与 demo1_esp_mqtt 保持一致，具有教学衔接性）：
 *   主题：humiture004    消息："#T.T#H.H"   （如 "#25.5#60.0"）
 *   主题：co2004         消息："351"         （CO2 ppm 值，纯数字）
 *   主题：photores004    消息："350"         （光照 lux 值，纯数字）
 *   主题：soil004        消息："65"          （土壤湿度 % 值，纯数字）
 *   主题：light002       消息："on" / "off" （云端下发控制LED3）
 */
#include "main.h"
#include "app_mqtt.h"
#include "esp.h"         /* esp_init / esp_exit_transparent_transmission / esp_reset / esp_enable_echo / esp_connect_ap / esp_connect_server 等 */
#include "esp_mqtt.h"    /* mqtt_connect / mqtt_subscribe_topic / mqtt_publish_data / mqtt_send_heart / BAFA_TOPIC_xxx / g_mqtt_msg 等 */
#include "sensor_data.h" /* g_sensor_data：只读共享传感器数据 */
#include "delay.h"       /* delay_ms（初始化阶段的延时） */
#include "led.h"         /* led_ctrl：LED1 状态指示 MQTT连接状态 */
#include <stdio.h>       /* printf / sprintf */
#include <string.h>      /* memset / strlen / strstr */

/* ============================ 外部变量声明 ============================ */

/* g_mqtt_msg 在 esp_mqtt.c 中定义，用于存储待发布的消息内容 */
extern char g_mqtt_msg[];

/* ============================ 内部状态与时间计数 ============================ */

static uint8_t s_mqtt_connected = 0;     /* MQTT是否已连接：1=已连接，0=未连接 */
static uint32_t s_last_heart_tick = 0;   /* 上次发送心跳的时间戳 */
static uint32_t s_last_publish_tick = 0; /* 上次发布数据的时间戳 */
static uint32_t s_last_publish_seq = 0;  /* 上次发布的数据序列（对比新鲜度） */

/* ============================ 内部辅助函数（mqtt 包解析简化版） ============================ */

/**
 * @brief  简化版 MQTT PUBLISH 消息处理
 *
 * @details
 *   处理 light002 主题下发的 on/off 控制指令：
 *     "on"  → LED3 点亮
 *     "off" → LED3 熄灭
 *   并串口打印原始信息（便于调试观察实际下发内容）。
 *   完整解析代码参考 16-mqtt\02-代码\demo1_esp_mqtt 中 esp_mqtt.c 的解析实现。
 *
 * @note   简化版使用 strstr 关键字匹配（透传模式 raw MQTT 字节流）。
 * @note   关键实现：跳过 MQTT 报头（4字节：固定报头1+剩余长度1+topic长度2），
 *         从 offset=4 开始搜索 on/off。
 *         原因：topic 长度字段高字节为 0x00（如 0x00 0x08），若从 buf[0] 搜索，
 *         0x00 会截断 strstr 字符串，导致永远找不到 on/off。
 * @note   先判断 "off"（更长）再判断 "on"，避免 "off" 中的 "on" 子串被先命中。
 * @note   生产环境建议逐字节解析 MQTT PUBLISH 报头后仅比较 payload 段。
 */
static void app_mqtt_handle_publish(void)
{
    /* MQTT PUBLISH 报文结构（巴法云 light002 主题下发 "on"/"off"）：
     *   [0]    固定报头第1字节：0x30 | (DUP/QoS/RETAIN)
     *   [1]    剩余长度（变长编码，短消息为1字节）
     *   [2-3]  Topic 长度（2字节大端，如 light002=8 → 0x00 0x08）
     *   [4..]  Topic 字符串（"light002"）
     *   [N..]  Payload（"on" 或 "off"）
     *
     * 关键问题：Topic 长度高字节为 0x00（如 0x00 0x08），
     *   若用 strstr 从 buf[0] 搜索，0x00 会截断字符串导致搜索失败。
     *   必须**跳过 MQTT 报头**，只在 Topic+Payload 区域搜索。
     *
     * 简化策略：从 offset=4（跳过"报头+剩余长度+topic长度"4字节）开始搜索
     *   - offset=4 起就是 Topic 字符串 "light002" + Payload "on"/"off"
     *   - "light002" 不含 "on"/"off" 子串，不会误匹配
     *   - 在搜索前先在 buf[g_esp_rx_cnt] 写 0 确保 null 终止
     */
    char *search_start;

    /* 确保 strstr 可靠：在有效数据末尾添加 null 终止符 */
    if (g_esp_rx_cnt < ESP_BUF_SIZE)
    {
        g_esp_rx_buf[g_esp_rx_cnt] = 0;
    }
    else
    {
        g_esp_rx_buf[ESP_BUF_SIZE - 1] = 0;
    }

    /* 先打印原始下发内容（调试用：便于观察实际 payload）
     * g_esp_rx_cnt 为本次接收总长度 */
    printf("[MQTT] 服务器下发内容(len=%lu): ", (unsigned long)g_esp_rx_cnt);
    {
        uint16_t i;
        for (i = 0; i < g_esp_rx_cnt; i++)
        {
            /* 可打印字符直接输出，不可打印用点替代 */
            char c = (char)g_esp_rx_buf[i];
            if (c >= 0x20 && c <= 0x7E)
                printf("%c", c);
            else
                printf(".");
        }
    }
    printf("\r\n");

    /* 跳过 MQTT 报头（固定报头1 + 剩余长度1 + topic长度2 = 4字节）
     * 从 offset=4 开始搜索，避开 topic 长度字段的 0x00 字节 */
    search_start = (char *)&g_esp_rx_buf[4];

    /* 主题白名单保护：仅处理 BAFA_TOPIC_LED (light002) 主题下发的指令，
     * 其它主题（如历史遗留 humiture004 订阅导致的自回推）不会触发 LED 控制 */
    if (strncmp(search_start, BAFA_TOPIC_LED, strlen(BAFA_TOPIC_LED)) != 0)
    {
        printf("[MQTT] 非控制主题（%s），忽略\r\n", BAFA_TOPIC_LED);
        return;
    }

    /* 检测 payload 中是否包含 on/off 关键字
     *   "on"  → LED3 点亮
     *   "off" → LED3 熄灭
     * 注意：先判断 "off"（更长）再判断 "on"，避免 "off" 中的 "on" 子串被先命中 */
    if (strstr(search_start, "off") != NULL)
    {
        printf("[MQTT] 指令: OFF → LED3 熄灭\r\n");
        led_ctrl(LED3, LED_OFF);
    }
    else if (strstr(search_start, "on") != NULL)
    {
        printf("[MQTT] 指令: ON → LED3 点亮\r\n");
        led_ctrl(LED3, LED_ON);
    }
    else
    {
        /* 其他指令仅打印，便于调试 */
        printf("[MQTT] 未识别的 on/off 指令（忽略）\r\n");
    }
}

/* ============================ 接口函数实现 ============================ */

/**
 * @brief  初始化 WiFi+MQTT（阻塞，仅初始化阶段调用一次）
 *
 * 流程（与 16-mqtt/demo1_esp_mqtt 的 esp_mqtt_init 代码逻辑完全一致，保持教学衔接）：
 *   [1] esp_init()                           初始化 ESP12-F（USART2 115200bps）
 *   [2] esp_exit_transparent_transmission()   退出透传模式（关键：确保AT指令可达）
 *   [3] esp_reset()                          AT+RST 复位模块（恢复已知干净状态）
 *   [4] esp_enable_echo(0)                   关闭回显（减少串口干扰）
 *   [5] esp_connect_ap(SSID, PWD)            Station 模式连接 WiFi 热点
 *   [6] esp_connect_server(TCP, bemfa, 9501) TCP 连接巴法云 MQTT 服务器
 *   [7] esp_entry_transparent_transmission() 进入透传模式（MQTT 字节流直通）
 *   [8] mqtt_connect(CLIENTID, user, pwd)    MQTT CONNECT 协议握手
 *   [9] mqtt_subscribe_topic(LED)            订阅控制主题 light002（接收 on/off 指令）
 *   [10]成功 → s_mqtt_connected=1，重置心跳/发布时间戳，点亮 LED1
 *
 * @note   稳定性要点：必须先 [2] 退出透传 → [3] 复位，再发任何 AT 指令
 *         否则模块若处于透传模式，AT 指令会被当普通数据发出去，初始化失败
 * @note   任何一步失败：熄灭 LED1，s_mqtt_connected=0，由 poll 自动重连
 */
void app_mqtt_init(void)
{
    esp_err_t esp_err;
    int32_t mqtt_err;

    printf("[MQTT] ===== 初始化 WiFi+MQTT =====\r\n");

    /* 进入初始化前先熄灭 LED1（表示未连接/正在初始化）
     * 只有所有步骤都成功走到函数末尾才会点亮 LED1 */
    led_ctrl(LED1, LED_OFF);
    s_mqtt_connected = 0;

    /* [1] 初始化 ESP12-F 模块（USART2 115200bps + 空闲中断接收） */
    printf("[MQTT] (1/9) 初始化 ESP12-F...\r\n");
    esp_init();

    /* [2] 退出透传模式（发送"+++"）
     *     这是稳定初始化的关键：若模块上次运行在透传模式，
     *     不退出就直接发 AT 指令会导致 AT 被当作数据发到服务器，指令无效 */
    printf("[MQTT] (2/9) 退出透传模式...\r\n");
    esp_err = esp_exit_transparent_transmission();
    if (esp_err != ESP_OK)
    {
        printf("[MQTT] 退出透传失败，标记未连接\r\n");
        led_ctrl(LED1, LED_OFF);
        s_mqtt_connected = 0;
        return;
    }
    delay_ms(2000);

    /* [3] 复位 ESP 模块（AT+RST）
     *     复位后模块回到已知干净状态，避免上次残留配置影响本次连接 */
    printf("[MQTT] (3/9) 复位 ESP 模块...\r\n");
    esp_err = esp_reset();
    if (esp_err != ESP_OK)
    {
        printf("[MQTT] 复位失败，标记未连接\r\n");
        led_ctrl(LED1, LED_OFF);
        s_mqtt_connected = 0;
        return;
    }
    delay_ms(2000);

    /* [4] 关闭回显（ATE0）
     *     关闭后 AT 指令响应中不再回显指令本身，简化解析 */
    printf("[MQTT] (4/9) 关闭回显...\r\n");
    esp_err = esp_enable_echo(0);
    if (esp_err != ESP_OK)
    {
        printf("[MQTT] 关闭回显失败，标记未连接\r\n");
        led_ctrl(LED1, LED_OFF);
        s_mqtt_connected = 0;
        return;
    }
    delay_ms(2000);

    /* [5] 连接 WiFi 热点（Station 模式） */
    printf("[MQTT] (5/9) 连接 WiFi SSID=%s...\r\n", WIFI_SSID);
    esp_err = esp_connect_ap((char *)WIFI_SSID, (char *)WIFI_PASSWORD);
    if (esp_err != ESP_OK)
    {
        printf("[MQTT] WiFi连接失败，标记未连接\r\n");
        led_ctrl(LED1, LED_OFF);
        s_mqtt_connected = 0;
        return;
    }
    printf("[MQTT] WiFi连接成功\r\n");
    delay_ms(2000);

    /* [6] TCP 连接巴法云 MQTT 服务器（bemfa.com:9501） */
    printf("[MQTT] (6/9) 连接巴法云 TCP 服务器...\r\n");
    esp_err = esp_connect_server(ESP_PROTOCOL_TCP, (char *)MQTT_BROKERADDRESS, 9501);
    if (esp_err != ESP_OK)
    {
        printf("[MQTT] TCP连接失败，标记未连接\r\n");
        led_ctrl(LED1, LED_OFF);
        s_mqtt_connected = 0;
        return;
    }
    printf("[MQTT] TCP连接成功\r\n");
    delay_ms(2000);

    /* [7] 进入透传模式（后续 MQTT 字节流直接透传到 TCP） */
    printf("[MQTT] (7/9) 进入透传模式...\r\n");
    esp_err = esp_entry_transparent_transmission();
    if (esp_err != ESP_OK)
    {
        printf("[MQTT] 进入透传失败，标记未连接\r\n");
        led_ctrl(LED1, LED_OFF);
        s_mqtt_connected = 0;
        return;
    }
    printf("[MQTT] 透传模式已开启\r\n");
    delay_ms(2000);

    /* [8] MQTT CONNECT 协议握手（发送 CONNECT，等待 CONNACK） */
    printf("[MQTT] (8/9) MQTT CONNECT 握手...\r\n");
    mqtt_err = mqtt_connect((char *)MQTT_CLIENTID, (char *)MQTT_USARNAME, (char *)MQTT_PASSWD);
    if (mqtt_err != 0)
    {
        printf("[MQTT] MQTT CONNECT失败，标记未连接\r\n");
        led_ctrl(LED1, LED_OFF);
        s_mqtt_connected = 0;
        return;
    }
    printf("[MQTT] MQTT CONNECT成功\r\n");
    delay_ms(2000);

    /* [9] 订阅主题
     *     - 仅订阅 BAFA_TOPIC_LED (light002)：接收云端下发的 LED on/off 控制指令
     *     上传主题（humiture004/co2004/photores004/soil004）只发布、不订阅：
     *       若订阅了自己发布的主题，MQTT 服务器会把自身发布的消息回推给自己，
     *       导致误判为控制指令（打印“未识别的 on/off 指令”）。 */
    printf("[MQTT] (9/9) 订阅主题...\r\n");
    if (mqtt_subscribe_topic((char *)BAFA_TOPIC_LED, 0, 1) != 0)
    {
        printf("[MQTT] 订阅 %s 失败，标记未连接\r\n", BAFA_TOPIC_LED);
        led_ctrl(LED1, LED_OFF);
        s_mqtt_connected = 0;
        return;
    }
    printf("[MQTT] 主题订阅成功 (%s)\r\n", BAFA_TOPIC_LED);

    /* [10] 初始化成功，重置时间戳，准备 poll 循环 */
    s_mqtt_connected = 1;
    s_last_heart_tick = HAL_GetTick();
    s_last_publish_tick = HAL_GetTick();
    s_last_publish_seq = 0;

    /* WiFi+TCP+MQTT 全部成功 → LED1 常亮，指示连接正常 */
    led_ctrl(LED1, LED_ON);

    printf("[MQTT] ===== WiFi+MQTT 初始化完成 =====\r\n\r\n");
}

/**
 * @brief  WiFi+MQTT 周期任务（由调度器调用，非阻塞）
 *
 * 处理流程（与 esp_mqtt_test1 主循环相同逻辑，但拆成非阻塞）：
 *
 *   [A] 透传缓冲区解析：
 *       g_esp_rx_end && g_esp_transparent_transmission_sta → 解析 MQTT 包
 *       → 判断 PUBLISH 报头 (buf[0] & 0xF0) == 0x30（兼容 QoS0/1/2 + DUP + RETAIN）
 *       → app_mqtt_handle_publish()：跳过报头搜索 on/off → 控制 LED3 亮灭
 *       → "CLOSED"/ERROR → 熄灭 LED1，s_mqtt_connected=0（触发重连）
 *
 *   [B] 心跳保活：每 60 秒调用 mqtt_send_heart()
 *
 *   [C] 数据发布：每 30 秒且 valid_flag=1 → 按主题分别发布
 *       humiture004 : "#温度#湿度"
 *       co2004      : CO2 ppm 值
 *       photores004 : 光照 lux 值
 *       soil004     : 土壤湿度 % 值
 *
 *   [D] 连接异常：s_mqtt_connected=0 →
 *       每 15 秒调用 app_mqtt_init() 重连（内部会重新点亮 LED1）
 *
 * @note   本函数不得含 HAL_Delay / while 等待等阻塞操作
 * @note   EVT_LORA_DATA_READY 事件由 app_oled 消费（事件驱动刷新显示），
 *         MQTT 发布为周期性，不依赖事件队列，避免消费者竞争
 * @note   LED1 状态：连接成功常亮，断开/失败熄灭（由 app_mqtt_init 控制）
 * @note   LED3 状态：light002 主题下发 on/off 控制（由 app_mqtt_handle_publish 控制）
 */
void app_mqtt_poll(void)
{
    /* ===========================================================
     * [A] 解析透传缓冲区 MQTT 消息（非阻塞）
     * =========================================================== */
    if (s_mqtt_connected && g_esp_rx_end && g_esp_transparent_transmission_sta)
    {
        uint16_t offset = 0;

        /* 简化版解析：判断是否为 MQTT PUBLISH 报文（0x3x）
         * MQTT 固定报头高4位=0x3 表示 PUBLISH，低4位为 DUP/QoS/RETAIN 标志
         *   0x30 = QoS0, 0x32 = QoS1, 0x3A = QoS1+DUP ...
         * 用 (buf[0] & 0xF0) == 0x30 兼容所有 PUBLISH 变体
         * （完整版逐包解析同 16-mqtt\02-代码\demo1_esp_mqtt\User\bsp\esp_mqtt.c） */
        if (g_esp_rx_cnt > 2)
        {
            if ((g_esp_rx_buf[offset] & 0xF0) == 0x30)
            {
                app_mqtt_handle_publish();
            }
        }

        /* 检查连接断开 */
        if (strstr((char *)g_esp_rx_buf, "CLOSED") != NULL ||
            strstr((char *)g_esp_rx_buf, "ERROR") != NULL)
        {
            printf("[MQTT] 检测到连接断开(CLOSED/ERROR)，标记待重连\r\n");
            led_ctrl(LED1, LED_OFF); /* 服务器断开 → LED1 熄灭 */
            s_mqtt_connected = 0;
        }

        /* 处理完毕，清空缓冲区并重启接收 */
        esp_clear_rx_buf();
    }

    /* ===========================================================
     * [B] 心跳保活：每60秒发送 PINGREQ
     * =========================================================== */
    if (s_mqtt_connected &&
        (HAL_GetTick() - s_last_heart_tick) >= 60000)
    {
        s_last_heart_tick = HAL_GetTick();
        printf("[MQTT] 发送心跳保活 (PINGREQ)\r\n");
        mqtt_send_heart();
    }

    /* ===========================================================
     * [C] 周期发布：每30秒且数据有效 → 按主题分别上传到巴法云
     *     主题映射（与 esp_mqtt.h 中 BAFA_TOPIC_xxx 宏定义一致）：
     *       humiture004 : "#温度#湿度"（巴法云温湿度格式，与 demo1_esp_mqtt 一致）
     *       co2004      : CO2 ppm 值（纯数字字符串）
     *       photores004 : 光照 lux 值（纯数字字符串）
     *       soil004     : 土壤湿度 % 值（纯数字字符串）
     * =========================================================== */
    if (s_mqtt_connected &&
        g_sensor_data.valid_flag == 1 &&
        (HAL_GetTick() - s_last_publish_tick) >= 30000)
    {
        int temp_i, temp_d, humi_i, humi_d;

        /* [1] 温湿度 → humiture004
         *     将温度、湿度（×10存储）恢复为一位小数的字符串
         *     255 → "25.5"，600 → "60.0"，最终："#25.5#60.0"
         */
        temp_i = g_sensor_data.temp_x10 / 10;
        temp_d = g_sensor_data.temp_x10 % 10;
        humi_i = g_sensor_data.humi_x10 / 10;
        humi_d = g_sensor_data.humi_x10 % 10;

        /* 负数保护：temp_x10 为负时小数位应保持正 */
        if (temp_d < 0)
            temp_d = -temp_d;
        if (humi_d < 0)
            humi_d = -humi_d;

        sprintf(g_mqtt_msg, "#%d.%d#%d.%d", temp_i, temp_d, humi_i, humi_d);
        mqtt_publish_data(BAFA_TOPIC_HUMITURE, g_mqtt_msg, 0);
        printf("[MQTT][发布] %s -> %s\r\n", g_mqtt_msg, BAFA_TOPIC_HUMITURE);

        /* [2] CO2 浓度 → co2004（纯数字字符串） */
        sprintf(g_mqtt_msg, "%d", g_sensor_data.co2_ppm);
        mqtt_publish_data(BAFA_TOPIC_CO2, g_mqtt_msg, 0);
        printf("[MQTT][发布] %s -> %s\r\n", g_mqtt_msg, BAFA_TOPIC_CO2);

        /* [3] 光照强度 → photores004（纯数字字符串） */
        sprintf(g_mqtt_msg, "%d", g_sensor_data.lux);
        mqtt_publish_data(BAFA_TOPIC_PHOTORES, g_mqtt_msg, 0);
        printf("[MQTT][发布] %s -> %s\r\n", g_mqtt_msg, BAFA_TOPIC_PHOTORES);

        /* [4] 土壤湿度 → soil004（纯数字字符串，百分比 0~100） */
        sprintf(g_mqtt_msg, "%d", g_sensor_data.soil_humi);
        mqtt_publish_data(BAFA_TOPIC_SOIL, g_mqtt_msg, 0);
        printf("[MQTT][发布] %s -> %s\r\n", g_mqtt_msg, BAFA_TOPIC_SOIL);

        s_last_publish_tick = HAL_GetTick();
        s_last_publish_seq++;
    }

    /* ===========================================================
     * [D] 连接异常重连（简化版：失败后15秒重试一次）
     * =========================================================== */
    if (!s_mqtt_connected)
    {
        static uint32_t s_last_reconnect_tick = 0;

        if ((HAL_GetTick() - s_last_reconnect_tick) >= 15000)
        {
            s_last_reconnect_tick = HAL_GetTick();
            printf("[MQTT] 尝试重连 WiFi+MQTT...\r\n");
            app_mqtt_init(); /* 复用初始化流程：ESP自检→WiFi→MQTT */
        }
    }
}
