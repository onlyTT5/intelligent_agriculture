/*
 * ============================================================================
 * 文件名称：esp_mqtt.c
 * 文件描述：ESP12-F MQTT客户端实现文件（巴法云版本）
 *
 * 功能描述：
 *   本文件实现了基于ESP12-F模块的MQTT客户端功能，支持巴法云平台接入。
 *   包含MQTT连接、订阅、发布、心跳等核心功能。
 *
 * MQTT协议实现：
 *   - 协议版本：MQTT 3.1.1
 *   - 支持QoS 0消息发布
 *   - 支持心跳保活机制（60秒）
 *   - 透传模式下的数据收发
 *
 * 实现流程：
 *   1. ESP模块初始化（退出透传、复位、关闭回显）
 *   2. 连接WiFi热点
 *   3. 连接MQTT服务器（TCP连接）
 *   4. 进入透传模式
 *   5. MQTT协议握手（CONNECT）
 *   6. 订阅主题（SUBSCRIBE）
 *   7. 定时发送心跳包（PINGREQ）
 *   8. 发布数据（PUBLISH）
 *
 * 硬件说明：
 *   - ESP12-F连接在USART2（TX: PA2, RX: PA3）
 *   - 波特率：115200bps
 *   - AT指令集与ESP8266完全兼容
 *   - 使用HAL_UARTEx_ReceiveToIdle_IT实现空闲中断接收
 *   - 调试输出通过USART1（PA9/PA10）printf打印
 *
 * 作者：温老师
 * 日期：2026/05/09
 * 版本：V1.0（巴法云适配版）
 * ============================================================================
 */

#include "main.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "delay.h"
#include "usart.h"
#include "esp.h"
#include "esp_mqtt.h"

/* ==================== MQTT协议固定数据包定义 ==================== */

/**
 * @brief MQTT CONNECT ACK响应包
 * 服务器收到CONNECT请求后返回的成功响应
 * 格式：0x20(类型) + 0x02(长度) + 0x00 0x00(返回码，0表示成功)
 */
const uint8_t g_packet_connect_ack[4] = {0x20, 0x02, 0x00, 0x00};

/**
 * @brief MQTT DISCONNECT数据包
 * 客户端主动断开连接时发送
 * 格式：0xe0(类型) + 0x00(长度)
 */
const uint8_t g_packet_disconnect[2] = {0xe0, 0x00};

/**
 * @brief MQTT PINGREQ心跳包
 * 客户端向服务器发送心跳，保持连接活跃
 * 格式：0xc0(类型) + 0x00(长度)
 */
const uint8_t g_packet_heart[2] = {0xc0, 0x00};

/**
 * @brief MQTT PINGRESP心跳响应包
 * 服务器对PINGREQ的响应
 * 格式：0xd0(类型) + 0x00(长度)
 */
const uint8_t g_packet_heart_reply[2] = {0xd0, 0x00};

/**
 * @brief MQTT SUBSCRIBE ACK响应包
 * 服务器对SUBSCRIBE请求的响应
 * 格式：0x90(类型) + 0x03(长度)
 */
const uint8_t g_packet_sub_ack[2] = {0x90, 0x03};

/* ==================== 全局变量定义 ==================== */

/**
 * @brief MQTT消息缓冲区
 * 用于存储待发布的消息内容
 */
char g_mqtt_msg[526];

/**
 * @brief MQTT发送数据长度
 * 记录当前正在组装的MQTT数据包长度
 */
uint32_t g_mqtt_tx_len;

/* ==================== MQTT基础函数实现 ==================== */

/**
 * @brief MQTT发送数据
 * 将数据通过ESP模块发送到MQTT服务器
 * @param buf 数据缓冲区指针
 * @param len 数据长度
 */
void mqtt_send_bytes(uint8_t *buf, uint32_t len)
{
    esp_send_bytes(buf, len);
}

/**
 * @brief 发送MQTT心跳包
 * 向服务器发送PINGREQ，保持连接活跃
 */
