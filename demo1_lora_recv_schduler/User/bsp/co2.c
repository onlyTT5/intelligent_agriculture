/**
 * @file    co2.c
 * @brief   CO2 传感器驱动模块实现
 *
 * 实现原理：
 *   1. 串口2以「接收指定长度或空闲中断」方式接收数据帧，一帧 6 字节；
 *   2. 数据帧在中断回调中保存到内部缓冲区，并重新启动下一次接收；
 *   3. 应用层调用 co2_read() 时，对缓冲区做帧头与校验和验证，
 *      再解析出 CO2 浓度与满量程。
 */
#include "main.h"
#include "usart.h"
#include "co2.h"

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

/* g_co2_rx_buf 虽然是 static，但 co2_rx_restart_from_error 可以访问 */

/* ============================ 对外接口函数 ============================ */
/**
 * @brief  初始化 CO2 传感器，启动串口2空闲中断接收
 *
 * @retval CO2_OK  初始化成功
 */
int co2_init(void)
{
	g_co2_rx_size = 0;
	g_co2_frame_end = 0;

	/* 启动串口2空闲中断接收，CO2 传感器每 1~2 秒自动上报一帧数据 */
	co2_rx_restart();

	return CO2_OK;
}

/**
 * @brief  串口2接收事件处理函数（由 HAL 回调函数调用）
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

	/* 帧头、帧长、校验和验证 */
	if (co2_frame_check(g_co2_rx_buf, g_co2_rx_size) != CO2_OK)
		return CO2_ERR_CHECK;

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
 * @brief  重新启动串口2空闲中断接收
 *
 * @note   必须在串口2初始化完成且处于空闲状态下调用，
 *         否则 HAL 会返回 HAL_BUSY。
 */
static void co2_rx_restart(void)
{
	HAL_StatusTypeDef hal_status;

	hal_status = HAL_UARTEx_ReceiveToIdle_IT(&huart2, g_co2_rx_buf, CO2_FRAME_SIZE);

	/* 如果UART忙（例如正在发送），稍后由应用层重试 */
	if (hal_status != HAL_OK)
	{
		/* 标记需要重试，但不阻塞中断 */
		g_co2_frame_end = 0;
	}
}

void co2_rx_restart_from_error(void)
{
	/* 清除UART错误标志 */
	__HAL_UART_CLEAR_OREFLAG(&huart2);
	__HAL_UART_CLEAR_FEFLAG(&huart2);
	__HAL_UART_CLEAR_NEFLAG(&huart2);

	/* 强制将UART状态恢复为READY */
	huart2.RxState = HAL_UART_STATE_READY;

	/* 重新启动接收 */
	co2_rx_restart();
}
