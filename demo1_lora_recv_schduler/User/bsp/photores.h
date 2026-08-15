#ifndef __PHOTORES_H__
#define __PHOTORES_H__

extern void photores_init(void);
extern uint16_t photores_get_voltage(void);
extern uint16_t photores_get_lux(void);

extern void photores_test1(void);
extern void photores_test2(void);

typedef struct
{
	uint32_t ohm;	 // 光明电阻的电阻值
	uint16_t lux;	 // 光强
	const char *str; // 描述
} photores_t;

/* 光照等级查询表（在 photores.c 中定义） */
extern const photores_t g_photores_tbl[14];

#endif
