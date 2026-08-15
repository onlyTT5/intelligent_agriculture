/*
 * ============================================================================
 * 文件名称：esp.c
 * 文件描述：ESP12-F Wi-Fi模块驱动实现文件（函数封装版）
 *
 * 功能描述：
 *   本文件实现了ESP12-F Wi-Fi模块的完整驱动功能，包括：
 *   1. 串口通信初始化和数据收发（USART2，HAL库）
 *   2. AT指令发送和响应解析
 *   3. Wi-Fi热点连接（封装函数：esp_connect_ap）
 *   4. TCP/UDP客户端连接服务器（封装函数：esp_connect_server）
 *   5. 透传模式管理（封装函数：esp_entry/exit_transparent_transmission）
 *   6. 服务器连接断开（封装函数：esp_disconnect_server）
 *   7. AT指令集测试（esp_test1）
 *   8. TCP客户端透传收发测试（esp_test2，裸AT指令方式）
 *   9. TCP客户端透传收发测试（esp_test3，封装函数方式）
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
 * 版本：V3.0（函数封装版）
 * ============================================================================
 */

#include "main.h"
#include "usart.h"
#include "delay.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* 包含头文件 */
#include "esp.h"

/* ==================== 全局变量定义 ==================== */

/**
 * @brief 发送缓冲区
 */
uint8_t g_esp_tx_buf[ESP_BUF_SIZE];

/**
 * @brief 接收缓冲区（volatile修饰，中断中使用）
 */
volatile uint8_t g_esp_rx_buf[ESP_BUF_SIZE];

/**
 * @brief 接收数据计数（volatile修饰，中断中使用）
 */
volatile uint32_t g_esp_rx_cnt = 0;

/**
 * @brief 接收完成标志（volatile修饰，中断中使用）
 */
volatile uint32_t g_esp_rx_end = 0;

/**
 * @brief 透传模式状态标志
 */
volatile uint32_t g_esp_transparent_transmission_sta = 0;

/**
 * @brief 当前工作状态
 */
esp_state_t g_esp_state = ESP_STATE_IDLE;

/* ==================== 调试宏定义 ==================== */

#if EN_DEBUG_ESP
#define ESP_DEBUG(fmt, ...)    printf("[ESP] " fmt "\r\n", ##__VA_ARGS__)
#else
#define ESP_DEBUG(fmt, ...)
#endif

/* ==================== 私有函数声明 ==================== */

/**
 * @brief 将协议类型转换为字符串
 */
static const char* esp_protocol_to_str(esp_protocol_t protocol);

#if EN_DEBUG_ESP
/**
 * @brief 将状态转换为字符串
 */
static const char* esp_state_to_str(esp_state_t state);
#endif

/**
 * @brief 更新状态机
 */
static void esp_update_state(esp_state_t new_state);

/* ==================== 公共函数实现 ==================== */

/**
 * @brief 初始化ESP模块
 *
 * 重新配置USART2波特率为115200bps（ESP12-F默认波特率），
 * 启动空闲中断接收，初始化状态机
 */
void esp_init(void)
{
    /* 重新配置USART2波特率为115200（CubeMX默认生成9600，ESP需要115200） */
    huart2.Init.BaudRate = 115200;
    HAL_UART_Init(&huart2);

    /* 初始化状态机 */
    g_esp_state = ESP_STATE_IDLE;

    /* 清空缓冲区并启动接收 */
    esp_clear_rx_buf();

    ESP_DEBUG("Init completed. State: %s", esp_state_to_str(g_esp_state));
}

/**
 * @brief 串口2接收事件处理函数（由HAL回调函数调用）
 *
 * 将接收到的数据追加到接收缓冲区，并重新启动下一次接收
 *
 * @param size 本次实际接收到的字节数
 */
void esp_rx_event_handler(uint16_t size)
{
    /* 累加接收计数 */
    g_esp_rx_cnt += size;
    g_esp_rx_end = 1;

    /* 缓冲区未满时，继续从当前偏移处接收下一帧数据 */
    if (g_esp_rx_cnt < ESP_BUF_SIZE)
    {
        HAL_UARTEx_ReceiveToIdle_IT(&huart2,
                                    (uint8_t*)&g_esp_rx_buf[g_esp_rx_cnt],
                                    ESP_BUF_SIZE - g_esp_rx_cnt);
    }
    else
    {
        /* 缓冲区已满，从头开始覆盖接收 */
        g_esp_rx_cnt = 0;
        HAL_UARTEx_ReceiveToIdle_IT(&huart2,
                                    (uint8_t*)g_esp_rx_buf,
                                    ESP_BUF_SIZE);
    }
}

