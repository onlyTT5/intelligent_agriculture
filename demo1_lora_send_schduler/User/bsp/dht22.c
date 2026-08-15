/**
 * @file    dht22.c
 * @brief   DHT22 温湿度传感器驱动源文件
 * @note    本文件实现了 DHT22 传感器的底层驱动，包括 GPIO 初始化、起始信号、
 *          数据读取及校验等功能。基于 STM32 HAL 库实现，使用 PC15 引脚与传感器通信。
 */

#include "main.h"  /* 包含主头文件，定义了硬件相关的宏和函数 */
#include "delay.h" /* 包含延时函数的头文件 */
#include "dht22.h" /* 包含 DHT22 传感器的接口声明 */
#include <stdio.h> /* 包含标准输入输出库，用于 printf 调试 */

/**
 * @brief  将 PC15 引脚配置为推挽输出模式
 * @param  无
 * @return 无
 * @note   DHT22 通信起始阶段，主机需要通过该引脚向传感器发送低电平信号。
 */
void dht22_set_output_mode(void)
{
	/* 定义 GPIO 初始化结构体并清零 */
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	/* 指定要配置的 GPIO 引脚为 15 号引脚 (PC15) */
	GPIO_InitStruct.Pin = GPIO_PIN_15;
	/* 设置引脚模式为推挽输出，可输出高/低电平 */
	//GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	
	/* 设置引脚模式为开漏输出，只能可输出低电平或进入高阻态 */
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;	
	
	/* 启用内部上拉电阻，防止引脚悬空 */
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	/* 设置引脚速度为低，降低功耗并减少信号反射 */
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	/* 调用 HAL 库函数完成 GPIO 初始化 */
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/**
 * @brief  将 PC15 引脚配置为输入模式
 * @param  无
 * @return 无
 * @note   发送起始信号后，需切换为输入模式以读取 DHT22 的响应信号和数据。
 */
void dht22_set_input_mode(void)
{
	/* 定义 GPIO 初始化结构体并清零 */
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	/* 指定要配置的 GPIO 引脚为 15 号引脚 (PC15) */
	GPIO_InitStruct.Pin = GPIO_PIN_15;
	/* 设置引脚模式为浮空输入，由外部信号驱动 */
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	/* 启用内部上拉电阻，DHT22 开漏输出时需要外部上拉 */
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	/* 输入模式下 Speed 字段无效，此处注释掉 */
	// GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	/* 调用 HAL 库函数完成 GPIO 初始化 */
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/**
 * @brief  初始化 DHT22 传感器
 * @param  无
 * @return 无
 * @note   使能 GPIOC 时钟，将 PC15 引脚初始化为高电平输出，准备通信。
 */
void dht22_init(void)
{
	/* 使能 GPIOC 外设时钟 */
	__HAL_RCC_GPIOC_CLK_ENABLE();
	/* 将 PC15 引脚输出拉高，处于空闲状态 */
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, GPIO_PIN_SET);
	/* 将 PC15 引脚设置为输出模式 */
	dht22_set_output_mode();
}

/**
 * @brief  主机向 DHT22 发送起始信号并等待响应
 * @param  无
 * @return 0: 成功
 *         -1: 等待响应低电平超时
 *         -2: 响应低电平持续时间不足
 *         -3: 响应高电平持续时间不足
 * @note   起始时序：主机拉低至少 500us -> 拉高 30us -> DHT22 拉低 80us -> 拉高 80us
 */
int32_t dht22_start(void)
{
	uint32_t t; /* 超时计时变量 */

	/* 1. 保证 STM32 的 PC15 引脚为输出模式，准备发送起始信号 */
	dht22_set_output_mode();

	/* 2. 主机将总线拉低至少 500us，通知 DHT22 即将通信 */
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, GPIO_PIN_RESET);
	delay_us(1000); /* 延时 1000us 满足起始信号要求 */

	/* 3. 主机释放总线，由上拉电阻拉高，持续约 30us */
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, GPIO_PIN_SET);
	delay_us(30);

	/* 4. 切换 PC15 为输入模式，准备接收 DHT22 的响应信号 */
	dht22_set_input_mode();

	/* 5. 超时检测：等待 DHT22 拉低总线 (响应信号起始) */
	t = 0;
	while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15) == GPIO_PIN_SET)
	{
		t++;		 /* 计时加 1 */
		delay_us(1); /* 每 1us 检测一次 */

		if (t >= 5000) /* 超时 5000us，DHT22 无响应 */
			return -1;
	}

	/* 6. 超时检测：等待 DHT22 拉低总线结束 (响应低电平合法) */
	t = 0;
	while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15) == GPIO_PIN_RESET)
	{
		t++;
		delay_us(1);

		if (t >= 100) /* 低电平持续超过 100us 则超时 */
			return -2;
	}

	/* 7. 超时检测：等待 DHT22 拉高总线 (响应高电平合法) */
	t = 0;
	while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15) == GPIO_PIN_SET)
	{
		t++;
		delay_us(1);

		if (t >= 100) /* 高电平持续超过 100us 则超时 */
			return -3;
	}

	return 0; /* 起始信号成功 */
}

/**
 * @brief  从 DHT22 读取一个字节
 * @param  byte: 存储读取到字节的指针
 * @return 0: 成功
 *         -1: 等待前导低电平超时
 *         -2: 等待数据位高电平超时
 * @note   DHT22 数据位协议：50us 低电平起始 + 高电平持续时间判断 (26-28us 为 0, 70us 为 1)
 */
