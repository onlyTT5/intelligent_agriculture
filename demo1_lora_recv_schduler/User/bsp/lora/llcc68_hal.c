#include "llcc68_hal.h"
#include <string.h>

// ------------------- 辅助函数：等待BUSY引脚空闲 -------------------
static llcc68_hal_status_t llcc68_hal_wait_busy_idle(const llcc68_hal_context_t *ctx, uint32_t timeout)
{
    uint32_t start_tick = HAL_GetTick();
    // 等待BUSY引脚拉低（模块空闲）
    while (HAL_GPIO_ReadPin(ctx->busy_port, ctx->busy_pin) == GPIO_PIN_SET)
    {
        if ((HAL_GetTick() - start_tick) >= timeout)
        {
            timeout = 0;
            break;
        }
        HAL_Delay(1);    
    }
    if (timeout > 0)
        return LLCC68_HAL_STATUS_OK;
    else
        return LLCC68_HAL_STATUS_ERROR;
}

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS PROTOTYPES ---------------------------------------------
 */

/**
 * Radio data transfer - write
 *
 * @remark Shall be implemented by the user
 *
 * @param [in] context          Radio implementation parameters
 * @param [in] command          Pointer to the buffer to be transmitted
 * @param [in] command_length   Buffer size to be transmitted
 * @param [in] data             Pointer to the buffer to be transmitted
 * @param [in] data_length      Buffer size to be transmitted
 *
 * @returns Operation status
 */
llcc68_hal_status_t llcc68_hal_write(const void *context, const uint8_t *command, const uint16_t command_length,
                                     const uint8_t *data, const uint16_t data_length)
{
    const llcc68_hal_context_t *ctx = (const llcc68_hal_context_t *)context;
    if (ctx == NULL)
        return LLCC68_HAL_STATUS_ERROR;

    // 1. 等待BUSY引脚空闲（避免指令冲突）
    if (llcc68_hal_wait_busy_idle(ctx, 100) != LLCC68_HAL_STATUS_OK)
    {
        return LLCC68_HAL_STATUS_ERROR;
    }
        

    // 2. 拉低NSS选中模块
    HAL_GPIO_WritePin(ctx->nss_port, ctx->nss_pin, GPIO_PIN_RESET);

    // 3. 发送command（寄存器地址/命令）
    if (command != NULL && command_length > 0)
    {
        if (HAL_SPI_Transmit(ctx->hspi, (uint8_t *)command, command_length, 100) != HAL_OK)
        {
            HAL_GPIO_WritePin(ctx->nss_port, ctx->nss_pin, GPIO_PIN_SET);
            return LLCC68_HAL_STATUS_ERROR;
        }
    }

    // 4. 发送data（寄存器数据）
    if (data != NULL && data_length > 0)
    {
        if (HAL_SPI_Transmit(ctx->hspi, (uint8_t *)data, data_length, 100) != HAL_OK)
        {
            HAL_GPIO_WritePin(ctx->nss_port, ctx->nss_pin, GPIO_PIN_SET);
            return LLCC68_HAL_STATUS_ERROR;
        }
    }

    // 5. 拉高NSS取消选中
    HAL_GPIO_WritePin(ctx->nss_port, ctx->nss_pin, GPIO_PIN_SET);

    return LLCC68_HAL_STATUS_OK;
}

/**
 * Radio data transfer - read
 *
 * @remark Shall be implemented by the user
 *
 * @param [in] context          Radio implementation parameters
 * @param [in] command          Pointer to the buffer to be transmitted
 * @param [in] command_length   Buffer size to be transmitted
 * @param [in] data             Pointer to the buffer to be received
 * @param [in] data_length      Buffer size to be received
 *
 * @returns Operation status
 */
