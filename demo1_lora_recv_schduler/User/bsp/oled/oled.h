//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//中景园电子
//店铺地址：http://shop73023976.taobao.com/?spm=2013.1.0.0.M4PqC2
//
//  文 件 名   : main.c
//  版 本 号   : v2.0
//  作    者   : Evk123
//  生成日期   : 2014-0101
//  最近修改   : 
//  功能描述   : 0.69寸OLED 接口演示例程(STM32F103ZE系列IIC)
//              说明: 
//              ----------------------------------------------------------------
//              GND   电源地
//              VCC   接5V或3.3v电源
//              SCL   接PD6（SCL）
//              SDA   接PD7（SDA）            
//              ----------------------------------------------------------------
//Copyright(C) 中景园电子2014/3/16
//All rights reserved
//////////////////////////////////////////////////////////////////////////////////
#ifndef __OLED_H
#define __OLED_H			
#include <stdint.h>
#include "stdlib.h"	    	
#define OLED_MODE 0
#define SIZE 8
#define XLevelL		0x00
#define XLevelH		0x10
#define Max_Column	128
#define Max_Row		64
#define	Brightness	0xFF 
#define X_WIDTH 	128
#define Y_WIDTH 	64	    						  
//-----------------OLED IIC端口定义----------------  					   

 		     
#define OLED_CMD  0	//写命令
#define OLED_DATA 1	//写数据


//OLED控制用函数
extern void OLED_WR_Byte(unsigned dat,unsigned cmd);  
extern void OLED_Display_On(void);
extern void OLED_Display_Off(void);	   							   		    
extern void OLED_Init(void);
extern void OLED_Clear(uint8_t dot);
extern void OLED_DrawPoint(uint8_t x,uint8_t y,uint8_t t);
extern void OLED_Fill(uint8_t x1,uint8_t y1,uint8_t x2,uint8_t y2,uint8_t dot);
extern void OLED_Set_Pos(unsigned char x, unsigned char y);
extern void OLED_DrawBMP(unsigned char x0, unsigned char y0,unsigned char x1, unsigned char y1,const unsigned char BMP[]);
extern void Write_IIC_Command(unsigned char IIC_Command);
extern void Write_IIC_Data(unsigned char IIC_Data);
extern void Write_IIC_Byte(unsigned char IIC_Byte);
extern void oled_show_string(uint8_t x, uint8_t y, uint8_t* str, uint16_t font_size);


#endif  
	 