void mqtt_send_heart(void)
{
    mqtt_send_bytes((uint8_t *)g_packet_heart, sizeof(g_packet_heart));
}

/**
 * @brief MQTT断开连接
 * 发送DISCONNECT数据包，主动断开与服务器的连接
 */
void mqtt_disconnect(void)
{
    mqtt_send_bytes((uint8_t *)g_packet_disconnect, sizeof(g_packet_disconnect));
}

/**
 * @brief MQTT协议初始化
 * 清空收发缓冲区并发送断开连接包，确保处于干净状态
 * @param prx 接收缓冲区指针（未使用）
 * @param rxlen 接收缓冲区长度（未使用）
 * @param ptx 发送缓冲区指针（未使用）
 * @param txlen 发送缓冲区长度（未使用）
 */
void mqtt_init(uint8_t *prx, uint16_t rxlen, uint8_t *ptx, uint16_t txlen)
{
    /* 清空ESP发送缓冲区 */
    memset(g_esp_tx_buf, 0, sizeof(g_esp_tx_buf));
    /* 清空ESP接收缓冲区 */
    memset((void *)g_esp_rx_buf, 0, sizeof(g_esp_rx_buf));

    /* 发送两次断开连接包，确保断开干净 */
    mqtt_disconnect();
    delay_ms(100);
    mqtt_disconnect();
    delay_ms(100);
}

/**
 * @brief MQTT连接服务器（打包并发送CONNECT数据包）
 *
 * 构建并发送MQTT CONNECT数据包，建立客户端与MQTT服务器的连接。
 * CONNECT是MQTT协议中客户端发起连接的第一个数据包，必须在完成TCP连接后发送。
 * 服务器收到CONNECT后会验证客户端身份，并返回CONNACK响应。
 *
 * CONNECT数据包结构：
 *   固定报头：0x10 + 剩余长度（变长编码）
 *   可变报头：协议名("MQTT") + 协议级别(0x04) + 连接标志(0x02) + 心跳间隔(60秒)
 *   Payload：客户端ID长度 + 客户端ID内容
 *
 * CONNACK响应：
 *   0x20 0x02 + 会话标志 + 返回码（0x00=成功）
 *
 * 连接标志0x02 = Clean Session=1，不使用遗嘱、用户名密码
 *
 * @param client_id 客户端标识符（巴法云使用UID）
 * @param user_name 用户名（巴法云可为空）
 * @param password 密码（巴法云可为空）
 * @return 0=连接成功，-1=连接失败
 */
