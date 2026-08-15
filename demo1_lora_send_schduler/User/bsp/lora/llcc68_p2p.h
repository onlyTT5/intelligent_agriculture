/**
 * @file llcc68_p2p.h
 * @brief LLCC68 LoRa点对点通信驱动头文件
 *
 * @details
 *   本文件定义了LLCC68 LoRa模块点对点通信所需的硬件引脚、射频参数、
 *   全局变量和函数声明。LLCC68是一款由Semtech推出的Sub-GHz LoRa芯片，
 *   支持470-510MHz中国频段，最大发射功率+22dBm。
 *
 *   本驱动可被两种业务方式使用：
 *   - 传统演示方式：调用 llcc68_p2p_demo() 做 while(1) 阻塞式收发
 *   - 调度器方式   ：调用 llcc68_init/llcc68_lora_send 等独立接口，
 *                    由 app_lora.c 的调度任务按周期非阻塞调用
 *
 *   功能包括：
 *   1. LoRa模块初始化（复位、校准、射频参数配置）
 *   2. LoRa数据发送（支持自定义超时）
 *   3. LoRa数据接收（支持连续接收模式和超时接收模式）
 *   4. DIO1外部中断回调（处理发送完成、接收完成、超时等事件）
 *
 *   硬件连接说明：
 *   - SPI1: SCK=PA5, MISO=PA6, MOSI=PA7
 *   - NSS=PA4（片选，软件控制）
 *   - RST=PB1（复位）
 *   - BUSY=PB0（忙信号，输入）
 *   - DIO1=PB10（中断信号，上升沿触发）
 *
 *   依赖文件：
 *   - llcc68.h / llcc68.c        —— LLCC68底层驱动（Semtech官方API封装）
 *   - llcc68_hal.h / llcc68_hal.c —— HAL层抽象（SPI读写、GPIO控制、延时）
 *   - llcc68_regs.h               —— 寄存器定义
 *   - llcc68_status.h             —— 状态码定义
 *
 * @version 0.2
 * @date 2026-02-01
 *
 * @author 温老师
 * @copyright Copyright (c) 2026
 *
 */
#ifndef LLCC68_P2P_H
#define LLCC68_P2P_H

/* 包含LLCC68底层驱动头文件和HAL抽象层头文件 */
#include "llcc68.h"
#include "llcc68_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/************************ 硬件引脚定义 ************************/

/* SPI句柄：使用SPI1 */
#define LLCC68_SPI_HANDLE &hspi1

/* NSS片选引脚：PA4，软件控制，低电平有效 */
#define LLCC68_NSS_PORT GPIOA
#define LLCC68_NSS_PIN  GPIO_PIN_4

/* RST复位引脚：PB1，低电平复位 */
#define LLCC68_RST_PORT GPIOB
#define LLCC68_RST_PIN  GPIO_PIN_1

/* BUSY忙信号引脚：PB0，输入，高电平表示模块忙 */
#define LLCC68_BUSY_PORT GPIOB
#define LLCC68_BUSY_PIN  GPIO_PIN_0

/* DIO1中断引脚：PB10，上升沿触发外部中断 */
#define LLCC68_DIO1_PORT GPIOB
#define LLCC68_DIO1_PIN  GPIO_PIN_10

/************************ LoRa核心参数 ************************/

/* 射频频率：470.5MHz（中国LoRa频段范围：470.0MHz~510.0MHz） */
#define LORA_FREQ 470500000UL

/* 扩频因子：SF9（扩频因子越大，传输距离越远，但速率越低） */
#define LORA_SF LLCC68_LORA_SF9

/* 带宽：125kHz（带宽越小，灵敏度越高，但速率越低） */
#define LORA_BW LLCC68_LORA_BW_125

/* 编码率：4/5（编码率越低，纠错能力越强，但有效数据率越低） */
#define LORA_CR LLCC68_LORA_CR_4_5

/* 前导码长度：8个符号（收发端必须一致） */
#define LORA_PREAMBLE_LEN 8

/* 数据包最大长度：255字节（LLCC68芯片FIFO最大支持255字节） */
#define LORA_PAYLOAD_LEN 255

/* 发射功率：+22dBm（LLCC68最大发射功率，约158mW） */
#define LORA_TX_POWER_DBM 22

/************************ 全局变量声明 ************************/

/* LoRa模块硬件上下文，包含SPI句柄和各GPIO引脚信息 */
extern llcc68_hal_context_t llcc68_ctx;

/* 接收数据缓冲区（以int16_t为单位，最多存放127个int16_t数据） */
extern volatile int16_t rx_data[LORA_PAYLOAD_LEN / 2];

/************************ 函数声明 ************************/

