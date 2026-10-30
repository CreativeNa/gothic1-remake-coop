/* npc module: shared world, step 1 (host-authoritative position mirror).
 *  - host (seat 0, or build\role.txt = "host"): lists the NPCs / monsters near either player and sends id, position, yaw and speed on the second blob channel
 *  - guest (seat 1, or build\role.txt = "guest"): switches the AI of these NPCs off (Controller.UnPossess) and places them from the host's data every frame;
 *    an NPC the host stops reporting for a few seconds gets its controller back
 * NPCs are matched by a hash of their object name (stable spawn-point names, the same in both copies of one save).
 * build\npc_off.txt switches the whole module off. Hot-reloadable. UNTESTED.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "coreapi.h"

#define MAXN 160
#define RADIUS 6000.0
#define BUILD "C:/Program Files (x86)/Steam/steamapps/common/Gothic1R-coop/native/build/"

#pragma pack(push, 1)
typedef struct { uint32_t id; float x, y, z; int16_t yaw; uint16_t speed; uint8_t flags, pad[3]; } Rec;     /* 24 bytes; yaw in 1/182.04 degrees */
#pragma pack(pop)

typedef struct { uint8_t *actor; uint32_t id; } Known;
typedef struct { uint8_t *actor, *ctrl; uint32_t id; Rec want; unsigned long last_seen; int frozen; double cur[3]; int have_cur; } Mirror;

static struct {
    Known known[2048]; int nknown; unsigned long last_scan, last_send, last_stat;
    struct { uint8_t *cls; int v; } cc[256]; int ncc;
    Mirror m[MAXN]; int nm; int last_seq;
    int role, off; int off_vel; unsigned long t_prev;
    int n_sent, n_applied, n_missing;
} N;

static uint32_t fnv(const char *s) { uint32_t h = 2166136261u; for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; } return h; }

static void release(CoreAPI *a, Mirror *m);

__declspec(dllexport) void mod_init(CoreAPI *a) { (void)a; memset(&N, 0, sizeof N); }
__declspec(dllexport) void mod_shutdown(CoreAPI *a) { for (int i = 0; i < N.nm; i++) release(a, &N.m[i]); }

static uint8_t *ptr_prop(CoreAPI *a, uint8_t *obj, const char *name)
{
    if (!obj) return NULL;
    int off = a->find_prop_offset(a->obj_class(obj), name);
    uint8_t *p = NULL; if (off >= 0) a->read_mem(obj + off, &p, 8);
    return p;
}

static int is_character_class(CoreAPI *a, uint8_t *cls)
{
    for (int i = 0; i < N.ncc; i++) if (N.cc[i].cls == cls) return N.cc[i].v;
    int v = 0; char nm[96]; uint8_t *c = cls;
    for (int d = 0; c && d < 14; d++) {
        a->obj_name(c, nm, sizeof nm);
        if (strcmp(nm, "AIAgentCharacter") == 0) { v = 1; break; }
        uint8_t *sup = NULL; a->read_mem(c + 0x40, &sup, 8); c = sup;
    }
    if (N.ncc < 256) { N.cc[N.ncc].cls = cls; N.cc[N.ncc].v = v; N.ncc++; }
    return v;
}

static void rescan(CoreAPI *a)
{
    N.nknown = 0; char nm[200];
    int n = a->num_objects();
    for (int i = 0; i < n && N.nknown < 2048; i++) {
        uint8_t *o = a->object_at(i); if (!o) continue;
        uint8_t *cls = a->obj_class(o); if (!cls || !is_character_class(a, cls)) continue;
        a->obj_name(o, nm, sizeof nm); if (strncmp(nm, "Default__", 9) == 0) continue;
        if (a->read_bool(o, "bActorIsBeingDestroyed") == 1) continue;
        N.known[N.nknown].actor = o; N.known[N.nknown].id = fnv(nm); N.nknown++;
    }
}

static int loc_of(CoreAPI *a, uint8_t *o, double *v) { double r[3] = { 0, 0, 0 }; if (!a->call_fn(o, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, r, 24)) return 0; v[0] = r[0]; v[1] = r[1]; v[2] = r[2]; return 1; }

static double speed_of(CoreAPI *a, uint8_t *o)
{
    uint8_t *cmc = ptr_prop(a, o, "CharacterMovement"); if (!cmc) return 0;
    if (!N.off_vel) N.off_vel = a->find_prop_offset(a->obj_class(cmc), "Velocity");
    double v[3] = { 0, 0, 0 }; if (N.off_vel > 0) a->read_mem(cmc + N.off_vel, v, 24);
    return sqrt(v[0] * v[0] + v[1] * v[1]);
}