/**
 * @brief 清空接收缓冲区
 *
 * 清空缓冲区并重新启动空闲中断接收
 */
void esp_clear_rx_buf(void)
{
    /* 阻塞式中止当前接收，确保 UART 完全回到 READY 状态 */
    HAL_UART_Abort(&huart2);

    /* 禁用中断保护 */
    __disable_irq();

    /* 清空接收缓冲区 */
    memset((void*)g_esp_rx_buf, 0, sizeof(g_esp_rx_buf));

    /* 重置接收计数和标志 */
    g_esp_rx_cnt = 0;
    g_esp_rx_end = 0;

    /* 启用中断 */
    __enable_irq();

    /* 重新启动空闲中断接收（此时 UART 已 READY，不会返回 HAL_BUSY） */
    HAL_UARTEx_ReceiveToIdle_IT(&huart2, (uint8_t*)g_esp_rx_buf, ESP_BUF_SIZE);

    ESP_DEBUG("RX buffer cleared");
}

/**
 * @brief 发送AT指令
 *
 * 发送AT指令并清空接收缓冲区
 *
 * @param str AT指令字符串（建议以"\r\n"结尾）
 */
void esp_send_at(const char* str)
{
    /* 参数校验 */
    if (str == NULL)
    {
        ESP_DEBUG("Error: NULL pointer in esp_send_at");
        return;
    }

    /* 清空接收缓冲区 */
    esp_clear_rx_buf();

    /* 发送AT指令 */
    HAL_UART_Transmit(&huart2, (uint8_t*)str, strlen(str), HAL_MAX_DELAY);

    ESP_DEBUG("Send AT: %s", str);
}

/**
 * @brief 发送字节数据
 *
 * @param buf 数据缓冲区指针
 * @param len 数据长度
 */
void esp_send_bytes(uint8_t* buf, uint32_t len)
{
    /* 参数校验 */
    if (buf == NULL || len == 0)
    {
        ESP_DEBUG("Error: Invalid parameters in esp_send_bytes");
        return;
    }

    /* 发送数据 */
    HAL_UART_Transmit(&huart2, buf, len, HAL_MAX_DELAY);

    ESP_DEBUG("Send %u bytes", len);
}

/**
 * @brief 发送字符串
 *
 * @param buf 字符串指针（以'\0'结尾）
 */
void esp_send_str(char* buf)
{
    /* 参数校验 */
    if (buf == NULL)
    {
        ESP_DEBUG("Error: NULL pointer in esp_send_str");
        return;
    }

    /* 发送字符串 */
    HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), HAL_MAX_DELAY);

    ESP_DEBUG("Send string: %s", buf);
}

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
esp_err_t esp_find_str_in_rx_packet(char* str, uint32_t timeout)
{
    /* 参数校验 */
    if (str == NULL)
    {

        return ESP_ERR_INVALID;
    }

    /* 等待串口接收完毕或超时退出 */
    while ((strstr((char*)g_esp_rx_buf, str) == NULL) && timeout)
    {
        delay_ms(1);
        timeout--;
    }
	printf("response: %s", g_esp_rx_buf);
    if (timeout > 0)
    {
        
        return ESP_OK;
    }


    return ESP_ERR_TIMEOUT;
}

/**
 * @brief ESP自检
 *
 * 发送AT指令检测ESP模块是否正常响应
 *
 * @return esp_err_t
 *         - ESP_OK: 自检成功
 *         - ESP_ERR_TIMEOUT: 超时未响应
 */
esp_err_t esp_self_test(void)
{
    esp_err_t ret;

    ESP_DEBUG("Self test start...");

    /* 发送AT指令 */
    esp_send_at("AT\r\n");

    /* 等待响应 */
    ret = esp_find_str_in_rx_packet("OK", ESP_DEFAULT_TIMEOUT);

    if (ret == ESP_OK)
    {
        ESP_DEBUG("Self test passed");
    }
    else
    {
        ESP_DEBUG("Self test failed: %d", ret);
    }

    return ret;
}

