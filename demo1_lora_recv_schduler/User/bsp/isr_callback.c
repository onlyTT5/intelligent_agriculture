/**
 * @file    isr_callback.c
 * @brief   HAL 中断回调函数集合
 *
 * 回调分发说明：
 *   1. USART1（调试串口）：接收事件上报给应用层，由应用层处理；
 *   2. USART2（ESP12-F Wi-Fi模块）：接收事件转发给 ESP 驱动模块处理，
 *      由 esp_rx_event_handler() 完成数据保存与下次接收启动。
 */
#include "main.h"
#include "esp.h"
#include "co2.h"
#include "llcc68_p2p.h"


/* 引用应用层定义的串口1接收标志 */
extern uint32_t g_uart1_rx_cnt;
extern uint32_t g_uart1_rx_end;


//GPIO外部中断回调函数
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if(GPIO_Pin==LORA_DIO1_Pin){
		//调用处理Lora收发的回调
		DIO1_EXTI_Callback();
	}
}
/**
 * @brief  UART 错误回调（Overrun/Framing/Noise等错误时由HAL调用）
 *
 * 当UART2收到噪声或Overrun错误时，HAL会停止接收并调用此函数。
 * 如果不重新启动接收，UART2将永久停止工作。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART2)
	{
		/* UART2发生错误（Overrun/Noise/Framing），重启接收 */
		co2_rx_restart_from_error();
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

	/* USART2 连接 ESP12-F Wi-Fi模块：交给 ESP 驱动模块处理 */
	if (huart->Instance == USART2)
		esp_rx_event_handler(Size);
}
