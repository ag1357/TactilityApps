#define _POSIX_C_SOURCE 200809L
#include "../core/action_input.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef AI_TEST_THREADS
#include <pthread.h>
#include <time.h>
#endif

static AiControl key(unsigned code) {
    AiControl control = {AI_BACKEND_KEYBOARD,(uint16_t)code,0,AI_DIGITAL}; return control;
}
static AiControl axis(unsigned device, unsigned code) {
    AiControl control = {AI_BACKEND_SEESAW,(uint16_t)code,device,AI_ANALOG}; return control;
}
static AiBinding binding(AiControl source, int direction, AiAction action, int sign) {
    AiBinding result = {source,(int8_t)direction,(uint8_t)action,(int8_t)sign}; return result;
}
static void defaults_and_chords(void) {
    AiInput input; AiFrame frame; ai_init(&input);
    ai_device_event(&input,1,key('w'),1,10000);
    ai_device_event(&input,1,key('p'),1,10000);
    ai_consume(&input,20000,&frame);
    assert(frame.axes[AI_MOVE_Y] == 1000 && frame.axes[AI_LOOK_X] == 1000);
    ai_device_event(&input,1,key('p'),0,30000);
    ai_consume(&input,40000,&frame);
    assert(frame.axes[AI_MOVE_Y] == 1000 && frame.axes[AI_LOOK_X] == 0);
    /* A held control is valid indefinitely, with no event-driven timeout. */
    ai_consume(&input,UINT64_C(3600000000),&frame);
    assert(frame.axes[AI_MOVE_Y] == 1000);
    ai_device_event(&input,1,key('s'),1,UINT64_C(3600000001));
    ai_consume(&input,UINT64_C(3600000010),&frame);
    assert(frame.axes[AI_MOVE_Y] == 0);
    ai_device_event(&input,1,key('s'),0,UINT64_C(3600000020));
    ai_consume(&input,UINT64_C(3600000030),&frame);
    assert(frame.axes[AI_MOVE_Y] == 1000);
    puts("PASS defaults, held chords, opposing directions, no artificial release");
}
static void edges_and_integrals(void) {
    AiInput input; AiFrame frame; ai_init(&input);
    ai_consume(&input,0,&frame);
    ai_device_event(&input,1,key(' '),1,10000);
    ai_device_event(&input,1,key(' '),1,11000); /* Keyboard autorepeat is not another edge. */
    ai_device_event(&input,1,key('w'),1,10000);
    ai_device_event(&input,1,key(' '),0,30000);
    ai_device_event(&input,1,key('w'),0,40000);
    ai_device_event(&input,1,key(' '),1,50000);
    ai_device_event(&input,1,key(' '),0,60000);
    ai_consume(&input,150000,&frame);
    assert(frame.pressed[AI_JUMP] == 2 && frame.released[AI_JUMP] == 2);
    assert(!(frame.held & (1u<<AI_JUMP)));
    assert(frame.axes[AI_MOVE_Y] == 0 && frame.average_axes[AI_MOVE_Y] == 193);
    ai_consume(&input,160000,&frame);
    assert(!frame.pressed[AI_JUMP] && !frame.average_axes[AI_MOVE_Y]);
    /* Capacity is explicit and observable rather than corrupting state. */
    for (unsigned i=0; i<260; ++i) {
        ai_device_event(&input,1,key(' '),1,170000+i*2);
        ai_device_event(&input,1,key(' '),0,170001+i*2);
    }
    ai_consume(&input,180000,&frame);
    assert(frame.pressed[AI_JUMP] == AI_EDGE_LIMIT && frame.dropped_events == 10);
    puts("PASS slow-frame queued edges, repeat suppression, time-weighted movement, overflow telemetry");
}
static void multi_device_disconnect(void) {
    AiInput input; AiFrame frame; ai_init(&input);
    assert(ai_bind(&input,binding(axis(0x50,0),1,AI_MOVE_Y,1),0,0) == AI_BIND_OK);
    assert(ai_bind(&input,binding(axis(0x51,0),1,AI_LOOK_X,1),0,0) == AI_BIND_OK);
    ai_device_event(&input,1,key('w'),1,10000);
    ai_device_event(&input,2,axis(0x50,0),1000,10000);
    ai_device_event(&input,3,axis(0x51,0),1000,10000);
    ai_disconnect(&input,1,20000);
    ai_consume(&input,20000,&frame);
    assert(frame.axes[AI_MOVE_Y] == 1000 && frame.axes[AI_LOOK_X] == 1000);
    ai_disconnect(&input,3,30000);
    ai_consume(&input,30000,&frame);
    assert(frame.axes[AI_MOVE_Y] == 1000 && frame.axes[AI_LOOK_X] == 0);
    ai_device_event(&input,4,key(' '),1,40000);
    ai_device_event(&input,5,key(' '),1,40000);
    ai_consume(&input,40000,&frame); assert(frame.pressed[AI_JUMP] == 1);
    ai_disconnect(&input,4,50000);
    ai_consume(&input,50000,&frame); assert(frame.held & (1u<<AI_JUMP));
    ai_disconnect(&input,5,60000);
    ai_consume(&input,60000,&frame); assert(frame.released[AI_JUMP] == 1);
    puts("PASS independent devices, two gamepad identities, disconnect reconciliation");
}
static void queued_navigation(void) {
    AiInput input; AiFrame frame; ai_init(&input);
    ai_device_event(&input,1,key(AI_KEY_UP),1,1000);
    ai_device_event(&input,1,key(AI_KEY_UP),0,1000);
    ai_device_event(&input,1,key(AI_KEY_UP),1,2000);
    ai_device_event(&input,1,key(AI_KEY_UP),0,2000);
    ai_device_event(&input,1,key(AI_KEY_DOWN),1,3000);
    ai_device_event(&input,1,key(AI_KEY_DOWN),0,3000);
    ai_consume(&input,150000,&frame);
    assert(frame.axes[AI_MOVE_Y] == 0 && frame.average_axes[AI_MOVE_Y] == 0);
    assert(frame.axis_pressed[AI_MOVE_Y][1] == 2 && frame.axis_pressed[AI_MOVE_Y][0] == 1);
    ai_consume(&input,160000,&frame); assert(!frame.axis_pressed[AI_MOVE_Y][1]);
    assert(ai_bind(&input,binding(axis(0x50,0),1,AI_MOVE_Y,1),0,170000) == AI_BIND_OK);
    ai_device_event(&input,2,axis(0x50,0),590,180000); /* Remapped to navigation threshold 500. */
    ai_consume(&input,180000,&frame); assert(frame.axis_pressed[AI_MOVE_Y][1] == 1);
    ai_device_event(&input,2,axis(0x50,0),550,190000);
    ai_device_event(&input,2,axis(0x50,0),600,200000);
    ai_consume(&input,200000,&frame); assert(!frame.axis_pressed[AI_MOVE_Y][1]);
    ai_device_event(&input,2,axis(0x50,0),300,210000); /* Below navigation release threshold. */
    ai_device_event(&input,2,axis(0x50,0),600,220000);
    ai_consume(&input,220000,&frame); assert(frame.axis_pressed[AI_MOVE_Y][1] == 1);
    ai_clear(&input,230000);
    ai_device_event(&input,2,axis(0x50,0),900,240000);
    ai_consume(&input,240000,&frame); assert(!frame.axis_pressed[AI_MOVE_Y][1]);
    puts("PASS queued menu direction taps and analog navigation hysteresis");
}
static void clear_and_capture(void) {
    AiInput input; AiFrame frame; ai_init(&input);
    ai_device_event(&input,1,key('w'),1,10000);
    ai_clear(&input,20000);
    ai_device_event(&input,1,key('w'),1,30000);
    ai_consume(&input,30000,&frame); assert(!frame.axes[AI_MOVE_Y]);
    ai_capture_begin(&input,AI_JUMP,1,40000);
    ai_device_event(&input,1,key('w'),1,50000);
    assert(input.capture_state == AI_CAPTURE_WAITING);
    ai_device_event(&input,1,key('w'),0,60000);
    ai_device_event(&input,1,key('w'),1,70000);
    assert(input.capture_state == AI_CAPTURE_READY);
    unsigned previous_count = input.binding_count;
    assert(ai_capture_accept(&input,0,70000) == AI_BIND_CONFLICT);
    assert(input.binding_count == previous_count && input.capture_state == AI_CAPTURE_READY);
    ai_capture_cancel(&input,70000);
    assert(input.binding_count == previous_count && input.capture_state == AI_CAPTURE_IDLE);
    ai_device_event(&input,1,key('w'),0,80000);
    ai_capture_begin(&input,AI_JUMP,1,80000);
    ai_device_event(&input,1,key('w'),1,90000);
    assert(ai_capture_accept(&input,1,90000) == AI_BIND_OK);
    ai_consume(&input,100000,&frame); assert(!frame.held && !frame.axes[AI_MOVE_Y]);
    ai_device_event(&input,1,key('w'),0,110000);
    ai_device_event(&input,1,key('w'),1,120000);
    ai_consume(&input,130000,&frame);
    assert(frame.pressed[AI_JUMP] == 1 && !frame.axes[AI_MOVE_Y]);
    ai_defaults(&input,140000);
    ai_device_event(&input,1,key('w'),0,150000);
    ai_device_event(&input,1,key('w'),1,160000);
    ai_consume(&input,170000,&frame);
    assert(frame.axes[AI_MOVE_Y] == 1000 && !frame.pressed[AI_JUMP]);
    puts("PASS transitions inhibit held controls, capture conflict/cancel/replace/defaults");
}
static void analog_capture(void) {
    AiInput input; AiFrame frame; ai_init(&input);
    ai_device_event(&input,2,axis(0x50,0),900,1000);
    ai_capture_begin(&input,AI_LOOK_Y,-1,2000);
    ai_device_event(&input,2,axis(0x50,0),950,3000);
    assert(input.capture_state == AI_CAPTURE_WAITING);
    ai_device_event(&input,2,axis(0x50,0),100,4000);
    ai_device_event(&input,2,axis(0x50,0),500,5000);
    assert(input.capture_state == AI_CAPTURE_WAITING);
    ai_device_event(&input,2,axis(0x50,0),-700,6000);
    assert(input.capture_state == AI_CAPTURE_READY && input.candidate.direction == -1);
    assert(ai_capture_accept(&input,0,6000) == AI_BIND_OK);
    ai_device_event(&input,2,axis(0x50,0),0,7000);
    ai_device_event(&input,2,axis(0x50,0),-590,8000);
    ai_consume(&input,9000,&frame); assert(frame.axes[AI_LOOK_Y] == -500);
    assert(ai_bind(&input,binding(axis(0x50,1),1,AI_RUN,1),0,10000) == AI_BIND_OK);
    ai_device_event(&input,2,axis(0x50,1),179,11000);
    ai_consume(&input,11000,&frame); assert(!(frame.held & (1u<<AI_RUN)));
    ai_device_event(&input,2,axis(0x50,1),180,12000);
    ai_consume(&input,12000,&frame); assert(frame.pressed[AI_RUN] == 1);
    ai_device_event(&input,2,axis(0x50,1),150,13000);
    ai_consume(&input,13000,&frame); assert(frame.held & (1u<<AI_RUN));
    ai_device_event(&input,2,axis(0x50,1),120,14000);
    ai_consume(&input,14000,&frame); assert(frame.released[AI_RUN] == 1);
    AiControl pulse = {AI_BACKEND_CARDKB2,'x',0x5f,AI_DIGITAL};
    assert(ai_bind(&input,binding(pulse,1,AI_RUN,1),0,15000) == AI_BIND_INVALID);
    ai_capture_begin(&input,AI_MOVE_X,1,16000);
    ai_device_event(&input,8,pulse,1,17000);
    ai_device_event(&input,8,pulse,0,17000);
    assert(input.capture_state == AI_CAPTURE_WAITING);
    ai_capture_cancel(&input,18000);
    ai_capture_begin(&input,AI_INTERACT,1,19000);
    ai_device_event(&input,8,pulse,1,20000);
    ai_device_event(&input,8,pulse,0,20000);
    assert(input.capture_state == AI_CAPTURE_READY);
    assert(ai_capture_accept(&input,0,20000) == AI_BIND_OK);
    ai_device_event(&input,8,pulse,1,21000);
    ai_device_event(&input,8,pulse,0,21000);
    ai_consume(&input,22000,&frame); assert(frame.pressed[AI_INTERACT] == 1 && !frame.held);
    puts("PASS analog capture/negative direction/deadzone/hysteresis; pulse-only backend restrictions");
}
static void persistence(void) {
    AiInput input,loaded; ai_init(&input); ai_init(&loaded);
    char path[] = "/tmp/anaphorum-bindings-XXXXXX";
    int descriptor = mkstemp(path); assert(descriptor >= 0); close(descriptor);
    assert(ai_bind(&input,binding(axis(0x1050,1),-1,AI_LOOK_Y,-1),0,0) == AI_BIND_OK);
    assert(ai_bindings_save(&input,path));
    assert(ai_bindings_load(&loaded,path,0));
    assert(input.binding_count == loaded.binding_count);
    for (unsigned i=0; i<input.binding_count; ++i) {
        char a[128],b[128];
        ai_format_binding(&input.bindings[i],a,sizeof(a));
        ai_format_binding(&loaded.bindings[i],b,sizeof(b));
        assert(!strcmp(a,b));
    }
    const char *invalid[] = {
        "ANAPHORUM_BINDINGS 2\n",
        "ANAPHORUM_BINDINGS 1\n1 0 119 1 1 1 1\n1 0 119 1 1 4 1\n",
        "ANAPHORUM_BINDINGS 1\n1 -1 119 1 1 1 1\n",
        "ANAPHORUM_BINDINGS 1\n1 0 65536 1 1 1 1\n",
        "ANAPHORUM_BINDINGS 1\n1 0 119 1 1 1 1 junk\n",
        "ANAPHORUM_BINDINGS 1\n4 95 119 1 1 1 1\n"
    };
    for (unsigned i=0; i<sizeof(invalid)/sizeof(invalid[0]); ++i) {
        AiInput before = loaded;
        FILE *file = fopen(path,"w"); assert(file); fputs(invalid[i],file); fclose(file);
        assert(!ai_bindings_load(&loaded,path,0));
        assert(!memcmp(&before,&loaded,sizeof(loaded)));
    }
    unlink(path);
    assert(!ai_bindings_load(&loaded,path,0));
    puts("PASS versioned atomic binding round-trip and malformed/conflicting load rollback");
}
#ifdef AI_TEST_THREADS
typedef struct {
    AiInput input;
    pthread_mutex_t lock;
    uint64_t start, previous, sum, minimum, maximum;
    unsigned ticks;
} SamplingFixture;
static uint64_t clock_us(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint64_t)now.tv_sec*1000000u + (uint64_t)now.tv_nsec/1000u;
}
static void *sample_independently(void *argument) {
    SamplingFixture *fixture = argument;
    const struct timespec delay = {0,10000000};
    for (unsigned i=0; i<12; ++i) {
        nanosleep(&delay,NULL);
        uint64_t now = clock_us();
        pthread_mutex_lock(&fixture->lock);
        uint64_t cadence = now-fixture->previous;
        if (!fixture->ticks || cadence < fixture->minimum) fixture->minimum = cadence;
        if (cadence > fixture->maximum) fixture->maximum = cadence;
        fixture->sum += cadence; ++fixture->ticks; fixture->previous = now;
        if (i == 1 || i == 5) {
            ai_device_event(&fixture->input,1,key(' '),1,now-fixture->start);
            ai_device_event(&fixture->input,1,key('w'),1,now-fixture->start);
        }
        if (i == 2 || i == 6) {
            ai_device_event(&fixture->input,1,key(' '),0,now-fixture->start);
            ai_device_event(&fixture->input,1,key('w'),0,now-fixture->start);
        }
        pthread_mutex_unlock(&fixture->lock);
    }
    return NULL;
}
static void real_slow_renderer(void) {
    SamplingFixture fixture; memset(&fixture,0,sizeof(fixture)); ai_init(&fixture.input);
    pthread_mutex_init(&fixture.lock,NULL);
    fixture.previous = fixture.start = clock_us();
    pthread_t thread; assert(!pthread_create(&thread,NULL,sample_independently,&fixture));
    const struct timespec render_delay = {0,150000000};
    nanosleep(&render_delay,NULL); /* No input calls by the simulated render thread. */
    pthread_join(thread,NULL);
    AiFrame frame; ai_consume(&fixture.input,clock_us()-fixture.start,&frame);
    assert(frame.pressed[AI_JUMP] == 2 && frame.released[AI_JUMP] == 2);
    assert(frame.average_axes[AI_MOVE_Y] > 0 && frame.axes[AI_MOVE_Y] == 0);
    assert(fixture.ticks == 12);
    printf("PASS independent host sampler during 150ms render stall: %u ticks; mean %.3fms min %.3fms max %.3fms\n",
        fixture.ticks,fixture.sum/(double)fixture.ticks/1000.,fixture.minimum/1000.,fixture.maximum/1000.);
    pthread_mutex_destroy(&fixture.lock);
}
#endif
int main(void) {
    defaults_and_chords(); edges_and_integrals(); multi_device_disconnect(); queued_navigation();
    clear_and_capture(); analog_capture(); persistence();
#ifdef AI_TEST_THREADS
    real_slow_renderer();
#endif
    puts("Action input tests passed"); return 0;
}