int32_t dht22_read_byte(uint8_t *byte)
{
	int32_t i;		/* 循环计数器 */
	uint32_t t = 0; /* 超时计时变量 */
	uint8_t d = 0;	/* 用于存储读取到的字节数据 */

	/* 高位先出，优先接收 bit7 -> bit0 */
	for (i = 7; i >= 0; i--)
	{
		/* 等待前导 50us 低电平持续完毕 */
		t = 0;
		while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15) == GPIO_PIN_RESET)
		{
			t++;
			delay_us(1);

			if (t >= 100) /* 前导低电平超时 */
				return -1;
		}

		/* 延时 40us，判断高电平持续时间区分 0 和 1 */
		delay_us(40);

		/* 判断 40us 后引脚是否仍为高电平 */
		if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15) == GPIO_PIN_SET)
		{
			/* 将 d 变量对应的 bit 位置 1，表示数据位为 1 */
			d |= 1 << i;

			/* 等待剩余的高电平持续完毕 (位 1 的高电平约 70us) */
			t = 0;
			while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15) == GPIO_PIN_SET)
			{
				t++;
				delay_us(1);

				if (t >= 100) /* 高电平持续超时 */
					return -2;
			}
		}
	}

	/* 返回接收到的数据 */
	*byte = d;
	return 0;
}

/**
 * @brief  读取 DHT22 的温湿度数据
 * @param  humi: 存储湿度值的指针 (单位: %)
 * @param  temp: 存储温度值的指针 (单位: ℃)
 * @return 0: 成功
 *         -1: 起始信号失败
 *         -2: 字节读取失败
 *         -3: 校验码错误
 * @note   DHT22 返回 5 字节数据:湿度高字节 | 湿度低字节 | 温度高字节 | 温度低字节 | 校验和
 */
int32_t dht22_read_data(float *humi, float *temp)
{
	int32_t rt;			  /* 存储起始信号的返回值 */
	int32_t i;			  /* 循环计数器 */
	uint8_t buf[5] = {0}; /* 存储读取的 5 个字节数据 */
	uint8_t check_sum;	  /* 计算得到的校验和 */

	/* 发送起始信号，通知 DHT22 开始数据传输 */
	rt = dht22_start();

	/* 检查起始信号是否成功 */
	if (rt != 0)
	{
		printf("dht22 start error code %d\r\n", rt);
		return -1;
	}

	/* 连续接收 5 个字节 (湿度高/低、温度高/低、校验和) */
	for (i = 0; i < 5; i++)
	{
		if (dht22_read_byte(&buf[i]) < 0)
			return -2; /* 读取字节失败 */
	}

	/* 延时 50us，等待 DHT22 释放总线 */
	delay_us(50);

	/* 对接收的前 4 个字节求和，取低 8 位作为校验和 */
	check_sum = (buf[0] + buf[1] + buf[2] + buf[3]) & 0x00FF;

	/* 判断计算的校验和是否与 DHT22 发送的校验字节一致 */
	if (check_sum != buf[4])
		return -3; /* 校验失败，数据不可用 */

	/* 计算湿度值 (湿度高字节<<8 | 湿度低字节) / 10.0f */
	*humi = ((buf[0] << 8) | buf[1]) / 10.0f;

	/* 判断温度最高位，确定温度的正负 */
	if (buf[2] & 0x80) /* 最高位为 1，表示负温度 */
	{
		/* 计算负温度值，取反后除以 10 */
		*temp = (((buf[2] & 0x7F) << 8) | buf[3]) / -10.0f;
	}
	else /* 最高位为 0，表示正温度 */
	{
		/* 计算正温度值 */
		*temp = ((buf[2] << 8) | buf[3]) / 10.0f;
	}

	return 0; /* 数据读取成功 */
}

/**
 * @brief  DHT22 起始信号测试函数
 * @param  无
 * @return 无
 * @note   无限循环测试 DHT22 的起始信号，成功或失败均通过串口打印状态信息，
 *         用于验证 DHT22 硬件连接和通信时序是否正确。
 */
void dht22_test1(void)
{
	int32_t rt; /* 存储起始信号的返回值 */

	/* 无限循环，持续测试 */
	while (1)
	{
		/* 发送起始信号 */
		rt = dht22_start();

		/* 打印测试结果 */
		if (rt == 0)
		{
			printf("dht22 start success\r\n");
		}
		else
		{
			printf("dht22 start fail,error code=%d\r\n", rt);
		}

		/* 延时 6 秒，DHT22 最小采样周期为 2 秒 */
		delay_ms(6000);
	}
}

/**
 * @brief  DHT22 温湿度数据读取测试函数
 * @param  无
 * @return 无
 * @note   无限循环读取 DHT22 温湿度数据，成功时打印湿度和温度值，
 *         失败时打印错误码，用于验证数据读取和解析功能。
 */
void dht22_test2(void)
{
	int32_t rt; /* 存储读取数据的返回值 */
	float humi; /* 存储湿度值 */
	float temp; /* 存储温度值 */

	/* 无限循环，持续读取 */
	while (1)
	{
		/* 读取温湿度数据 */
		rt = dht22_read_data(&humi, &temp);

		/* 打印读取结果 */
		if (rt == 0)
		{
			printf("humi=%.1f,temp=%.1f\r\n", humi, temp);
		}
		else
		{
			printf("dht22 read data fail,error code=%d\r\n", rt);
		}

		/* 延时 6 秒，确保 DHT22 有足够时间完成一次采样 */
		delay_ms(6000);
	}
}
