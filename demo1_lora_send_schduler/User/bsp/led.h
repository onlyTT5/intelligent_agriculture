#ifndef __LED_H__
#define __LED_H__

#define LED1			1
#define LED2      2
#define LED3      3

#define LED_OFF   0
#define LED_ON		1

#define LED1_PIN  GPIO_PIN_8
#define LED1_PORT GPIOA

#define LED2_PIN  GPIO_PIN_15
#define LED2_PORT GPIOB

#define LED3_PIN  GPIO_PIN_13
#define LED3_PORT GPIOC

#define LED1_MODE_GPIO  1
#define LED1_MODE_PWM   2

#define LED1_MODE       LED1_MODE_PWM


extern void led_init(void);
extern void led_ctrl(uint32_t led_num,uint32_t led_state);
extern void led1_set_duty(uint8_t duty);
#endif


