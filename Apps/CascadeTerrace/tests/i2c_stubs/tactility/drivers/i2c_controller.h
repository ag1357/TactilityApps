#ifndef TEST_I2C_CONTROLLER_H
#define TEST_I2C_CONTROLLER_H
#include <stddef.h>
#include <stdint.h>
#include <tactility/device.h>
typedef unsigned TickType_t;
#define pdMS_TO_TICKS(ms) ((ms)/10)
extern const struct DeviceType I2C_CONTROLLER_TYPE;
int i2c_controller_write(struct Device *,uint8_t,const uint8_t *,uint16_t,TickType_t);
int i2c_controller_read(struct Device *,uint8_t,uint8_t *,size_t,TickType_t);
#endif
