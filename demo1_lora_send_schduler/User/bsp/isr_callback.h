/**
 * @file    isr_callback.h
 * @brief   HAL 中断回调函数集合头文件
 *
 * 本文件声明 isr_callback.c 中实现的回调分发逻辑。
 * 当前仅包含头文件保护宏，回调函数均由 HAL 库通过弱符号链接直接调用，
 * 无需在此处额外声明。
 *
 * 回调分发说明（详见 isr_callback.c）：
 *   1. USART1（调试串口）：接收事件上报给应用层
 *   2. USART2（CO2传感器 MH-Z14A）：HAL_UART_RxCpltCallback 中处理
 *   3. EXTI DIO1（LLCC68 LoRa中断）：HAL_GPIO_EXTI_Callback 中处理
 */
#ifndef __ISR_CALLBACK_H__
#define __ISR_CALLBACK_H__



#endif
