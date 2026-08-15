/**
 * @file llcc68_p2p.c
 * @brief LLCC68 LoRa点对点通信驱动实现文件
 *
 * @details
 *   本文件实现了LLCC68 LoRa模块的点对点通信功能，包括：
 *   1. 模块初始化（复位、校准、射频参数配置、中断映射）
 *   2. LoRa数据发送（写入TX FIFO → 启动发送 → 等待DIO1中断）
 *   3. LoRa数据接收（配置接收模式 → 等待DIO1中断 → 读取RX FIFO）
 *   4. DIO1中断回调处理（TX_DONE / RX_DONE / TIMEOUT / CRC_ERROR）
 *   5. 点对点通信演示函数（llcc68_p2p_demo）
 *
 *   硬件连接：
 *   - SPI1: SCK=PA5, MISO=PA6, MOSI=PA7
 *   - NSS=PA4, RST=PB1, BUSY=PB0, DIO1=PB10
 *   - DIO1配置为上升沿外部中断（EXTI15_10）
 *
 *   工作流程：
 *   1. llcc68_init() 初始化模块
 *   2. llcc68_lora_receive_mode() 进入连续接收模式
 *   3. llcc68_lora_receive_data() 阻塞等待接收
 *   4. 收到数据时DIO1上升沿 → EXTI中断 → DIO1_EXTI_Callback()
 *   5. 回调函数设置rx_done标志 → receive_data读取FIFO数据
 *
 *   依赖文件：
 *   - llcc68.h / llcc68.c        —— LLCC68底层驱动
 *   - llcc68_hal.h / llcc68_hal.c —— HAL层抽象
 *   - llcc68_regs.h               —— 寄存器定义
 *   - llcc68_status.h             —— 状态码定义
 *
 * @version 0.2
 * @date 2026-02-01
 *
 * @author 温老师
 * @copyright Copyright (c) 2026
 *
 */

/* 包含标准库头文件 */
#include <stdio.h>
#include <string.h>

/* 包含STM32 HAL库头文件 */
#include "stm32f1xx_hal.h"
#include "main.h"
#include "spi.h"

/* 包含LLCC68底层驱动头文件 */
#include "llcc68_p2p.h"
#include "llcc68.h"
#include "llcc68_regs.h"
#include "llcc68_status.h"

/* 包含OLED显示头文件 */
#include "oled.h"

/* ==================== 全局变量定义 ==================== */

/**
 * @brief LoRa模块硬件上下文
 *
 * 包含SPI句柄和所有GPIO引脚的端口/引脚号，
 * 在所有LLCC68 API调用时作为context参数传递
 */
llcc68_hal_context_t llcc68_ctx = {
    .hspi = LLCC68_SPI_HANDLE,       /* SPI1句柄 */
    .nss_port = LLCC68_NSS_PORT,     /* NSS端口：GPIOA */
    .nss_pin = LLCC68_NSS_PIN,       /* NSS引脚：PA4 */
    .rst_port = LLCC68_RST_PORT,     /* RST端口：GPIOB */
    .rst_pin = LLCC68_RST_PIN,       /* RST引脚：PB1 */
    .busy_port = LLCC68_BUSY_PORT,   /* BUSY端口：GPIOB */
    .busy_pin = LLCC68_BUSY_PIN,     /* BUSY引脚：PB0 */
    .dio1_port = LLCC68_DIO1_PORT,   /* DIO1端口：GPIOB */
    .dio1_pin = LLCC68_DIO1_PIN,     /* DIO1引脚：PB10 */
};

/**
 * @brief 发送完成标志（volatile，在中断和主循环中共享）
 *
 * DIO1中断回调中检测到TX_DONE时置为true，
 * llcc68_lora_send函数中轮询此标志判断发送是否完成
 */
volatile static bool tx_done = false;

/**
 * @brief 接收完成标志（volatile，在中断和主循环中共享）
 *
 * DIO1中断回调中检测到RX_DONE时置为true，
 * llcc68_lora_receive_data函数中轮询此标志判断接收是否完成
 */