int32_t mqtt_connect(char *client_id, char *user_name, char *password)
{
    uint32_t client_id_len = strlen(client_id);
    uint32_t user_name_len = strlen(user_name);
    uint32_t password_len = strlen(password);
    uint32_t data_len;
    uint32_t cnt = 2; /* 重试次数 */
    uint32_t wait = 0;

    g_mqtt_tx_len = 0;

    /* 计算数据包长度：可变报头(10) + Payload */
    data_len = 10 + (client_id_len + 2);
    if (user_name_len > 0)
    {
        data_len += (user_name_len + 2);
    }
    if (password_len > 0)
    {
        data_len += (password_len + 2);
    }

    /* 构建固定报头：报文类型 */
    g_esp_tx_buf[g_mqtt_tx_len++] = 0x10;

    /* 剩余长度（变长编码） */
    do
    {
        uint8_t encodedByte = data_len % 128;
        data_len = data_len / 128;
        if (data_len > 0)
        {
            encodedByte |= 128;
        }
        g_esp_tx_buf[g_mqtt_tx_len++] = encodedByte;
    } while (data_len > 0);

    /* 构建可变报头 */
    /* 协议名长度 */
    g_esp_tx_buf[g_mqtt_tx_len++] = 0;
    g_esp_tx_buf[g_mqtt_tx_len++] = 4;
    /* 协议名 "MQTT" */
    g_esp_tx_buf[g_mqtt_tx_len++] = 'M';
    g_esp_tx_buf[g_mqtt_tx_len++] = 'Q';
    g_esp_tx_buf[g_mqtt_tx_len++] = 'T';
    g_esp_tx_buf[g_mqtt_tx_len++] = 'T';
    /* 协议级别 MQTT 3.1.1 */
    g_esp_tx_buf[g_mqtt_tx_len++] = 4;
    /* 连接标志：Clean Session=1 */
    g_esp_tx_buf[g_mqtt_tx_len++] = 0x02;
    /* 心跳间隔：60秒 */
    g_esp_tx_buf[g_mqtt_tx_len++] = 0;
    g_esp_tx_buf[g_mqtt_tx_len++] = 60;

    /* 构建Payload：客户端ID */
    g_esp_tx_buf[g_mqtt_tx_len++] = BYTE1(client_id_len);
    g_esp_tx_buf[g_mqtt_tx_len++] = BYTE0(client_id_len);
    memcpy(&g_esp_tx_buf[g_mqtt_tx_len], client_id, client_id_len);
    g_mqtt_tx_len += client_id_len;

    /* 用户名字段（可选） */
    if (user_name_len > 0)
    {
        g_esp_tx_buf[g_mqtt_tx_len++] = BYTE1(user_name_len);
        g_esp_tx_buf[g_mqtt_tx_len++] = BYTE0(user_name_len);
        memcpy(&g_esp_tx_buf[g_mqtt_tx_len], user_name, user_name_len);
        g_mqtt_tx_len += user_name_len;
    }

    /* 密码字段（可选） */
    if (password_len > 0)
    {
        g_esp_tx_buf[g_mqtt_tx_len++] = BYTE1(password_len);
        g_esp_tx_buf[g_mqtt_tx_len++] = BYTE0(password_len);
        memcpy(&g_esp_tx_buf[g_mqtt_tx_len], password, password_len);
        g_mqtt_tx_len += password_len;
    }

    /* 发送CONNECT并等待CONNACK响应，最多重试2次 */
    while (cnt--)
    {
        /* 必须调用esp_clear_rx_buf()重启UART接收（HAL库特性） */
        esp_clear_rx_buf();

        mqtt_send_bytes(g_esp_tx_buf, g_mqtt_tx_len);

        wait = 3000;
        while (wait--)
        {
            uint16_t i;
            delay_ms(1);

            /* 在整个接收缓冲区中搜索CONNACK响应（0x20 0x02） */
            for (i = 0; i + 1 < g_esp_rx_cnt; i++)
            {
                if ((g_esp_rx_buf[i] == g_packet_connect_ack[0]) &&
                    (g_esp_rx_buf[i + 1] == g_packet_connect_ack[1]))
                {
                    return 0; /* 连接成功 */
                }
            }
        }
    }

    return -1; /* 连接失败 */
}

/**
 * @brief MQTT订阅/取消订阅主题
 *
 * 构建并发送MQTT SUBSCRIBE（订阅）或UNSUBSCRIBE（取消订阅）数据包。
 *
 * SUBSCRIBE数据包结构：
 *   固定报头：0x82(SUBSCRIBE) / 0xA2(UNSUBSCRIBE) + 剩余长度
 *   可变报头：报文标识符（2字节，固定0x0001）
 *   Payload：主题名长度 + 主题名 + QoS等级（仅订阅时）
 *
 * SUBACK响应：0x90 0x03 + 报文标识符 + 授予的QoS
 *
 * @param topic    主题名称
 * @param qos      QoS等级（0或1）
 * @param whether  1=订阅，0=取消订阅
 * @return 0=成功，-1=失败
 */
