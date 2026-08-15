#ifndef __DHT22_H__
#define __DHT22_H__

extern void dht22_init(void);
extern void dht22_test1(void);
extern void dht22_test2(void);
extern int32_t dht22_read_data(float *humi, float *temp);
#endif