static double yaw_of(CoreAPI *a, uint8_t *o) { double r[3] = { 0, 0, 0 }; a->call_fn(o, "Actor", "K2_GetActorRotation", 0, NULL, NULL, NULL, r, 24); return r[1]; }   /* FRotator: pitch, yaw, roll */

/* ---- host ---- */
static void host_tick(CoreAPI *a, unsigned long now)
{
    if (now - N.last_send < 100) return;
    N.last_send = now;
    double c1[3], c2[3]; int have2 = a->puppet && loc_of(a, a->puppet, c2);
    if (!loc_of(a, a->hero, c1)) return;
    uint8_t buf[2 + MAXN * sizeof(Rec)]; int cnt = 0;
    for (int i = 0; i < N.nknown && cnt < MAXN; i++) {
        uint8_t *o = N.known[i].actor; if (!a->obj_alive(o, NULL)) continue;
        double p[3]; if (!loc_of(a, o, p)) continue;
        double d1 = sqrt((p[0] - c1[0]) * (p[0] - c1[0]) + (p[1] - c1[1]) * (p[1] - c1[1]) + (p[2] - c1[2]) * (p[2] - c1[2]));
        double d2 = have2 ? sqrt((p[0] - c2[0]) * (p[0] - c2[0]) + (p[1] - c2[1]) * (p[1] - c2[1]) + (p[2] - c2[2]) * (p[2] - c2[2])) : 1e18;
        if (d1 > RADIUS && d2 > RADIUS) continue;
        Rec r; memset(&r, 0, sizeof r);
        r.id = N.known[i].id; r.x = (float)p[0]; r.y = (float)p[1]; r.z = (float)p[2];
        r.yaw = (int16_t)(yaw_of(a, o) * 182.04); r.speed = (uint16_t)speed_of(a, o);
        memcpy(buf + 2 + cnt * sizeof(Rec), &r, sizeof r); cnt++;
    }
    buf[0] = (uint8_t)(cnt & 255); buf[1] = (uint8_t)(cnt >> 8);
    a->set_local_blob2(buf, 2 + cnt * (int)sizeof(Rec));
    N.n_sent = cnt;
}

/* ---- guest ---- */
static Mirror *mirror_for(CoreAPI *a, uint32_t id)
{
    for (int i = 0; i < N.nm; i++) if (N.m[i].id == id) return &N.m[i];
    if (N.nm >= MAXN) return NULL;
    uint8_t *act = NULL; for (int i = 0; i < N.nknown; i++) if (N.known[i].id == id) { act = N.known[i].actor; break; }
    if (!act || !a->obj_alive(act, NULL)) return NULL;
    Mirror *m = &N.m[N.nm++]; memset(m, 0, sizeof *m); m->actor = act; m->id = id;
    return m;
}

static void freeze(CoreAPI *a, Mirror *m)
{
    m->ctrl = ptr_prop(a, m->actor, "Controller");
    if (m->ctrl) a->call_fn(m->ctrl, "Controller", "UnPossess", 0, NULL, NULL, NULL, NULL, 0);
    m->frozen = 1;
}
static void release(CoreAPI *a, Mirror *m)
{
    if (m->frozen && m->ctrl && m->actor && a->obj_alive(m->ctrl, NULL) && a->obj_alive(m->actor, NULL)) {
        const char *n[1] = { "InPawn" }; const void *v[1] = { &m->actor }; int s[1] = { 8 };
        a->call_fn(m->ctrl, "Controller", "Possess", 1, n, v, s, NULL, 0);
    }
    m->frozen = 0; m->have_cur = 0;
}

