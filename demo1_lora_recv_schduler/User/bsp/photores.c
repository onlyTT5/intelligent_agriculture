/**
 * @file    photores.c
 * @brief   光敏电阻（光照强度）传感器驱动源文件
 * @note    使用说明：
 *          1. 硬件接线：
 *             - 光敏电阻模块信号输出端接 PA0（ADC1_IN0）
 *             - VCC 接 3.3V，GND 接 GND
 *             - 模块内部为光敏电阻与 10kΩ 电阻分压电路
 *          2. 检测原理：
 *             - 光照越强，光敏电阻阻值越小，分压输出电压越低
 *             - 通过 ADC1 通道0（PA0）采集电压，ADC 为 12 位，参考电压 3.3V
 *             - 电压换算：电压(mV) = ADC值 * 3300 / 4095
 *             - 按分压公式反算光敏电阻阻值：R = 10kΩ * U / (3.3V - U)
 *             - 将电阻与 g_photores_tbl 表比较，得到光照强度等级（lux，0~350）
 *          3. 常用接口：
 *             - photores_get_voltage()：返回传感器电压（mV），并串口打印
 *             - photores_get_lux()     ：返回光照强度（lux）
 *             - photores_test1()/photores_test2()：独立测试函数，用于单独调试光敏电阻
 *          4. 注意事项：
 *             - ADC1 初始化见 Core/Src/adc.c 的 MX_ADC1_Init()，PA0/PA1 均已配置为模拟输入
 *             - 通道切换由 adc_get_channel_voltage() 自动完成（停ADC->选通道->启动->等待->读取），
 *               可与土壤湿度（PA1/ADC_CHANNEL_1）共用 ADC1 交替采样
 */

#include "main.h"	  /* 包含主头文件，提供 HAL 库与硬件外设的定义 */
#include "adc.h"	  /* 包含 ADC 外设头文件，声明 hadc1 与 adc_get_channel_voltage() */
#include "photores.h" /* 包含光敏电阻驱动头文件，声明相关函数与 photores_t 类型 */
#include "app_main.h"
#include <stdio.h>	  /* 包含标准输入输出库，用于 printf 串口打印 */

/* 宏：计算数组元素个数，x 为数组名 */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

/* 光照等级查询表：按光敏电阻阻值从大到小排列，越靠前环境越暗 */
const photores_t g_photores_tbl[] = {

	{100000, 0, "接近全黑"},	  // 电阻>=100000Ω → 光照 0 lux（接近全黑）
	{70000, 1, "极暗环境"},		  // 电阻>=70000Ω → 光照 1 lux（极暗环境）
	{50000, 1, "深夜无灯"},		  // 电阻>=50000Ω → 光照 1 lux（深夜无灯）
	{40000, 1, "昏暗角落"},		  // 电阻>=40000Ω → 光照 1 lux（昏暗角落）
	{30000, 2, "弱光环境里"},	  // 电阻>=30000Ω → 光照 2 lux（弱光环境）
	{20000, 4, "夜间灯光"},		  // 电阻>=20000Ω → 光照 4 lux（夜间灯光旁）
	{15000, 5, "傍晚室内"},		  // 电阻>=15000Ω → 光照 5 lux（傍晚室内）
	{10000, 10, "昏暗室内"},	  // 电阻>=10000Ω → 光照 10 lux（昏暗室内）
	{7000, 17, "普通室内"},		  // 电阻>=7000Ω → 光照 17 lux（普通室内）
	{5000, 29, "室内台灯"},		  // 电阻>=5000Ω → 光照 29 lux（室内台灯旁）
	{4000, 45, "明亮室内"},		  // 电阻>=4000Ω → 光照 45 lux（明亮室内）
	{3000, 68, "晴天窗边"},		  // 电阻>=3000Ω → 光照 68 lux（晴天窗边）
	{2000, 124, "晴天户外阴影"},  // 电阻>=2000Ω → 光照 124 lux（晴天户外阴影）
	{1000, 350, "烈日/强光直射"}, // 电阻>=1000Ω → 光照 350 lux（烈日/强光直射）

};

/**
 * @brief  光敏电阻模块初始化
 * @param  无
 * @return 无
 * @note   当前为空实现，可在此处添加供电控制等初始化代码
 */
void photores_init(void)
{
}

/**
 * @brief  启动 ADC1 转换（光敏电阻通道）
 * @param  无
 * @return 无
 * @note   仅供旧测试函数（photores_test1/photores_test2）使用；
 *         photores_get_voltage() 内部已自行完成启动转换，无需再调用本函数
 */
void photores_adc_start(void)
{
	HAL_ADC_Start(&hadc1); /* 启动 ADC1 的转换 */
}

/**
 * @brief  读取光敏电阻输出电压
 * @param  无
 * @return 电压值，单位 mV；转换失败返回 0
 * @note   光敏电阻接 PA0（ADC1_IN0），
 *         通过 adc_get_channel_voltage() 完成"配置通道->启动->等待转换->读取"
 */