int32_t mqtt_subscribe_topic(char *topic, uint8_t qos, uint8_t whether)
{
    uint32_t cnt = 2;
    uint32_t wait = 0;
    uint32_t topiclen = strlen(topic);

    /* 计算数据包长度 */
    uint32_t data_len = 2 + (topiclen + 2) + (whether ? 1 : 0);

    g_mqtt_tx_len = 0;

    /* 构建固定报头 */
    if (whether)
    {
        g_esp_tx_buf[g_mqtt_tx_len++] = 0x82; /* SUBSCRIBE */
    }
    else
    {
        g_esp_tx_buf[g_mqtt_tx_len++] = 0xA2; /* UNSUBSCRIBE */
    }

    /* 剩余长度（变长编码） */
    do
    {
        uint8_t encodedByte = data_len % 128;
        data_len = data_len / 128;
        if (data_len > 0)
        {
            encodedByte |= 128;
        }
        g_esp_tx_buf[g_mqtt_tx_len++] = encodedByte;
    } while (data_len > 0);

    /* 构建可变报头：报文标识符 */
    g_esp_tx_buf[g_mqtt_tx_len++] = 0;
    g_esp_tx_buf[g_mqtt_tx_len++] = 0x01;

    /* 构建Payload：主题名 */
    g_esp_tx_buf[g_mqtt_tx_len++] = BYTE1(topiclen);
    g_esp_tx_buf[g_mqtt_tx_len++] = BYTE0(topiclen);
    memcpy(&g_esp_tx_buf[g_mqtt_tx_len], topic, topiclen);
    g_mqtt_tx_len += topiclen;

    /* 订阅时添加QoS等级 */
    if (whether)
    {
        g_esp_tx_buf[g_mqtt_tx_len++] = qos;
    }

    /* 发送并等待SUBACK响应，最多重试2次 */
    while (cnt--)
    {
        /* 必须调用esp_clear_rx_buf()重启UART接收（HAL库特性） */
        esp_clear_rx_buf();

        mqtt_send_bytes(g_esp_tx_buf, g_mqtt_tx_len);

        wait = 3000;
        while (wait--)
        {
            uint16_t i;
            delay_ms(1);

            /* 在整个接收缓冲区中搜索SUBACK响应（0x90 0x03） */
            for (i = 0; i + 1 < g_esp_rx_cnt; i++)
            {
                if ((g_esp_rx_buf[i] == g_packet_sub_ack[0]) &&
                    (g_esp_rx_buf[i + 1] == g_packet_sub_ack[1]))
                {
                    return 0; /* 订阅成功 */
                }
            }
        }
    }

    return -1; /* 订阅失败 */
}

/**
 * @brief MQTT发布消息
 *
 * 构建并发送MQTT PUBLISH数据包，向服务器发布消息。
 *
 * PUBLISH数据包结构：
 *   固定报头：0x30 + 剩余长度（变长编码）
 *   可变报头：主题名长度 + 主题名 + [报文标识符（QoS>0时）]
 *   Payload：消息内容
 *
 * @param topic   主题名称
 * @param message 消息内容
 * @param qos     QoS等级（0或1）
 * @return 发送的字节数
 */
uint32_t mqtt_publish_data(char *topic, char *message, uint8_t qos)
{
    static uint16_t id = 0; /* 报文标识符，QoS>0时使用 */

    uint32_t topicLength = strlen(topic);
    uint32_t messageLength = strlen(message);
    uint32_t data_len;
    uint8_t encodedByte;

    g_mqtt_tx_len = 0;

    /* 计算剩余长度 */
    if (qos)
    {
        data_len = (2 + topicLength) + 2 + messageLength;
    }
    else
    {
        data_len = (2 + topicLength) + messageLength;
    }

    /* 构建固定报头 */
    g_esp_tx_buf[g_mqtt_tx_len++] = 0x30;

    /* 剩余长度（变长编码） */
    do
    {
        encodedByte = data_len % 128;
        data_len = data_len / 128;
        if (data_len > 0)
        {
            encodedByte |= 128;
        }
        g_esp_tx_buf[g_mqtt_tx_len++] = encodedByte;
    } while (data_len > 0);

    /* 构建可变报头：主题名 */
    g_esp_tx_buf[g_mqtt_tx_len++] = BYTE1(topicLength);
    g_esp_tx_buf[g_mqtt_tx_len++] = BYTE0(topicLength);
    memcpy(&g_esp_tx_buf[g_mqtt_tx_len], topic, topicLength);
    g_mqtt_tx_len += topicLength;

    /* QoS>0时添加报文标识符 */
    if (qos)
    {
        g_esp_tx_buf[g_mqtt_tx_len++] = BYTE1(id);
        g_esp_tx_buf[g_mqtt_tx_len++] = BYTE0(id);
        id++;
    }

    /* 构建Payload：消息内容 */
    memcpy(&g_esp_tx_buf[g_mqtt_tx_len], message, messageLength);
    g_mqtt_tx_len += messageLength;

    /* 发送数据包 */
    mqtt_send_bytes(g_esp_tx_buf, g_mqtt_tx_len);

    return g_mqtt_tx_len;
}

