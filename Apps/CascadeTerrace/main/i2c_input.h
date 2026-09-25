#ifndef ANAPHORUM_TACTILITY_I2C_INPUT_H
#define ANAPHORUM_TACTILITY_I2C_INPUT_H

#include "../core/i2c_controls.h"
struct Device;
typedef struct {
    CtI2cControl control;
    struct Device *bus;
    uint64_t retry_us;
} CtI2cInputDevice;
typedef struct {
    CtI2cInputDevice devices[CT_I2C_MAX_DEVICES];
    unsigned count, next;
    CtI2cEmit emit;
    CtI2cDisconnect disconnect;
    void *context;
    uint32_t polls, failures;
} CtI2cInput;

/* No bus enumeration, pin reassignment, firmware setting, or mandatory peripheral.
 * Returns configured count, 0 when disabled, -1 for invalid config. Caller owns this
 * object and calls poll/close serially from its input worker. */
int ct_i2c_input_open(CtI2cInput *, const char *config_path, CtI2cEmit,
                      CtI2cDisconnect, void *context);
/* Round-robin one configured device per invocation, without holding game/UI locks. */
void ct_i2c_input_poll(CtI2cInput *, uint64_t now_us);
void ct_i2c_input_close(CtI2cInput *, uint64_t now_us);

#endif