llcc68_hal_status_t llcc68_hal_read(const void *context, const uint8_t *command, const uint16_t command_length,
                                    uint8_t *data, const uint16_t data_length)
{
    const llcc68_hal_context_t *ctx = (const llcc68_hal_context_t *)context;
    if (ctx == NULL || data == NULL)
        return LLCC68_HAL_STATUS_ERROR;

    // 1. 等待BUSY引脚空闲（避免指令冲突）
    if (llcc68_hal_wait_busy_idle(ctx, 100) != LLCC68_HAL_STATUS_OK)
        return LLCC68_HAL_STATUS_ERROR;

    // 2. 拉低NSS选中模块
    HAL_GPIO_WritePin(ctx->nss_port, ctx->nss_pin, GPIO_PIN_RESET);

    // 3. 发送读命令（寄存器地址）
    if (command != NULL && command_length > 0)
    {
        if (HAL_SPI_Transmit(ctx->hspi, (uint8_t *)command, command_length, 100) != HAL_OK)
        {
            HAL_GPIO_WritePin(ctx->nss_port, ctx->nss_pin, GPIO_PIN_SET);
            return LLCC68_HAL_STATUS_ERROR;
        }
    }

    // 4. 读取数据（dummy字节用0x00填充）
    memset(data, 0, data_length); // 清空接收缓存
    if (HAL_SPI_Receive(ctx->hspi, data, data_length, 100) != HAL_OK)
    {
        HAL_GPIO_WritePin(ctx->nss_port, ctx->nss_pin, GPIO_PIN_SET);
        return LLCC68_HAL_STATUS_ERROR;
    }

    // 5. 拉高NSS取消选中
    HAL_GPIO_WritePin(ctx->nss_port, ctx->nss_pin, GPIO_PIN_SET);

    return LLCC68_HAL_STATUS_OK;
}

/**
 * Reset the radio
 *
 * @remark Shall be implemented by the user
 *
 * @param [in] context Radio implementation parameters
 *
 * @returns Operation status
 */
llcc68_hal_status_t llcc68_hal_reset(const void *context)
{
    const llcc68_hal_context_t* ctx = (const llcc68_hal_context_t*)context;
    if (ctx == NULL) return LLCC68_HAL_STATUS_ERROR;

    // 1. 拉低RST引脚（复位），保持至少10ms
    HAL_GPIO_WritePin(ctx->rst_port, ctx->rst_pin, GPIO_PIN_RESET);
    HAL_Delay(10);

    // 2. 拉高RST引脚（释放复位），等待模块启动
    HAL_GPIO_WritePin(ctx->rst_port, ctx->rst_pin, GPIO_PIN_SET);
    HAL_Delay(10);

    // 3. 等待BUSY引脚空闲
    if (llcc68_hal_wait_busy_idle(ctx, 100) != LLCC68_HAL_STATUS_OK)
        return LLCC68_HAL_STATUS_ERROR;

    return LLCC68_HAL_STATUS_OK;
}

/**
 * Wake the radio up.
 *
 * @remark Shall be implemented by the user
 *
 * @param [in] context Radio implementation parameters
 *
 * @returns Operation status
 */
llcc68_hal_status_t llcc68_hal_wakeup(const void *context)
{
    const llcc68_hal_context_t* ctx = (const llcc68_hal_context_t*)context;
    if (ctx == NULL) return LLCC68_HAL_STATUS_ERROR;

    // LLCC68唤醒方式：拉低NSS至少10us，触发SPI唤醒
    HAL_GPIO_WritePin(ctx->nss_port, ctx->nss_pin, GPIO_PIN_RESET);
    HAL_Delay(1);  // 实际只需10us，简化为1ms

    // 拉高NSS
    HAL_GPIO_WritePin(ctx->nss_port, ctx->nss_pin, GPIO_PIN_SET);

    // 等待BUSY引脚空闲，确认唤醒成功
    if (llcc68_hal_wait_busy_idle(ctx, 100) != LLCC68_HAL_STATUS_OK)
        return LLCC68_HAL_STATUS_ERROR;

    return LLCC68_HAL_STATUS_OK;
}
