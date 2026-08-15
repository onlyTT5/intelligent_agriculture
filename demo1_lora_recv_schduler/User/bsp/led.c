#include "led.h"
#include "gpio.h"
#include "tim.h"

// led初始化
void led_init(void)
{
  MX_GPIO_Init();

#if (LED1_MODE == LED1_MODE_PWM)
  // 启动定时器1的通道1输出PWM
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
#endif
}

void led_ctrl(uint32_t led_num, uint32_t led_state)
{
  switch (led_num)
  {
  case LED1:
  {
#if (LED1_MODE == LED1_MODE_GPIO)
    HAL_GPIO_WritePin(LED1_PORT, LED1_PIN,
                      led_state ? GPIO_PIN_RESET : GPIO_PIN_SET);
#endif

#if (LED1_MODE == LED1_MODE_PWM)
    __HAL_TIM_SetCompare(&htim1, TIM_CHANNEL_1, led_state ? 0 : 100);
#endif
  }
  break;

  case LED2:
  {
    HAL_GPIO_WritePin(LED2_PORT, LED2_PIN,
                      led_state ? GPIO_PIN_RESET : GPIO_PIN_SET);
  }
  break;

  case LED3:
  {
    HAL_GPIO_WritePin(LED3_PORT, LED3_PIN,
                      (!led_state) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }
  break;

  default:
    break;
  }
}

void led_test(void)
{
  led_ctrl(LED1, LED_ON);
  led_ctrl(LED2, LED_ON);
  led_ctrl(LED3, LED_ON);

  HAL_Delay(1000);

  led_ctrl(LED1, LED_OFF);
  led_ctrl(LED2, LED_OFF);
  led_ctrl(LED3, LED_OFF);
}

void led1_set_duty(uint8_t duty)
{
  __HAL_TIM_SetCompare(&htim1, TIM_CHANNEL_1, duty);
}
