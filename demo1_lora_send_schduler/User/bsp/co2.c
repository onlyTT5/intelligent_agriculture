/**
 * @file    co2.c
 * @brief   CO2 传感器驱动模块实现
 *
 * 实现原理：
 *   1. 串口2以「固定6字节 + 空闲中断」方式接收数据帧，一帧 6 字节；
 *   2. 数据帧在中断回调中保存到内部缓冲区，并立即重新启动下一次接收；
 *   3. 应用层调用 co2_read() 时，对缓冲区做帧头与校验和验证，
 *      再解析出 CO2 浓度与满量程。
 *
 * 设计要点（与 demo2_co2_read_封装函数 一致，已验证可工作）：
 *   - 使用 HAL_UARTEx_ReceiveToIdle_IT 固定接收 6 字节，收满或检测到
 *     空闲即触发 HAL_UARTEx_RxEventCallback；
 *   - 收到一帧后在 co2_rx_event_handler() 中保存长度、置完成标志并
 *     立即重启接收，保证传感器连续上报时数据不丢失；
 *   - USART2 的接收事件在 isr_callback.c 的 HAL_UARTEx_RxEventCallback
 *     中分发到 co2_rx_event_handler()（不是 RxCpltCallback）。
 *
 * 注意：不要改回「单字节 HAL_UART_Receive_IT + 状态机」方案 ——
 *   该方案依赖 HAL_UART_RxCpltCallback 逐字节处理，而工程中该回调
 *   只处理 USART1，USART2 的字节永远不会被消费，接收会永久挂起，
 *   co2_read() 将一直返回 CO2_ERR_NOFRAME(-2)。
 */
#include "main.h"
#include "usart.h"
#include "co2.h"
#include <stdio.h>

/* ============================ 私有全局变量 ============================
 * 接收缓冲区与状态标志仅在本模块内部使用，外部只能通过接口函数访问，
 * 实现数据的封装与保护。
 */
static uint8_t  g_co2_rx_buf[CO2_FRAME_SIZE]; /* 串口2接收缓冲区，存放一帧 CO2 数据 */
static uint32_t g_co2_rx_size;                /* 本次接收到的有效字节数            */
static uint8_t  g_co2_frame_end;              /* 一帧数据接收完成标志（1=完成）   */

/* ============================ 内部函数声明 ============================ */
static int  co2_frame_check(const uint8_t *frame, uint32_t size);
static void co2_rx_restart (void);

/* ============================ 对外接口函数 ============================ */
/**
 * @brief  初始化 CO2 传感器，启动串口2「固定6字节+空闲中断」接收
 *
 * @retval CO2_OK  初始化成功
 */
int co2_init(void)
{
	g_co2_rx_size = 0;
	g_co2_frame_end = 0;

	/* 清空DR中可能残留的全部字节（USART2初始化后传感器可能已发送多帧数据，
	 * 移位寄存器中可能堆积了旧字节）。必须用 while 循环清空：
	 * RXNE=1 表示 DR 中有一个字节，读DR后 RXNE 自动清零，
	 * 但若移位寄存器中还有下一个待传字节，硬件会立即再次置 RXNE=1，
	 * 因此需要循环直到 RXNE=0，确保 DR 完全清空。 */
	while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE))
		(void)huart2.Instance->DR;

	/* 清除ORE溢出标志（读SR再读DR，此时DR已空不会丢有效字节） */
	if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE))
		__HAL_UART_CLEAR_OREFLAG(&huart2);

	/* 启动串口2「固定6字节+空闲中断」接收，CO2 传感器每 1~2 秒自动上报一帧 */
	co2_rx_restart();

	return CO2_OK;
}

/**
 * @brief  串口2接收事件处理函数（由 HAL_UARTEx_RxEventCallback 调用）
 *
 * @param  size 本次实际接收到的字节数
 */
void co2_rx_event_handler(uint32_t size)
{
	g_co2_rx_size = size;
	g_co2_frame_end = 1;

	/* 立即重新启动下一次接收，保证传感器连续上报时数据不丢失 */
	co2_rx_restart();
}

/**
 * @brief  读取并解析 CO2 传感器数据
 *
 * @param  co2_data 测量结果输出指针
 *
 * @retval CO2_OK          读取成功
 * @retval CO2_ERR_INVAL   参数错误
 * @retval CO2_ERR_NOFRAME 尚未收到新数据帧
 * @retval CO2_ERR_CHECK   数据帧校验失败
 */
