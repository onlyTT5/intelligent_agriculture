/**
 * @file    event.c
 * @brief   事件队列（中断 → 主循环）实现
 *
 * 实现原理：
 *   1. 使用环形缓冲区（ring buffer）存储事件；
 *   2. head 指向下一个待取出事件，tail 指向下一个写入位置；
 *   3. event_post() 写入 tail 并推进，event_get() 读出 head 并推进；
 *   4. 读写指针变更时关中断，保证中断与主循环之间的线程安全。
 *
 * 生产者/消费者模型：
 *   - 生产者：中断服务程序（多个中断源可同时投递）
 *   - 消费者：主循环任务（单消费者）
 */
#include "event.h"
#include "main.h"   /* __disable_irq / __enable_irq */

/* ============================ 私有变量 ============================ */
/* 事件环形缓冲区 */
static event_t s_queue[EVT_QUEUE_SIZE];

/* 头指针：下一个待取出事件的位置（消费者） */
static volatile uint8_t s_head = 0;

/* 尾指针：下一个待写入事件的位置（生产者） */
static volatile uint8_t s_tail = 0;

/* ============================ 接口函数实现 ============================ */
/**
 * @brief  初始化事件队列
 */
void event_init(void)
{
    __disable_irq();
    s_head = 0;
    s_tail = 0;
    __enable_irq();
}

/**
 * @brief  向队列投递一个事件（可在中断中调用）
 * @note   队列满时丢弃本次事件，避免覆盖未处理的旧事件
 */
void event_post(event_type_t type, uint32_t arg)
{
    uint8_t next;   /* 计算写入后的尾指针位置 */

    __disable_irq();

    /* 计算下一个尾指针位置（环形回绕） */
    next = (uint8_t)((s_tail + 1) % EVT_QUEUE_SIZE);

    /* 队列未满时写入事件 */
    if (next != s_head) {
        s_queue[s_tail].type = type;
        s_queue[s_tail].arg  = arg;
        s_tail = next;
    }
    /* 队列满则丢弃本次事件，保护已投递事件不被覆盖 */

    __enable_irq();
}

/**
 * @brief  从队列取出一个事件（在主循环中调用）
 * @return 事件结构体，队列空时返回 type=EVT_NONE
 */
event_t event_get(void)
{
    event_t evt;    /* 返回的事件 */

    evt.type = EVT_NONE;
    evt.arg  = 0;

    __disable_irq();

    /* 队列非空时取出最早投递的事件 */
    if (s_head != s_tail) {
        evt = s_queue[s_head];
        s_head = (uint8_t)((s_head + 1) % EVT_QUEUE_SIZE);
    }

    __enable_irq();

    return evt;
}

/**
 * @brief  查询队列是否还有未处理事件
 * @return 1: 有事件；0: 队列为空
 */
uint8_t event_has_event(void)
{
    uint8_t has;

    __disable_irq();
    has = (s_head != s_tail) ? 1u : 0u;
    __enable_irq();

    return has;
}
