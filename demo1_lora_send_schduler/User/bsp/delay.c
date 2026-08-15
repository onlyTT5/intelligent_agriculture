#include "main.h"


void delay_us(uint32_t us)
{
    uint32_t save_LOAD;   // 备份原SysTick->LOAD值
    uint32_t save_VAL;    // 备份原SysTick->VAL值（避免重置计数器导致HAL_GetTick丢 tick）
    uint32_t target_ticks;// 目标us对应的SysTick重载值

    // 1. 备份原始LOAD和VAL值
    save_LOAD = SysTick->LOAD;
    save_VAL  = SysTick->VAL;

    // 2. 计算目标us对应的重载值：SystemCoreClock(Hz) → 72MHz对应1us计数72个脉冲
    target_ticks = (1UL * SystemCoreClock / 1000000) * us;

    // 3. 关闭SysTick中断，CTRL寄存器的bit1清零
    SysTick->CTRL &= ~0x02;

    // 4. 配置SysTick为「目标us计数模式」，启动计数
    SysTick->LOAD = target_ticks - 1; // 重载值=计数值-1
    SysTick->VAL = 0UL;               // 计数器清零，立刻开始计数

    // 5. 硬件等待：直到计数完成（COUNTFLAG置1）
    while ((SysTick->CTRL & (1 << 16)) == 0);

    // 6. 恢复原始LOAD值
    SysTick->LOAD = save_LOAD;
    // 不重置VAL=0，避免HAL_GetTick丢失计数。改为恢复到接近原来的值
    SysTick->VAL = (save_VAL < save_LOAD) ? save_VAL : save_LOAD;

    // 7. 恢复SysTick中断（否则HAL_Delay无法使用）
    SysTick->CTRL |= 0x02;
}

void delay_ms(uint32_t ms)
{
		HAL_Delay(ms);
}
