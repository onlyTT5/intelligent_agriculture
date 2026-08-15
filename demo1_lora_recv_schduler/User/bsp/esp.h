/*
 * ============================================================================
 * 文件名称：esp.h
 * 文件描述：ESP12-F Wi-Fi模块驱动头文件
 *
 * 功能描述：
 *   本文件提供ESP12-F Wi-Fi模块的驱动接口，支持以下功能：
 *   1. ESP模块初始化和自检
 *   2. Wi-Fi热点连接
 *   3. TCP/UDP服务器连接
 *   4. 透传模式管理
 *   5. 数据发送和接收
 *
 * 硬件配置：
 *   - 通信接口：USART2（TX: PA2, RX: PA3）
 *   - 波特率：115200bps
 *   - 数据位：8位
 *   - 校验位：无
 *   - 停止位：1位
 *   - AT指令集与ESP8266兼容
 *
 * 作者：温老师
 * 日期：2026/05/09
 * 版本：V2.0（优化版）
 * ============================================================================
 */

#ifndef __ESP_H__
#define __ESP_H__

/* ==================== 配置宏定义 ==================== */

/**
 * @brief 调试开关
 * 0: 关闭调试输出
 * 1: 开启调试输出（需要printf支持）
 */
#define EN_DEBUG_ESP    0

/**
 * @brief 缓冲区大小配置
 * 根据实际需求调整，建议值：256-1024字节
 */
#define ESP_BUF_SIZE    512

/**
 * @brief 默认超时时间（毫秒）
 * 用于AT指令响应等待
 */
#define ESP_DEFAULT_TIMEOUT  1000
#define ESP_LONG_TIMEOUT     5000

/**
 * @brief WIFI热点配置
 * 根据实际网络环境修改
 */
#define WIFI_SSID         "LWH"
#define WIFI_PASSWORD     "88888888"

/* ==================== 类型定义 ==================== */

/**
 * @brief ESP错误码定义
 */
typedef enum {
    ESP_OK             = 0,    /* 操作成功 */
    ESP_ERR_TIMEOUT    = -1,   /* 超时错误 */
    ESP_ERR_INVALID    = -2,   /* 参数无效 */
    ESP_ERR_COMM       = -3,   /* 通信错误 */
    ESP_ERR_STATE      = -4,   /* 状态错误 */
    ESP_ERR_NOT_FOUND  = -5,   /* 未找到目标 */
} esp_err_t;

/**
 * @brief ESP工作模式枚举
 */
typedef enum {
    ESP_MODE_STATION   = 1,    /* Station模式（连接热点） */
    ESP_MODE_AP        = 2,    /* AP模式（作为热点） */
    ESP_MODE_BOTH      = 3,    /* 混合模式 */
} esp_mode_t;

/**
 * @brief ESP透传状态枚举
 */
typedef enum {
    ESP_STATE_IDLE         = 0,    /* 空闲状态 */
    ESP_STATE_STATION      = 1,    /* Station模式 */
    ESP_STATE_CONNECTED    = 2,    /* 已连接服务器 */
    ESP_STATE_TRANSPARENT  = 3,    /* 透传模式 */
} esp_state_t;

/**
 * @brief 协议类型枚举
 */
typedef enum {
    ESP_PROTOCOL_TCP     = 0,    /* TCP协议 */
    ESP_PROTOCOL_UDP     = 1,    /* UDP协议 */
} esp_protocol_t;

/* ==================== 全局变量声明 ==================== */

/**
 * @brief 发送缓冲区
 * 用于存储待发送的数据
 */
extern uint8_t g_esp_tx_buf[ESP_BUF_SIZE];

/**
 * @brief 接收缓冲区（volatile修饰，中断中使用）
 * 用于存储从ESP模块接收到的数据
 */
extern volatile uint8_t g_esp_rx_buf[ESP_BUF_SIZE];

/**
 * @brief 接收数据计数（volatile修饰，中断中使用）
 * 记录当前接收缓冲区中的字节数
 */
extern volatile uint32_t g_esp_rx_cnt;

/**
 * @brief 接收完成标志（volatile修饰，中断中使用）
 * 1: 接收完成，0: 接收中
 */
extern volatile uint32_t g_esp_rx_end;

/**
 * @brief 透传模式状态标志
 * 1: 透传模式，0: 命令模式
 */
extern volatile uint32_t g_esp_transparent_transmission_sta;

/**
 * @brief 当前工作状态
 * 用于状态机管理
 */
extern esp_state_t g_esp_state;

/* ==================== 函数原型声明 ==================== */

/**
 * @brief 初始化ESP模块
 *
 * 初始化USART2串口通信，配置波特率为115200bps，启动空闲中断接收
 *
 * @return 无
 */
extern void esp_init(void);

/**
 * @brief ESP自检
 *
 * 发送AT指令检测ESP模块是否正常响应
 *
 * @return esp_err_t
 *         - ESP_OK: 自检成功
 *         - ESP_ERR_TIMEOUT: 超时未响应
 */
extern esp_err_t esp_self_test(void);

