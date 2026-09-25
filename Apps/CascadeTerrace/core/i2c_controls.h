#ifndef ANAPHORUM_I2C_CONTROLS_H
#define ANAPHORUM_I2C_CONTROLS_H

#include "action_input.h"

enum { CT_I2C_MAX_DEVICES=4, CT_I2C_BUS_NAME=48 };
typedef enum { CT_I2C_SEESAW=1, CT_I2C_CARDKB2=2 } CtI2cKind;
typedef struct { unsigned kind; char bus[CT_I2C_BUS_NAME]; uint8_t address; } CtI2cConfig;
/* Transaction callbacks return nonzero on success. No platform or gameplay state. */
typedef struct {
    void *context;
    int (*write)(void *, uint8_t address, const uint8_t *, size_t);
    int (*read)(void *, uint8_t address, uint8_t *, size_t);
    void (*wait_us)(void *, unsigned);
} CtI2cTransport;
typedef void (*CtI2cEmit)(void *, uint32_t instance, AiControl, int value, uint64_t now_us);
typedef void (*CtI2cDisconnect)(void *, uint32_t instance, uint64_t now_us);
typedef struct {
    CtI2cConfig config;
    CtI2cTransport transport;
    uint32_t identity, instance;
    uint8_t ready, connected;
} CtI2cControl;

/* Missing file returns 0 (disabled); malformed file returns -1, without partial config. */
int ct_i2c_config_load(const char *path, CtI2cConfig out[CT_I2C_MAX_DEVICES]);
uint32_t ct_i2c_identity(const CtI2cConfig *config);
void ct_i2c_control_init(CtI2cControl *, CtI2cConfig, CtI2cTransport, uint32_t instance);
/* One device poll; real transport waits are independent from the render thread.
 * Seesaw controls: analog 0/X,1/Y; digital 0/A,1/B,2/X,3/Y,4/Select,5/Start.
 * CardKB2 controls are ASCII pulses, never inferred held/release state. */
int ct_i2c_control_poll(CtI2cControl *, CtI2cEmit, CtI2cDisconnect, void *, uint64_t);
void ct_i2c_control_disconnect(CtI2cControl *, CtI2cDisconnect, void *, uint64_t);

#endif