static void guest_tick(CoreAPI *a, unsigned long now)
{
    if (a->remote_blob2_seq != N.last_seq && a->remote_blob2_len >= 2) {          /* a new list from the host */
        N.last_seq = a->remote_blob2_seq;
        int cnt = a->remote_blob2[0] | (a->remote_blob2[1] << 8);
        if (cnt > MAXN) cnt = MAXN;
        if (2 + cnt * (int)sizeof(Rec) > a->remote_blob2_len) cnt = (a->remote_blob2_len - 2) / (int)sizeof(Rec);
        N.n_missing = 0;
        for (int k = 0; k < cnt; k++) {
            Rec r; memcpy(&r, a->remote_blob2 + 2 + k * sizeof(Rec), sizeof r);
            Mirror *m = mirror_for(a, r.id); if (!m) { N.n_missing++; continue; }
            m->want = r; m->last_seen = now;
            if (!m->frozen) freeze(a, m);
        }
    }
    double dt = N.t_prev ? (now - N.t_prev) / 1000.0 : 0.016; N.t_prev = now; if (dt > 0.1) dt = 0.1;
    int applied = 0;
    for (int i = 0; i < N.nm; i++) {
        Mirror *m = &N.m[i];
        if (!m->actor || !a->obj_alive(m->actor, NULL)) { m->frozen = 0; continue; }
        if (!m->frozen) continue;
        if (now - m->last_seen > 5000) { release(a, m); continue; }              /* the host no longer reports it (far away): give the AI back */
        double want[3] = { m->want.x, m->want.y, m->want.z };
        if (!m->have_cur) { memcpy(m->cur, want, sizeof want); m->have_cur = 1; }
        double dx = want[0] - m->cur[0], dy = want[1] - m->cur[1], dz = want[2] - m->cur[2], dist = sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > 400) memcpy(m->cur, want, sizeof want);                         /* teleport on big jumps */
        else { double k = 1.0 - exp(-dt * 12.0); m->cur[0] += dx * k; m->cur[1] += dy * k; m->cur[2] += dz * k; }
        uint8_t sw = 0, tp = 1;
        const char *nl[3] = { "NewLocation", "bSweep", "bTeleport" }; const void *vl[3] = { m->cur, &sw, &tp }; int sl[3] = { 24, 1, 1 };
        a->call_fn(m->actor, "Actor", "K2_SetActorLocation", 3, nl, vl, sl, NULL, 0);
        double rot[3] = { 0, m->want.yaw / 182.04, 0 };
        const char *nr[2] = { "NewRotation", "bTeleportPhysics" }; const void *vr[2] = { rot, &tp }; int sr[2] = { 24, 1 };
        a->call_fn(m->actor, "Actor", "K2_SetActorRotation", 2, nr, vr, sr, NULL, 0);
        /* let the animation blueprint see the speed: velocity along the facing */
        uint8_t *cmc = ptr_prop(a, m->actor, "CharacterMovement");
        if (cmc) {
            if (!N.off_vel) N.off_vel = a->find_prop_offset(a->obj_class(cmc), "Velocity");
            if (N.off_vel > 0) { double yr = rot[1] * 3.14159265358979 / 180.0, vel[3]; vel[0] = cos(yr) * m->want.speed; vel[1] = sin(yr) * m->want.speed; vel[2] = 0; a->write_mem(cmc + N.off_vel, vel, 24); }
        }
        applied++;
    }
    N.n_applied = applied;
}

static int role_of(void)
{
    char line[40] = ""; FILE *f = fopen(BUILD "role.txt", "r");
    if (f) { if (fgets(line, sizeof line, f)) { if (strstr(line, "guest")) { fclose(f); return 2; } if (strstr(line, "host")) { fclose(f); return 1; } } fclose(f); }
    const char *c = strstr(GetCommandLineA(), "-CoopSeat=");
    return (c && atoi(c + 10) > 0) ? 2 : 1;
}

__declspec(dllexport) void mod_update(CoreAPI *a)
{
    unsigned long now = GetTickCount();
    static unsigned long last_off;
    if (now - last_off > 1000) {
        last_off = now;
        int off = GetFileAttributesA(BUILD "npc_off.txt") != INVALID_FILE_ATTRIBUTES;
        if (off && !N.off) { for (int i = 0; i < N.nm; i++) release(a, &N.m[i]); N.nm = 0; a->log("N npc module OFF"); }
        if (!off && N.off) a->log("N npc module ON");
        N.off = off; N.role = role_of();
    }
    if (N.off || !a->have_remote || !a->hero) return;
    if (!N.last_scan || now - N.last_scan > 3000) { N.last_scan = now; rescan(a); }
    if (N.role == 1) host_tick(a, now); else guest_tick(a, now);
    if (now - N.last_stat > 5000) {
        N.last_stat = now;
        a->log("N %s: %d characters known, %s %d, missing %d", N.role == 1 ? "host" : "guest", N.nknown, N.role == 1 ? "sent" : "mirrored", N.role == 1 ? N.n_sent : N.n_applied, N.n_missing);
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) { (void)h; (void)r; (void)x; return TRUE; }