volatile static bool rx_done = false;

/**
 * @brief 接收超时标志（volatile，在中断和主循环中共享）
 *
 * DIO1中断回调中检测到TIMEOUT中断时置为true，
 * llcc68_lora_receive_data函数中轮询此标志判断是否超时
 */
volatile static bool rx_timeout = false;

/**
 * @brief 接收数据缓冲区
 *
 * 以int16_t为单位存储接收到的LoRa数据，
 * 最多可存放 LORA_PAYLOAD_LEN/2 = 127 个int16_t数据（254字节）
 */
volatile int16_t rx_data[LORA_PAYLOAD_LEN / 2] = {0};


/* ==================== 中断回调函数 ==================== */

/**
 * @brief DIO1外部中断回调函数
 *
 * @details
 *   当LLCC68模块产生DIO1中断信号（上升沿）时，由EXTI中断服务函数
 *   最终调用此函数。函数内部读取并清除中断状态寄存器，根据中断类型
 *   设置对应的标志位：
 *   - LLCC68_IRQ_TX_DONE   → tx_done = true
 *   - LLCC68_IRQ_RX_DONE   → rx_done = true
 *   - LLCC68_IRQ_TIMEOUT   → rx_timeout = true
 *   - LLCC68_IRQ_CRC_ERROR → 打印CRC错误信息
 *
 *   调用链：EXTI15_10_IRQHandler → HAL_GPIO_EXTI_IRQHandler
 *         → HAL_GPIO_EXTI_Callback → DIO1_EXTI_Callback
 */
void DIO1_EXTI_Callback(void)
{
    llcc68_irq_mask_t irq_status = 0;  /* 中断状态寄存器值 */

    /* 读取并清除中断状态寄存器，获取本次中断的类型 */
    llcc68_get_and_clear_irq_status(&llcc68_ctx, &irq_status);
    printf("IRQ tatus: %#x\r\n", irq_status);

    /* 检查是否为发送完成中断 */
    if (irq_status & LLCC68_IRQ_TX_DONE)
    {
        tx_done = 1;  /* 设置发送完成标志 */
        printf("IRQ Tx done\r\n");
    }

    /* 检查是否为接收完成中断 */
    if (irq_status & LLCC68_IRQ_RX_DONE)
    {
        rx_done = true;  /* 设置接收完成标志 */
        printf("IRQ Rx done\r\n");
    }

    /* 检查是否为接收超时中断 */
    if (irq_status & LLCC68_IRQ_TIMEOUT)
    {
        printf("IRQ Rx timeout\r\n");
        rx_timeout = true;  /* 设置超时标志 */
    }

    /* 检查是否为CRC校验错误中断 */
    if (irq_status & LLCC68_IRQ_CRC_ERROR)
    {
        printf("IRQ CRC error\r\n");
    }
}


/* ==================== 初始化函数 ==================== */

/**
 * @brief 初始化LLCC68 LoRa模块
 *
 * @details
 *   执行完整的硬件初始化流程，任何一步失败都会打印错误信息并返回。
 *   初始化成功后模块处于STDBY_XOSC待机模式，随时可以发送或接收。
 *
 * @param context LLCC68硬件上下文指针，包含SPI句柄和GPIO引脚信息
 * @return llcc68_status_t 初始化状态
 *   @retval LLCC68_STATUS_OK    初始化成功
 *   @retval LLCC68_STATUS_ERROR 初始化失败
 */
