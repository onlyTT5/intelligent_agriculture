/*
 * ============================================================================
 * 文件名称：esp_mqtt.h
 * 文件描述：ESP12-F MQTT客户端头文件（巴法云版本）
 *
 * 功能描述：
 *   本文件定义了基于ESP12-F模块的MQTT客户端接口，支持巴法云平台接入。
 *   巴法云是一个免费的物联网云平台，支持设备接入、消息发布和订阅等功能。
 *
 * MQTT协议说明：
 *   - 协议版本：MQTT 3.1.1
 *   - QoS支持：QoS 0（最多一次）
 *   - 连接方式：使用用户私钥(UID)作为客户端ID，无需身份验证
 *
 * 巴法云MQTT配置：
 *   - 服务器地址：bemfa.com
 *   - 端口号：9501（非加密端口）
 *   - 客户端ID：用户私钥(UID)
 *   - 用户名：为空
 *   - 密码：为空
 *
 * 设备主题配置：
 *   - light002：LED控制主题，接收on/off指令
 *   - humiture004：温湿度数据上报主题
 *
 * 使用说明：
 *   1. 在巴法云控制台获取用户私钥(UID)
 *   2. 修改BAFA_UID宏定义为自己的私钥
 *   3. 在巴法云控制台创建相应的主题
 *   4. 调用esp_mqtt_init()初始化连接
 *
 * 作者：温老师
 * 日期：2026/05/09
 * 版本：V1.0（巴法云适配版）
 * ============================================================================
 */

#ifndef __ESP_MQTT_H
#define __ESP_MQTT_H

#include "main.h"

/* ==================== 巴法云MQTT配置参数 ==================== */
/* 注意：请修改为自己的巴法云设备信息 */

/**
 * @brief MQTT服务器地址
 * 巴法云MQTT服务器地址固定为bemfa.com
 */
#define MQTT_BROKERADDRESS "bemfa.com"

/**
 * @brief MQTT服务器端口号
 * 9501为巴法云非加密端口，不使用SSL/TLS
 */
#define MQTT_PORT "9501"

/**
 * @brief 巴法云用户私钥(UID)
 * 在巴法云控制台获取，作为MQTT客户端ID，用于身份验证
 * 获取方式：登录巴法云 -> 用户中心 -> 私钥即可
 */
#define BAFA_UID "4b5baad8d4b1426a8d83c55af1ae1015"

/**
 * @brief MQTT客户端ID
 * 直接使用用户私钥作为客户端ID
 */
#define MQTT_CLIENTID BAFA_UID

/**
 * @brief MQTT用户名
 * 巴法云不需要用户名，设置为空字符串
 */
#define MQTT_USARNAME ""

/**
 * @brief MQTT密码
 * 巴法云不需要密码，设置为空字符串
 */
#define MQTT_PASSWD ""

/* ==================== 设备主题配置 ==================== */

/**
 * @brief LED控制主题
 * 用于接收巴法云的LED控制指令（on/off）
 * 需要在巴法云控制台创建同名的主题
 */
#define BAFA_TOPIC_LED "light002"

/**
 * @brief 温湿度数据上报主题
 * 用于向巴法云上报温湿度传感器数据
 * 需要在巴法云控制台创建同名的主题
 */
#define BAFA_TOPIC_HUMITURE "humiture004"

/**
 * @brief 默认发布主题
 * 设备上报数据时使用的主题
 */
#define MQTT_PUBLISH_TOPIC BAFA_TOPIC_HUMITURE

/**
 * @brief 默认订阅主题
 * 设备接收控制指令时使用的主题
 */
#define MQTT_SUBSCRIBE_TOPIC BAFA_TOPIC_LED

/* ==================== 字节操作宏定义 ==================== */

/**
 * @brief 获取32位整数的第0字节（低字节）
 */
#define BYTE0(dwTemp) (*(char *)(&dwTemp))

/**
 * @brief 获取32位整数的第1字节
 */
#define BYTE1(dwTemp) (*((char *)(&dwTemp) + 1))

/**
 * @brief 获取32位整数的第2字节
 */
#define BYTE2(dwTemp) (*((char *)(&dwTemp) + 2))

/**
 * @brief 获取32位整数的第3字节（高字节）
 */
#define BYTE3(dwTemp) (*((char *)(&dwTemp) + 3))

/* ==================== MQTT函数声明 ==================== */

/**
 * @brief MQTT发送数据
 * 将数据通过ESP模块发送到MQTT服务器
 * @param buf 数据缓冲区指针
 * @param len 数据长度
 */
extern void mqtt_send_bytes(uint8_t *buf, uint32_t len);

/**
 * @brief 发送MQTT心跳包
 * 向服务器发送PINGREQ，保持连接活跃
 */
extern void mqtt_send_heart(void);

/**
 * @brief MQTT断开连接
 * 发送DISCONNECT数据包，主动断开与服务器的连接
 */
extern void mqtt_disconnect(void);

/**
 * @brief MQTT协议初始化
 * 清空收发缓冲区并发送断开连接包，确保处于干净状态
 * @param prx 接收缓冲区指针（未使用）
 * @param rxlen 接收缓冲区长度（未使用）
 * @param ptx 发送缓冲区指针（未使用）
 * @param txlen 发送缓冲区长度（未使用）
 */
extern void mqtt_init(uint8_t *prx, uint16_t rxlen, uint8_t *ptx, uint16_t txlen);

/**
 * @brief MQTT连接服务器
 * @param client_id 客户端ID（巴法云使用UID）
 * @param user_name 用户名（巴法云可为空）
 * @param password 密码（巴法云可为空）
 * @return 0表示连接成功，-1表示连接失败
 */
extern int32_t mqtt_connect(char *client_id, char *user_name, char *password);

/**
 * @brief MQTT订阅/取消订阅主题
 * @param topic 主题名称
 * @param qos QoS等级（0或1）
 * @param whether 1=订阅，0=取消订阅
 * @return 0表示成功，-1表示失败
 */
extern int32_t mqtt_subscribe_topic(char *topic, uint8_t qos, uint8_t whether);

/**
 * @brief MQTT发布消息
 * @param topic 主题名称
 * @param message 消息内容
 * @param qos QoS等级（0或1）
 * @return 发送的字节数
 */
extern uint32_t mqtt_publish_data(char *topic, char *message, uint8_t qos);

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
extern int32_t esp_mqtt_init(void);

/**
 * @brief ESP MQTT测试函数
 *
 * 测试流程：
 *   1. 初始化ESP模块并连接巴法云MQTT服务器
 *   2. 订阅LED控制主题
 *   3. 发布测试数据
 *   4. while(1)接收服务器消息并打印
 *   5. 定时发送心跳保活
 *   6. 收到"end"退出循环
 *
 * @return 无
 */
extern void esp_mqtt_test1(void);

#endif /* __ESP_MQTT_H */
