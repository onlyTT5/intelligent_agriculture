/**
 * @file    isr_callback.c
 * @brief   HAL 中断回调函数集合
 *
 * 回调分发说明：
 *   1. USART1（调试串口）：接收事件上报给应用层，由应用层处理；
 *   2. USART2（CO2传感器 MH-Z14A/JW01-CO2）：使用
 *      HAL_UARTEx_ReceiveToIdle_IT「固定6字节+空闲中断」接收，收到一帧后
 *      在 HAL_UARTEx_RxEventCallback 中调用 co2_rx_event_handler() 处理。
 *      （与 demo2_co2_read_封装函数 一致，已验证可工作）
 *   3. EXTI DIO1（LLCC68 LoRa中断）：HAL_GPIO_EXTI_Callback 中调用
 *      DIO1_EXTI_Callback()，处理TX_DONE/RX_DONE/TIMEOUT/CRC_ERROR中断。
 */
#include "main.h"
#include "co2.h"

/* 引用应用层定义的串口1接收标志 */
extern uint32_t g_uart1_rx_cnt;
extern uint32_t g_uart1_rx_end;

#include "llcc68_p2p.h"


/**
 * @brief  UART 错误回调（Overrun/Framing/Noise等错误时由HAL调用）
 *
 * 当UART2收到噪声或Overrun错误时，HAL会停止接收并调用此函数。
 * 如果不重新启动接收，UART2将永久停止工作。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	/* 与 demo2_co2_read_封装函数 保持一致：UART 错误交由 HAL 内部处理，
	 * 不在 ErrorCallback 中强制重置 USART2 接收。
	 *
	 * 原因：若 UART2 频繁发生 ORE/FE 等错误，此处每次强制调用
	 * co2_rx_restart_from_error() 重置接收，会导致接收永远停留在帧中途、
	 * 只能收到帧尾（如 03 FF 93），永远无法对齐帧头 0x2C。
	 *
	 * USART2 接收状态由 co2_read() 中的自愈看门狗兜底恢复。
	 */
	(void)huart;
}

//GPIO外部中断回调函数
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if(GPIO_Pin==LORA_DIO1_Pin){
		//调用处理Lora收发的回调
		DIO1_EXTI_Callback();
	}
}

/**
 * @brief  UART 接收完成回调（接收满指定长度数据时由 HAL 调用）
 *
 * @param  huart 触发回调的串口句柄
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	/* USART1 接收完成：置位接收结束标志，供应用层轮询 */
	if (huart->Instance == USART1)
		g_uart1_rx_end = 1;
}

/**
 * @brief  UART 接收事件回调（收到指定长度或检测到空闲时由 HAL 调用）
 *
 * @param  huart 触发回调的串口句柄
 * @param  Size  本次实际接收到的字节数
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	/* USART1 接收事件：记录字节数并置位结束标志，供应用层轮询 */
	if (huart->Instance == USART1) {
		g_uart1_rx_cnt = Size;
		g_uart1_rx_end = 1;
	}

	/* USART2 连接 CO2 传感器（MH-Z14A/JW01-CO2，固定6字节+空闲中断）：
	 *   与 demo2_co2_read_封装函数 一致，收到6字节或检测到空闲后，
	 *   交给 CO2 驱动模块处理并重新启动下一次接收。 */
	if (huart->Instance == USART2)
		co2_rx_event_handler(Size);
}
