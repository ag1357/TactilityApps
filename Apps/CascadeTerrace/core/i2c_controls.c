#include "i2c_controls.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

int ct_i2c_config_load(const char *path, CtI2cConfig out[CT_I2C_MAX_DEVICES]) {
    CtI2cConfig parsed[CT_I2C_MAX_DEVICES];
    char line[160], type[20], bus[CT_I2C_BUS_NAME], addr[20], extra;
    unsigned count=0;
    FILE *f=fopen(path,"r");
    if (!f) return errno==ENOENT ? 0 : -1;
    if (!fgets(line,sizeof line,f) || strcmp(line,"ANAPHORUM_I2C 1\n")) goto invalid;
    while (fgets(line,sizeof line,f)) {
        char *p=line;
        while (*p==' ' || *p=='\t') ++p;
        if (*p=='#' || *p=='\n' || !*p) continue;
        if (count==CT_I2C_MAX_DEVICES ||
            sscanf(p,"%19s %47s %19s %c",type,bus,addr,&extra)!=3) goto invalid;
        unsigned kind=!strcmp(type,"seesaw") ? CT_I2C_SEESAW :
                      !strcmp(type,"cardkb2") ? CT_I2C_CARDKB2 : 0;
        char *end=NULL;
        unsigned long address=strtoul(addr,&end,0);
        if (!kind || !*addr || *end || address<0x08 || address>0x77 ||
            (kind==CT_I2C_CARDKB2 && address!=0x5f)) goto invalid;
        /* Avoid names whose truncation could accidentally select another bus. */
        for (unsigned i=0;i<count;++i)
            if (!strcmp(parsed[i].bus,bus) && parsed[i].address==address) goto invalid;
        memset(&parsed[count],0,sizeof parsed[count]);
        parsed[count].kind=kind;
        parsed[count].address=(uint8_t)address;
        memcpy(parsed[count].bus,bus,strlen(bus)+1);
        ++count;
    }
    if (ferror(f)) goto invalid;
    fclose(f);
    memcpy(out,parsed,count*sizeof *out);
    return (int)count;
invalid:
    fclose(f);
    return -1;
}

uint32_t ct_i2c_identity(const CtI2cConfig *config) {
    uint32_t hash=2166136261u;
    for (const unsigned char *p=(const unsigned char *)config->bus;*p;++p)
        hash=(hash^*p)*16777619u;
    hash=(hash^config->address)*16777619u;
    hash=(hash^config->kind)*16777619u;
    return hash ? hash : 1;
}

void ct_i2c_control_init(CtI2cControl *c,CtI2cConfig config,CtI2cTransport t,uint32_t instance) {
    memset(c,0,sizeof *c);
    c->config=config; c->transport=t; c->identity=ct_i2c_identity(&config); c->instance=instance;
}

static int reg_read(CtI2cControl *c,uint8_t module,uint8_t reg,uint8_t *out,size_t n) {
    uint8_t command[2]={module,reg};
    CtI2cTransport *t=&c->transport;
    /* Seesaw requires a STOP plus processing time, not an immediate repeated START. */
    if (!t->write(t->context,c->config.address,command,sizeof command)) return 0;
    t->wait_us(t->context,500);
    return t->read(t->context,c->config.address,out,n);
}

static int seesaw_setup(CtI2cControl *c) {
    uint8_t version[4];
    if (!reg_read(c,0x00,0x02,version,4) ||
        (((unsigned)version[0]<<8)|version[1])!=5743) return 0;
    /* Input + pullup on six button pins: 0,1,2,5,6,16. No reset/address/IRQ mutation. */
    const uint8_t registers[]={0x03,0x0b,0x05};
    uint8_t command[]={0x01,0,0x00,0x01,0x00,0x67};
    for (unsigned i=0;i<sizeof registers;++i) {
        command[1]=registers[i];
        if (!c->transport.write(c->transport.context,c->config.address,command,sizeof command)) return 0;
    }
    return 1;
}

static void emit(CtI2cControl *c,CtI2cEmit cb,void *ctx,unsigned control,unsigned kind,int value,uint64_t now) {
    AiControl source={(uint16_t)(c->config.kind==CT_I2C_SEESAW ? AI_BACKEND_SEESAW : AI_BACKEND_CARDKB2),
                      (uint16_t)control,c->identity,(uint8_t)kind};
    cb(ctx,c->instance,source,value,now);
}

void ct_i2c_control_disconnect(CtI2cControl *c,CtI2cDisconnect cb,void *ctx,uint64_t now) {
    if (c->connected && cb) cb(ctx,c->instance,now);
    c->connected=0; c->ready=0;
}

int ct_i2c_control_poll(CtI2cControl *c,CtI2cEmit cb,CtI2cDisconnect gone,void *ctx,uint64_t now) {
    if (!cb || !c->transport.read || !c->transport.write || !c->transport.wait_us) return 0;
    if (c->config.kind==CT_I2C_CARDKB2) {
        uint8_t ch=0;
        if (!c->transport.read(c->transport.context,c->config.address,&ch,1)) goto fail;
        c->ready=c->connected=1;
        if (ch) {
            /* FIFO empty (0) says nothing about physically held keys. */
            emit(c,cb,ctx,ch,AI_DIGITAL,1000,now);
            emit(c,cb,ctx,ch,AI_DIGITAL,0,now);
        }
        return 1;
    }
    if (c->config.kind!=CT_I2C_SEESAW) goto fail;
    if (!c->ready && !seesaw_setup(c)) goto fail;
    uint8_t x[2],y[2],gpio[4];
    if (!reg_read(c,0x09,0x15,x,2) || !reg_read(c,0x09,0x16,y,2) ||
        !reg_read(c,0x01,0x04,gpio,4)) goto fail;
    unsigned rawx=((unsigned)x[0]<<8)|x[1], rawy=((unsigned)y[0]<<8)|y[1];
    if (rawx>1023 || rawy>1023) goto fail;
    c->ready=c->connected=1;
    /* Product orientation follows Adafruit's 1023-raw convention; action bindings choose polarity. */
    emit(c,cb,ctx,0,AI_ANALOG,((1023-(int)rawx)*2000/1023)-1000,now);
    emit(c,cb,ctx,1,AI_ANALOG,((1023-(int)rawy)*2000/1023)-1000,now);
    uint32_t bits=((uint32_t)gpio[0]<<24)|((uint32_t)gpio[1]<<16)|((uint32_t)gpio[2]<<8)|gpio[3];
    const unsigned pins[]={5,1,6,2,0,16};
    for (unsigned i=0;i<6;++i) emit(c,cb,ctx,i,AI_DIGITAL,(bits&(1u<<pins[i])) ? 0 : 1000,now);
    return 1;
fail:
    ct_i2c_control_disconnect(c,gone,ctx,now);
    return 0;
}
