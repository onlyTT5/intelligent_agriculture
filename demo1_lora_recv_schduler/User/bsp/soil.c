/**
 * @file    soil.c
 * @brief   土壤湿度传感器驱动源文件
 * @note    使用说明：
 *          1. 硬件接线：
 *             - 土壤湿度传感器信号输出端（S）接 PA1（ADC1_IN1）
 *             - VCC 接 3.3V，GND 接 GND
 *             - 传感器内部为分压电路：土壤越湿，传感器阻值越小，分压输出越低
 *          2. 检测原理：
 *             - 通过 ADC1 通道1（PA1）采集传感器输出电压，ADC 为 12 位，参考电压 3.3V
 *             - 电压换算：电压(mV) = ADC值 * 3300 / 4095
 *             - 按分压公式反算传感器电阻：R = 10kΩ * U / (3.3V - U)
 *             - 将电阻与 g_soil_tbl 表比较，得到湿度等级（1=干燥 2=微湿 3=湿润 4=水分饱和）
 *          3. 常用接口：
 *             - soil_get_voltage()：返回传感器电压（mV），并串口打印
 *             - soil_get_humi()    ：返回湿度等级（1~4）
 *             - soil_test1()/soil_test2()：独立测试函数，用于单独调试土壤湿度
 *          4. 注意事项：
 *             - ADC1 初始化见 Core/Src/adc.c 的 MX_ADC1_Init()，PA0/PA1 均已配置为模拟输入
 *             - 通道切换由 adc_get_channel_voltage() 自动完成（停ADC->选通道->启动->等待->读取），
 *               可与光敏电阻（PA0/ADC_CHANNEL_0）共用 ADC1 交替采样
 */

#include "main.h"  /* 包含主头文件，提供 HAL 库与硬件外设的定义 */
#include "app_main.h"
#include "adc.h"   /* 包含 ADC 外设头文件，声明 hadc1 与 adc_get_channel_voltage() */
#include "soil.h"  /* 包含土壤湿度驱动头文件，声明相关函数与 soil_t 类型 */
#include <stdio.h> /* 包含标准输入输出库，用于 printf 串口打印 */

/* 宏：计算数组元素个数，x 为数组名 */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

/* 土壤湿度等级查询表：按传感器电阻值从大到小排列，越靠后湿度等级越高 */
const soil_t g_soil_tbl[] = {

	{100000, 1, "干燥：急需浇水，避免植物干旱枯萎"},				  // 电阻>=100000Ω → 等级1（干燥）
	{50000, 2, "微湿：可少量浇水，适合耐旱植物生长"},				  // 电阻>=50000Ω → 等级2（微湿）
	{20000, 3, "湿润：无需浇水，适合大多数常见植物（最佳生长湿度）"}, // 电阻>=20000Ω → 等级3（湿润）
	{10000, 4, "水分饱和：停止浇水，及时排水，防止植物烂根"},		  // 电阻>=10000Ω → 等级4（水分饱和）

};

/**
 * @brief  土壤湿度传感器初始化
 * @param  无
 * @return 无
 * @note   当前为空实现，可在此处添加传感器供电控制等初始化代码
 */
void soil_init(void)
{
}

/**
 * @brief  启动 ADC1 转换（土壤湿度通道）
 * @param  无
 * @return 无
 * @note   仅供旧测试函数（soil_test1/soil_test2）使用；
 *         soil_get_voltage() 内部已自行完成启动转换，无需再调用本函数
 */
void soil_adc_start(void)
{
	HAL_ADC_Start(&hadc1); /* 启动 ADC1 的转换 */
}

/**
 * @brief  读取土壤湿度传感器的输出电压
 * @param  无
 * @return 电压值，单位 mV；转换失败返回 0
 * @note   土壤湿度传感器接 PA1（ADC1_IN1），
 *         通过 adc_get_channel_voltage() 完成"配置通道->启动->等待转换->读取"
 */
uint16_t soil_get_voltage(void)
{
#if 0
	uint16_t adc_voltage; /* 保存换算后的电压值（单位 mV） */

	/* 土壤湿度传感器接在 PA1 -> ADC1_IN1，指定通道完成单次转换 */
	adc_voltage = adc_get_channel_voltage(ADC_CHANNEL_1); /* 读取 ADC1 通道1（PA1）的电压 */

	printf("soil_adc_voltage = %dmv\r\n", adc_voltage); /* 通过串口打印电压值 */
	printf("\r\n");										/* 打印换行，分隔每次输出 */

	return adc_voltage; /* 返回电压值（mV）给调用者 */
#else
	  printf("soil_adc_voltage = %dmv\r\n", g_adc_value_tbl[1]*3300/4095);
		return g_adc_value_tbl[1]*3300/4095;
	
#endif
}