/* ==================== ESP MQTT初始化函数 ==================== */

/**
 * @brief ESP MQTT完整初始化函数
 *
 * 初始化流程：
 *   1. ESP模块基础初始化
 *   2. 退出透传模式
 *   3. 复位ESP模块
 *   4. 关闭回显
 *   5. 连接WiFi热点
 *   6. 建立TCP连接到巴法云MQTT服务器
 *   7. 进入透传模式
 *   8. MQTT协议连接（CONNECT）
 *   9. 订阅主题（SUBSCRIBE）
 *
 * @return 0=成功，负数=失败
 */
int32_t esp_mqtt_init(void)
{
    int32_t rt;

    /* 步骤1: ESP模块基础初始化 */
    esp_init();

    /* 步骤2: 退出透传模式 */
    rt = esp_exit_transparent_transmission();
    if (rt)
    {
        printf("esp_exit_transparent_transmission fail\r\n");
        return -1;
    }
    printf("esp_exit_transparent_transmission success\r\n");
    delay_ms(2000);

    /* 步骤3: 复位ESP模块 */
    rt = esp_reset();
    if (rt)
    {
        printf("esp_reset fail\r\n");
        return -2;
    }
    delay_ms(2000);

    /* 步骤4: 关闭回显 */
    rt = esp_enable_echo(0);
    if (rt)
    {
        printf("esp_enable_echo(0) fail\r\n");
        return -3;
    }
    printf("esp_enable_echo(0) success\r\n");
    delay_ms(2000);

    /* 步骤5: 连接WiFi热点 */
    rt = esp_connect_ap(WIFI_SSID, WIFI_PASSWORD);
    if (rt)
    {
        printf("esp_connect_ap fail\r\n");
        return -4;
    }
    printf("esp_connect_ap success\r\n");
    delay_ms(2000);

    /* 步骤6: 连接巴法云MQTT服务器 */
    rt = esp_connect_server(ESP_PROTOCOL_TCP, MQTT_BROKERADDRESS, 9501);
    if (rt)
    {
        printf("esp_connect_server fail\r\n");
        return -5;
    }
    printf("esp_connect_server success\r\n");
    delay_ms(2000);

    /* 步骤7: 进入透传模式 */
    rt = esp_entry_transparent_transmission();
    if (rt)
    {
        printf("esp_entry_transparent_transmission fail\r\n");
        return -6;
    }
    printf("esp_entry_transparent_transmission success\r\n");
    delay_ms(2000);

    /* 步骤8: MQTT协议连接 */
    if (mqtt_connect(MQTT_CLIENTID, MQTT_USARNAME, MQTT_PASSWD))
    {
        printf("mqtt_connect fail\r\n");
        return -7;
    }
    printf("mqtt_connect success\r\n");
    delay_ms(2000);

    /* 步骤9: 订阅主题（仅订阅控制主题 light002，避免收到自己发布的传感器数据回推） */
    if (mqtt_subscribe_topic(BAFA_TOPIC_LED, 0, 1))
    {
        printf("mqtt_subscribe_topic BAFA_TOPIC_LED fail\r\n");
        return -8;
    }
    printf("mqtt_subscribe_topic success\r\n");

    return 0;
}