/**
 * @brief DIO1外部中断回调函数
 *
 * @details
 *   当LLCC68模块产生DIO1中断信号（上升沿）时，由EXTI中断服务函数
 *   最终调用此函数。函数内部读取并清除中断状态，根据中断类型设置
 *   对应的标志位（tx_done / rx_done / rx_timeout）。
 *
 *   调用链：EXTI15_10_IRQHandler → HAL_GPIO_EXTI_IRQHandler
 *         → HAL_GPIO_EXTI_Callback → DIO1_EXTI_Callback
 *
 * @param 无
 * @return 无
 */
void DIO1_EXTI_Callback(void);

/**
 * @brief 初始化LLCC68 LoRa模块
 *
 * @details
 *   执行完整的硬件初始化流程：
 *   1. 硬件复位
 *   2. 进入STDBY_RC待机模式
 *   3. 切换到STDBY_XOSC待机模式（启用外部晶振）
 *   4. 清除设备错误
 *   5. 校准PLL、ADC、Image
 *   6. 配置PA参数和OCP过流保护
 *   7. 设置包类型为LoRa
 *   8. 配置射频频率、扩频因子、带宽、编码率
 *   9. 配置DIO中断映射
 *   10. 设置缓冲区基地址并清除中断标志
 *
 * @param context LLCC68硬件上下文指针，包含SPI句柄和GPIO引脚信息
 * @return llcc68_status_t 初始化状态
 *   @retval LLCC68_STATUS_OK    初始化成功
 *   @retval LLCC68_STATUS_ERROR 初始化失败（SPI通信异常或芯片无响应）
 */
llcc68_status_t llcc68_init(const void *context);

/**
 * @brief LoRa数据发送函数
 *
 * @details
 *   将数据写入LLCC68的TX FIFO缓冲区，并启动发送。
 *   发送完成后通过DIO1中断设置tx_done标志位。
 *   发送前会重新配置包类型、数据包参数和调制参数，确保与接收端匹配。
 *
 * @param context       LLCC68硬件上下文指针
 * @param data          待发送数据缓冲区指针
 * @param len           待发送数据长度（字节），最大255
 * @param timeout_in_ms 发送超时时间（毫秒），超时后返回错误
 * @return llcc68_status_t 发送状态
 *   @retval LLCC68_STATUS_OK    发送成功
 *   @retval LLCC68_STATUS_ERROR 发送失败（超时或数据过长）
 */
llcc68_status_t llcc68_lora_send(const void *context, const uint8_t *data, uint8_t len, uint32_t timeout_in_ms);

/**
 * @brief 设置LoRa接收模式
 *
 * @details
 *   配置数据包参数和调制参数，将LLCC68切换到接收模式。
 *   支持两种接收模式：
 *   - 连续接收模式（timeout_in_ms=0）：模块持续监听，直到收到数据
 *   - 超时接收模式（timeout_in_ms>0）：在指定时间内监听，超时自动退出
 *
 *   接收到数据后，DIO1产生中断，在DIO1_EXTI_Callback中设置rx_done标志。
 *
 * @param context        LLCC68硬件上下文指针
 * @param timeout_in_ms  接收超时时间（毫秒），0表示连续接收模式
 * @return llcc68_status_t 设置状态
 *   @retval LLCC68_STATUS_OK    设置接收模式成功
 *   @retval LLCC68_STATUS_ERROR 设置失败
 */
llcc68_status_t llcc68_lora_receive_mode(const void *context, uint32_t timeout_in_ms);

/**
 * @brief 等待并读取LoRa接收数据
 *
 * @details
 *   阻塞等待rx_done或rx_timeout标志位被设置（由DIO1中断触发）。
 *   接收成功后，从RX FIFO读取数据，并获取信号强度（RSSI）和信噪比（SNR）。
 *   读取完成后自动清理标志位，为下一次接收做准备。
 *
 * @param context       LLCC68硬件上下文指针
 * @param data          接收数据缓冲区指针（由调用者分配）
 * @param len           输出参数，返回实际接收到的数据长度（字节）
 * @param pkt_status    输出参数，返回数据包状态（包含RSSI和SNR）
 * @param timeout_in_ms 等待超时时间（毫秒），超时后返回错误
 * @return llcc68_status_t 接收状态
 *   @retval LLCC68_STATUS_OK    接收成功
 *   @retval LLCC68_STATUS_ERROR 接收失败（超时或CRC错误）
 */
llcc68_status_t llcc68_lora_receive_data(const void *context, uint8_t *data, uint16_t *len,
                                         llcc68_pkt_status_lora_t *pkt_status, uint32_t timeout_in_ms);

#ifdef __cplusplus
}
#endif

#endif
