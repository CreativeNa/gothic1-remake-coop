/*
 * anim module: decides which clip the puppet plays. Hot-reloadable: rebuild with build_module.bat anim, the core picks it up
 * within a second, no game restart, the puppet stays visible. UNTESTED.
 *
 * stances: normal / crouched (stealth MM clips) / weapon drawn (combat PT clips), chosen from the other player's state.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "coreapi.h"

static struct {
    uint8_t *anim[4];               /* normal idle, walk, run, sprint */
    char names[4][96];
    int anim_state, cand_state, cand_n;
    char cur_act[96];
    int jump_phase;                 /* 0 none, 1 in, 2 loop, 3 landing */
    unsigned long jump_t; double jump_len;
    int weapon_drawn;
    int last_crouch, last_weapon;
    double rate_speed;
    int gait, pending; unsigned long pending_t; char cur_clip[96];
} M;

static const char *k_cand[4][4] = {
    {"A_Man_Idle_01", "A_Man_N_Idle_Breathe_Light", "A_Man_Idle_Breathe", "A_Man_MM_STL_Idle"},
    {"A_Man_NPC_WalkFwd_01", "A_Man_MM_N_Walk_Forward_v4", "", ""},
    {"A_Man_NPC_RunFwd_01", "A_Man_MM_N_Run_Forward_v4", "", ""},
    {"A_Man_NPC_SprintFwd_01", "A_Man_MM_N_Sprint_Forward_v6", "", ""}};

__declspec(dllexport) void mod_init(CoreAPI *a)
{
    memset(&M, 0, sizeof M);
    M.anim_state = -1;
    for (int k = 0; k < 4; k++) {
        for (int c = 0; c < 4 && !M.anim[k]; c++) {
            if (!k_cand[k][c][0]) continue;
            M.anim[k] = a->find_anim(k_cand[k][c]);
            if (M.anim[k]) snprintf(M.names[k], 96, "%s", k_cand[k][c]);
        }
        a->root_obj(M.anim[k]);
        a->log("M anim[%d] = %s %p", k, M.names[k][0] ? M.names[k] : "(none loaded)", M.anim[k]);
    }
    a->list_anims("A_Man_MM_", 400);
    a->list_anims("Fight", 40); a->list_anims("Battle", 40); a->list_anims("Guard", 40); a->list_anims("Stance", 40); a->list_anims("_Wpn_", 60); a->list_anims("NWpn", 60); a->list_anims("_1H_", 60); a->list_anims("M1H", 60);
    a->log("M module initialised (version 4: stances)");
}

__declspec(dllexport) void mod_shutdown(CoreAPI *a) { (void)a; }

static double clip_len(CoreAPI *a, const char *name) { uint8_t *s = a->clip_by_name(name, "-"); return s ? a->seq_length(s) : 0; }