/**
 * @brief 设置ESP工作模式
 *
 * @param mode 工作模式（ESP_MODE_STATION/AP/BOTH）
 *
 * @return esp_err_t
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_TIMEOUT: 超时
 *         - ESP_ERR_INVALID: 参数无效
 */
esp_err_t esp_set_mode(esp_mode_t mode)
{
    /* 参数校验 */
    if (mode != ESP_MODE_STATION &&
        mode != ESP_MODE_AP &&
        mode != ESP_MODE_BOTH)
    {
        ESP_DEBUG("Error: Invalid mode: %d", mode);
        return ESP_ERR_INVALID;
    }

    ESP_DEBUG("Setting mode to %d...", mode);

    /* 构建AT指令 */
    char buf[32] = {0};
    sprintf(buf, "AT+CWMODE_CUR=%d\r\n", mode);

    /* 发送指令 */
    esp_send_at(buf);

    /* 等待响应 */
    if (esp_find_str_in_rx_packet("OK", ESP_DEFAULT_TIMEOUT) != ESP_OK)
    {
        ESP_DEBUG("Failed to set mode");
        return ESP_ERR_TIMEOUT;
    }

    /* 更新状态 */
    if (mode == ESP_MODE_STATION)
    {
        esp_update_state(ESP_STATE_STATION);
    }

    ESP_DEBUG("Mode set successfully");
    return ESP_OK;
}

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
esp_err_t esp_connect_ap(char* ssid, char* pwd)
{
    /* 参数校验 */
    if (ssid == NULL || pwd == NULL)
    {
        ESP_DEBUG("Error: NULL pointer in esp_connect_ap");
        return ESP_ERR_INVALID;
    }

    ESP_DEBUG("Connecting to AP: %s", ssid);

    /* 设置为STATION模式 */
    if (esp_set_mode(ESP_MODE_STATION) != ESP_OK)
    {
        ESP_DEBUG("Failed to set station mode");
        return ESP_ERR_STATE;
    }

    /* 连接目标AP */
    esp_send_at("AT+CWJAP_CUR=");
    esp_send_at("\""); esp_send_at(ssid); esp_send_at("\"");
    esp_send_at(",");
    esp_send_at("\""); esp_send_at(pwd); esp_send_at("\"");
    esp_send_at("\r\n");

    /* 等待连接成功响应 */
    if (esp_find_str_in_rx_packet("OK", ESP_LONG_TIMEOUT) == ESP_OK)
    {
        ESP_DEBUG("Connected to AP successfully");
        return ESP_OK;
    }

    /* 如果OK没有返回，检查CONNECT响应 */
    if (esp_find_str_in_rx_packet("CONNECT", ESP_LONG_TIMEOUT) == ESP_OK)
    {
        ESP_DEBUG("Connected to AP (CONNECT)");
        return ESP_OK;
    }

    ESP_DEBUG("Failed to connect to AP");
    return ESP_ERR_NOT_FOUND;
}

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
esp_err_t esp_connect_server(esp_protocol_t protocol, char* ip, uint16_t port)
{
    /* 参数校验 */
    if (ip == NULL || port == 0)
    {
        ESP_DEBUG("Error: Invalid parameters in esp_connect_server");
        return ESP_ERR_INVALID;
    }

    ESP_DEBUG("Connecting to server: %s://%s:%u",
              esp_protocol_to_str(protocol), ip, port);

    /* 构建连接指令 */
    char port_buf[16] = {0};
    sprintf(port_buf, "%d", port);

    /* 发送AT指令 */
    esp_send_at("AT+CIPSTART=");
    esp_send_at("\""); esp_send_at(esp_protocol_to_str(protocol)); esp_send_at("\"");
    esp_send_at(",");
    esp_send_at("\""); esp_send_at(ip); esp_send_at("\"");
    esp_send_at(",");
    esp_send_at(port_buf);
    esp_send_at("\r\n");

    /* 等待连接成功 */
    if (esp_find_str_in_rx_packet("CONNECT", ESP_LONG_TIMEOUT) == ESP_OK)
    {
        esp_update_state(ESP_STATE_CONNECTED);
        ESP_DEBUG("Connected to server successfully");
        return ESP_OK;
    }

    /* 如果CONNECT没有返回，检查OK响应 */
    if (esp_find_str_in_rx_packet("OK", ESP_LONG_TIMEOUT) == ESP_OK)
    {
        esp_update_state(ESP_STATE_CONNECTED);
        ESP_DEBUG("Connected to server (OK)");
        return ESP_OK;
    }

    ESP_DEBUG("Failed to connect to server");
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief 断开与服务器的连接
 *
 * @return esp_err_t
 *         - ESP_OK: 断开成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
esp_err_t esp_disconnect_server(void)
{
    ESP_DEBUG("Disconnecting from server...");

    /* 发送断开指令 */
    esp_send_at("AT+CIPCLOSE\r\n");

    /* 等待响应 */
    if (esp_find_str_in_rx_packet("CLOSED", ESP_LONG_TIMEOUT) == ESP_OK)
    {
        esp_update_state(ESP_STATE_STATION);
        ESP_DEBUG("Disconnected from server (CLOSED)");
        return ESP_OK;
    }

    /* 如果CLOSED没有返回，检查OK响应 */
    if (esp_find_str_in_rx_packet("OK", ESP_LONG_TIMEOUT) == ESP_OK)
    {
        esp_update_state(ESP_STATE_STATION);
        ESP_DEBUG("Disconnected from server (OK)");
        return ESP_OK;
    }

    ESP_DEBUG("Failed to disconnect from server");
    return ESP_ERR_TIMEOUT;
}

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
esp_err_t esp_entry_transparent_transmission(void)
{
    /* 状态检查 */
    if (g_esp_state != ESP_STATE_CONNECTED)
    {
        ESP_DEBUG("Error: Not connected to server. Current state: %s",
                  esp_state_to_str(g_esp_state));
        return ESP_ERR_STATE;
    }

    ESP_DEBUG("Entering transparent transmission mode...");

    /* 进入透传模式 */
    esp_send_at("AT+CIPMODE=1\r\n");
    if (esp_find_str_in_rx_packet("OK", ESP_LONG_TIMEOUT) != ESP_OK)
    {
        ESP_DEBUG("Failed to set CIPMODE");
        return ESP_ERR_TIMEOUT;
    }

    /* 延时确保模式切换完成 */
    delay_ms(2000);

    /* 开启发送状态 */
    esp_send_at("AT+CIPSEND\r\n");
    if (esp_find_str_in_rx_packet(">", ESP_LONG_TIMEOUT) != ESP_OK)
    {
        ESP_DEBUG("Failed to enter send mode");
        return ESP_ERR_TIMEOUT;
    }

    /* 更新状态和标志 */
    esp_update_state(ESP_STATE_TRANSPARENT);
    g_esp_transparent_transmission_sta = 1;

    ESP_DEBUG("Entered transparent transmission mode");
    return ESP_OK;
}

/**
 * @brief 退出透传模式
 *
 * 发送+++退出透传模式，返回命令模式
 *
 * @return esp_err_t
 *         - ESP_OK: 退出成功
 */
esp_err_t esp_exit_transparent_transmission(void)
{
    ESP_DEBUG("Exiting transparent transmission mode...");

    /* 退出前静默1秒（ESP透传退出协议要求+++前至少1秒无数据） */
    delay_ms(1000);

    /* 发送+++退出透传模式（注意：+++前后不能有\r\n） */
    esp_send_at("+++");

    /* 退出后静默1秒，等待ESP回到AT命令模式 */
    delay_ms(1000);

    /* 更新状态和标志 */
    esp_update_state(ESP_STATE_CONNECTED);
    g_esp_transparent_transmission_sta = 0;

    ESP_DEBUG("Exited transparent transmission mode");
    return ESP_OK;
}

/**
 * @brief 使能/禁用多连接模式
 *
 * @param enable 1: 使能多连接，0: 禁用
 *
 * @return esp_err_t
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
esp_err_t esp_enable_multiple_id(uint32_t enable)
{
    ESP_DEBUG("Setting multiple ID mode: %s", enable ? "ON" : "OFF");

    /* 构建AT指令 */
    char buf[32] = {0};
    sprintf(buf, "AT+CIPMUX=%d\r\n", enable ? 1 : 0);

    /* 发送指令 */
    esp_send_at(buf);

    /* 等待响应 */
    if (esp_find_str_in_rx_packet("OK", ESP_LONG_TIMEOUT) == ESP_OK)
    {
        ESP_DEBUG("Multiple ID mode set successfully");
        return ESP_OK;
    }

    ESP_DEBUG("Failed to set multiple ID mode");
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief 创建TCP服务器
 *
 * @param port 服务器端口号
 *
 * @return esp_err_t
 *         - ESP_OK: 创建成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
esp_err_t esp_create_server(uint16_t port)
{
    ESP_DEBUG("Creating server on port %u...", port);

    /* 构建AT指令 */
    char buf[32] = {0};
    sprintf(buf, "AT+CIPSERVER=1,%d\r\n", port);

    /* 发送指令 */
    esp_send_at(buf);

    /* 等待响应 */
    if (esp_find_str_in_rx_packet("OK", ESP_LONG_TIMEOUT) == ESP_OK)
    {
        ESP_DEBUG("Server created successfully");
        return ESP_OK;
    }

    ESP_DEBUG("Failed to create server");
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief 关闭TCP服务器
 *
 * @param port 服务器端口号
 *
 * @return esp_err_t
 *         - ESP_OK: 关闭成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
esp_err_t esp_close_server(uint16_t port)
{
    ESP_DEBUG("Closing server on port %u...", port);

    /* 构建AT指令 */
    char buf[32] = {0};
    sprintf(buf, "AT+CIPSERVER=0,%d\r\n", port);

    /* 发送指令 */
    esp_send_at(buf);

    /* 等待响应 */
    if (esp_find_str_in_rx_packet("OK", ESP_LONG_TIMEOUT) == ESP_OK)
    {
        ESP_DEBUG("Server closed successfully");
        return ESP_OK;
    }

    ESP_DEBUG("Failed to close server");
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief 使能/禁用回显
 *
 * @param enable 1: 使能回显，0: 禁用回显
 *
 * @return esp_err_t
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
esp_err_t esp_enable_echo(uint32_t enable)
{
    ESP_DEBUG("Setting echo mode: %s", enable ? "ON" : "OFF");

    /* 发送AT指令 */
    if (enable)
    {
        esp_send_at("ATE1\r\n");
    }
    else
    {
        esp_send_at("ATE0\r\n");
    }

    /* 等待响应 */
    if (esp_find_str_in_rx_packet("OK", ESP_LONG_TIMEOUT) == ESP_OK)
    {
        ESP_DEBUG("Echo mode set successfully");
        return ESP_OK;
    }

    ESP_DEBUG("Failed to set echo mode");
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief 复位ESP模块
 *
 * 发送AT+RST指令复位模块
 *
 * @return esp_err_t
 *         - ESP_OK: 复位成功
 *         - ESP_ERR_TIMEOUT: 超时
 */
esp_err_t esp_reset(void)
{
    ESP_DEBUG("Resetting ESP...");

    /* 发送复位指令 */
    esp_send_at("AT+RST\r\n");

    /* 等待响应（复位时间较长，给10秒） */
    if (esp_find_str_in_rx_packet("OK", 10000) == ESP_OK)
    {
        /* 复位后状态变为空闲 */
        esp_update_state(ESP_STATE_IDLE);
        ESP_DEBUG("ESP reset successfully");
        return ESP_OK;
    }

    ESP_DEBUG("Failed to reset ESP");
    return ESP_ERR_TIMEOUT;
}


/* ==================== 私有函数实现 ==================== */

/**
 * @brief 将协议类型转换为字符串
 *
 * @param protocol 协议类型
 * @return 协议类型字符串
 */
static const char* esp_protocol_to_str(esp_protocol_t protocol)
{
    switch (protocol)
    {
        case ESP_PROTOCOL_TCP:
            return "TCP";
        case ESP_PROTOCOL_UDP:
            return "UDP";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief 将状态转换为字符串
 *
 * @param state 状态枚举值
 * @return 状态字符串
 */
#if EN_DEBUG_ESP
static const char* esp_state_to_str(esp_state_t state)
{
    switch (state)
    {
        case ESP_STATE_IDLE:
            return "IDLE";
        case ESP_STATE_STATION:
            return "STATION";
        case ESP_STATE_CONNECTED:
            return "CONNECTED";
        case ESP_STATE_TRANSPARENT:
            return "TRANSPARENT";
        default:
            return "UNKNOWN";
    }
}
#endif

/**
 * @brief 更新状态机
 *
 * @param new_state 新状态
 */
static void esp_update_state(esp_state_t new_state)
{
#if EN_DEBUG_ESP
    ESP_DEBUG("State change: %s -> %s",
              esp_state_to_str(g_esp_state),
              esp_state_to_str(new_state));
#endif
    g_esp_state = new_state;
}

/* ==================== AT指令集测试函数 ==================== */

/**
 * @brief AT指令集测试函数
 *
 * 依次测试常见AT指令，通过串口1 printf打印输出测试结果
 * 测试项目：
 *   1. AT 自检
 *   2. ATE0 关闭回显
 *   3. AT+CWMODE? 查询工作模式
 *   4. AT+CIFSR 查询IP地址
 *   5. AT+CIPMUX? 查询连接模式
 *
 * @return 无
 */
void esp_test1(void)
{
	esp_send_at("AT\r\n");
	delay_ms(1000);
	printf("%s",g_esp_rx_buf);
	
	esp_send_at("ATE0\r\n");
	delay_ms(1000);
	printf("%s",g_esp_rx_buf);	
	
	esp_send_at("AT+CWMODE?\r\n");
	delay_ms(1000);
	printf("%s",g_esp_rx_buf);	
	
	esp_send_at("AT+CIPMUX?\r\n");
	delay_ms(1000);
	printf("%s",g_esp_rx_buf);		
	
	esp_send_at("AT+CIFSR\r\n");
	delay_ms(1000);
	printf("%s",g_esp_rx_buf);		
	
	while(1);
}


void esp_test2(void)
{
    printf("\r\n");
    printf("========================================\r\n");
    printf("      ESP12-F TCP客户端测试\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    /* [1] 初始化ESP模块，USART2 115200bps */
    printf("[1] 初始化ESP模块...\r\n");
    esp_init();
    delay_ms(500);
    printf("    初始化完成\r\n");
    printf("    回应: %s\r\n", (char*)g_esp_rx_buf);
    printf("\r\n");

    /* [2] AT 自检 */
    printf("[2] 执行 AT 自检...\r\n");
    esp_send_at("AT\r\n");
    delay_ms(1000);
    printf("    回应: %s\r\n", (char*)g_esp_rx_buf);
    printf("\r\n");

    /* [3] 关闭回显 ATE0 */
    printf("[3] 关闭回显 ATE0...\r\n");
    esp_send_at("ATE0\r\n");
    delay_ms(1000);
    printf("    回应: %s\r\n", (char*)g_esp_rx_buf);
    printf("\r\n");

    /* [4] 设置为 Station 模式，连接热点 */
    printf("[4] 设置 Station 模式并连接热点 SSID=%s...\r\n", WIFI_SSID);
    esp_send_at("AT+CWMODE=1\r\n");
    delay_ms(1000);
    printf("    回应: %s\r\n", (char*)g_esp_rx_buf);

    {
        char cmd[128];
        sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
        esp_send_at(cmd);
        delay_ms(8000);  /* 连接热点需要较长时间 */
        printf("    CWJAP 回应: %s\r\n", (char*)g_esp_rx_buf);
    }
    printf("\r\n");

    /* [5] 设置单连接模式（CIPMUX=0），对应 AT+CIPSEND=<len> 格式 */
    printf("[5] 设置单连接模式 CIPMUX=0...\r\n");
    esp_send_at("AT+CIPMUX=0\r\n");
    delay_ms(1000);
    printf("    回应: %s\r\n", (char*)g_esp_rx_buf);
    printf("\r\n");

    /* [6] 以TCP客户端连接服务器 192.168.31.154:9501 */
    printf("[6] TCP连接服务器 192.168.31.154:9501...\r\n");
    esp_send_at("AT+CIPSTART=\"TCP\",\"192.168.31.154\",9501\r\n");
    delay_ms(5000);
    printf("    CIPSTART 回应: %s\r\n", (char*)g_esp_rx_buf);

    /* 检查连接是否成功：必须含有 "CONNECT" 或 "OK"，且不含 "ERROR" */
    if ((strstr((char*)g_esp_rx_buf, "CONNECT") == NULL) &&
        (strstr((char*)g_esp_rx_buf, "OK") == NULL) &&
        (strstr((char*)g_esp_rx_buf, "ALREADY CONN") == NULL))
    {
        printf("    [FAIL] TCP连接服务器失败，停止测试\r\n");
        while (1)
        {
        }
    }
    printf("    [OK] TCP连接服务器成功\r\n");
    printf("\r\n");

    /* [7] 进入透传模式 */
    printf("[7] 进入透传模式...\r\n");
    esp_send_at("AT+CIPMODE=1\r\n");
    delay_ms(1000);
    printf("    CIPMODE 回应: %s\r\n", (char*)g_esp_rx_buf);

    esp_send_at("AT+CIPSEND\r\n");
    delay_ms(1000);
    printf("    CIPSEND 回应: %s\r\n", (char*)g_esp_rx_buf);
    printf("\r\n");

    /* [8] 透传模式下直接发送 "hello world" */
    printf("[8] 发送数据 \"hello world\"...\r\n");
    HAL_UART_Transmit(&huart2, (uint8_t*)"hello world", 11, HAL_MAX_DELAY);
    delay_ms(1000);
    printf("    发送完成\r\n");
    printf("\r\n");

    /* [9] while(1) 循环：接收服务器数据，原样回传（echo） */
    printf("[9] 进入透传接收->回传循环（发送\"end\"退出）...\r\n");
    printf("========================================\r\n");
    while (1)
    {
        /* 透传模式下，服务器数据直接以裸字节到达，无 +IPD 前缀 */
        if (g_esp_rx_end == 0)
        {
            delay_ms(1);  /* 短暂延时，避免空转占满CPU */
            continue;
        }

        /* 串口1打印收到的数据 */
        printf("    [收到 %u 字节] ", g_esp_rx_cnt);
        HAL_UART_Transmit(&huart1, (uint8_t*)g_esp_rx_buf, g_esp_rx_cnt, HAL_MAX_DELAY);
        printf("\r\n");

        /* 检查是否收到 "end" 退出指令 */
        if (strstr((char*)g_esp_rx_buf, "end") != NULL)
        {
            printf("    [收到退出指令，退出透传模式]\r\n");
            break;
        }

        /* 透传模式下直接回传，无需 AT+CIPSEND */
        HAL_UART_Transmit(&huart2, (uint8_t*)g_esp_rx_buf, g_esp_rx_cnt, HAL_MAX_DELAY);

        /* 清空缓冲区，准备下一次接收 */
        esp_clear_rx_buf();
    }

    /* [10] 退出透传模式：发送 +++（前后需1秒静默） */
    printf("[10] 发送 +++ 退出透传模式...\r\n");
    delay_ms(1000);  /* 退出前静默1秒 */
    HAL_UART_Transmit(&huart2, (uint8_t*)"+++", 3, HAL_MAX_DELAY);
    delay_ms(1000);  /* 退出后静默1秒，等待ESP回到AT命令模式 */
    printf("    已发送 +++\r\n");
    printf("    ESP回应: %s\r\n", (char*)g_esp_rx_buf);
    printf("\r\n");
	
	/* 发送断开指令 */
    esp_send_at("AT+CIPCLOSE\r\n");
    printf("========================================\r\n");
    printf("      TCP客户端测试完成\r\n");
    printf("========================================\r\n");

}

void esp_test3(void)
{
    esp_err_t ret;

    printf("\r\n");
    printf("========================================\r\n");
    printf("      ESP12-F TCP客户端测试（封装函数）\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    /* [1] 初始化ESP模块 */
    printf("[1] 初始化ESP模块...\r\n");
    esp_init();
    printf("    初始化完成\r\n");
    printf("\r\n");
    delay_ms(500);

    /* [2] AT 自检 */
    printf("[2] AT 自检...\r\n");
    ret = esp_self_test();
    if (ret == ESP_OK)
    {
        printf("    [OK] 自检成功\r\n");
    }
    else
    {
        printf("    [FAIL] 自检失败，错误码: %d\r\n", ret);
    }
    printf("\r\n");

    /* [3] 关闭回显 */
    printf("[3] 关闭回显...\r\n");
    ret = esp_enable_echo(0);
    if (ret == ESP_OK)
    {
        printf("    [OK] 关闭回显成功\r\n");
    }
    else
    {
        printf("    [FAIL] 关闭回显失败，错误码: %d\r\n", ret);
    }
    printf("\r\n");

    /* [4] 连接Wi-Fi热点 */
    printf("[4] 连接Wi-Fi热点 SSID=%s...\r\n", WIFI_SSID);
    ret = esp_connect_ap(WIFI_SSID, WIFI_PASSWORD);
    if (ret == ESP_OK)
    {
        printf("    [OK] 连接热点成功\r\n");
    }
    else
    {
        printf("    [FAIL] 连接热点失败，错误码: %d\r\n", ret);
        printf("    停止测试\r\n");
        while (1) {}
    }
    printf("\r\n");

    /* [5] 设置单连接模式 */
    printf("[5] 设置单连接模式 CIPMUX=0...\r\n");
    ret = esp_enable_multiple_id(0);
    if (ret == ESP_OK)
    {
        printf("    [OK] 单连接模式设置成功\r\n");
    }
    else
    {
        printf("    [FAIL] 设置失败，错误码: %d\r\n", ret);
    }
    printf("\r\n");

    /* [6] TCP连接服务器 */
    printf("[6] TCP连接服务器 192.168.31.154:9501...\r\n");
    ret = esp_connect_server(ESP_PROTOCOL_TCP, "192.168.31.154", 9501);
    if (ret == ESP_OK)
    {
        printf("    [OK] TCP连接服务器成功\r\n");
    }
    else
    {
        printf("    [FAIL] TCP连接服务器失败，错误码: %d\r\n", ret);
        printf("    停止测试\r\n");
        while (1) {}
    }
    printf("\r\n");

    /* [7] 进入透传模式 */
    printf("[7] 进入透传模式...\r\n");
    ret = esp_entry_transparent_transmission();
    if (ret == ESP_OK)
    {
        printf("    [OK] 已进入透传模式\r\n");
    }
    else
    {
        printf("    [FAIL] 进入透传模式失败，错误码: %d\r\n", ret);
        printf("    停止测试\r\n");
        while (1) {}
    }
    printf("\r\n");

    /* [8] 透传模式下发送 "hello world" */
    printf("[8] 发送数据 \"hello world\"...\r\n");
    esp_send_str("hello world");
    delay_ms(1000);
    printf("    发送完成\r\n");
    printf("\r\n");

    /* [9] while(1) 循环：接收服务器数据，原样回传（echo） */
    printf("[9] 进入接收->回传循环（发送\"end\"退出）...\r\n");
    printf("========================================\r\n");
    while (1)
    {
        if (g_esp_rx_end == 0)
        {
            delay_ms(1);
            continue;
        }

        /* 串口1打印收到的数据 */
        printf("    [收到 %u 字节] ", g_esp_rx_cnt);
        HAL_UART_Transmit(&huart1, (uint8_t*)g_esp_rx_buf, g_esp_rx_cnt, HAL_MAX_DELAY);
        printf("\r\n");

        /* 检查是否收到 "end" 退出指令 */
        if (strstr((char*)g_esp_rx_buf, "end") != NULL)
        {
            printf("    [收到退出指令，准备退出]\r\n");
            break;
        }

        /* 透传模式下直接回传 */
        esp_send_bytes((uint8_t*)g_esp_rx_buf, g_esp_rx_cnt);

        /* 清空缓冲区，准备下一次接收 */
        esp_clear_rx_buf();
    }

    /* [10] 退出透传模式 */
    printf("[10] 退出透传模式...\r\n");
    ret = esp_exit_transparent_transmission();
    if (ret == ESP_OK)
    {
        printf("    [OK] 已退出透传模式\r\n");
    }
    else
    {
        printf("    [FAIL] 退出透传模式失败，错误码: %d\r\n", ret);
    }
    printf("\r\n");

    /* [11] 断开服务器连接 */
    printf("[11] 断开服务器连接...\r\n");
    ret = esp_disconnect_server();
    if (ret == ESP_OK)
    {
        printf("    [OK] 已断开服务器连接\r\n");
    }
    else
    {
        printf("    [FAIL] 断开服务器连接失败，错误码: %d\r\n", ret);
    }
    printf("\r\n");

    printf("========================================\r\n");
    printf("      TCP客户端测试完成\r\n");
    printf("========================================\r\n");
}