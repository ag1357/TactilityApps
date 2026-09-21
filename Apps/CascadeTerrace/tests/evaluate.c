#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static Game g;
static NpcView view;
static void json(const char* s) {
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') putchar('\\');
        if ((unsigned char)*s < 32) {
            printf("\\u%04x", (unsigned char)*s);
        } else
            putchar(*s);
    }
    putchar('"');
}
static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    FILE* f = fopen(argv[1], "r");
    if (!f) return 2;
    char line[1200];
    int mode = argc > 2 ? atoi(argv[2]) : 1, variant = argc > 3 ? atoi(argv[3]) : 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        line[strcspn(line, "\n")] = 0;
        char *id = strtok(line, "\t"), *stage = strtok(NULL, "\t"), *expected = strtok(NULL, "\t"), *text = strtok(NULL, "");
        if (!id || !stage || !expected || !text) continue;
        game_new(&g, 42, variant);
        g.state.player_pos = npc_position(&g);
        Conversation c = {0};
        Reply r;
        if (strstr(stage, "repair")) {
            g.state.player.quantity[IT_COUPLING] = 1;
            game_apply(&g, (Operation) {REPAIR, PLAYER_ID, 0, IT_COUPLING, 1, NULL});
        }
        if (strstr(stage, "claim")) dialogue(&g, &c, "I noticed distant lights after sunset.", mode, &r);
        if (strstr(stage, "promise")) {
            dialogue(&g, &c, "I will deliver the component tomorrow.", mode, &r);
            world_advance(&g, 86400001);
            g.state.player_pos = npc_position(&g);
        }
        if (strstr(stage, "reboot")) {
            save_game(&g, "/tmp/cascade-eval");
            memset(&g, 0, sizeof(g));
            if (!load_game(&g, "/tmp/cascade-eval")) return 2;
            memset(&c, 0, sizeof(c));
            g.state.player_pos = npc_position(&g);
        }
        if (strstr(stage, "source")) dialogue(&g, &c, "Which rumors concern Dax?", mode, &r);
        double start = now();
        dialogue(&g, &c, text, mode, &r);
        double latency = now() - start;
        npc_view(&g, &view);
        int grounded = 1;
        for (int i = 0; i < r.count; i++) {
            int found = 0;
            for (int j = 0; j < view.count; j++)
                if (view.facts[j].id == r.evidence[i] && view.facts[j].status == r.statuses[i]) found = 1;
            if (!found) grounded = 0;
        }
        int pass = 0;
        if (!strcmp(expected, "unknown")) pass = r.abstained;
        else if (!strcmp(expected, "claim")) {
            for (int i = 0; i < r.count; i++)
                if (r.statuses[i] == CLAIM) pass = 1;
        } else if (!strcmp(expected, "repair_memory")) {
            for (int i = 0; i < r.count; i++)
                for (int j = 0; j < view.count; j++)
                    if (view.facts[j].id == r.evidence[i] && view.facts[j].status == MEMORY && !strcmp(view.facts[j].relation, "repaired")) pass = 1;
        } else if (!strcmp(expected, "promise"))
            pass = g.state.promise_state == 1;
        else {
            char* end = NULL;
            int target = (int)strtol(expected, &end, 10);
            for (int i = 0; i < r.count; i++)
                if ((int)r.evidence[i] == target) pass = 1;
        }
        printf("{\"id\":%s,\"variant\":%d,\"mode\":%d,\"prompt\":", id, variant, mode);
        json(text);
        printf(",\"response\":");
        json(r.text);
        printf(",\"latency_ms\":%.6f,\"provenance_valid\":%s,\"minimum_behavior_pass\":%s,\"abstained\":%s,\"fact_ids\":[", latency, grounded ? "true" : "false", pass ? "true" : "false", r.abstained ? "true" : "false");
        for (int i = 0; i < r.count; i++) printf("%s%u", i ? "," : "", r.evidence[i]);
        printf("]}\n");
    }
    fclose(f);
    return 0;
}