llcc68_status_t llcc68_init(const void *context)
{
    llcc68_status_t status;  /* API返回状态 */

    /* [1] 硬件复位：拉低RST引脚再拉高，使芯片恢复初始状态 */
    status = (llcc68_status_t)llcc68_hal_reset(context);
    if (status != LLCC68_STATUS_OK)
    {
        printf("LLCC68 reset failed!\r\n");
        return status;
    }

    /* [2] 进入STDBY_RC待机模式：使用内部RC振荡器，是最基础的待机状态 */
    status = llcc68_set_standby(context, LLCC68_STANDBY_CFG_RC);
    if (status != LLCC68_STATUS_OK)
    {
        printf("LLCC68 set STDBY_RC failed!\r\n");
        return status;
    }
    HAL_Delay(50);  /* 等待50ms让芯片稳定 */

    /* [3] 切换到STDBY_XOSC待机模式：启用外部晶振，提供更精确的时钟 */
    status = llcc68_set_standby(context, LLCC68_STANDBY_CFG_XOSC);
    if (status != LLCC68_STATUS_OK)
    {
        printf("LLCC68 set standby failed!\r\n");
        return status;
    }
    HAL_Delay(1000);  /* 等待1000ms让外部晶振稳定启动 */

    /* [4] 清除复位后可能残留的设备错误状态 */
    status = llcc68_clear_device_errors(context);
    if (status != LLCC68_STATUS_OK)
    {
        printf("LLCC68 clear errors failed!\r\n");
        return status;
    }

    /* [5] 校准：PLL锁相环、ADC脉冲校准、ADC批量校准、镜像校准 */
    llcc68_cal_mask_t cal_mask =
        LLCC68_CAL_PLL |              /* PLL锁相环校准 */
        LLCC68_CAL_ADC_PULSE |        /* ADC脉冲校准 */
        LLCC68_CAL_ADC_BULK_N |       /* ADC批量负向校准 */
        LLCC68_CAL_ADC_BULK_P |       /* ADC批量正向校准 */
        LLCC68_CAL_IMAGE;             /* 镜像频率校准 */
    status = llcc68_cal(context, cal_mask);
    if (status != LLCC68_STATUS_OK)
    {
        printf("LLCC68 cal failed!\r\n");
        return status;
    }

    /* [6] 中国频段（470-510MHz）Image镜像校准，抑制镜像频率干扰 */
    status = llcc68_cal_img_in_mhz(context, 470, 510);
    if (status != LLCC68_STATUS_OK)
    {
        printf("LLCC68 image cal failed!\r\n");
        return status;
    }

    /* [7] 配置PA（功率放大器）参数，优化发射功率与可靠性 */
    llcc68_pa_cfg_params_t pa_cfg = {
        .pa_duty_cycle = 0x03,  /* PA占空比：+20dBm最优值 */
        .hp_max = 0x05,         /* 最大增益等级：+20dBm对应值 */
        .device_sel = 0x00,     /* 使用内置PA（外部PA需设为0x01） */
        .pa_lut = 0x01          /* 启用PA校准表 */
    };
    status = llcc68_set_pa_cfg(context, &pa_cfg);
    if (status != LLCC68_STATUS_OK)
    {
        printf("LLCC68 set PA cfg failed!\r\n");
        return status;
    }

    /* [8] 配置OCP（过流保护），防止PA因天线失配等原因过流损坏 */
    uint8_t ocp_value = LLCC68_OCP_PARAM_VALUE_140_MA;  /* 限流140mA */
    status = llcc68_set_ocp_value(context, ocp_value);
    if (status != LLCC68_STATUS_OK)
    {
        printf("LLCC68 set OCP failed!\r\n");
        return status;
    }

    /* [9] 设置电源模式为LDO（低压差线性稳压器模式） */
    status = llcc68_set_reg_mode(context, LLCC68_REG_MODE_LDO);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [10] 设置包类型为LoRa（另一种是GFSK，这里使用LoRa） */
    status = llcc68_set_pkt_type(context, LLCC68_PKT_TYPE_LORA);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [11] 设置射频频率为470.5MHz（必须先设置包类型才能设置频率） */
    status = llcc68_set_rf_freq(context, LORA_FREQ);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [12] 配置LoRa调制参数：扩频因子、带宽、编码率 */
    llcc68_mod_params_lora_t lora_mod_params = {
        .sf = LORA_SF,    /* 扩频因子SF9 */
        .bw = LORA_BW,    /* 带宽125kHz */
        .cr = LORA_CR,    /* 编码率4/5 */
        .ldro = 0         /* 关闭低数据率优化（SF9+125kHz无需启用） */
    };
    status = llcc68_set_lora_mod_params(context, &lora_mod_params);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [13] 设置发射功率为+22dBm，上升沿时间200us */
    status = llcc68_set_tx_params(context, LORA_TX_POWER_DBM, LLCC68_RAMP_200_US);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [14] 优化调制质量：修复500kHz带宽下的调制偏差问题 */
    status = llcc68_tx_modulation_workaround(context, LLCC68_PKT_TYPE_LORA, LORA_BW);
    if (status != LLCC68_STATUS_OK)
    {
        printf("LLCC68 tx modulation workaround failed!\r\n");
        return status;
    }

    /* [15] 增强PA抗天线失配能力，防止天线不匹配时PA损坏 */
    status = llcc68_cfg_tx_clamp(context);
    if (status != LLCC68_STATUS_OK)
    {
        printf("LLCC68 cfg tx clamp failed!\r\n");
        return status;
    }

    /* [16] 配置DIO中断映射：将TX_DONE、RX_DONE、TIMEOUT映射到DIO1引脚 */
    status =
        llcc68_set_dio_irq_params(context,
                                  LLCC68_IRQ_TX_DONE | LLCC68_IRQ_RX_DONE | LLCC68_IRQ_TIMEOUT,  /* 使能系统中断 */
                                  LLCC68_IRQ_TX_DONE | LLCC68_IRQ_RX_DONE | LLCC68_IRQ_TIMEOUT,  /* DIO1映射 */
                                  LLCC68_IRQ_NONE,                                               /* DIO2不映射 */
                                  LLCC68_IRQ_NONE);                                              /* DIO3不映射 */
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [17] 设置缓冲区基地址：TX从0开始，RX从0开始 */
    status = llcc68_set_buffer_base_address(context, 0, 0);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [18] 清除所有中断标志，确保初始化后无残留中断 */
    llcc68_clear_irq_status(context, LLCC68_IRQ_ALL);

    printf("LLCC68 init success! Chip mode: STDBY_XOSC, RF freq: %lu Hz\r\n", LORA_FREQ);
    return LLCC68_STATUS_OK;
}