/**
 * @brief  获取土壤湿度等级
 * @param  无
 * @return 湿度等级 humi（1=干燥 2=微湿 3=湿润 4=水分饱和）
 * @note   流程：读取电压 -> 换算传感器电阻 -> 查 g_soil_tbl 表得到等级
 */
uint16_t soil_get_humi(void)
{
	uint32_t i;	   /* 循环变量，用于遍历查询表 */
	uint16_t humi; /* 保存查表得到的湿度等级 */

#if 0
	// 方案一（浮点版本，当前被屏蔽未编译）：用浮点计算电阻值
	float adc_voltage;			/* 保存浮点电压值（单位 V） */

	adc_voltage = soil_get_voltage()/1000.0f;	/* 将电压由 mV 换算为 V */

	uint32_t resistance = (uint32_t)(10.0f * 1000 * adc_voltage ) / (3.3 - adc_voltage);	/* 按分压公式反算传感器电阻 */


	printf("adc_voltage=%.3f,resistance=%d\r\n",adc_voltage,resistance);	/* 打印电压与电阻，便于调试 */
#else
	uint16_t adc_voltage; /* 保存电压值（单位 mV） */

	adc_voltage = soil_get_voltage(); /* 读取土壤湿度传感器输出电压（mV） */

	uint32_t resistance = (uint32_t)(10 * 1000 * adc_voltage) / (3300 - adc_voltage); /* 方案二（整型）：由 10kΩ 分压电阻反算传感器电阻(Ω) */

	// 遍历结构体数组，提取光强
	for (i = 0; i < ARRAY_SIZE(g_soil_tbl); i++) /* 从第一项开始依次比较 */
	{
		humi = g_soil_tbl[i].humi; /* 先把当前项的湿度等级暂存 */

		if (resistance >= g_soil_tbl[i].ohm) /* 若实际电阻 >= 表中该项电阻阈值 */
			break;							 /* 命中等级，跳出循环 */
	}

	// printf("adc_voltage=%d,resistance=%d\r\n",adc_voltage,resistance);	/* 调试打印，需要时可取消注释 */

#endif

	return humi; /* 返回湿度等级（1~4） */
}

/**
 * @brief  土壤湿度测试函数1：循环打印传感器电压
 * @param  无
 * @return 无
 * @note   用于单独调试：每 1 秒打印一次土壤湿度输出电压
 */
void soil_test1(void)
{
	// 启动对土壤湿度传感器的ADC转换
	soil_adc_start(); /* 启动 ADC1 转换 */

	while (1) /* 无限循环 */
	{

		// 获取电压值
		soil_get_voltage(); /* 读取并打印土壤湿度电压 */

		HAL_Delay(1000); /* 延时 1 秒 */
	}
}

/**
 * @brief  土壤湿度测试函数2：循环打印湿度等级与说明
 * @param  无
 * @return 无
 * @note   用于单独调试：每 1 秒打印一次湿度等级及对应文字说明
 */
void soil_test2(void)
{
	uint32_t i;	   /* 循环变量，用于遍历查询表 */
	uint16_t humi; /* 保存湿度等级 */
	// 启动对土壤湿度传感器的ADC转换
	soil_adc_start(); /* 启动 ADC1 转换 */

	while (1) /* 无限循环 */
	{

		// 获取湿度值
		humi = soil_get_humi(); /* 读取土壤湿度等级 */

		printf("湿度等级:%d\r\n", humi); /* 打印湿度等级 */

		// 遍历结构体数组，提取说明
		for (i = 0; i < ARRAY_SIZE(g_soil_tbl); i++) /* 遍历查询表 */
		{
			if (humi == g_soil_tbl[i].humi) /* 找到与等级匹配的表项 */
			{
				printf("说明:%s\r\n", g_soil_tbl[i].str); /* 打印对应湿度说明 */
				break;									  /* 匹配成功后退出循环 */
			}
		}

		HAL_Delay(1000); /* 延时 1 秒 */
	}
}