uint16_t photores_get_voltage(void)
{
#if 0
	uint16_t adc_voltage; /* 保存换算后的电压值（单位 mV） */

	/* 光敏电阻接在 PA0 -> ADC1_IN0，指定通道完成单次转换 */
	adc_voltage = adc_get_channel_voltage(ADC_CHANNEL_0); /* 读取 ADC1 通道0（PA0）的电压 */

	printf("photores_adc_voltage = %dmv\r\n", adc_voltage); /* 通过串口打印电压值 */
	printf("\r\n");											/* 打印换行，分隔每次输出 */
	
	return adc_voltage; /* 返回电压值（mV）给调用者 */
#else
	
	return g_adc_value_tbl[0]*3300/4095;
#endif

	
}

/**
 * @brief  获取光照强度等级
 * @param  无
 * @return 光照强度 lux（0~350）
 * @note   流程：读取电压 -> 反算光敏电阻阻值 -> 查 g_photores_tbl 表得到 lux
 */
uint16_t photores_get_lux(void)
{
	uint32_t i;	  /* 循环变量，用于遍历查询表 */
	uint16_t lux; /* 保存查表得到的光照强度 */

#if 0
	// 方案一（浮点版本，当前被屏蔽未编译）：用浮点计算电阻值
	float adc_voltage;			/* 保存浮点电压值（单位 V） */

	adc_voltage = photores_get_voltage()/1000.0f;	/* 将电压由 mV 换算为 V */

	uint32_t resistance = (uint32_t)(10.0f * 1000 * adc_voltage ) / (3.3 - adc_voltage);	/* 按分压公式反算光敏电阻阻值 */


	printf("adc_voltage=%.3f,resistance=%d\r\n",adc_voltage,resistance);	/* 打印电压与电阻，便于调试 */
#else
	uint16_t adc_voltage; /* 保存电压值（单位 mV） */

	adc_voltage = photores_get_voltage(); /* 读取光敏电阻输出电压（mV） */

	uint32_t resistance = (uint32_t)(10.0f * 1000 * adc_voltage) / (3300 - adc_voltage); /* 方案二（整型）：由 10kΩ 分压电阻反算光敏电阻阻值(Ω) */

	// 遍历结构体数组，提取光强
	for (i = 0; i < ARRAY_SIZE(g_photores_tbl); i++) /* 从第一项开始依次比较 */
	{
		lux = g_photores_tbl[i].lux; /* 先把当前项的光照强度暂存 */

		if (resistance >= g_photores_tbl[i].ohm) /* 若实际电阻 >= 表中该项电阻阈值 */
			break;								 /* 命中等级，跳出循环 */
	}

	// printf("adc_voltage=%d,resistance=%d\r\n",adc_voltage,resistance);	/* 调试打印，需要时可取消注释 */

#endif

	return lux; /* 返回光照强度（lux） */
}

/**
 * @brief  光敏电阻测试函数1：循环打印传感器电压
 * @param  无
 * @return 无
 * @note   用于单独调试：每 1 秒打印一次光敏电阻输出电压
 */
void photores_test1(void)
{
	// 启动对光敏电阻的ADC转换
	photores_adc_start(); /* 启动 ADC1 转换 */

	while (1) /* 无限循环 */
	{
		// 启动对光敏电阻的ADC转换
		photores_adc_start(); /* 再次启动 ADC1 转换（连续模式下刷新转换） */
		// 获取电压值
		photores_get_voltage(); /* 读取并打印光敏电阻电压 */

		HAL_Delay(1000); /* 延时 1 秒 */
	}
}

/**
 * @brief  光敏电阻测试函数2：循环打印光照强度与当前环境
 * @param  无
 * @return 无
 * @note   用于单独调试：每 1 秒打印一次光照强度及对应环境描述
 */
void photores_test2(void)
{
	uint16_t lux; /* 保存光照强度 */
	uint32_t i;	  /* 循环变量，用于遍历查询表 */
	// 启动对光敏电阻的ADC转换
	photores_adc_start(); /* 启动 ADC1 转换 */

	while (1) /* 无限循环 */
	{
		lux = photores_get_lux(); /* 读取光照强度 */

		printf("lux=%hd\r\n", lux); /* 打印光照强度（%hd 对应 uint16_t） */

		// 遍历结构体数组，提取光强
		for (i = 0; i < ARRAY_SIZE(g_photores_tbl); i++) /* 遍历查询表 */
		{
			if (lux == g_photores_tbl[i].lux) /* 找到与 lux 匹配的表项 */
			{
				printf("当前环境:%s\r\n", g_photores_tbl[i].str); /* 打印对应环境描述 */
				break;											  /* 匹配成功后退出循环 */
			}
		}

		HAL_Delay(1000); /* 延时 1 秒 */
	}
}
