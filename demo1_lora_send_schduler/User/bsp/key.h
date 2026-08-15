#ifndef __KEY_H__
#define __KEY_H__


#define KEY_STA_RELEASE  0
#define KEY_STA_PRESSED	 1

#define KEY1				1
#define KEY2				2

#define KEY1_PIN		GPIO_PIN_12
#define KEY1_PORT		GPIOB


#define KEY2_PIN		GPIO_PIN_13
#define KEY2_PORT		GPIOB


extern void key_init(void);
extern uint32_t key_read(uint32_t key_num);


#endif
