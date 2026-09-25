#include "i2c_input.h"
#include <string.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/error.h>
#include <esp_timer.h>

/* Transfers are timed. The optional I2C worker is separate from keyboard
 * acquisition because the platform driver's mutex acquisition is not timed. */
static TickType_t timeout_ticks(void) {
    TickType_t ticks=pdMS_TO_TICKS(5);
    return ticks ? ticks : 1;
}
static int bus_write(void *ctx,uint8_t addr,const uint8_t *bytes,size_t n) {
    CtI2cInputDevice *d=ctx;
    return d->bus && i2c_controller_write(d->bus,addr,bytes,(uint16_t)n,timeout_ticks())==ERROR_NONE;
}
static int bus_read(void *ctx,uint8_t addr,uint8_t *bytes,size_t n) {
    CtI2cInputDevice *d=ctx;
    return d->bus && i2c_controller_read(d->bus,addr,bytes,n,timeout_ticks())==ERROR_NONE;
}
static void wait_us(void *ctx,unsigned us) {
    (void)ctx;
    /* Seesaw has a sub-tick register preparation delay; bounded to 500 us by core.
     * This uses an already-exported clock without adding a ROM/IDF export. */
    int64_t end=esp_timer_get_time()+us;
    while (esp_timer_get_time()<end) { }
}
static int acquire_bus(CtI2cInputDevice *d) {
    struct Device *bus=NULL;
    if (device_get_by_name(d->control.config.bus,&bus)!=ERROR_NONE) return 0;
    if (device_get_type(bus)!=&I2C_CONTROLLER_TYPE || !device_is_ready(bus)) {
        device_put(bus);
        return 0;
    }
    d->bus=bus;
    return 1;
}
int ct_i2c_input_open(CtI2cInput *in,const char *path,CtI2cEmit emit,
                      CtI2cDisconnect disconnect,void *ctx) {
    memset(in,0,sizeof *in);
    in->emit=emit; in->disconnect=disconnect; in->context=ctx;
    CtI2cConfig configs[CT_I2C_MAX_DEVICES];
    int count=ct_i2c_config_load(path,configs);
    if (count<=0) return count;
    in->count=(unsigned)count;
    for (unsigned i=0;i<in->count;++i) {
        CtI2cInputDevice *d=&in->devices[i];
        CtI2cTransport t={d,bus_write,bus_read,wait_us};
        /* Runtime namespace separate from LVGL/keyboard identities; never persisted. */
        ct_i2c_control_init(&d->control,configs[i],t,0x40000000u+i);
    }
    return count;
}
void ct_i2c_input_poll(CtI2cInput *in,uint64_t now) {
    if (!in->count) return;
    CtI2cInputDevice *d=&in->devices[in->next++%in->count];
    if (now<d->retry_us) return;
    if (!acquire_bus(d)) {
        ct_i2c_control_disconnect(&d->control,in->disconnect,in->context,now);
        d->retry_us=now+1000000;
        return;
    }
    ++in->polls;
    if (!ct_i2c_control_poll(&d->control,in->emit,in->disconnect,in->context,now)) {
        ++in->failures;
        d->retry_us=now+1000000;
    }
    /* Bracket this short operation only: the controller may stop while the
     * input worker sleeps or the application is in the background. */
    device_put(d->bus); d->bus=NULL;
}
void ct_i2c_input_close(CtI2cInput *in,uint64_t now) {
    for (unsigned i=0;i<in->count;++i) {
        CtI2cInputDevice *d=&in->devices[i];
        ct_i2c_control_disconnect(&d->control,in->disconnect,in->context,now);
        if (d->bus) device_put(d->bus);
        d->bus=NULL;
    }
    in->count=0;
}