/* ==================== 发送函数 ==================== */

/**
 * @brief LoRa数据发送函数
 *
 * @details
 *   将数据写入LLCC68的TX FIFO缓冲区，并启动发送。
 *   发送完成后通过DIO1中断设置tx_done标志位。
 *   发送前会重新配置包类型、数据包参数和调制参数，确保与接收端匹配。
 *
 * @param context       LLCC68硬件上下文指针
 * @param data          待发送数据缓冲区指针
 * @param len           待发送数据长度（字节），最大255
 * @param timeout_in_ms 发送超时时间（毫秒），超时后返回错误
 * @return llcc68_status_t 发送状态
 *   @retval LLCC68_STATUS_OK    发送成功
 *   @retval LLCC68_STATUS_ERROR 发送失败（超时或数据过长）
 */
llcc68_status_t llcc68_lora_send(const void *context, const uint8_t *data, uint8_t len, uint32_t timeout_in_ms)
{
    llcc68_status_t status;  /* API返回状态 */

    /* 配置LoRa数据包参数 */
    llcc68_pkt_params_lora_t lora_pkt_params = {
        .preamble_len_in_symb = LORA_PREAMBLE_LEN,  /* 前导码长度：8个符号 */
        .header_type = LLCC68_LORA_PKT_EXPLICIT,    /* 显式头模式（含数据长度信息） */
        .pld_len_in_bytes = len,                    /* 有效载荷长度（动态设置） */
        .crc_is_on = true,                          /* 启用CRC校验 */
        .invert_iq_is_on = false                    /* 不反转IQ信号 */
    };

    tx_done = false;  /* 清除发送完成标志 */

    /* [1] 进入频率合成模式（FS），准备切换到发送 */
    status = llcc68_set_fs(context);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [2] 设置包类型为LoRa（每次发送前重新设置，确保正确） */
    status = llcc68_set_pkt_type(context, LLCC68_PKT_TYPE_LORA);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [3] 设置LoRa数据包参数 */
    status = llcc68_set_lora_pkt_params(context, &lora_pkt_params);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [4] 配置LoRa调制参数（与接收端保持一致） */
    llcc68_mod_params_lora_t lora_mod_params = {
        .sf = LORA_SF,    /* 扩频因子SF9 */
        .bw = LORA_BW,    /* 带宽125kHz */
        .cr = LORA_CR,    /* 编码率4/5 */
        .ldro = 0         /* 关闭低数据率优化 */
    };
    status = llcc68_set_lora_mod_params(context, &lora_mod_params);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [5] 检查数据长度是否超过FIFO最大容量 */
    if (len > LORA_PAYLOAD_LEN)
    {
        printf("Data length is too long!\r\n");
        return LLCC68_STATUS_ERROR;
    }

    /* [6] 将待发送数据写入TX FIFO缓冲区（偏移地址0） */
    status = llcc68_write_buffer(context, 0, data, len);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [7] 启动发送，设置超时时间 */
    status = llcc68_set_tx(context, timeout_in_ms);
    if (status != LLCC68_STATUS_OK)
        return status;

    printf("sending...\r\n");

    /* [8] 等待发送完成（DIO1中断会设置tx_done标志）或超时 */
    uint32_t start = HAL_GetTick();  /* 记录开始时间 */
    while (!tx_done)
    {
        /* 检查是否超时 */
        if (HAL_GetTick() - start > timeout_in_ms)
        {
            tx_done = false;
            printf("Send timeout...!\r\n");
            return LLCC68_STATUS_ERROR;
        }
        HAL_Delay(1);  /* 1ms延时，避免CPU空转 */
    }

    printf("Sent completed!\r\n");
    return LLCC68_STATUS_OK;
}