/* ==================== ESP MQTT测试函数 ==================== */

/**
 * @brief ESP MQTT测试函数
 *
 * 测试流程：
 *   1. 初始化ESP模块并连接巴法云MQTT服务器
 *   2. 发布测试数据到温湿度主题
 *   3. while(1)循环接收服务器消息并打印
 *   4. 定时发送心跳保活（60秒）
 *   5. 收到"end"退出循环
 *
 * MQTT PUBLISH消息解析：
 *   Byte 0: 消息类型 (0x30 = PUBLISH)
 *   Byte 1: 剩余长度
 *   Byte 2-3: 主题长度（大端序）
 *   Byte 4-N: 主题名
 *   Byte N+1-...: 消息内容
 */
void esp_mqtt_test1(void)
{
    uint32_t last_heart_tick = 0;   /* 上次心跳时间（HAL_GetTick） */
    uint32_t last_publish_tick = 0; /* 上次发布时间（HAL_GetTick） */
    uint16_t payload_offset;        /* 消息内容起始位置 */
    char topic[32] = {0};           /* 主题缓冲区 */

    printf("\r\n");
    printf("========================================\r\n");
    printf("      ESP12-F 巴法云MQTT测试\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    /* [1] 初始化并连接巴法云MQTT服务器 */
    printf("[1] 初始化ESP模块并连接巴法云MQTT服务器...\r\n");
    while (esp_mqtt_init())
    {
        printf("esp_mqtt_init ...重试\r\n");
        delay_ms(1000);
    }
    printf("    [OK] 连接巴法云成功\r\n");
    printf("\r\n");

    /* [2] 发布测试数据到温湿度主题 */
    printf("[2] 发布测试数据到主题 %s...\r\n", MQTT_PUBLISH_TOPIC);
    sprintf(g_mqtt_msg, "#25.5#60.0");
    mqtt_publish_data(MQTT_PUBLISH_TOPIC, g_mqtt_msg, 0);
    printf("    发布数据: %s\r\n", g_mqtt_msg);
    printf("\r\n");

    /* [3] 发布LED状态到LED主题 */
    printf("[3] 发布LED状态到主题 %s...\r\n", BAFA_TOPIC_LED);
    sprintf(g_mqtt_msg, "on");
    mqtt_publish_data(BAFA_TOPIC_LED, g_mqtt_msg, 0);
    printf("    发布数据: %s\r\n", g_mqtt_msg);
    printf("\r\n");

    printf("========================================\r\n");
    printf("      进入消息接收循环（发送\"end\"退出）\r\n");
    printf("========================================\r\n");

    /* [4] while(1)循环：接收消息、发送心跳 */
    while (1)
    {
        /* 检查是否收到ESP透传数据 */
        if (g_esp_rx_end && g_esp_transparent_transmission_sta)
        {
            /*
             * 一次接收可能包含多个MQTT数据包（粘包），需要逐包解析。
             * 常见类型：
             *   0x90 = SUBACK（订阅响应）
             *   0x30 = PUBLISH（发布消息）
             *   0xD0 = PINGRESP（心跳响应）
             */
            uint16_t offset = 0;

            while (offset + 1 < g_esp_rx_cnt)
            {
                uint8_t pkt_type = g_esp_rx_buf[offset];
                uint8_t remain_len = g_esp_rx_buf[offset + 1];

                /* 检查剩余长度是否合法 */
                if (remain_len == 0 || offset + 2 + remain_len > g_esp_rx_cnt)
                {
                    break; /* 数据不完整，等待下一帧 */
                }

                if (pkt_type == 0x30)
                {
                    /* ===== PUBLISH 消息解析 ===== */
                    uint16_t topic_len = (g_esp_rx_buf[offset + 2] << 8) | g_esp_rx_buf[offset + 3];
                    uint16_t pkt_end = offset + 2 + remain_len;

                    printf("\r\n========== MQTT PUBLISH 消息 ==========\r\n");
                    printf("消息类型: 0x%02X (PUBLISH)\r\n", pkt_type);
                    printf("剩余长度: %u\r\n", remain_len);
                    printf("主题长度: %u\r\n", topic_len);

                    /* 提取并打印主题名 */
                    uint16_t copy_len = topic_len > 31 ? 31 : topic_len;
                    memset(topic, 0, sizeof(topic));
                    memcpy(topic, (void *)&g_esp_rx_buf[offset + 4], copy_len);
                    topic[copy_len] = '\0';
                    printf("主题名: \"%s\"\r\n", topic);

                    /* 计算消息内容起始位置 */
                    payload_offset = offset + 2 + 2 + topic_len;

                    /* 提取并打印消息内容 */
                    if (pkt_end > payload_offset)
                    {
                        char *payload = (char *)&g_esp_rx_buf[payload_offset];
                        uint16_t payload_len = pkt_end - payload_offset;

                        printf("消息内容: \"");
                        for (uint16_t i = 0; i < payload_len && i < 32; i++)
                        {
                            printf("%c", payload[i]);
                        }
                        printf("\" (长度: %u)\r\n", payload_len);

                        /* 检查是否收到退出指令 */
                        if (strstr(payload, "end") != NULL)
                        {
                            printf("    [收到退出指令，退出MQTT测试]\r\n");
                            goto mqtt_exit;
                        }

                        /* 检查LED控制指令 */
                        if (strstr(payload, "on"))
                        {
                            printf("    [LED ON 指令]\r\n");
                        }
                        else if (strstr(payload, "off"))
                        {
                            printf("    [LED OFF 指令]\r\n");
                        }
                    }
                    printf("========================================\r\n\r\n");
                }
                else if (pkt_type == 0x90)
                {
                    /* SUBACK，跳过 */
                    printf("[SUBACK] 订阅响应 (offset=%u)\r\n", offset);
                }
                else if (pkt_type == 0xD0)
                {
                    /* PINGRESP，跳过 */
                    printf("[PINGRESP] 心跳响应\r\n");
                }
                else
                {
                    /* 未知类型，打印并跳过 */
                    printf("[未知] 类型: 0x%02X (offset=%u)\r\n", pkt_type, offset);
                }

                /* 移动到下一个数据包 */
                offset += 2 + remain_len;
            }

            /* 清空接收缓冲区并重启UART接收，准备接收下一帧数据 */
            esp_clear_rx_buf();
        }

        /* 每60秒发送一次心跳保活 */
        if ((HAL_GetTick() - last_heart_tick) >= 60000)
        {
            mqtt_send_heart();
            printf("[心跳] 已发送PINGREQ\r\n");
            last_heart_tick = HAL_GetTick();
        }

        /* 每10秒发布一次温湿度测试数据 */
        if ((HAL_GetTick() - last_publish_tick) >= 10000)
        {
            sprintf(g_mqtt_msg, "#25.5#60.0");
            mqtt_publish_data(MQTT_PUBLISH_TOPIC, g_mqtt_msg, 0);
            printf("[发布] %s -> %s\r\n", g_mqtt_msg, MQTT_PUBLISH_TOPIC);
            last_publish_tick = HAL_GetTick();
        }
    }

mqtt_exit:
    /* [5] 退出处理 */
    printf("\r\n[5] 退出MQTT测试...\r\n");

    /* 退出透传模式 */
    esp_exit_transparent_transmission();
    printf("    已退出透传模式\r\n");

    /* 断开服务器连接 */
    esp_disconnect_server();
    printf("    已断开服务器连接\r\n");

    printf("\r\n========================================\r\n");
    printf("      巴法云MQTT测试完成\r\n");
    printf("========================================\r\n");
}