static void set_rate(CoreAPI *a, float rate)
{
    int bits; memcpy(&bits, &rate, 4);
    a->call_int(a->puppet_mesh, "SkeletalMeshComponent", "SetPlayRate", 1, &bits, NULL);
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* loop a clip with a play rate scaled to the movement speed; returns 1 if the clip exists */
static int loop_clip(CoreAPI *a, const char *name, double rate)
{
    uint8_t *s = a->clip_by_name(name, "-");
    if (!s) return 0;
    a->play_seq(s, 1);
    set_rate(a, (float)rate);
    return 1;
}

/* "W=1" in the other player's equipment text means a weapon is in the hand (written by the equip module) */
static int remote_weapon(CoreAPI *a)
{
    const char *w = a->remote_extra ? strstr(a->remote_extra, "W=") : NULL;
    return w ? w[2] == '1' : -1;
}

__declspec(dllexport) void mod_update(CoreAPI *a)
{
    if (!a->have_remote) return;
    unsigned long now = GetTickCount();

    {   /* debug switch: build\anim_off.txt = this module does nothing (bisecting a crash) */
        static unsigned long off_t; static int off;
        if (now - off_t > 1000) { off_t = now; int v = GetFileAttributesA("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Gothic1R-coop\\native\\build\\anim_off.txt") != INVALID_FILE_ATTRIBUTES; if (v != off) { off = v; a->log("M anim module %s (anim_off.txt)", v ? "OFF" : "ON"); } }
        if (off) return;
    }

    /* the pose module hides this mesh while it drives the puppet by bones: playing clips on it is pointless then, and loading clips by path stalls the game */
    static unsigned long vis_t; static int hidden;
    if (now - vis_t > 400 && a->puppet_mesh) {
        vis_t = now;
        int vis = 1; a->call_fn(a->puppet_mesh, "SceneComponent", "IsVisible", 0, NULL, NULL, NULL, &vis, 4);
        int h = (vis & 255) == 0;
        if (h != hidden) { hidden = h; M.anim_state = -1; M.cur_clip[0] = 0; M.cur_act[0] = 0; a->log("M clip mesh %s: clips %s", h ? "hidden" : "visible", h ? "paused" : "resumed"); }
    }
    if (hidden) return;

    /* trial channel for picking clips by eye: build\try_clip.txt holds a clip name (first line); empty/missing = normal behaviour */
    static unsigned long try_t; static char try_cur[96];
    if (now - try_t > 400) {
        try_t = now;
        char nm[96] = ""; FILE *f = fopen("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Gothic1R-coop\\native\\build\\try_clip.txt", "r");
        if (f) { if (fgets(nm, sizeof nm, f)) { size_t n = strlen(nm); while (n && (nm[n-1] == '\n' || nm[n-1] == '\r' || nm[n-1] == ' ')) nm[--n] = 0; } fclose(f); }
        if (strcmp(nm, try_cur) != 0) {
            snprintf(try_cur, sizeof try_cur, "%s", nm);
            if (nm[0] == '!') { uint8_t *s = a->clip_by_name(nm + 1, "-"); if (s) a->freeze_at_end(s); a->log("T trying frozen-at-end clip '%s' -> %p", nm + 1, s); }
            else if (nm[0]) { set_rate(a, 1.0f); a->log("T trying clip '%s' -> %d", nm, a->play_named(nm, 1)); }
            else { M.anim_state = -1; M.cur_clip[0] = 0; }
        }
    }
    if (try_cur[0]) return;
    int acting = strcmp(a->action, "-") != 0;
    int airborne = a->movemode == 3;
    if (acting) M.jump_phase = 0;

    /* ---- jump: In clip -> loop -> landing. Phases end by clip length, a ledge fall (no upward speed) only loops ---- */
    if (!acting) {
        int gait = a->speed < 25 ? 0 : (a->speed < 300 ? 1 : (a->speed < 560 ? 2 : 3));
        const char *in_clip[4]   = {"A_Man_N_In_Place_Jump_In", "A_Man_N_Walk_Jump_In", "A_Man_N_Run_Jump_In", "A_Man_N_Sprint_Jump_In"};
        const char *land_clip[4] = {"A_Man_N_In_Place_Jump_Landing", "A_Man_N_Walk_Jump_Landing", "A_Man_N_Run_Jump_Landing", "A_Man_N_Sprint_Jump_Landing"};
        if ((M.jump_phase == 0 || M.jump_phase == 3) && airborne && a->vz > 120) {      /* also straight out of a landing: jump again at once */
            set_rate(a, 1.0f);
            a->play_named(in_clip[gait], 0); M.jump_phase = 1; M.cur_clip[0] = 0; M.jump_t = now; M.jump_len = clip_len(a, in_clip[gait]); M.anim_state = 99;
            a->log("J start vz=%.0f speed=%.0f -> %s (%.2fs)", a->vz, a->speed, in_clip[gait], M.jump_len);
        } else if (M.jump_phase == 0 && airborne && now - a->fall_since > 300) {
            set_rate(a, 1.0f);
            a->play_named("A_Man_N_Run_Jump_Loop", 1); M.jump_phase = 2; M.jump_t = now; M.anim_state = 99;
            a->log("J ledge fall -> loop");
        } else if (M.jump_phase == 1 && airborne && (now - M.jump_t) / 1000.0 > (M.jump_len > 0.1 ? M.jump_len - 0.08 : 0.45)) {
            a->play_named("A_Man_N_Run_Jump_Loop", 1); M.jump_phase = 2; M.jump_t = now;
        } else if ((M.jump_phase == 1 || M.jump_phase == 2) && !airborne) {
            a->play_named(land_clip[gait], 0); M.jump_phase = 3; M.jump_t = now; M.jump_len = clip_len(a, land_clip[gait]);
            a->log("J land -> %s (%.2fs)", land_clip[gait], M.jump_len);
        } else if (M.jump_phase == 3 && (now - M.jump_t) / 1000.0 > (M.jump_len > 0.1 ? (M.jump_len * 0.5 < 0.6 ? M.jump_len * 0.5 : 0.6) : 0.5)) {
            M.jump_phase = 0; M.anim_state = -1; M.cur_clip[0] = 0;
        }
    }

    /* ---- one-shot actions: attack, dodge, eat, draw, sheathe ... ---- */
    if (acting) {
        if (strcmp(a->action, M.cur_act) != 0) {
            uint8_t *seq = a->clip_by_name(a->action, a->action_pkg);
            set_rate(a, 1.0f);
            if (seq) { a->play_seq(seq, 0); a->log("A play action '%s'", a->action); }
            snprintf(M.cur_act, sizeof M.cur_act, "%s", a->action);
        }
    } else if (M.cur_act[0]) { M.cur_act[0] = 0; M.anim_state = -1; M.cur_clip[0] = 0; }

    /* the weapon state comes from the other player's equipment text; actions are only the fallback for older senders */
    int rw = remote_weapon(a);
    if (rw >= 0) M.weapon_drawn = rw;
    else if (acting) {
        if (strstr(a->action, "S1H_Tr_Draw")) M.weapon_drawn = 1;
        if (strstr(a->action, "S1H_Tr_Shelve")) M.weapon_drawn = 0;
    }
    if (M.weapon_drawn != M.last_weapon) { M.last_weapon = M.weapon_drawn; M.anim_state = -1; a->log("S remote weapon in hand -> %d", M.weapon_drawn); }
    if (a->crouched != M.last_crouch) { M.last_crouch = a->crouched; M.anim_state = -1; a->log("C remote crouch -> %d", a->crouched); }

    /* ---- locomotion. Crouched uses the stealth clips; a weapon in the hand does NOT change the clips (the combat pose looked like sitting),
     * the sword itself comes from the equip module. Clips switch with time hysteresis, and a clip is never restarted if it is already playing. ---- */
    if (!acting && M.jump_phase == 0) {
        /* gait with hysteresis: idle<->move at 20/45 u/s, walk<->run at 300 +-25, run<->sprint at 560 +-30; a change must persist for a while */
        double sp = a->speed;
        int raw;
        if (M.gait == 0) raw = sp > 45 ? (sp < 300 ? 1 : (sp < 560 ? 2 : 3)) : 0;
        else {
            if (sp < 20) raw = 0;
            else if (M.gait == 1) raw = sp > 325 ? (sp > 590 ? 3 : 2) : 1;
            else if (M.gait == 2) raw = sp < 275 ? 1 : (sp > 590 ? 3 : 2);
            else raw = sp < 530 ? (sp < 275 ? 1 : 2) : 3;
        }
        if (raw != M.gait) {
            if (raw != M.pending) { M.pending = raw; M.pending_t = now; }
            unsigned long dwell = raw == 0 ? 450 : (M.gait == 0 ? 160 : 250);
            if (now - M.pending_t >= dwell) { M.gait = raw; M.pending = raw; }
        } else M.pending = raw;

        int crouch = a->crouched;
        const char *clip; double rate = 1.0; int loop = 1;
        char normal_name[96];
        if (crouch) {
            clip = M.gait == 0 ? "A_Man_MM_STL_Idle" : "A_Man_MM_STL_Run_Snake_03";
            rate = M.gait == 0 ? 1.0 : clampd(sp / 160.0, 0.6, 1.8);
        } else if (M.weapon_drawn && M.gait == 0 && a->find_anim("A_Man_S1H_N_Idle_Pose")) {
            snprintf(normal_name, sizeof normal_name, "A_Man_S1H_N_Idle_Pose");        /* the game's own sword-drawn pose */
            clip = normal_name;
        } else if (M.weapon_drawn && M.gait == 0 && a->clip_by_name("A_Man_Training_M1H_Idle_01", "/Game/AnimLibrary/Humanoid/Man/Living/Training/MH1/A_Man_Training_M1H_Idle_01")) {
            snprintf(normal_name, sizeof normal_name, "A_Man_Training_M1H_Idle_01");   /* one-handed sword stance while standing */
            clip = normal_name;
        } else {
            int eff = M.gait;
            if (M.anim[eff] && !a->obj_alive(M.anim[eff], M.names[eff])) { M.anim[eff] = a->find_anim(M.names[eff]); a->root_obj(M.anim[eff]); }
            while (eff > 0 && !M.anim[eff]) eff--;
            snprintf(normal_name, sizeof normal_name, "%s", M.names[eff]);
            clip = normal_name;
        }
        if (clip[0]) {
            if (strcmp(clip, M.cur_clip) != 0 || M.anim_state == -1) {
                if (crouch ? loop_clip(a, clip, rate) : (a->play_named(clip, loop) ? (set_rate(a, 1.0f), 1) : 0)) {
                    a->log("L %s %s (speed %.0f, gait %d)", crouch ? "crouched" : "normal", clip, sp, M.gait);
                    snprintf(M.cur_clip, sizeof M.cur_clip, "%s", clip); M.anim_state = 0; M.rate_speed = sp;
                }
            } else if (crouch && M.gait != 0 && M.rate_speed > 0 && (sp > M.rate_speed * 1.3 || sp < M.rate_speed * 0.75)) {
                set_rate(a, (float)rate); M.rate_speed = sp;      /* same clip: only adapt the play rate */
            }
        }
    }
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) { (void)h; (void)r; (void)x; return TRUE; }