/* ==================== 接收函数 ==================== */

/**
 * @brief 设置LoRa接收模式
 *
 * @details
 *   配置数据包参数和调制参数，将LLCC68切换到接收模式。
 *   支持两种接收模式：
 *   - 连续接收模式（timeout_in_ms=0）：模块持续监听，直到收到数据
 *   - 超时接收模式（timeout_in_ms>0）：在指定时间内监听，超时自动退出
 *
 * @param context        LLCC68硬件上下文指针
 * @param timeout_in_ms  接收超时时间（毫秒），0表示连续接收模式
 * @return llcc68_status_t 设置状态
 *   @retval LLCC68_STATUS_OK    设置接收模式成功
 *   @retval LLCC68_STATUS_ERROR 设置失败
 */
llcc68_status_t llcc68_lora_receive_mode(const void *context, uint32_t timeout_in_ms)
{
    llcc68_status_t status;  /* API返回状态 */

    /* 配置LoRa数据包参数（与发送端保持一致） */
    llcc68_pkt_params_lora_t lora_pkt_params = {
        .preamble_len_in_symb = LORA_PREAMBLE_LEN,  /* 前导码长度：8个符号 */
        .header_type = LLCC68_LORA_PKT_EXPLICIT,    /* 显式头模式 */
        .pld_len_in_bytes = LORA_PAYLOAD_LEN,       /* 最大接收长度255字节 */
        .crc_is_on = true,                          /* 启用CRC校验 */
        .invert_iq_is_on = false                    /* 不反转IQ信号 */
    };

    /* [1] 设置接收模式为Boosted（增强灵敏度模式） */
    status = llcc68_cfg_rx_boosted(context, true);
    if (status != LLCC68_STATUS_OK)
    {
        printf("Failed to set RX boosted mode.\r\n");
        /* 非致命错误，继续执行 */
    }

    /* [2] 设置包类型为LoRa */
    status = llcc68_set_pkt_type(context, LLCC68_PKT_TYPE_LORA);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [3] 设置LoRa数据包参数 */
    status = llcc68_set_lora_pkt_params(context, &lora_pkt_params);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [4] 进入频率合成模式（FS），准备切换到接收 */
    status = llcc68_set_fs(context);
    if (status != LLCC68_STATUS_OK)
        return status;

    /* [5] 设置为接收模式 */
    if (timeout_in_ms == 0)
    {
        /* 超时时间为0：连续接收模式，模块持续监听直到收到数据 */
        status = llcc68_set_rx_with_timeout_in_rtc_step(context, LLCC68_RX_CONTINUOUS);
    }
    else
    {
        /* 超时时间非0：定时接收模式，超时后自动退出接收 */
        status = llcc68_set_rx(context, timeout_in_ms);
    }

    if (status != LLCC68_STATUS_OK)
    {
        printf("Set RX mode failed!\r\n");
    }

    return status;
}

