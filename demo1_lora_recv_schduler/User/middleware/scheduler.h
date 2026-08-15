/**
 * @file    scheduler.h
 * @brief   裸机任务调度器（时间片轮询）头文件
 *
 * 使用流程：
 *   1. scheduler_init()                初始化调度器
 *   2. scheduler_add_task(func, period) 注册任务及其执行周期
 *   3. scheduler_run()                  启动调度，永不返回
 *
 * 特点：
 *   - 基于 HAL_GetTick() 提供毫秒级时间基准
 *   - 主循环空闲时调用 __wfi() 进入低功耗，由中断唤醒
 *   - 非抢占式：任务依次执行，禁止长时间阻塞
 */
#ifndef __SCHEDULER_H__
#define __SCHEDULER_H__

#include <stdint.h>

/* ============================ 配置宏 ============================ */
#define SCHED_TASK_MAX_NUM  8   /* 调度器支持的最大任务数 */

/* ============================ 类型定义 ============================ */
/**
 * @brief 任务函数原型
 * @note  任务函数不得长时间阻塞（如 delay_ms），否则影响其他任务调度
 */
typedef void (*task_func_t)(void);

/**
 * @brief 任务描述符
 *        记录任务函数、执行周期与上次执行时间戳
 */
typedef struct {
    task_func_t func;       /* 任务函数指针               */
    uint32_t    period_ms;  /* 执行周期(ms)，0 表示每次调用 */
    uint32_t    last_ms;    /* 上次执行的时间戳            */
} task_t;

/* ============================ 接口函数声明 ============================ */
/**
 * @brief  初始化调度器，清空任务表
 * @param  无
 * @return 无
 * @note   必须在 scheduler_add_task / scheduler_run 之前调用
 */
void scheduler_init(void);

/**
 * @brief  向调度器注册一个周期任务
 * @param  func      任务函数指针
 * @param  period_ms 执行周期(ms)，0 表示每次主循环都调用
 * @return 0: 注册成功
 *         -1: 任务表已满或参数非法
 */
int scheduler_add_task(task_func_t func, uint32_t period_ms);

/**
 * @brief  启动调度器，按时间片轮询执行各任务
 * @param  无
 * @return 无
 * @note   本函数永不返回，内部为无限循环；
 *         空闲时调用 __wfi() 进入低功耗，由中断唤醒继续轮询。
 */
void scheduler_run(void);

#endif /* __SCHEDULER_H__ */
