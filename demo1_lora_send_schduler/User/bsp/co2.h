/**
 * @file    co2.h
 * @brief   CO2 传感器（MH-Z14A 协议）驱动模块头文件
 *
 * 使用流程：
 *   1. 调用 co2_init() 启动串口2「固定6字节+空闲中断」接收；
 *   2. 在 HAL_UARTEx_RxEventCallback 回调中调用 co2_rx_event_handler()
 *      完成数据帧接收并自动重启下一次接收；
 *   3. 主循环中调用 co2_read() 获取解析后的 CO2 浓度与满量程。
 *
 * 接收方案说明：
 *   使用 HAL_UARTEx_ReceiveToIdle_IT 固定接收6字节+空闲中断，
 *   与 demo2_co2_read_封装函数 一致（已在同样硬件上验证可工作）。
 *
 * 代码风格遵循 Linux 内核规范：Tab 缩进、K&R 大括号、小写下划线命名。
 */
#ifndef __CO2_H__
#define __CO2_H__

#include <stdint.h>

/* ============================ 数据帧定义 ============================
 * MH-Z14A CO2 传感器串口输出一帧共 6 字节：
 *   [0] 帧头     ：固定 0x2C
 *   [1] 浓度高字节：CO2 浓度 PPM 值的高 8 位
 *   [2] 浓度低字节：CO2 浓度 PPM 值的低 8 位
 *   [3] 量程高字节：满量程 PPM 值的高 8 位
 *   [4] 量程低字节：满量程 PPM 值的低 8 位
 *   [5] 校验和   ：前 5 字节累加和的低 8 位
 * ================================================================== */
#define CO2_FRAME_SIZE     6    /* 一帧数据的字节数                */
#define CO2_FRAME_HEADER   0x2C /* 帧头固定值（实测 MH-Z14A 帧头） */
#define CO2_IDX_HEADER     0    /* 帧头索引                        */
#define CO2_IDX_CONC_H     1    /* CO2 浓度高字节索引              */
#define CO2_IDX_CONC_L     2    /* CO2 浓度低字节索引              */
#define CO2_IDX_RANGE_H    3    /* 满量程高字节索引                */
#define CO2_IDX_RANGE_L    4    /* 满量程低字节索引                */
#define CO2_IDX_CHECKSUM   5    /* 校验和索引                      */

/* ============================ 返回值定义 ============================ */
#define CO2_OK          0    /* 操作成功                          */
#define CO2_ERR_INVAL   (-1) /* 参数错误                          */
#define CO2_ERR_NOFRAME (-2) /* 暂无可用的新数据帧（未收到数据）  */
#define CO2_ERR_CHECK   (-3) /* 数据帧格式或校验和错误            */

/* ============================ 数据结构定义 ========================== */
/* CO2 传感器测量结果 */
struct co2_t {
	uint16_t concentration; /* CO2 浓度（单位：PPM）          */
	uint16_t full_scale;    /* 传感器满量程（单位：PPM）      */
};

/* ============================ 接口函数声明 ========================== */
/**
 * @brief  初始化 CO2 传感器，启动串口2「固定6字节+空闲中断」接收
 *
 * @note   必须在串口2初始化完成之后调用，且仅需调用一次。
 *         初始化前会清空DR中可能残留的旧字节，从干净状态开始。
 *         接收到的数据帧由 co2_rx_event_handler() 处理。
 *
 * @retval CO2_OK          初始化成功
 */
int co2_init(void);

/**
 * @brief  读取并解析 CO2 传感器数据
 *
 * @param  co2_data 测量结果输出指针，成功时返回 CO2 浓度与满量程
 *
 * @retval CO2_OK          读取成功，数据已填入 co2_data
 * @retval CO2_ERR_INVAL   co2_data 为空指针
 * @retval CO2_ERR_NOFRAME 尚未接收到新的数据帧（非错误，可稍后重试）
 * @retval CO2_ERR_CHECK   数据帧格式或校验和错误，该帧数据已丢弃
 */
int co2_read(struct co2_t *co2_data);

/**
 * @brief  串口2接收事件处理函数（由 HAL_UARTEx_RxEventCallback 调用）
 *
 * @note   在 HAL_UARTEx_RxEventCallback 中当 USART2 收到6字节或检测到
 *         空闲时调用。函数内部保存本次接收长度，并自动重新启动下一次
 *         接收（固定6字节+空闲中断）。
 *
 * @param  size 本次实际接收到的字节数
 */
void co2_rx_event_handler(uint32_t size);

/**
 * @brief  重新启动串口2接收（用于UART错误恢复）
 *
 * @note   当UART2发生Overrun/Framing等错误时，HAL会停止接收。
 *         调用此函数可清除错误标志、恢复状态并重启
 *         固定6字节+空闲中断接收。
 */
void co2_rx_restart_from_error(void);

#endif /* __CO2_H__ */
