#ifndef __MYIMAGEDOT_H__
#define __MYIMAGEDOT_H__

extern const unsigned char g_image_dot_logo1_128x64[1024];
extern const unsigned char g_image_dot_signal_mono_34x24[102];
extern const unsigned char g_image_dot_signal_24bit_34x24[102];

// 图像信息结构体
typedef struct {
    const char* name;              // 图像名字，方便索引指定图像数据
    const unsigned char* address;  // 图像数组入口地址
    unsigned int width;            // 图像宽度
    unsigned int height;           // 图像高度
    unsigned int size;             // 图像数组大小
} image_info_t;

static const image_info_t g_image_dot_tbl[3] = {
    {"logo1", g_image_dot_logo1_128x64, 128, 64, 1024 },
    {"signal_mono", g_image_dot_signal_mono_34x24, 34, 24, 102 },
    {"signal_24bit", g_image_dot_signal_24bit_34x24, 34, 24, 102 },
};

#endif //__MYIMAGEDOT_H__
