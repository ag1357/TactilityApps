#ifndef TEST_I2C_DEVICE_H
#define TEST_I2C_DEVICE_H
#include <stdbool.h>
struct DeviceType { int value; };
struct Device { unsigned references; };
int device_get_by_name(const char *,struct Device **);
const struct DeviceType *device_get_type(struct Device *);
bool device_is_ready(const struct Device *);
void device_put(struct Device *);
#endif
