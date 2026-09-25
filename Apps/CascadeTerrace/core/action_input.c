#include "action_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int absolute(int value) { return value < 0 ? -value : value; }
static int clamp(int value) { return value < -1000 ? -1000 : value > 1000 ? 1000 : value; }
static int valid_source(AiControl source) {
    return source.backend >= AI_BACKEND_KEYBOARD && source.backend <= AI_BACKEND_GAMEPAD &&
        (source.kind == AI_DIGITAL || source.kind == AI_ANALOG);
}
static int valid_binding(AiBinding binding) {
    return valid_source(binding.source) && binding.action < AI_ACTION_COUNT &&
        !(binding.source.backend == AI_BACKEND_CARDKB2 &&
          (binding.action < AI_JUMP || binding.action == AI_RUN)) &&
        (binding.direction == 1 || (binding.source.kind == AI_ANALOG && binding.direction == -1)) &&
        (binding.sign == 1 || (binding.action < AI_JUMP && binding.sign == -1));
}
static int same_control(AiControl a, AiControl b) {
    return a.backend == b.backend && a.control == b.control && a.device == b.device && a.kind == b.kind;
}
static int overlaps(AiBinding a, AiBinding b) {
    return a.source.backend == b.source.backend && a.source.control == b.source.control &&
        a.source.kind == b.source.kind && a.direction == b.direction &&
        (!a.source.device || !b.source.device || a.source.device == b.source.device);
}
static int exact_binding(AiBinding a, AiBinding b) {
    return same_control(a.source, b.source) && a.direction == b.direction &&
        a.action == b.action && a.sign == b.sign;
}
static int incompatible(AiBinding a, AiBinding b) {
    /* Overlapping wildcard/specific bindings must also be resolved explicitly;
       otherwise one physical control could contribute twice to an axis. */
    return overlaps(a,b) && !exact_binding(a,b);
}
static void tick(AiInput *input, uint64_t now) {
    if (!input->clock_started) {
        input->last_us = input->consume_us = now;
        input->clock_started = 1;
    }
    if (now < input->last_us) now = input->last_us;
    uint64_t dt = now - input->last_us;
    for (unsigned i=0; i<4; ++i) input->axis_integral[i] += (int64_t)dt * input->axes[i];
    input->last_us = now;
}
static void edge(AiInput *input, uint8_t *count) {
    if (*count < AI_EDGE_LIMIT) ++*count;
    else ++input->dropped_events;
}
static void recompute(AiInput *input) {
    int totals[4] = {0};
    uint32_t held = 0;
    if (input->capture_state == AI_CAPTURE_IDLE) {
        for (unsigned b=0; b<input->binding_count; ++b) {
            const AiBinding *binding = &input->bindings[b];
            int strongest = 0;
            for (unsigned c=0; c<AI_MAX_CONTROLS; ++c) {
                const AiHeldControl *control = &input->controls[c];
                if (!control->used || control->inhibited ||
                    control->source.backend != binding->source.backend ||
                    control->source.control != binding->source.control ||
                    control->source.kind != binding->source.kind ||
                    (binding->source.device && binding->source.device != control->source.device)) continue;
                int magnitude = 0;
                if (control->source.kind == AI_DIGITAL) magnitude = control->value ? 1000 : 0;
                else if (control->active_direction == binding->direction) {
                    magnitude = absolute(control->value) - AI_DEADZONE;
                    magnitude = magnitude > 0 ? magnitude * 1000 / (1000 - AI_DEADZONE) : 0;
                    /* Digital analog bindings retain hysteresis even inside the deadzone. */
                    if (binding->action >= AI_JUMP) magnitude = 1000;
                }
                if (magnitude > strongest) strongest = magnitude;
            }
            if (binding->action < AI_JUMP) totals[binding->action] += strongest * binding->sign;
            else if (strongest) held |= 1u << binding->action;
        }
    }
    for (unsigned a=0; a<4; ++a) {
        input->axes[a] = (int16_t)clamp(totals[a]);
        int direction = input->axis_direction[a];
        if (input->axes[a] >= AI_NAV_THRESHOLD) direction = 1;
        else if (input->axes[a] <= -AI_NAV_THRESHOLD) direction = -1;
        else if (absolute(input->axes[a]) <= AI_NAV_RELEASE) direction = 0;
        if (direction && direction != input->axis_direction[a])
            edge(input,&input->axis_pressed[a][direction > 0]);
        input->axis_direction[a] = (int8_t)direction;
    }
    for (unsigned a=AI_JUMP; a<AI_ACTION_COUNT; ++a) {
        uint32_t bit = 1u << a;
        if ((held & bit) && !(input->held & bit)) edge(input, &input->pressed[a]);
        if (!(held & bit) && (input->held & bit)) edge(input, &input->released[a]);
    }
    input->held = held;
}
void ai_init(AiInput *input) {
    memset(input, 0, sizeof(*input));
    ai_defaults(input, 0);
    input->clock_started = 0;
}
void ai_clear(AiInput *input, uint64_t now) {
    tick(input,now);
    for (unsigned i=0; i<AI_MAX_CONTROLS; ++i) {
        AiHeldControl *control = &input->controls[i];
        if (control->used) {
            control->inhibited = control->source.kind == AI_DIGITAL ? control->value != 0 :
                absolute(control->value) > AI_RELEASE_ZONE;
            control->capture_ready = !control->inhibited;
        }
    }
    memset(input->axes,0,sizeof(input->axes));
    memset(input->axis_integral,0,sizeof(input->axis_integral));
    memset(input->pressed,0,sizeof(input->pressed));
    memset(input->released,0,sizeof(input->released));
    memset(input->axis_pressed,0,sizeof(input->axis_pressed));
    memset(input->axis_direction,0,sizeof(input->axis_direction));
    input->held = 0;
    input->consume_us = input->last_us;
}
AiBindResult ai_bind(AiInput *input, AiBinding binding, int replace, uint64_t now) {
    if (!valid_binding(binding)) return AI_BIND_INVALID;
    unsigned conflict_count = 0;
    int exists = 0;
    for (unsigned i=0; i<input->binding_count; ++i) {
        if (exact_binding(input->bindings[i],binding)) exists = 1;
        if (incompatible(input->bindings[i],binding)) ++conflict_count;
    }
    if (conflict_count && !replace) return AI_BIND_CONFLICT;
    if (!exists && input->binding_count - conflict_count >= AI_MAX_BINDINGS) return AI_BIND_FULL;
    ai_clear(input,now);
    unsigned count = 0;
    for (unsigned i=0; i<input->binding_count; ++i)
        if (!incompatible(input->bindings[i],binding)) input->bindings[count++] = input->bindings[i];
    input->binding_count = count;
    if (!exists) input->bindings[input->binding_count++] = binding;
    return AI_BIND_OK;
}
void ai_defaults(AiInput *input, uint64_t now) {
    static const struct { uint16_t key; uint8_t action; int8_t sign; } defaults[] = {
        {'a',AI_MOVE_X,-1},{'d',AI_MOVE_X,1},{'w',AI_MOVE_Y,1},{'s',AI_MOVE_Y,-1},
        {'o',AI_LOOK_X,-1},{'p',AI_LOOK_X,1},{'i',AI_VIEW_TOGGLE,1},
        {'u',AI_INTERACT,1},{' ',AI_JUMP,1},{AI_KEY_SHIFT,AI_RUN,1},
        {AI_KEY_ESCAPE,AI_MENU,1},{8,AI_BACK,1},{'r',AI_RUN,1},
        {AI_KEY_LEFT,AI_MOVE_X,-1},{AI_KEY_RIGHT,AI_MOVE_X,1},
        {AI_KEY_UP,AI_MOVE_Y,1},{AI_KEY_DOWN,AI_MOVE_Y,-1},{AI_KEY_ENTER,AI_INTERACT,1}
    };
    ai_clear(input,now);
    input->capture_state = AI_CAPTURE_IDLE;
    input->binding_count = 0;
    for (unsigned i=0; i<sizeof(defaults)/sizeof(defaults[0]); ++i) {
        AiBinding binding = {{AI_BACKEND_KEYBOARD,defaults[i].key,0,AI_DIGITAL},
                             1,defaults[i].action,defaults[i].sign};
        input->bindings[input->binding_count++] = binding;
    }
    for (unsigned action=0; action<AI_ACTION_COUNT; ++action) {
        AiBinding binding = {{AI_BACKEND_TOUCH,(uint16_t)action,1,
            action < AI_JUMP ? AI_ANALOG : AI_DIGITAL},1,(uint8_t)action,1};
        input->bindings[input->binding_count++] = binding;
        if (action < AI_JUMP) {
            binding.direction = binding.sign = -1;
            input->bindings[input->binding_count++] = binding;
        }
    }
    for (unsigned i=0; i<sizeof(defaults)/sizeof(defaults[0]); ++i) {
        if (defaults[i].action < AI_JUMP || defaults[i].action == AI_RUN) continue;
        AiBinding binding = {{AI_BACKEND_CARDKB2,defaults[i].key,0,AI_DIGITAL},
                             1,defaults[i].action,defaults[i].sign};
        input->bindings[input->binding_count++] = binding;
    }
}
void ai_device_event(AiInput *input, uint32_t instance, AiControl source, int value, uint64_t now) {
    if (!valid_source(source)) { ++input->dropped_events; return; }
    tick(input,now);
    AiHeldControl *control = NULL, *free_control = NULL;
    for (unsigned i=0; i<AI_MAX_CONTROLS; ++i) {
        AiHeldControl *entry = &input->controls[i];
        if (entry->used && entry->instance == instance && same_control(entry->source,source)) {
            control = entry; break;
        }
        if ((!entry->used || !entry->value) && !free_control) free_control = entry;
    }
    if (!control) {
        if (!free_control) { ++input->dropped_events; return; }
        control = free_control;
        memset(control,0,sizeof(*control));
        control->used = control->capture_ready = 1;
        control->instance = instance;
        control->source = source;
    }
    control->value = source.kind == AI_DIGITAL ? (value ? 1000 : 0) : (int16_t)clamp(value);
    int neutral = source.kind == AI_DIGITAL ? !value : absolute(control->value) <= AI_RELEASE_ZONE;
    if (neutral) {
        control->inhibited = 0;
        control->capture_ready = 1;
        control->active_direction = 0;
    } else if (source.kind == AI_ANALOG) {
        if (control->value >= AI_DEADZONE) control->active_direction = 1;
        else if (control->value <= -AI_DEADZONE) control->active_direction = -1;
    }
    if (input->capture_state == AI_CAPTURE_WAITING && control->capture_ready && !control->inhibited &&
        !(source.backend == AI_BACKEND_CARDKB2 &&
          (input->candidate.action < AI_JUMP || input->candidate.action == AI_RUN)) &&
        (source.kind == AI_DIGITAL ? value != 0 : absolute(control->value) >= AI_CAPTURE_THRESHOLD)) {
        input->candidate.source = source;
        input->candidate.direction = source.kind == AI_ANALOG && value < 0 ? -1 : 1;
        input->capture_state = AI_CAPTURE_READY;
        control->capture_ready = 0;
    }
    recompute(input);
}
void ai_disconnect(AiInput *input, uint32_t instance, uint64_t now) {
    tick(input,now);
    for (unsigned i=0; i<AI_MAX_CONTROLS; ++i)
        if (input->controls[i].used && input->controls[i].instance == instance)
            memset(&input->controls[i],0,sizeof(input->controls[i]));
    recompute(input);
}
void ai_consume(AiInput *input, uint64_t now, AiFrame *frame) {
    tick(input,now);
    memset(frame,0,sizeof(*frame));
    frame->elapsed_us = input->last_us - input->consume_us;
    for (unsigned a=0; a<4; ++a) {
        frame->axes[a] = input->axes[a];
        frame->average_axes[a] = frame->elapsed_us ?
            (int16_t)(input->axis_integral[a] / (int64_t)frame->elapsed_us) : input->axes[a];
    }
    frame->held = input->held;
    frame->dropped_events = input->dropped_events;
    memcpy(frame->pressed,input->pressed,sizeof(frame->pressed));
    memcpy(frame->released,input->released,sizeof(frame->released));
    memcpy(frame->axis_pressed,input->axis_pressed,sizeof(frame->axis_pressed));
    memset(input->pressed,0,sizeof(input->pressed));
    memset(input->released,0,sizeof(input->released));
    memset(input->axis_pressed,0,sizeof(input->axis_pressed));
    memset(input->axis_integral,0,sizeof(input->axis_integral));
    input->consume_us = input->last_us;
}
void ai_capture_begin(AiInput *input, AiAction action, int sign, uint64_t now) {
    if ((unsigned)action >= AI_ACTION_COUNT || (sign != 1 && (action >= AI_JUMP || sign != -1))) return;
    ai_clear(input,now);
    memset(&input->candidate,0,sizeof(input->candidate));
    input->candidate.action = (uint8_t)action;
    input->candidate.sign = (int8_t)sign;
    input->capture_state = AI_CAPTURE_WAITING;
}
AiBindResult ai_capture_accept(AiInput *input, int replace, uint64_t now) {
    if (input->capture_state != AI_CAPTURE_READY) return AI_BIND_INVALID;
    AiBindResult result = ai_bind(input,input->candidate,replace,now);
    if (result == AI_BIND_OK) {
        input->capture_state = AI_CAPTURE_IDLE;
        ai_clear(input,now);
    }
    return result;
}
void ai_capture_cancel(AiInput *input, uint64_t now) {
    input->capture_state = AI_CAPTURE_IDLE;
    memset(&input->candidate,0,sizeof(input->candidate));
    ai_clear(input,now);
}
const char *ai_action_name(AiAction action) {
    static const char *names[] = {"Move X","Move Y","Look X","Look Y","Jump","Run",
        "Interact","Menu","Back","View Toggle"};
    return (unsigned)action < AI_ACTION_COUNT ? names[action] : "Unknown";
}
const char *ai_backend_name(unsigned backend) {
    static const char *names[] = {"Unknown","Keyboard","Touch","Seesaw","CardKB2","Gamepad"};
    return backend <= AI_BACKEND_GAMEPAD ? names[backend] : names[0];
}
void ai_format_binding(const AiBinding *binding, char *out, size_t capacity) {
    char control[32];
    if (!capacity) return;
    if (binding->source.backend == AI_BACKEND_KEYBOARD && binding->source.kind == AI_DIGITAL) {
        unsigned key = binding->source.control;
        if (key == ' ') snprintf(control,sizeof(control),"Space");
        else if (key == AI_KEY_SHIFT) snprintf(control,sizeof(control),"Shift");
        else if (key == AI_KEY_ESCAPE) snprintf(control,sizeof(control),"Esc");
        else if (key == 8) snprintf(control,sizeof(control),"Backspace");
        else if (key >= 33 && key <= 126) snprintf(control,sizeof(control),"%c",(int)key);
        else snprintf(control,sizeof(control),"Key %u",key);
    } else snprintf(control,sizeof(control),"%s %u%s",binding->source.kind == AI_ANALOG ? "Axis" : "Button",
                    binding->source.control,binding->source.kind == AI_ANALOG ? (binding->direction < 0 ? "-" : "+") : "");
    if (binding->source.device)
        snprintf(out,capacity,"%s[%lu] %s%s",ai_backend_name(binding->source.backend),
            (unsigned long)binding->source.device,control,binding->action < AI_JUMP ? (binding->sign < 0 ? " -> -" : " -> +") : "");
    else snprintf(out,capacity,"%s %s%s",ai_backend_name(binding->source.backend),control,
            binding->action < AI_JUMP ? (binding->sign < 0 ? " -> -" : " -> +") : "");
}
int ai_bindings_save(const AiInput *input, const char *path) {
    char temporary[1024];
    if (!path || snprintf(temporary,sizeof(temporary),"%s.tmp",path) >= (int)sizeof(temporary)) return 0;
    FILE *file = fopen(temporary,"w");
    if (!file) return 0;
    int good = fprintf(file,"ANAPHORUM_BINDINGS 1\n") > 0;
    for (unsigned i=0; good && i<input->binding_count; ++i) {
        const AiBinding *b = &input->bindings[i];
        good = fprintf(file,"%u %lu %u %u %d %u %d\n",b->source.backend,(unsigned long)b->source.device,
            b->source.control,b->source.kind,b->direction,b->action,b->sign) > 0;
    }
    if (fclose(file)) good = 0;
    if (good && rename(temporary,path) == 0) return 1;
    remove(temporary);
    return 0;
}
int ai_bindings_load(AiInput *input, const char *path, uint64_t now) {
    if (!path) return 0;
    FILE *file = fopen(path,"r");
    if (!file) return 0;
    AiInput *loaded = calloc(1,sizeof(*loaded));
    char line[192];
    int good = loaded && fgets(line,sizeof(line),file) && !strcmp(line,"ANAPHORUM_BINDINGS 1\n");
    while (good && fgets(line,sizeof(line),file)) {
        long long backend,device,control,kind,direction,action,sign;
        char extra;
        if (sscanf(line,"%lld %lld %lld %lld %lld %lld %lld %c",&backend,&device,&control,&kind,
                   &direction,&action,&sign,&extra) != 7 ||
            backend < AI_BACKEND_KEYBOARD || backend > AI_BACKEND_GAMEPAD || device < 0 || device > UINT32_MAX ||
            control < 0 || control > UINT16_MAX || (kind != AI_DIGITAL && kind != AI_ANALOG) ||
            (direction != -1 && direction != 1) || action < 0 || action >= AI_ACTION_COUNT ||
            (sign != -1 && sign != 1)) { good = 0; break; }
        AiBinding binding = {{(uint16_t)backend,(uint16_t)control,(uint32_t)device,(uint8_t)kind},
                             (int8_t)direction,(uint8_t)action,(int8_t)sign};
        if (ai_bind(loaded,binding,0,0) != AI_BIND_OK) good = 0;
    }
    if (ferror(file)) good = 0;
    fclose(file);
    if (good) {
        ai_clear(input,now);
        memcpy(input->bindings,loaded->bindings,sizeof(input->bindings));
        input->binding_count = loaded->binding_count;
        input->capture_state = AI_CAPTURE_IDLE;
    }
    free(loaded);
    return good;
}
