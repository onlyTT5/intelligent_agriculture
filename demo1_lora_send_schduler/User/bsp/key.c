#include "gpio.h"
#include "key.h"



void key_init(void)
{
#if 1 //这些初始化代码其实在MX_GPIO_Init已实现
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	
	//端口B硬件时钟使能，就是对端口B供电
	__HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pins : PB12 PB13 */
  GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

#endif
}

uint32_t key_read(uint32_t key_num)
{
		uint32_t key_sta;
	
		if(key_num == KEY1)
		{
				if(GPIO_PIN_RESET==HAL_GPIO_ReadPin(KEY1_PORT,KEY1_PIN))
				{
					 key_sta = KEY_STA_PRESSED;
				
				}
				else
				{
					 key_sta=KEY_STA_RELEASE;
				}
		}

		if(key_num == KEY2)
		{
				if(GPIO_PIN_RESET==HAL_GPIO_ReadPin(KEY2_PORT,KEY2_PIN))
				{
					 key_sta = KEY_STA_PRESSED;
				
				}
				else
				{
					 key_sta=KEY_STA_RELEASE;
				}
		}
		
		return key_sta;

}