/**
 * @brief 等待并读取LoRa接收数据
 *
 * @details
 *   阻塞等待rx_done或rx_timeout标志位被设置（由DIO1中断触发）。
 *   接收成功后，从RX FIFO读取数据，并获取信号强度（RSSI）和信噪比（SNR）。
 *   读取完成后自动清理标志位，为下一次接收做准备。
 *
 * @param context       LLCC68硬件上下文指针
 * @param data          接收数据缓冲区指针（由调用者分配）
 * @param len           输出参数，返回实际接收到的数据长度（字节）
 * @param pkt_status    输出参数，返回数据包状态（包含RSSI和SNR）
 * @param timeout_in_ms 等待超时时间（毫秒），超时后返回错误
 * @return llcc68_status_t 接收状态
 *   @retval LLCC68_STATUS_OK    接收成功
 *   @retval LLCC68_STATUS_ERROR 接收失败（超时或CRC错误）
 */
llcc68_status_t llcc68_lora_receive_data(const void *context, uint8_t *data, uint16_t *len,
                                         llcc68_pkt_status_lora_t *pkt_status, uint32_t timeout_in_ms)
{
    llcc68_status_t status = LLCC68_STATUS_ERROR;  /* 默认返回错误状态 */

    /* [1] 等待接收完成或超时（轮询中断回调函数设置的标志位） */
    uint32_t start = HAL_GetTick();  /* 记录等待开始时间 */
    while (!rx_done && !rx_timeout)
    {
        /* 检查是否超时（额外加10ms容差） */
        if (HAL_GetTick() - start > timeout_in_ms + 10)
        {
            tx_done = false;  /* 清除发送标志（此处应为笔误，不影响功能） */
            //printf("Receive timeout...!\r\n");
            return status;
        }
        HAL_Delay(1);  /* 1ms延时，避免CPU空转 */
    }

    /* [2] 接收完成，读取数据 */
    if (rx_done)
    {
        /* 获取RX缓冲区状态（数据长度和起始地址） */
        llcc68_rx_buffer_status_t rx_buffer_status;
        status = llcc68_get_rx_buffer_status(context, &rx_buffer_status);
        if (status != LLCC68_STATUS_OK)
        {
            printf("Get RX buffer status failed!\r\n");
            goto rx;  /* 跳转到清理标签 */
        }

        /* 获取数据包状态（RSSI信号强度和SNR信噪比） */
        status = llcc68_get_lora_pkt_status(context, pkt_status);
        if (status != LLCC68_STATUS_OK)
        {
            printf("Get packet status failed!\r\n");
            goto rx;
        }

        /* 从RX FIFO读取接收到的数据 */
        *len = rx_buffer_status.pld_len_in_bytes;  /* 获取数据长度 */
        status = llcc68_read_buffer(context, rx_buffer_status.buffer_start_pointer, data, *len);
        if (status != LLCC68_STATUS_OK)
        {
            printf("Read buffer failed!\r\n");
            goto rx;
        }

        printf("Reception completed\r\n");
    }
    else
    {
        /* 接收超时（rx_timeout标志被设置） */
        printf("Receive timeout...\r\n");
        goto rx;
    }

    /* [3] 处理接收完成后的清理工作（清除中断、恢复待机等） */
    llcc68_handle_rx_done(context);

rx:
    /* [4] 清理标志位，为下一次接收做准备 */
    rx_done = false;      /* 清除接收完成标志 */
    rx_timeout = false;   /* 清除超时标志 */
    return status;
}


/* ==================== 演示函数 ==================== */

