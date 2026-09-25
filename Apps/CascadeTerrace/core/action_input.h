#ifndef ANAPHORUM_ACTION_INPUT_H
#define ANAPHORUM_ACTION_INPUT_H

#include <stddef.h>
#include <stdint.h>

/* Platform adapters own synchronization and sampling. No renderer or OS calls. */
enum { AI_MAX_BINDINGS=96, AI_MAX_CONTROLS=128, AI_DEADZONE=180,
       AI_RELEASE_ZONE=120, AI_CAPTURE_THRESHOLD=600, AI_EDGE_LIMIT=255,
       AI_NAV_THRESHOLD=500, AI_NAV_RELEASE=250 };
typedef enum {
    AI_MOVE_X, AI_MOVE_Y, AI_LOOK_X, AI_LOOK_Y,
    AI_JUMP, AI_RUN, AI_INTERACT, AI_MENU, AI_BACK, AI_VIEW_TOGGLE,
    AI_ACTION_COUNT
} AiAction;
typedef enum {
    AI_BACKEND_KEYBOARD=1, AI_BACKEND_TOUCH, AI_BACKEND_SEESAW,
    AI_BACKEND_CARDKB2, AI_BACKEND_GAMEPAD
} AiBackend;
typedef enum { AI_DIGITAL=1, AI_ANALOG=2 } AiKind;
enum { AI_KEY_SHIFT=0x100, AI_KEY_UP, AI_KEY_DOWN, AI_KEY_LEFT,
       AI_KEY_RIGHT, AI_KEY_ENTER=13, AI_KEY_ESCAPE=27 };
typedef struct {
    uint16_t backend, control;
    uint32_t device; /* Stable backend identity (e.g. I2C address); 0 = any in binding. */
    uint8_t kind;
} AiControl;
typedef struct {
    AiControl source;
    int8_t direction; /* Physical analog half-axis, or +1 for digital. */
    uint8_t action;
    int8_t sign; /* Target axis polarity, +1 for digital actions. */
} AiBinding;
typedef struct {
    AiControl source;
    uint32_t instance; /* Runtime connection identity, never persisted. */
    int16_t value;
    int8_t active_direction;
    uint8_t used, inhibited, capture_ready;
} AiHeldControl;
typedef struct {
    int16_t axes[4];
    int16_t average_axes[4]; /* Time-weighted input since preceding consume. */
    uint32_t held;
    uint8_t pressed[AI_ACTION_COUNT], released[AI_ACTION_COUNT];
    uint8_t axis_pressed[4][2]; /* Queued navigation crossings: [axis][negative=0, positive=1]. */
    uint64_t elapsed_us;
    uint32_t dropped_events;
} AiFrame;
typedef enum { AI_CAPTURE_IDLE, AI_CAPTURE_WAITING, AI_CAPTURE_READY } AiCaptureState;
typedef enum { AI_BIND_OK, AI_BIND_CONFLICT, AI_BIND_FULL, AI_BIND_INVALID } AiBindResult;
typedef struct {
    AiBinding bindings[AI_MAX_BINDINGS];
    unsigned binding_count;
    AiHeldControl controls[AI_MAX_CONTROLS];
    int16_t axes[4];
    uint32_t held;
    uint8_t pressed[AI_ACTION_COUNT], released[AI_ACTION_COUNT];
    uint8_t axis_pressed[4][2];
    int8_t axis_direction[4];
    int64_t axis_integral[4];
    uint64_t last_us, consume_us;
    uint32_t dropped_events;
    uint8_t clock_started;
    AiCaptureState capture_state;
    AiBinding candidate;
} AiInput;

void ai_init(AiInput *input);
void ai_defaults(AiInput *input, uint64_t now_us);
const char *ai_action_name(AiAction action);
const char *ai_backend_name(unsigned backend);
void ai_format_binding(const AiBinding *binding, char *out, size_t capacity);
/* DIGITAL value: 0/release or nonzero/press. ANALOG value: -1000..1000. */
void ai_device_event(AiInput *input, uint32_t instance, AiControl source,
                     int value, uint64_t now_us);
void ai_disconnect(AiInput *input, uint32_t instance, uint64_t now_us);
/* Reconcile held controls until their real release/neutral; discard pending edges. */
void ai_clear(AiInput *input, uint64_t now_us);
void ai_consume(AiInput *input, uint64_t now_us, AiFrame *frame);
AiBindResult ai_bind(AiInput *input, AiBinding binding, int replace_conflicts,
                    uint64_t now_us);
void ai_capture_begin(AiInput *input, AiAction action, int sign, uint64_t now_us);
AiBindResult ai_capture_accept(AiInput *input, int replace_conflicts, uint64_t now_us);
void ai_capture_cancel(AiInput *input, uint64_t now_us);
/* Versioned app-local file, atomic replacement; load failure leaves state unchanged. */
int ai_bindings_save(const AiInput *input, const char *path);
int ai_bindings_load(AiInput *input, const char *path, uint64_t now_us);

#endif
