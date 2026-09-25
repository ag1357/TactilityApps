/* VI-P2: input-to-gameplay boundaries, without any platform dependencies. */
#include "../core/interaction.h"
#include "../core/render.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static Game game, walking, running;
static Renderer renderer;
static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; } } while (0)
static void place(Game* g, int32_t x, int32_t z) {
    g->state.player_pos = (Pos){x, ground_at(&g->world,x,z), z};
    g->state.grounded = 1;
    g->state.yaw = 0;
}

int main(void) {
    game_new(&game, 42, 0);
    CtInteraction target = {0};
    CHECK(!ct_interaction_resolve(&game, &target));
    CHECK(target.entity_id == 0 && target.kind == CT_INTERACT_NONE);

    /* Resolve the actual moving NPC anchor, including repeated close/open. */
    Pos npc = npc_position(&game);
    game.state.player_pos = npc;
    for (int i = 0; i < 3; i++) {
        CHECK(ct_interaction_resolve(&game, &target));
        CHECK(target.entity_id == KYRA_ID && ct_interaction_dialogue_supported(&target));
        memset(&target, 0, sizeof(target)); /* Session close clears identity. */
    }
    game.state.player_pos.y += 2500;
    CHECK(!ct_interaction_resolve(&game, &target)); /* Not merely planar distance. */
    game.state.player_pos = npc;
    game.state.player_pos.z += 2201;
    CHECK(!ct_interaction_resolve(&game, &target));

    /* Generic entity selection cannot substitute Kyra for a different NPC. */
    game.state.player_pos = npc;
    CtInteraction candidates[2] = {
        {77, CT_INTERACT_NPC, npc, 2000, {0}, "Other NPC"},
        {78, CT_INTERACT_NPC, npc, 2000, {0}, "Another NPC"}
    };
    CHECK(ct_interaction_select(&game, candidates, 2, &target));
    CHECK(target.entity_id == 77 && !ct_interaction_dialogue_supported(&target));
    CtInteraction swap = candidates[0]; candidates[0] = candidates[1]; candidates[1] = swap;
    CHECK(ct_interaction_select(&game, candidates, 2, &target) && target.entity_id == 77);
    candidates[0].position.z += 100;
    candidates[1].position.z += 200;
    CHECK(ct_interaction_select(&game, candidates, 2, &target) && target.entity_id == 78);

    /* A nearby entity through a solid wall is inaccessible. */
    Site station = game.world.sites[STATION];
    place(&game, station.center.x + station.halfx + 500, station.center.z);
    candidates[0].position = (Pos){station.center.x + station.halfx - 1000,
                                game.state.player_pos.y, station.center.z};
    candidates[0].range_mm = 2200;
    CHECK(!ct_interaction_select(&game, candidates, 1, &target));
    CHECK(target.entity_id == 0);

    game.state.player_pos = game.world.evidence[1];
    CHECK(ct_interaction_resolve(&game, &target));
    CHECK(target.kind == CT_INTERACT_PICKUP && target.operation.item == IT_LOG);
    CHECK(game_apply(&game, target.operation));
    CHECK(!ct_interaction_resolve(&game, &target));
    place(&game, station.center.x, station.center.z + station.halfz);
    CHECK(ct_interaction_resolve(&game, &target));
    CHECK(target.kind == CT_INTERACT_REPAIR);
    place(&game, game.world.sites[MARKET].center.x, game.world.sites[MARKET].center.z);
    CHECK(ct_interaction_resolve(&game, &target));
    CHECK(target.kind == CT_INTERACT_TRADE && target.operation.target == 3);

    game_new(&game, 42, 0);
    place(&game, 22000, -90000);
    int facing = game.actor.facing;
    game_tick(&game, (Input){.turn=2}, 200);
    CHECK(game.state.yaw == 20 && game.actor.facing == facing);
    CHECK(game.actor.phase_milliradians == 0 && game.actor.locomotion == CT_LOCOMOTION_IDLE);
    CHECK(game.actor.move_x == 0 && game.actor.move_z == 0);
    game.state.yaw = 0;
    game_tick(&game, (Input){.strafe=1000}, 20);
    CHECK(game.actor.move_x > 0 && game.actor.move_z == 0);
    CHECK(game.actor.facing == 90 && game.actor.locomotion == CT_LOCOMOTION_WALK);
    CHECK(game.actor.phase_milliradians == 100);
    uint32_t stopped_phase = game.actor.phase_milliradians;
    game_tick(&game, (Input){.turn=2}, 200);
    CHECK(game.actor.facing == 90 && game.actor.phase_milliradians == stopped_phase);
    CHECK(game.actor.locomotion == CT_LOCOMOTION_IDLE);

    game_new(&walking, 42, 0); place(&walking, 22000, -90000);
    running = walking;
    game_tick(&walking, (Input){.forward=1000}, 100);
    game_tick(&running, (Input){.forward=1000,.run=1}, 100);
    CHECK(walking.actor.locomotion == CT_LOCOMOTION_WALK && running.actor.locomotion == CT_LOCOMOTION_RUN);
    CHECK(running.actor.phase_milliradians > walking.actor.phase_milliradians);
    CHECK(running.actor.move_z == 2 * walking.actor.move_z);
    CHECK(running.actor.facing == 0 && walking.actor.facing == 0);

    /* Requested walking into a wall does not animate, including run-held. */
    game_new(&game, 42, 0);
    station = game.world.sites[STATION];
    place(&game, station.center.x + station.halfx + 301, station.center.z);
    game.actor.facing = 45;
    game_tick(&game, (Input){.strafe=-1000,.run=1}, 100);
    CHECK(game.actor.move_x == 0 && game.actor.move_z == 0);
    CHECK(game.actor.locomotion == CT_LOCOMOTION_IDLE && game.actor.phase_milliradians == 0);
    CHECK(game.actor.facing == 45);
    game_tick(&game, (Input){.jump=1}, 20);
    CHECK(game.actor.locomotion == CT_LOCOMOTION_AIR && game.actor.phase_milliradians == 0);

    /* Camera look and presentation never mutate canonical state or save wire. */
    static uint8_t before[32768], after[32768];
    size_t before_size = state_encode(&game.state, before, sizeof(before));
    CHECK(before_size > 0);
    game.actor.facing = 137;
    game.actor.phase_milliradians = 129;
    renderer.look_pitch = .6f;
    render(&renderer, &game);
    CHECK(fabsf(renderer.pitch - .41f) < .0001f);
    size_t after_size = state_encode(&game.state, after, sizeof(after));
    CHECK(before_size == after_size && !memcmp(before,after,before_size));
    CHECK(game.actor.facing == 137 && game.actor.phase_milliradians == 129);
    renderer.look_pitch = 100.f;
    render(&renderer, &game);
    CHECK(renderer.pitch <= .71f);
    static State restored;
    CHECK(state_decode(&restored, before, before_size));
    CHECK(!memcmp(&restored, &game.state, sizeof(State)));
    printf("VI-P2 gameplay: %u checks passed\n", checks);
    return 0;
}