/**
 * @brief LoRa点对点通信演示函数（接收端）
 *
 * @details
 *   演示LLCC68模块的LoRa接收功能，接收发送端传来的传感器数据：
 *   1. 初始化LLCC68模块（失败则每秒重试）
 *   2. 进入连续接收模式
 *   3. 在while(1)循环中等待接收数据
 *   4. 收到数据后通过串口打印传感器数据，同时在OLED上显示
 *
 *   接收数据帧格式（4个int16_t，共8字节）：
 *   - rx_data[0] = 温度 ×10（除以10得到实际温度值）
 *   - rx_data[1] = 湿度 ×10（除以10得到实际湿度值）
 *   - rx_data[2] = CO2 浓度（PPM）
 *   - rx_data[3] = 光照强度（lux）
 *
 *   使用方法：在main函数中直接调用llcc68_p2p_demo()即可
 */
void llcc68_p2p_demo(void)
{
    llcc68_status_t status;          /* API返回状态 */
    uint16_t rx_data_len = 0;        /* 接收数据长度 */
    llcc68_pkt_status_lora_t pkt_status;  /* 数据包状态（RSSI/SNR） */
    char display_buf[32] = {0};      /* OLED显示字符串缓冲区 */

    /* [1] 初始化LLCC68模块，失败则每秒重试直到成功 */
    while (1)
    {
        status = llcc68_init(&llcc68_ctx);

        if (status != LLCC68_STATUS_OK)
        {
            printf("LLCC68初始化失败！\r\n");
        }
        else
        {
            printf("LLCC68初始化成功！\r\n");
            break;  /* 初始化成功，退出重试循环 */
        }
        HAL_Delay(1000);  /* 等待1秒后重试 */
    }

    /* [2] 进入连续接收模式（timeout=0表示持续监听） */
    llcc68_lora_receive_mode(&llcc68_ctx, 0);

    /* [3] 主循环：等待接收数据并打印/显示结果 */
    while (1)
    {
        /* 等待接收数据，超时时间1000ms */
        if (llcc68_lora_receive_data(&llcc68_ctx, (uint8_t *)rx_data, &rx_data_len, &pkt_status, 1000) == LLCC68_STATUS_OK)
        {
            /* 检查数据长度是否为8字节（4个int16_t） */
            if (rx_data_len >= 8)
            {
                /* 解析传感器数据 */
                int16_t temp = rx_data[0];   /* 温度 ×10 */
                int16_t humi = rx_data[1];   /* 湿度 ×10 */
                int16_t co2  = rx_data[2];   /* CO2浓度 PPM */
                int16_t lux  = rx_data[3];   /* 光照强度 lux */

                /* 串口打印接收到的传感器数据 */
                printf("温度: %.1f℃  湿度: %.1f%%  CO2: %dppm  光照: %dlux\r\n",
                       temp / 10.0f, humi / 10.0f, co2, lux);
                printf("RSSI: %d dBm  SNR: %d dB\r\n",
                       pkt_status.rssi_pkt_in_dbm, pkt_status.snr_pkt_in_db);

                /* OLED显示传感器数据 */
                /* 第0行：温度和湿度 */
                sprintf(display_buf, "T:%d.%d H:%d.%d",
                        temp / 10, temp % 10, humi / 10, humi % 10);
                oled_show_string(0, 0, (uint8_t *)display_buf, 16);

                /* 第1行：CO2浓度 */
                sprintf(display_buf, "CO2:%dppm", co2);
                oled_show_string(0, 2, (uint8_t *)display_buf, 16);

                /* 第2行：光照强度 */
                sprintf(display_buf, "Lux:%d", lux);
                oled_show_string(0, 4, (uint8_t *)display_buf, 16);

                /* 第3行：信号强度 */
                sprintf(display_buf, "RSSI:%ddBm", pkt_status.rssi_pkt_in_dbm);
                oled_show_string(0, 6, (uint8_t *)display_buf, 16);
            }
            else
            {
                printf("数据长度错误: %d 字节（应为8字节）\r\n", rx_data_len);
            }
        }
    }
}
