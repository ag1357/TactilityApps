#include "../main/i2c_input.h"
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const struct DeviceType I2C_CONTROLLER_TYPE={1};
static const struct DeviceType wrong_type={2};
static struct Device bus;
static unsigned gets,releases,reads,disconnected;
static int absent,not_ready,wrong,fail;
int device_get_by_name(const char *name,struct Device **out) {
    assert(!strcmp(name,"external")); ++gets;
    if (absent) return -1;
    assert(!bus.references); ++bus.references; *out=&bus; return 0;
}
const struct DeviceType *device_get_type(struct Device *d) {
    assert(d==&bus && bus.references==1); return wrong ? &wrong_type : &I2C_CONTROLLER_TYPE;
}
bool device_is_ready(const struct Device *d) { assert(d==&bus); return !not_ready; }
void device_put(struct Device *d) { assert(d==&bus && bus.references==1); --bus.references; ++releases; }
int i2c_controller_write(struct Device *d,uint8_t address,const uint8_t *bytes,uint16_t n,TickType_t timeout) {
    (void)d; (void)address; (void)bytes; (void)n; (void)timeout;
    assert(!"CardKB2 polling must not write a selector"); return -1;
}
int i2c_controller_read(struct Device *d,uint8_t address,uint8_t *bytes,size_t n,TickType_t timeout) {
    assert(d==&bus && bus.references==1 && address==0x5f && n==1 && timeout==1);
    ++reads; *bytes=0; return fail ? -1 : 0;
}
int64_t esp_timer_get_time(void) { static int64_t t; return ++t; }
static void event(void *ctx,uint32_t instance,AiControl c,int value,uint64_t now) {
    (void)ctx; (void)instance; (void)c; (void)value; (void)now;
    assert(!"No character was provided");
}
static void gone(void *ctx,uint32_t instance,uint64_t now) {
    (void)ctx; (void)now; assert(instance==0x40000000u); ++disconnected;
}
int main(void) {
    char path[100]; snprintf(path,sizeof path,"/tmp/anaphorum-i2c-platform-%ld.cfg",(long)getpid());
    FILE *f=fopen(path,"w"); assert(f);
    assert(fputs("ANAPHORUM_I2C 1\ncardkb2 external 0x5f\n",f)>=0); assert(!fclose(f));
    CtI2cInput input;
    assert(ct_i2c_input_open(&input,path,event,gone,NULL)==1); assert(!gets && !bus.references);
    ct_i2c_input_poll(&input,0); assert(gets==1 && releases==1 && reads==1 && !bus.references);
    ct_i2c_input_poll(&input,10000); assert(gets==2 && releases==2 && reads==2 && !bus.references);
    fail=1; ct_i2c_input_poll(&input,20000);
    assert(gets==3 && releases==3 && disconnected==1 && !bus.references);
    ct_i2c_input_poll(&input,30000); assert(gets==3 && !bus.references);
    fail=0; not_ready=1; ct_i2c_input_poll(&input,1020000);
    assert(gets==4 && releases==4 && !bus.references);
    not_ready=0; wrong=1; ct_i2c_input_poll(&input,2020000);
    assert(gets==5 && releases==5 && !bus.references);
    wrong=0; absent=1; ct_i2c_input_poll(&input,3020000);
    assert(gets==6 && releases==5 && !bus.references);
    absent=0; ct_i2c_input_poll(&input,4020000);
    assert(gets==7 && releases==6 && !bus.references);
    ct_i2c_input_close(&input,4030000); assert(releases==6 && disconnected==2 && !input.count);
    assert(!unlink(path));
    puts("VI-P2 I2C platform: operation-scoped refs, reconnect, failures, close: PASS");
    return 0;
}
