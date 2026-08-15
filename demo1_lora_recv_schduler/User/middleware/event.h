/**
 * @file    event.h
 * @brief   事件队列（中断 → 主循环）头文件
 *
 * 使用场景：
 *   - 中断服务程序中调用 event_post() 投递事件；
 *   - 主循环任务中调用 event_get() / event_has_event() 消费事件。
 *
 * 特点：
 *   - 环形缓冲区实现，FIFO 先进先出；
 *   - 临界区采用关中断保护，保证多生产者/单消费者安全；
 *   - 队列满时丢弃最新事件，避免覆盖未处理的旧事件。
 */
#ifndef __EVENT_H__
#define __EVENT_H__

#include <stdint.h>

/* ============================ 配置宏 ============================ */
#define EVT_QUEUE_SIZE  16   /* 事件队列容量（必须为 2 的幂以便取模优化，此处用通用取模） */

/* ============================ 事件类型定义 ============================ */
/**
 * @brief 系统事件类型枚举
 * @note  新增业务事件时在此扩展
 */
typedef enum {
    EVT_NONE = 0,           /* 无事件                  */
    EVT_KEY1_PRESSED,       /* KEY1 按下（启动 CO2 采集） */
    EVT_KEY2_PRESSED,       /* KEY2 按下（停止 CO2 采集） */
    EVT_CO2_FRAME_READY,    /* CO2 传感器一帧数据就绪   */
    EVT_UART1_RX,           /* 调试串口接收到数据       */
    EVT_LORA_DATA_READY,    /* LoRa 接收到新的传感器数据  */
    /* ... 后续业务事件在此扩展 ... */
    EVT_MAX
} event_type_t;

/**
 * @brief 事件结构体
 *        携带事件类型与附加参数（如字节长度、数值等）
 */
typedef struct {
    event_type_t type;   /* 事件类型           */
    uint32_t     arg;    /* 附加参数，按需使用 */
} event_t;

/* ============================ 接口函数声明 ============================ */
/**
 * @brief  初始化事件队列
 * @param  无
 * @return 无
 * @note   清空头尾指针，队列为空。使用前必须调用一次。
 */
void event_init(void);

/**
 * @brief  向队列投递一个事件（可在中断中调用）
 * @param  type 事件类型
 * @param  arg  附加参数
 * @return 无
 * @note   内部关中断保护临界区；队列满时丢弃本次事件。
 */
void event_post(event_type_t type, uint32_t arg);

/**
 * @brief  从队列取出一个事件（在主循环中调用）
 * @param  无
 * @return event_t 若队列非空返回最早投递的事件，否则返回 type=EVT_NONE
 */
event_t event_get(void);

/**
 * @brief  查询队列是否还有未处理事件
 * @param  无
 * @return 1: 有事件
 *         0: 队列为空
 */
uint8_t event_has_event(void);

#endif /* __EVENT_H__ */
