#include "../core/i2c_controls.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint8_t address,module,reg,pulse;
    unsigned reads,writes,waits,setup,events,disconnected;
    unsigned rawx,rawy,product;
    uint32_t buttons;
    int fail;
    AiInput actions;
} Mock;
static int write_bus(void *ctx,uint8_t address,const uint8_t *bytes,size_t n) {
    Mock *m=ctx; assert(address==m->address); ++m->writes;
    if (m->fail) return 0;
    assert(n==2 || n==6);
    m->module=bytes[0]; m->reg=bytes[1];
    if (n==6) {
        assert(bytes[0]==1);
        assert(bytes[1]==3 || bytes[1]==11 || bytes[1]==5);
        assert(bytes[2]==0 && bytes[3]==1 && bytes[4]==0 && bytes[5]==0x67);
        ++m->setup;
    }
    return 1;
}
static int read_bus(void *ctx,uint8_t address,uint8_t *out,size_t n) {
    Mock *m=ctx; assert(address==m->address); ++m->reads;
    if (m->fail) return 0;
    if (address==0x5f) { assert(n==1); *out=m->pulse; m->pulse=0; return 1; }
    memset(out,0,n);
    if (m->module==0 && m->reg==2) {
        assert(n==4); out[0]=(uint8_t)(m->product>>8); out[1]=(uint8_t)m->product;
    } else if (m->module==9) {
        assert(n==2 && (m->reg==0x15 || m->reg==0x16));
        unsigned raw=m->reg==0x15 ? m->rawx : m->rawy;
        out[0]=(uint8_t)(raw>>8); out[1]=(uint8_t)raw;
    } else {
        assert(n==4 && m->module==1 && m->reg==4);
        for (unsigned i=0;i<4;++i) out[i]=(uint8_t)(m->buttons>>((3-i)*8));
    }
    return 1;
}
static void wait_bus(void *ctx,unsigned us) { Mock *m=ctx; assert(us==500); ++m->waits; }
static void event(void *ctx,uint32_t instance,AiControl source,int value,uint64_t now) {
    Mock *m=ctx; ++m->events; ai_device_event(&m->actions,instance,source,value,now);
}
static void gone(void *ctx,uint32_t instance,uint64_t now) {
    Mock *m=ctx; ++m->disconnected; ai_disconnect(&m->actions,instance,now);
}
static CtI2cConfig config(unsigned kind,unsigned address) {
    CtI2cConfig c={0}; c.kind=kind; c.address=(uint8_t)address;
    strcpy(c.bus,"i2c-external"); return c;
}
static void setup(Mock *m,CtI2cControl *c,unsigned kind,unsigned address,unsigned instance) {
    memset(m,0,sizeof *m); m->address=(uint8_t)address; m->product=5743;
    m->rawx=m->rawy=512; m->buttons=0xffffffffu; ai_init(&m->actions); m->actions.binding_count=0;
    CtI2cTransport t={m,write_bus,read_bus,wait_bus};
    ct_i2c_control_init(c,config(kind,address),t,instance);
}
static void bind(Mock *m,CtI2cControl *c,unsigned control,unsigned kind,unsigned action) {
    AiBinding b={{(uint16_t)(c->config.kind==CT_I2C_SEESAW ? AI_BACKEND_SEESAW : AI_BACKEND_CARDKB2),
                  (uint16_t)control,c->identity,(uint8_t)kind},1,(uint8_t)action,1};
    assert(ai_bind(&m->actions,b,0,0)==AI_BIND_OK);
}
static void test_seesaw(void) {
    Mock m; CtI2cControl c; AiFrame frame;
    setup(&m,&c,CT_I2C_SEESAW,0x50,77);
    bind(&m,&c,0,AI_ANALOG,AI_MOVE_X); bind(&m,&c,0,AI_DIGITAL,AI_JUMP);
    assert(ct_i2c_control_poll(&c,event,gone,&m,1000));
    assert(m.setup==3 && m.waits==4 && m.events==8);
    ai_consume(&m.actions,1000,&frame); assert(!frame.axes[0] && !frame.held);
    m.rawx=0; m.buttons&=~(1u<<5);
    assert(ct_i2c_control_poll(&c,event,gone,&m,11000));
    ai_consume(&m.actions,11000,&frame);
    assert(frame.axes[AI_MOVE_X]==1000 && frame.pressed[AI_JUMP]==1);
    /* A second stable snapshot must not synthesize another press. */
    assert(ct_i2c_control_poll(&c,event,gone,&m,21000));
    ai_consume(&m.actions,21000,&frame); assert(frame.pressed[AI_JUMP]==0);
    m.rawx=512; m.buttons=0xffffffffu;
    assert(ct_i2c_control_poll(&c,event,gone,&m,31000));
    ai_consume(&m.actions,31000,&frame);
    assert(frame.axes[0]==0 && frame.released[AI_JUMP]==1);
    m.fail=1; unsigned events=m.events;
    assert(!ct_i2c_control_poll(&c,event,gone,&m,41000));
    assert(m.disconnected==1 && m.events==events && !c.connected);
    assert(!ct_i2c_control_poll(&c,event,gone,&m,51000)); assert(m.disconnected==1);
    m.fail=0; assert(ct_i2c_control_poll(&c,event,gone,&m,61000)); assert(m.setup==6);
    ct_i2c_control_disconnect(&c,gone,&m,71000); assert(m.disconnected==2);
    setup(&m,&c,CT_I2C_SEESAW,0x51,78); m.product=123;
    assert(!ct_i2c_control_poll(&c,event,gone,&m,0)); assert(!m.setup && !m.events);
}
static void test_two_devices(void) {
    Mock a,b; CtI2cControl ca,cb; AiFrame frame;
    setup(&a,&ca,CT_I2C_SEESAW,0x50,1); setup(&b,&cb,CT_I2C_SEESAW,0x51,2);
    assert(ca.identity!=cb.identity);
    bind(&a,&ca,0,AI_ANALOG,AI_MOVE_X); bind(&a,&cb,0,AI_ANALOG,AI_LOOK_X);
    a.rawx=0; b.rawx=0;
    assert(ct_i2c_control_poll(&ca,event,gone,&a,100));
    assert(ct_i2c_control_poll(&cb,event,gone,&a,100));
    ai_consume(&a.actions,100,&frame); assert(frame.axes[0]==1000 && frame.axes[2]==1000);
    a.fail=1; assert(!ct_i2c_control_poll(&ca,event,gone,&a,200));
    ai_consume(&a.actions,200,&frame); assert(frame.axes[0]==0 && frame.axes[2]==1000);
    CtI2cConfig other=config(CT_I2C_SEESAW,0x50); strcpy(other.bus,"another-bus");
    assert(ct_i2c_identity(&other)!=ca.identity);
}
static void test_cardkb2(void) {
    Mock m; CtI2cControl c; AiFrame frame;
    setup(&m,&c,CT_I2C_CARDKB2,0x5f,9);
    bind(&m,&c,'u',AI_DIGITAL,AI_INTERACT);
    m.pulse='u'; assert(ct_i2c_control_poll(&c,event,gone,&m,100));
    /* Pulse remains queued through an artificially long render gap, without a held bit. */
    ai_consume(&m.actions,185100,&frame);
    assert(frame.pressed[AI_INTERACT]==1 && !frame.held);
    assert(ct_i2c_control_poll(&c,event,gone,&m,190000));
    ai_consume(&m.actions,190000,&frame); assert(!frame.pressed[AI_INTERACT]);
    assert(!m.writes && !m.waits);
    AiBinding invalid={{AI_BACKEND_CARDKB2,'w',c.identity,AI_DIGITAL},1,AI_MOVE_Y,1};
    assert(ai_bind(&m.actions,invalid,0,190000)==AI_BIND_INVALID);
    invalid.action=AI_RUN; assert(ai_bind(&m.actions,invalid,0,190000)==AI_BIND_INVALID);
}
static void write_config(const char *path,const char *text) {
    FILE *f=fopen(path,"w"); assert(f); assert(fputs(text,f)>=0); assert(!fclose(f));
}
static void test_config(void) {
    char path[100]; snprintf(path,sizeof path,"/tmp/anaphorum-i2c-test-%ld.cfg",(long)getpid());
    CtI2cConfig c[CT_I2C_MAX_DEVICES]; memset(c,0,sizeof c);
    assert(ct_i2c_config_load(path,c)==0);
    write_config(path,"ANAPHORUM_I2C 1\nseesaw i2c-external 0x50\nseesaw i2c-external 0x51\ncardkb2 i2c-external 0x5f\n");
    assert(ct_i2c_config_load(path,c)==3);
    assert(c[0].address==0x50 && c[1].address==0x51 && c[2].kind==CT_I2C_CARDKB2);
    uint32_t identity=ct_i2c_identity(&c[0]);
    write_config(path,"ANAPHORUM_I2C 1\nseesaw i2c-external 0x50\ncardkb2 i2c-external 0x50\n");
    assert(ct_i2c_config_load(path,c)==-1 && ct_i2c_identity(&c[0])==identity);
    write_config(path,"ANAPHORUM_I2C 2\n"); assert(ct_i2c_config_load(path,c)==-1);
    write_config(path,"ANAPHORUM_I2C 1\nseesaw i2c-external 0x50\nseesaw i2c-external 0x50\n");
    assert(ct_i2c_config_load(path,c)==-1); assert(!unlink(path));
}
int main(void) {
    test_seesaw(); test_two_devices(); test_cardkb2(); test_config();
    puts("VI-P2 I2C: protocol, multi-device isolation, pulse semantics, config: PASS");
    return 0;
}