int co2_read(struct co2_t *co2_data)
{
	if (co2_data == NULL)
		return CO2_ERR_INVAL;

	/* 尚未收到新数据帧，非错误，可稍后重试 */
	if (!g_co2_frame_end)
		return CO2_ERR_NOFRAME;

	/* 无论解析成功与否，都清除结束标志，避免重复处理同一帧 */
	g_co2_frame_end = 0;

	/* 调试打印：显示收到的原始字节（HEX格式），放在校验之前，
	 * 这样即使校验失败也能看到实际收到的内容 */
	{
		uint8_t i;
		printf("[CO2] 收到 %d 字节:", (int)g_co2_rx_size);
		for (i = 0; i < g_co2_rx_size; i++)
			printf(" %02X", g_co2_rx_buf[i]);
		printf("\r\n");
	}

	/* 帧头、帧长、校验和验证 */
	if (co2_frame_check(g_co2_rx_buf, g_co2_rx_size) != CO2_OK)
	{
		printf("[CO2] 帧校验失败 (size=%d)\r\n", (int)g_co2_rx_size);
		return CO2_ERR_CHECK;
	}

	/* 解析 CO2 浓度与满量程（大端格式：高字节在前） */
	co2_data->concentration = (uint16_t)((g_co2_rx_buf[CO2_IDX_CONC_H] << 8) |
					       g_co2_rx_buf[CO2_IDX_CONC_L]);
	co2_data->full_scale = (uint16_t)((g_co2_rx_buf[CO2_IDX_RANGE_H] << 8) |
					     g_co2_rx_buf[CO2_IDX_RANGE_L]);

	return CO2_OK;
}

/* ============================ 内部功能函数 ============================ */
/**
 * @brief  校验一帧 CO2 数据（帧头、帧长、校验和）
 *
 * @param  frame 数据帧缓冲区指针
 * @param  size  数据帧有效字节数
 *
 * @retval CO2_OK        校验通过
 * @retval CO2_ERR_CHECK 校验失败
 */
static int co2_frame_check(const uint8_t *frame, uint32_t size)
{
	uint8_t checksum = 0;
	uint8_t i;

	/* 必须收到完整的一帧数据 */
	if (size < CO2_FRAME_SIZE)
		return CO2_ERR_CHECK;

	/* 帧头校验：MH-Z14A 帧头固定为 0x2C */
	if (frame[CO2_IDX_HEADER] != CO2_FRAME_HEADER)
		return CO2_ERR_CHECK;

	/* 校验和 = 前 5 字节累加和的低 8 位 */
	for (i = 0; i < CO2_FRAME_SIZE - 1; i++)
		checksum += frame[i];

	if (checksum != frame[CO2_IDX_CHECKSUM])
		return CO2_ERR_CHECK;

	return CO2_OK;
}

/**
 * @brief  重新启动串口2「固定6字节+空闲中断」接收
 *
 * @note   必须在串口2初始化完成且处于空闲状态下调用，
 *         否则 HAL 会返回 HAL_BUSY。
 */
static void co2_rx_restart(void)
{
	HAL_UARTEx_ReceiveToIdle_IT(&huart2, g_co2_rx_buf, CO2_FRAME_SIZE);
}

/**
 * @brief  UART错误恢复：清除错误标志并重新启动接收
 *
 * @note   当UART2发生Overrun/Framing/Noise等错误时，HAL会停止接收。
 *         调用此函数可清除错误状态并重新启动「固定6字节+空闲中断」接收。
 *         （当前 isr_callback.c 的 ErrorCallback 不强制调用本函数，
 *           保留此接口供必要时手动恢复使用。）
 */
void co2_rx_restart_from_error(void)
{
	/* 清除UART错误标志（每个清除宏的触发条件不同，逐一清除） */
	if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE))
		__HAL_UART_CLEAR_OREFLAG(&huart2);
	if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_FE))
		__HAL_UART_CLEAR_FEFLAG(&huart2);
	if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_NE))
		__HAL_UART_CLEAR_NEFLAG(&huart2);

	/* 强制将UART状态恢复为READY（HAL因错误会将RxState置为READY） */
	huart2.RxState = HAL_UART_STATE_READY;

	/* 重新启动「固定6字节+空闲中断」接收 */
	HAL_UARTEx_ReceiveToIdle_IT(&huart2, g_co2_rx_buf, CO2_FRAME_SIZE);
}