/**
 * @brief 设置ESP工作模式
 *
 * @param mode 工作模式（ESP_MODE_STATION/AP/BOTH）
 *
 * @return esp_err_t
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
extern esp_err_t esp_set_mode(esp_mode_t mode);

/**
 * @brief 连接Wi-Fi热点
 *
 * @param ssid    热点名称（字符串）
 * @param pwd     热点密码（字符串）
 *
 * @return esp_err_t
 *         - ESP_OK: 连接成功
 *         - ESP_ERR_TIMEOUT: 超时
 *         - ESP_ERR_NOT_FOUND: 未找到热点或密码错误
 */
extern esp_err_t esp_connect_ap(char* ssid, char* pwd);

/**
 * @brief 连接远程服务器
 *
 * @param protocol 协议类型（TCP/UDP）
 * @param ip       服务器IP地址或域名
 * @param port     服务器端口号
 *
 * @return esp_err_t
 *         - ESP_OK: 连接成功
 *         - ESP_ERR_TIMEOUT: 超时
 *         - ESP_ERR_INVALID: 参数无效
 */
extern esp_err_t esp_connect_server(esp_protocol_t protocol, char* ip, uint16_t port);

/**
 * @brief 断开与服务器的连接
 *
 * @return esp_err_t
 *         - ESP_OK: 断开成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
extern esp_err_t esp_disconnect_server(void);

/**
 * @brief 进入透传模式
 *
 * 在透传模式下，ESP模块将接收到的数据直接转发到串口
 *
 * @return esp_err_t
 *         - ESP_OK: 进入成功
 *         - ESP_ERR_TIMEOUT: 超时
 *         - ESP_ERR_STATE: 当前状态不允许进入透传模式
 */
extern esp_err_t esp_entry_transparent_transmission(void);

/**
 * @brief 退出透传模式
 *
 * 发送+++退出透传模式，返回命令模式
 *
 * @return esp_err_t
 *         - ESP_OK: 退出成功
 */
extern esp_err_t esp_exit_transparent_transmission(void);

/**
 * @brief 发送字节数据
 *
 * @param buf 数据缓冲区指针
 * @param len 数据长度
 *
 * @return 无
 */
extern void esp_send_bytes(uint8_t* buf, uint32_t len);

/**
 * @brief 发送字符串
 *
 * @param buf 字符串指针（以'\0'结尾）
 *
 * @return 无
 */
extern void esp_send_str(char* buf);

/**
 * @brief 发送AT指令
 *
 * 发送AT指令并清空接收缓冲区
 *
 * @param str AT指令字符串（建议以"\r\n"结尾）
 *
 * @return 无
 */
extern void esp_send_at(const char* str);

/**
 * @brief 在接收数据中查找指定字符串
 *
 * @param str     要查找的字符串
 * @param timeout 超时时间（毫秒）
 *
 * @return esp_err_t
 *         - ESP_OK: 找到目标字符串
 *         - ESP_ERR_TIMEOUT: 超时未找到
 */
extern esp_err_t esp_find_str_in_rx_packet(char* str, uint32_t timeout);

/**
 * @brief 使能/禁用多连接模式
 *
 * @param enable 1: 使能多连接，0: 禁用
 *
 * @return esp_err_t
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
extern esp_err_t esp_enable_multiple_id(uint32_t enable);

/**
 * @brief 创建TCP服务器
 *
 * @param port 服务器端口号
 *
 * @return esp_err_t
 *         - ESP_OK: 创建成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
extern esp_err_t esp_create_server(uint16_t port);

/**
 * @brief 关闭TCP服务器
 *
 * @param port 服务器端口号
 *
 * @return esp_err_t
 *         - ESP_OK: 关闭成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
extern esp_err_t esp_close_server(uint16_t port);

/**
 * @brief 使能/禁用回显
 *
 * @param enable 1: 使能回显，0: 禁用回显
 *
 * @return esp_err_t
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
extern esp_err_t esp_enable_echo(uint32_t enable);

/**
 * @brief 复位ESP模块
 *
 * 发送AT+RST指令复位模块
 *
 * @return esp_err_t
 *         - ESP_OK: 复位成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
extern esp_err_t esp_reset(void);

/**
 * @brief 清空接收缓冲区
 *
 * @return 无
 */
extern void esp_clear_rx_buf(void);

/**
 * @brief 串口2接收事件处理函数（由HAL回调函数调用）
 *
 * @param size 本次实际接收到的字节数
 */
extern void esp_rx_event_handler(uint16_t size);

/**
 * @brief AT指令集测试函数
 *
 * 依次测试常见AT指令，通过串口1 printf打印输出测试结果
 *
 * @return 无
 */
extern void esp_test1(void);

/**
 * @brief TCP客户端测试函数（裸AT指令方式）
 *
 * @return 无
 */
extern void esp_test2(void);

/**
 * @brief TCP客户端测试函数（封装函数方式）
 *
 * 使用已封装好的驱动函数实现：
 *   1. 初始化ESP模块
 *   2. 连接Wi-Fi热点
 *   3. 以TCP客户端连接服务器
 *   4. 进入透传模式，发送"hello world"
 *   5. while(1)接收服务器数据并回传（echo）
 *   6. 收到"end"退出循环，退出透传模式，断开服务器连接
 *
 * @return 无
 */
extern void esp_test3(void);

#endif /* __ESP_H__ */
