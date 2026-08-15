#ifndef __SOLI_H__
#define __SOLI_H__

extern void soil_init(void);
extern uint16_t soil_get_voltage(void);
extern uint16_t soil_get_humi(void);

extern void soil_test1(void);
extern void soil_test2(void);

typedef struct
{
	uint32_t ohm;	//土壤湿度传感器的电阻值
	uint16_t humi;//湿度
	const char *str;//描述说明
}soil_t;

/* 土壤湿度等级查询表（在 soil.c 中定义） */
extern const soil_t g_soil_tbl[4];

#endif

