/**
 * @file    scheduler.c
 * @brief   裸机任务调度器（时间片轮询）实现
 *
 * 实现原理：
 *   1. 维护一张任务表，每个任务记录函数指针、执行周期与上次执行时间戳；
 *   2. 主循环中读取 HAL_GetTick() 当前时间，若距上次执行时间超过周期，
 *      则调用该任务函数并刷新时间戳；
 *   3. 一轮扫描完毕后调用 __wfi() 进入低功耗，等待下一个中断唤醒。
 *
 * 注意：
 *   - 非抢占式调度，任务函数禁止长时间阻塞；
 *   - 时间基准依赖 HAL_GetTick()（SysTick），使用前确保已启动该定时器。
 */
#include "scheduler.h"
#include "main.h"   /* HAL_GetTick() / __wfi() */

/* ============================ 私有变量 ============================ */
/* 任务表：存放所有已注册的任务 */
static task_t s_task_table[SCHED_TASK_MAX_NUM];

/* 当前已注册任务数 */
static uint8_t s_task_cnt = 0;

/* ============================ 接口函数实现 ============================ */
/**
 * @brief  初始化调度器，清空任务表
 */
void scheduler_init(void)
{
    uint8_t i;

    /* 清空任务表与计数器 */
    for (i = 0; i < SCHED_TASK_MAX_NUM; i++) {
        s_task_table[i].func      = (task_func_t)0;
        s_task_table[i].period_ms = 0;
        s_task_table[i].last_ms   = 0;
    }
    s_task_cnt = 0;
}

/**
 * @brief  向调度器注册一个周期任务
 */
int scheduler_add_task(task_func_t func, uint32_t period_ms)
{
    /* 参数合法性检查 */
    if (func == (task_func_t)0) {
        return -1;
    }

    /* 任务表已满 */
    if (s_task_cnt >= SCHED_TASK_MAX_NUM) {
        return -1;
    }

    /* 注册任务到任务表 */
    s_task_table[s_task_cnt].func      = func;
    s_task_table[s_task_cnt].period_ms = period_ms;
    s_task_table[s_task_cnt].last_ms   = 0;
    s_task_cnt++;

    return 0;
}

/**
 * @brief  启动调度器，按时间片轮询执行各任务
 * @note   永不返回；空闲时 __wfi() 进入低功耗，由中断唤醒继续轮询
 */
void scheduler_run(void)
{
    uint32_t now;   /* 当前时间戳 */
    uint8_t  i;     /* 任务索引   */

    for (;;) {
        /* 获取当前系统滴答时间 */
        now = HAL_GetTick();

        /* 依次扫描任务表，到期则执行 */
        for (i = 0; i < s_task_cnt; i++) {
            /* period_ms 为 0 表示每次主循环都调用 */
            if (s_task_table[i].period_ms == 0) {
                s_task_table[i].func();
                s_task_table[i].last_ms = now;
            }
            /* 判断是否到达执行周期 */
            else if ((now - s_task_table[i].last_ms) >= s_task_table[i].period_ms) {
                s_task_table[i].last_ms = now;
                s_task_table[i].func();
            }
        }

        /*
         * 一轮任务扫描完毕，进入低功耗等待中断唤醒。
         * __wfi 执行后 CPU 内核暂停，直至任意使能的中断或调试事件唤醒，
         * 随后从中断服务程序返回后继续执行下一条指令。
         */
        __wfi();
    }
}
