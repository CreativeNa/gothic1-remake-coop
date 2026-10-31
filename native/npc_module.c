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

#define MAXN 80
#define MAXB 480
#define NFOCUS 3
#define FOCUS_R 2500.0
#define RADIUS 6000.0
#define BUILD "C:/Program Files (x86)/Steam/steamapps/common/Gothic1R-coop/native/build/"

#pragma pack(push, 1)
typedef struct { uint32_t id; float x, y, z; int16_t yaw; uint16_t speed; uint8_t flags; uint8_t pad; uint16_t anim; } Rec;   /* anim: 1 + index into the string table (the NPC's current montage clip), 0 = none */
     /* 24 bytes; yaw in 1/182.04 degrees */
#pragma pack(pop)

typedef struct { double q[4]; double t[3]; double pad0; double s[3]; double pad1; } Transform;   /* FTransform: FQuat, translation, scale */
typedef struct { uint8_t *comp, *mesh; int nb, nbn, bad; uint8_t fnm[MAXB][8]; char names[MAXB][48]; uint8_t data[MAXB * 12], applied[MAXB * 12]; int16_t sc[MAXB][3]; unsigned shash; int have, dirty, pending, napplied, checked, nparent; int16_t parent[MAXB]; unsigned long t, topt, titems; uint8_t orig_col; } GPose;   /* guest: bone-driven copy of one NPC */
typedef struct { uint8_t *actor, *mesh; int nb; uint8_t fnm[MAXB][8]; uint8_t last[MAXB * 12]; unsigned long next_t; int interval; } HPose;                                                                                              /* host: cached bone names of one focus NPC */
typedef struct { uint8_t *actor; uint32_t id; } Known;
typedef struct { uint8_t *actor, *ctrl; uint32_t id; Rec want; unsigned long last_seen, t_hp, t_wp; struct { uint8_t *item; char holster[64]; } wp[4]; int nwp; int frozen; double cur[3]; int have_cur; int unposs; char anim[320]; GPose *gp; } Mirror;

static struct {
    Known known[2048]; int nknown; unsigned long last_scan, last_send, last_stat;
    struct { uint8_t *cls; int v; } cc[16384]; int ncc; int scan_pos, scanning, scan_n; Known tmp[2048]; int ntmp;
    Mirror m[MAXN]; int nm; int last_seq;
    int role, off; int off_vel; unsigned long t_prev;
    int n_sent, n_applied, n_missing; uint8_t *last_hero; unsigned long t_hero;
    struct { uint32_t id; int8_t owner; } own[512]; int nown; int seat; unsigned long last_own;
    struct { uint8_t *outer, *set; } hs[600], hs_tmp[600]; int nhs, nhs_tmp;        /* AttributeSet_Health objects by their owner (an NPC's PlayerState) */
    HPose hp[NFOCUS]; uint8_t *focus[NFOCUS]; int nfocus; unsigned long last_focus; int rr, n_posed;
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

static int class_chain_has_agent(CoreAPI *a, uint8_t *cls)
{
    char nm[96]; uint8_t *c = cls;
    a->obj_name(cls, nm, sizeof nm); if (strcmp(nm, "AttributeSet_Health") == 0) return 2;
    for (int d = 0; c && d < 14; d++) {
        a->obj_name(c, nm, sizeof nm);
        if (strcmp(nm, "AIAgentCharacter") == 0) return 1;
        uint8_t *sup = NULL; a->read_mem(c + 0x40, &sup, 8); c = sup;
    }
    return 0;
}
/* hashed cache (the game has thousands of distinct classes); when the table is full a class is simply computed again */
static int is_character_class(CoreAPI *a, uint8_t *cls)
{
    unsigned h = (unsigned)(((uintptr_t)cls >> 4) * 2654435761u) & 16383;
    for (int p = 0; p < 64; p++) {
        unsigned i = (h + p) & 16383;
        if (N.cc[i].cls == cls) return N.cc[i].v;
        if (!N.cc[i].cls) { int v = class_chain_has_agent(a, cls); N.cc[i].cls = cls; N.cc[i].v = v; return v; }
    }
    return class_chain_has_agent(a, cls);
}

/* the object array has ~450000 entries: scan it in slices over many frames (one slice per call) instead of one long frame every few seconds */
static void rescan_slice(CoreAPI *a)
{
    char nm[200];
    int n = a->num_objects();
    if (!N.scanning) { N.scanning = 1; N.scan_pos = 0; N.ntmp = 0; N.nhs_tmp = 0; }
    int end = N.scan_pos + 25000; if (end > n) end = n;
    for (int i = N.scan_pos; i < end && N.ntmp < 2048; i++) {
        uint8_t *o = a->object_at(i); if (!o) continue;
        uint8_t *cls = a->obj_class(o); if (!cls) continue;
        int kind = is_character_class(a, cls); if (!kind) continue;
        if (kind == 2) { uint8_t *ou = NULL; a->read_mem(o + 0x20, &ou, 8); if (ou && N.nhs_tmp < 600) { N.hs_tmp[N.nhs_tmp].outer = ou; N.hs_tmp[N.nhs_tmp].set = o; N.nhs_tmp++; } continue; }
        a->obj_name(o, nm, sizeof nm); if (strncmp(nm, "Default__", 9) == 0) continue;
        if (a->read_bool(o, "bActorIsBeingDestroyed") == 1) continue;
        N.tmp[N.ntmp].actor = o; N.tmp[N.ntmp].id = fnv(nm); N.ntmp++;
    }
    N.scan_pos = end;
    if (end >= n || N.ntmp >= 2048) { memcpy(N.known, N.tmp, (size_t)N.ntmp * sizeof(Known)); N.nknown = N.ntmp; memcpy(N.hs, N.hs_tmp, (size_t)N.nhs_tmp * sizeof N.hs[0]); N.nhs = N.nhs_tmp; N.scanning = 0; N.last_scan = GetTickCount(); }
}

/* an actor we may safely call functions on: alive, not being destroyed (a cell that streams out leaves actors that are dying but still pass obj_alive) */
static int actor_ok(CoreAPI *a, uint8_t *o) { return o && a->obj_alive(o, NULL) && a->read_bool(o, "bActorIsBeingDestroyed") != 1; }
static int loc_of(CoreAPI *a, uint8_t *o, double *v) { double r[3] = { 0, 0, 0 }; if (!a->call_fn(o, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, r, 24)) return 0; v[0] = r[0]; v[1] = r[1]; v[2] = r[2]; return 1; }

static double speed_of(CoreAPI *a, uint8_t *o)
{
    uint8_t *cmc = ptr_prop(a, o, "CharacterMovement"); if (!cmc) return 0;
    if (!N.off_vel) N.off_vel = a->find_prop_offset(a->obj_class(cmc), "Velocity");
    double v[3] = { 0, 0, 0 }; if (N.off_vel > 0) a->read_mem(cmc + N.off_vel, v, 24);
    return sqrt(v[0] * v[0] + v[1] * v[1]);
}

static double yaw_of(CoreAPI *a, uint8_t *o) { double r[3] = { 0, 0, 0 }; a->call_fn(o, "Actor", "K2_GetActorRotation", 0, NULL, NULL, NULL, r, 24); return r[1]; }   /* FRotator: pitch, yaw, roll */



/* the NPC's health: its PlayerState owns the AttributeSet_Health (Health = BaseValue at +72, CurrentValue at +76 of the set) */
static uint8_t *health_set(CoreAPI *a, uint8_t *actor)
{
    uint8_t *pst = ptr_prop(a, actor, "PlayerState"); if (!pst) return NULL;
    for (int i = 0; i < N.nhs; i++) if (N.hs[i].outer == pst) return a->obj_alive(N.hs[i].set, NULL) ? N.hs[i].set : NULL;
    return NULL;
}
static int npc_health(CoreAPI *a, uint8_t *actor) { uint8_t *hs = health_set(a, actor); if (!hs) return -1; float h = 0; a->read_mem(hs + 64 + 12, &h, 4); return (int)(h + 0.5f); }
static void npc_set_health(CoreAPI *a, uint8_t *actor, int v) { uint8_t *hs = health_set(a, actor); if (!hs) return; float f = (float)v; a->write_mem(hs + 64 + 8, &f, 4); a->write_mem(hs + 64 + 12, &f, 4); }

/* the clip of the NPC's active montage: name and package path, like the hero's action */
static int clip_of(CoreAPI *a, uint8_t *o, char *name, int ncap, char *pkg, int pcap)
{
    name[0] = pkg[0] = 0;
    uint8_t *mesh = ptr_prop(a, o, "Mesh"); if (!mesh) return 0;
    uint8_t *ai = ptr_prop(a, mesh, "AnimScriptInstance"); if (!ai) return 0;
    uint8_t *mont = NULL; if (!a->call_fn(ai, "AnimInstance", "GetCurrentActiveMontage", 0, NULL, NULL, NULL, &mont, 8) || !mont) return 0;
    int ost = a->find_prop_offset(a->obj_class(mont), "SlotAnimTracks"); if (ost < 0) return 0;
    uint8_t *tracks = NULL; int32_t nt = 0; a->read_mem(mont + ost, &tracks, 8); a->read_mem(mont + ost + 8, &nt, 4);
    if (!tracks || nt <= 0) return 0;
    /* every slot track (FSlotAnimationTrack, 24 bytes) and every segment (FAnimSegment, 32 bytes): the first track can be a voice / facial clip (GVL_*), which on the whole mesh gives a T-pose */
    for (int t = 0; t < nt && t < 6; t++) {
        uint8_t *segs = NULL; int32_t ns = 0; a->read_mem(tracks + (size_t)t * 24 + 8, &segs, 8); a->read_mem(tracks + (size_t)t * 24 + 16, &ns, 4);
        if (!segs || ns <= 0) continue;
        for (int k = 0; k < ns && k < 4; k++) {
            uint8_t *ref = NULL; a->read_mem(segs + (size_t)k * 32, &ref, 8); if (!ref || !a->obj_alive(ref, NULL)) continue;
            char tmp[120]; a->obj_name(ref, tmp, sizeof tmp);
            uint8_t *pk = NULL; a->read_mem(ref + 0x20, &pk, 8); char pn[300] = ""; if (pk) a->obj_name(pk, pn, sizeof pn);
            if (strncmp(tmp, "FA_", 3) == 0 || strncmp(tmp, "GVL_", 4) == 0 || strstr(pn, "DialogFacials") || strstr(pn, "_Voicetypes")) continue;   /* FA_* are additive fight clips (hits, attacks, defeated loops) and GVL_* voice / facial clips: played alone on the whole mesh they give a T-pose */
            snprintf(name, ncap, "%s", tmp); snprintf(pkg, pcap, "%s", pn);
            return 1;
        }
    }
    return 0;
}


static int16_t q16(double v, double k) { double r = floor(v * k + 0.5); if (r > 32767) r = 32767; if (r < -32767) r = -32767; return (int16_t)r; }

static void set_visible(CoreAPI *a, uint8_t *comp, int vis)
{
    uint8_t v = (uint8_t)vis, prop = 0;
    const char *names[2] = { "bNewVisibility", "bPropagateToChildren" }; const void *vals[2] = { &v, &prop }; int sizes[2] = { 1, 1 };
    a->call_fn(comp, "SceneComponent", "SetVisibility", 2, names, vals, sizes, NULL, 0);
}

/* ---- host: bone pose of one focus NPC (component space, 12 bytes per bone, like the hero's pose blob) ---- */
static HPose *hpose_for(uint8_t *actor)
{
    for (int i = 0; i < NFOCUS; i++) if (N.hp[i].actor == actor) return &N.hp[i];
    int slot = 0;
    for (int i = 0; i < NFOCUS; i++) { int used = 0; for (int k = 0; k < N.nfocus; k++) if (N.focus[k] == N.hp[i].actor) used = 1; if (!used) { slot = i; break; } }
    HPose *h = &N.hp[slot]; memset(h, 0, sizeof *h); h->actor = actor;
    return h;
}
static int read_pose(CoreAPI *a, uint8_t *actor, uint8_t *out, int *nb_out, int16_t (*sp)[4], int *nsp)
{
    uint8_t *mesh = ptr_prop(a, actor, "Mesh"); if (!mesh) return 0;
    HPose *h = hpose_for(actor);
    if (h->mesh != mesh || h->nb <= 0) {
        int n = 0; a->call_fn(mesh, "SkinnedMeshComponent", "GetNumBones", 0, NULL, NULL, NULL, &n, 4);
        if (n <= 0 || n > MAXB) return 0;
        for (int i = 0; i < n; i++) { const char *nn[1] = { "BoneIndex" }; const void *vv[1] = { &i }; int ss[1] = { 4 }; a->call_fn(mesh, "SkinnedMeshComponent", "GetBoneName", 1, nn, vv, ss, h->fnm[i], 8); }
        h->mesh = mesh; h->nb = n;
    }
    *nsp = 0;
    for (int i = 0; i < h->nb; i++) {
        Transform tr; memset(&tr, 0, sizeof tr); tr.q[3] = 1; tr.s[0] = tr.s[1] = tr.s[2] = 1; uint8_t space = 2;   /* RTS_Component */
        const char *names[2] = { "InSocketName", "TransformSpace" }; const void *vals[2] = { h->fnm[i], &space }; int sizes[2] = { -2, 1 };
        a->call_fn(mesh, "SceneComponent", "GetSocketTransform", 2, names, vals, sizes, &tr, (int)sizeof tr);
        int bad = 0; for (int c = 0; c < 4; c++) if (!isfinite(tr.q[c])) bad = 1; for (int c = 0; c < 3; c++) if (!isfinite(tr.t[c]) || fabs(tr.t[c]) > 600) bad = 1;
        if (bad) { memset(&tr, 0, sizeof tr); tr.q[3] = 1; tr.s[0] = tr.s[1] = tr.s[2] = 1; }
        for (int c = 0; c < 3; c++) if (!isfinite(tr.s[c]) || tr.s[c] < 0.01 || tr.s[c] > 30) tr.s[c] = 1;
        double nq = sqrt(tr.q[0] * tr.q[0] + tr.q[1] * tr.q[1] + tr.q[2] * tr.q[2] + tr.q[3] * tr.q[3]); if (nq < 1e-6) { tr.q[0] = tr.q[1] = tr.q[2] = 0; tr.q[3] = 1; nq = 1; }
        double sg = tr.q[3] < 0 ? -1.0 / nq : 1.0 / nq;
        int16_t v[6] = { q16(tr.q[0] * sg, 32767), q16(tr.q[1] * sg, 32767), q16(tr.q[2] * sg, 32767), q16(tr.t[0], 50), q16(tr.t[1], 50), q16(tr.t[2], 50) };
        memcpy(out + i * 12, v, 12);
        if (fabs(tr.s[0] - 1) > 0.002 || fabs(tr.s[1] - 1) > 0.002 || fabs(tr.s[2] - 1) > 0.002) {      /* bones with a scale (body proportions: broad shoulders, big hands ...): without it the skin is pulled into spikes */
            if (*nsp < 160) { sp[*nsp][0] = (int16_t)i; sp[*nsp][1] = q16(tr.s[0], 1000); sp[*nsp][2] = q16(tr.s[1], 1000); sp[*nsp][3] = q16(tr.s[2], 1000); (*nsp)++; }
        }
    }
    {   /* diagnostics: log an NPC whenever the number of bones the host could not report changes (the level of detail switches with the distance to a player) */
        static uint8_t *seen[16]; static int lastz[16]; static int ns;
        int zero = 0; char first[400] = "";
        for (int i = 1; i < h->nb; i++) { int16_t *v = (int16_t *)(out + i * 12); if (!(v[0] | v[1] | v[2] | v[3] | v[4] | v[5])) { zero++; if (strlen(first) < 300) { char nm[64]; a->fname_to_str(h->fnm[i], nm, sizeof nm); if (strncmp(nm, "face_", 5) != 0) { strcat(first, nm); strcat(first, " "); } } } }
        int idx = -1; for (int q = 0; q < ns; q++) if (seen[q] == actor) idx = q;
        if (idx < 0 && ns < 16) { idx = ns++; seen[idx] = actor; lastz[idx] = -1; }
        if (idx >= 0 && lastz[idx] != zero) {
            lastz[idx] = zero; char an[160]; a->obj_name(actor, an, sizeof an);
            a->log("N host: pose of %s: %d bones, %d empty (%s), %d scaled bones", an, h->nb, zero, first, *nsp);
        }
    }
    *nb_out = h->nb; return 1;
}
/* ---- ownership: the copy whose hero is nearer to an NPC simulates it (AI, hits, health); the other copy mirrors it.
 * Everything starts with seat 0. A copy takes an NPC over when its hero is clearly nearer (HYST), and gives it up when it sees the other copy's claim for it. ---- */
#define HYST 600.0
static int own_get(uint32_t id) { for (int i = 0; i < N.nown; i++) if (N.own[i].id == id) return N.own[i].owner; return 0; }
static void own_set(CoreAPI *a, uint32_t id, int owner, const char *why)
{
    for (int i = 0; i < N.nown; i++) if (N.own[i].id == id) { if (N.own[i].owner != owner) { N.own[i].owner = (int8_t)owner; a->log("N owner of %08x -> seat %d (%s)", id, owner, why); } return; }
    if (owner == 0) return;                                                 /* the default needs no entry */
    if (N.nown < 512) { N.own[N.nown].id = id; N.own[N.nown].owner = (int8_t)owner; N.nown++; a->log("N owner of %08x -> seat %d (%s)", id, owner, why); }
}
static double dist3(const double *a, const double *b) { return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }


/* the characters closest to either player (a few at most): they are bone-driven on the guest */
static void pick_focus(CoreAPI *a, const double *c1, int have2, const double *c2)
{
    N.nfocus = 0; double best[NFOCUS];
    for (int i = 0; i < N.nknown; i++) {
        uint8_t *o = N.known[i].actor; if (!actor_ok(a, o)) continue;
        double p[3]; if (!loc_of(a, o, p)) { N.known[i].actor = NULL; continue; }
        double d1 = sqrt((p[0] - c1[0]) * (p[0] - c1[0]) + (p[1] - c1[1]) * (p[1] - c1[1]) + (p[2] - c1[2]) * (p[2] - c1[2]));
        double d2 = have2 ? sqrt((p[0] - c2[0]) * (p[0] - c2[0]) + (p[1] - c2[1]) * (p[1] - c2[1]) + (p[2] - c2[2]) * (p[2] - c2[2])) : 1e18;
        double d = d1 < d2 ? d1 : d2; if (d > FOCUS_R) continue;
        if (own_get(N.known[i].id) != N.seat) continue;                          /* the other copy simulates this one and sends its bones */
        {   /* bone-driven copying only for NPCs that are defeated (lying, knocked out): a clip cannot show that pose. Everyone else keeps the normal mesh and animation: the bone-driven mesh of an NPC
             * that is sitting, standing or walking showed stretched arms and heads once a player came close. */
            uint8_t df = 0, dd = 0;
            if (!a->call_fn(o, "GothicCharacter", "IsDefeated", 0, NULL, NULL, NULL, &df, 1)) { N.known[i].actor = NULL; continue; }     /* the call faulted: the actor is gone (streamed out), forget it */
            a->call_fn(o, "GothicCharacter", "IsDead", 0, NULL, NULL, NULL, &dd, 1);
            if (!df && !dd) continue;
        }
        int pos = N.nfocus; while (pos > 0 && best[pos - 1] > d) pos--;
        if (pos >= NFOCUS) continue;
        for (int k = (N.nfocus < NFOCUS ? N.nfocus : NFOCUS - 1); k > pos; k--) { best[k] = best[k - 1]; N.focus[k] = N.focus[k - 1]; }
        best[pos] = d; N.focus[pos] = o; if (N.nfocus < NFOCUS) N.nfocus++;
    }
}

/* ---- host ---- */
static void host_tick(CoreAPI *a, unsigned long now)
{
    if (now - N.last_send < 100) return;
    N.last_send = now;
    double c1[3], c2[3]; int have2 = a->puppet && loc_of(a, a->puppet, c2);
    if (!loc_of(a, a->hero, c1)) return;
    if (now - N.last_own > 500) {                                            /* take over the NPCs my hero is clearly nearer to */
        N.last_own = now;
        for (int i = 0; i < N.nknown && have2; i++) {
            uint8_t *o = N.known[i].actor; if (!actor_ok(a, o)) continue;
            double p[3]; if (!loc_of(a, o, p)) { N.known[i].actor = NULL; continue; }
            double dm = dist3(p, c1), dother = dist3(p, c2);
            if (own_get(N.known[i].id) != N.seat && dm < RADIUS && dm + HYST < dother) {
                own_set(a, N.known[i].id, N.seat, "my hero is nearer");
                for (int k = 0; k < N.nm; k++) if (N.m[k].id == N.known[i].id && N.m[k].frozen) release(a, &N.m[k]);
            }
        }
    }
    if (now - N.last_focus > 500) { N.last_focus = now; pick_focus(a, c1, have2, c2); }
    static uint8_t buf[9000]; int cnt = 0, nstr = 0, strpos = 0;
    static char strs[40][320]; static char strtab[6000];
    static Rec recs[MAXN];
    for (int i = 0; i < N.nknown && cnt < MAXN; i++) {
        uint8_t *o = N.known[i].actor; if (!actor_ok(a, o)) continue;
        double p[3]; if (!loc_of(a, o, p)) continue;
        double d1 = sqrt((p[0] - c1[0]) * (p[0] - c1[0]) + (p[1] - c1[1]) * (p[1] - c1[1]) + (p[2] - c1[2]) * (p[2] - c1[2]));
        double d2 = have2 ? sqrt((p[0] - c2[0]) * (p[0] - c2[0]) + (p[1] - c2[1]) * (p[1] - c2[1]) + (p[2] - c2[2]) * (p[2] - c2[2])) : 1e18;
        if (d1 > RADIUS && d2 > RADIUS) continue;
        if (own_get(N.known[i].id) != N.seat) continue;                           /* only the NPCs this copy simulates */
        Rec r; memset(&r, 0, sizeof r);
        r.id = N.known[i].id; r.x = (float)p[0]; r.y = (float)p[1]; r.z = (float)p[2];
        r.yaw = (int16_t)(yaw_of(a, o) * 182.04); r.speed = (uint16_t)speed_of(a, o);
        {   uint8_t df = 0, dd = 0; int hp = npc_health(a, o);
            if (!a->call_fn(o, "GothicCharacter", "IsDefeated", 0, NULL, NULL, NULL, &df, 1)) { N.known[i].actor = NULL; continue; }
            a->call_fn(o, "GothicCharacter", "IsDead", 0, NULL, NULL, NULL, &dd, 1);
            { uint8_t *carry = ptr_prop(a, o, "m_CarryComponent"), *eq = ptr_prop(a, carry, "m_EquippedItem"); char ec[96] = "";
              if (eq) a->obj_name(a->obj_class(eq), ec, sizeof ec);
              if (strstr(ec, "ItMw")) r.flags |= 4; else if (strstr(ec, "ItRw")) r.flags |= 8; }
            r.flags |= (uint8_t)((df & 1) | ((dd & 1) << 1)); r.pad = (uint8_t)(hp < 0 ? 255 : (hp > 254 ? 254 : hp)); }
        char cn[120], cp[200];
        if (clip_of(a, o, cn, sizeof cn, cp, sizeof cp)) {
            { static char lastc[8][120]; static int nl; int seen = 0; for (int q = 0; q < nl; q++) if (!strcmp(lastc[q], cn)) seen = 1; if (!seen && nl < 8) { snprintf(lastc[nl++], 120, "%s", cn); a->log("N host: NPC clip %s (%s)", cn, cp); } }
            char both[320]; snprintf(both, sizeof both, "%s|%s", cn, cp);
            int k = -1; for (int j = 0; j < nstr; j++) if (strcmp(strs[j], both) == 0) k = j;
            if (k < 0 && nstr < 40 && strpos + (int)strlen(both) + 1 < (int)sizeof strtab) { k = nstr; snprintf(strs[nstr++], 320, "%s", both); memcpy(strtab + strpos, both, strlen(both) + 1); strpos += (int)strlen(both) + 1; }
            if (k >= 0) r.anim = (uint16_t)(k + 1);
        }
        recs[cnt++] = r;
    }
    buf[0] = (uint8_t)(cnt & 255); buf[1] = (uint8_t)(cnt >> 8); buf[2] = (uint8_t)nstr; buf[3] = (uint8_t)(strpos & 255); buf[4] = (uint8_t)(strpos >> 8);
    memcpy(buf + 5, recs, (size_t)cnt * sizeof(Rec)); memcpy(buf + 5 + cnt * sizeof(Rec), strtab, (size_t)strpos);
    int blen = 5 + cnt * (int)sizeof(Rec) + strpos;
    buf[blen++] = 0;                                                         /* pose section: 0 = none, 1 = id (4), bone count (2), 12 bytes per bone */
    static unsigned long t_nb; static int nobones; if (now - t_nb > 1000) { t_nb = now; nobones = GetFileAttributesA(BUILD "npc_nobones.txt") != INVALID_FILE_ATTRIBUTES; }
    if (!nobones && N.nfocus > 0 && blen + 6 + MAXB * 12 <= (int)sizeof buf) {
        uint8_t *o = NULL; HPose *hp = NULL;                                      /* the next focus NPC whose turn has come (a pose that did not change is read less and less often) */
        for (int k = 0; k < N.nfocus && !o; k++) { uint8_t *c = N.focus[N.rr++ % N.nfocus]; HPose *h = hpose_for(c); if (now >= h->next_t) { o = c; hp = h; } }
        static uint8_t pb[MAXB * 12]; int nb = 0; static int16_t spb[160][4]; int nsp = 0;
        uint32_t id = 0; if (o) for (int i = 0; i < N.nknown; i++) if (N.known[i].actor == o) id = N.known[i].id;
        if (id && read_pose(a, o, pb, &nb, spb, &nsp) && blen + 6 + nb * 12 + 2 + nsp * 8 <= (int)sizeof buf) {
            int same = hp->nb == nb && memcmp(hp->last, pb, (size_t)nb * 12) == 0;
            memcpy(hp->last, pb, (size_t)nb * 12);
            hp->interval = same ? (hp->interval < 150 ? 300 : (hp->interval * 2 > 2500 ? 2500 : hp->interval * 2)) : 150;
            hp->next_t = now + (unsigned long)hp->interval;
            buf[blen - 1] = 1; memcpy(buf + blen, &id, 4); buf[blen + 4] = (uint8_t)(nb & 255); buf[blen + 5] = (uint8_t)(nb >> 8); memcpy(buf + blen + 6, pb, (size_t)nb * 12); blen += 6 + nb * 12;
            buf[blen] = (uint8_t)(nsp & 255); buf[blen + 1] = (uint8_t)(nsp >> 8); memcpy(buf + blen + 2, spb, (size_t)nsp * 8); blen += 2 + nsp * 8;
        }
    }
    a->set_local_blob2(buf, blen);
    N.n_sent = cnt;
}

/* ---- guest ---- */
static Mirror *mirror_for(CoreAPI *a, uint32_t id)
{
    for (int i = 0; i < N.nm; i++) if (N.m[i].id == id) {
        Mirror *m = &N.m[i];
        if (m->actor && a->obj_alive(m->actor, NULL)) return m;
        uint8_t *act = NULL; for (int k = 0; k < N.nknown; k++) if (N.known[k].id == id) { act = N.known[k].actor; break; }   /* the NPC was destroyed and re-created (streaming): find the new actor, never touch the dead one */
        if (!act || !a->obj_alive(act, NULL)) { m->actor = NULL; m->frozen = 0; return NULL; }
        m->actor = act; m->frozen = 0; m->ctrl = NULL; m->gp = NULL; m->have_cur = 0; m->anim[0] = 0;
        return m;
    }
    if (N.nm >= MAXN) return NULL;
    uint8_t *act = NULL; for (int i = 0; i < N.nknown; i++) if (N.known[i].id == id) { act = N.known[i].actor; break; }
    if (!act || !a->obj_alive(act, NULL)) return NULL;
    Mirror *m = &N.m[N.nm++]; memset(m, 0, sizeof *m); m->actor = act; m->id = id;
    return m;
}


/* play the host's montage clip on the frozen NPC (single-node animation), or give the animation blueprint back when the host plays none */
static void set_anim(CoreAPI *a, Mirror *m)
{
    return;   /* NPC clips are NOT played any more: PlayAnimation switches the NPC's mesh to a single-node animation instance, and the game's own animation component then crashed (null read
               * at exe+0x5ae259a, AnimSingleNodeInstance in rcx) when the guest's AI of that NPC ran an equip ability. Non-defeated NPCs are placed and given their speed only; defeated ones are bone-driven. */
    if (m->gp && m->gp->comp) return;                                         /* bone-driven NPCs do not play clips */
    uint8_t *mesh = ptr_prop(a, m->actor, "Mesh"); if (!mesh) return;
    if (!m->anim[0]) {
        uint8_t mode = 0; const char *n[1] = { "InAnimationMode" }; const void *v[1] = { &mode }; int s[1] = { 1 };
        a->call_fn(mesh, "SkeletalMeshComponent", "SetAnimationMode", 1, n, v, s, NULL, 0);
        return;
    }
    char nm[320], pk[320] = "-"; snprintf(nm, sizeof nm, "%s", m->anim);
    char *bar = strchr(nm, '|'); if (bar) { *bar = 0; snprintf(pk, sizeof pk, "%s", bar + 1); }
    uint8_t *seq = a->clip_by_name(nm, pk);
    if (!seq) { a->log("N guest: clip %s not found (pkg %s)", nm, pk); return; }
    uint8_t lp = 1; const char *n[2] = { "NewAnimToPlay", "bLooping" }; const void *v[2] = { &seq, &lp }; int s[2] = { 8, 1 };
    int ok = a->call_fn(mesh, "SkeletalMeshComponent", "PlayAnimation", 2, n, v, s, NULL, 0);
    a->log("N guest: play %s -> ok=%d", nm, ok);
}


/* The game's BFGTickOptimizerSystem crashed (null read at exe+0x363dfff, address 0xd50) with a bone-driven puppet; the same would happen with a bone-driven NPC:
 * opt the actor out of the optimizer before a poseable component is added to it, and again every few seconds. */
static void tickopt_off(CoreAPI *a, uint8_t *actor)
{
    static uint8_t *sys; static unsigned long t_find;
    if (sys && !a->obj_alive(sys, NULL)) sys = NULL;
    if (!sys && GetTickCount() - t_find > 3000) {
        t_find = GetTickCount(); char nm[160], cn[96]; int n = a->num_objects();
        for (int i = 0; i < n && !sys; i++) {
            uint8_t *o = a->object_at(i); if (!o) continue;
            a->obj_name(a->obj_class(o), cn, sizeof cn); if (strcmp(cn, "BFGTickOptimizerSystem") != 0) continue;
            a->obj_name(o, nm, sizeof nm); if (strncmp(nm, "Default__", 9) != 0) sys = o;
        }
    }
    if (!sys) return;
    uint8_t off = 0; uint8_t *act = actor;
    const char *names[2] = { "Actor", "OptimizationEnabled" }; const void *vals[2] = { &act, &off }; int sizes[2] = { 8, 1 };
    a->call_fn(sys, "BFGTickOptimizerSystem", "SetTickOptimizationRuntime_Enabled", 2, names, vals, sizes, NULL, 0);
}

/* Weapons, quiver, bow ... are separate actors attached to the NPC's original mesh sockets. While the NPC is bone-driven that mesh stays hidden in the animation blueprint's pose, so
 * the items would float where the NPC used to stand: move every item that hangs on `from` to the same socket of `to` (and back when the bone-driven mesh goes away). */
static int reparent_items(CoreAPI *a, uint8_t *from, uint8_t *to, uint8_t *owner_actor)
{
    int oat = a->find_prop_offset(a->obj_class(from), "AttachChildren"); if (oat < 0) return 0;
    uint8_t *kids = NULL; int nk = 0; a->read_mem(from + oat, &kids, 8); a->read_mem(from + oat + 8, &nk, 4);
    if (!kids || nk <= 0 || nk > 100) return 0;
    uint8_t *list[100]; int n = 0;
    for (int i = 0; i < nk; i++) { uint8_t *comp = NULL; a->read_mem(kids + (size_t)i * 8, &comp, 8); if (comp && comp != to) list[n++] = comp; }   /* copy first: attaching changes the array */
    int moved = 0;
    for (int i = 0; i < n; i++) {
        uint8_t *comp = list[i], *o = NULL; a->read_mem(comp + 0x20, &o, 8);                 /* a component's outer is its actor */
        if (!o || o == owner_actor || !a->obj_alive(o, NULL)) continue;
        if (ptr_prop(a, o, "RootComponent") != comp) continue;                                 /* only whole item actors, not parts of the NPC itself */
        int os = a->find_prop_offset(a->obj_class(comp), "AttachSocketName"); char sock[96] = "None";
        if (os >= 0) a->fname_to_str(comp + os, sock, sizeof sock);
        uint8_t snap = 2, weld = 0; uint8_t *par = to;
        const char *n3[6] = { "Parent", "SocketName", "LocationRule", "RotationRule", "ScaleRule", "bWeldSimulatedBodies" };
        const void *v3[6] = { &par, sock, &snap, &snap, &snap, &weld }; int s3[6] = { 8, -1, 1, 1, 1, 1 };
        a->call_fn(comp, "SceneComponent", "K2_AttachToComponent", 6, n3, v3, s3, NULL, 0);
        moved++;
    }
    return moved;
}

static char SKIP[300]; static unsigned long skip_t;
static int npc_skip_has(const char *name)
{
    unsigned long now = GetTickCount();
    if (now - skip_t > 1500) { skip_t = now; SKIP[0] = 0; FILE *f = fopen(BUILD "npc_skip.txt", "r"); if (f) { if (fgets(SKIP, sizeof SKIP, f)) { char *e = SKIP + strlen(SKIP); while (e > SKIP && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0; } fclose(f); } }
    {   /* Bones that only exist in the close-up level of detail (face, eyes, jaw, armour plates, ribs, scapula, radius twist, belt, bags): the host sends them only while a player stands near the NPC, and applied in component space they pull the skin into spikes.
         * They stay in the reference pose, exactly like the same bones of the hero puppet. */
        static const char *def[] = { "face_", "eye_", "Jaw", "tongue", "helmet", "radius_", "scapula_", "shoulderguard", "lowerarmarmor", "chestpiece", "rib_", "belt_", "bag_", "teeth", "brow", "lip" };
        for (size_t k = 0; k < sizeof def / sizeof def[0]; k++) if (strstr(name, def[k])) return 1;
    }
    if (!SKIP[0]) return 0;
    char tmp[300]; snprintf(tmp, sizeof tmp, "%s", SKIP);
    for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) if (*t && strstr(name, t)) return 1;
    return 0;
}

typedef struct { double r[9]; double t[3]; } Xf;                       /* p' = R p + t */
static void q2m(const double *q, double *m)
{
    double x = q[0], y = q[1], z = q[2], w = q[3], n = sqrt(x * x + y * y + z * z + w * w); if (n < 1e-9) { x = y = z = 0; w = 1; n = 1; }
    x /= n; y /= n; z /= n; w /= n;
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - z * w);     m[2] = 2 * (x * z + y * w);
    m[3] = 2 * (x * y + z * w);     m[4] = 1 - 2 * (x * x + z * z); m[5] = 2 * (y * z - x * w);
    m[6] = 2 * (x * z - y * w);     m[7] = 2 * (y * z + x * w);     m[8] = 1 - 2 * (x * x + y * y);
}
static void m2q(const double *m, double *q)
{
    double tr = m[0] + m[4] + m[8], x, y, z, w;
    if (tr > 0) { double sq = sqrt(tr + 1.0) * 2; w = 0.25 * sq; x = (m[7] - m[5]) / sq; y = (m[2] - m[6]) / sq; z = (m[3] - m[1]) / sq; }
    else if (m[0] > m[4] && m[0] > m[8]) { double sq = sqrt(1.0 + m[0] - m[4] - m[8]) * 2; w = (m[7] - m[5]) / sq; x = 0.25 * sq; y = (m[1] + m[3]) / sq; z = (m[2] + m[6]) / sq; }
    else if (m[4] > m[8]) { double sq = sqrt(1.0 + m[4] - m[0] - m[8]) * 2; w = (m[2] - m[6]) / sq; x = (m[1] + m[3]) / sq; y = 0.25 * sq; z = (m[5] + m[7]) / sq; }
    else { double sq = sqrt(1.0 + m[8] - m[0] - m[4]) * 2; w = (m[3] - m[1]) / sq; x = (m[2] + m[6]) / sq; y = (m[5] + m[7]) / sq; z = 0.25 * sq; }
    double n = sqrt(x * x + y * y + z * z + w * w); if (n < 1e-9) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    q[0] = x / n; q[1] = y / n; q[2] = z / n; q[3] = w / n;
}
static Xf xf_mul(const Xf *a, const Xf *b)                              /* a * b: a first, then b */
{
    Xf o; for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o.r[i * 3 + j] = b->r[i * 3] * a->r[j] + b->r[i * 3 + 1] * a->r[3 + j] + b->r[i * 3 + 2] * a->r[6 + j];
    for (int i = 0; i < 3; i++) o.t[i] = b->r[i * 3] * a->t[0] + b->r[i * 3 + 1] * a->t[1] + b->r[i * 3 + 2] * a->t[2] + b->t[i];
    return o;
}
static Xf xf_inv(const Xf *a)
{
    Xf o; for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o.r[i * 3 + j] = a->r[j * 3 + i];
    for (int i = 0; i < 3; i++) o.t[i] = -(o.r[i * 3] * a->t[0] + o.r[i * 3 + 1] * a->t[1] + o.r[i * 3 + 2] * a->t[2]);
    return o;
}
static Xf xf_from(const double *q, const double *t) { Xf o; q2m(q, o.r); o.t[0] = t[0]; o.t[1] = t[1]; o.t[2] = t[2]; return o; }

/* ---- guest: bone-driven NPC (a PoseableMeshComponent with the NPC's own mesh asset, the original mesh hidden) ---- */
static void gp_drop(CoreAPI *a, Mirror *m)
{
    if (!m->gp) return;
    GPose *g = m->gp;
    if (g->comp && a->obj_alive(g->comp, NULL) && g->mesh && a->obj_alive(g->mesh, NULL)) reparent_items(a, g->comp, g->mesh, m->actor);
    if (g->comp && a->obj_alive(g->comp, NULL)) { const char *n[1] = { "Object" }; const void *v[1] = { &g->comp }; int s[1] = { 8 }; a->call_fn(g->comp, "ActorComponent", "K2_DestroyComponent", 1, n, v, s, NULL, 0); }
    if (m->actor && a->obj_alive(m->actor, NULL)) {
        uint8_t *mesh = ptr_prop(a, m->actor, "Mesh");
        if (mesh) {
            set_visible(a, mesh, 1);
            uint8_t col = g->orig_col, ten = 1;
            { const char *nn[1] = { "NewType" }; const void *vv[1] = { &col }; int ss[1] = { 1 }; a->call_fn(mesh, "PrimitiveComponent", "SetCollisionEnabled", 1, nn, vv, ss, NULL, 0); }
            { const char *nn[1] = { "bEnabled" }; const void *vv[1] = { &ten }; int ss[1] = { 1 }; a->call_fn(mesh, "ActorComponent", "SetComponentTickEnabled", 1, nn, vv, ss, NULL, 0); }
        }
    }
    free(g); m->gp = NULL;
}
static int gp_ensure(CoreAPI *a, Mirror *m, GPose *g)
{
    uint8_t *mesh = ptr_prop(a, m->actor, "Mesh"); if (!mesh) return 0;
    if (g->comp && g->mesh == mesh && a->obj_alive(g->comp, NULL)) return 1;
    static uint8_t *pclass; if (!pclass) pclass = a->find_object("Class", "PoseableMeshComponent", "/Script/Engine");
    if (!pclass) return 0;
    tickopt_off(a, m->actor);
    Transform id; memset(&id, 0, sizeof id); id.q[3] = 1; id.s[0] = id.s[1] = id.s[2] = 1;
    uint8_t manual = 1, defer = 0; uint8_t *comp = NULL;
    const char *names[4] = { "Class", "bManualAttachment", "RelativeTransform", "bDeferredFinish" };
    const void *vals[4] = { &pclass, &manual, &id, &defer }; int sizes[4] = { 8, 1, (int)sizeof id, 1 };
    a->call_fn(m->actor, "Actor", "AddComponentByClass", 4, names, vals, sizes, &comp, 8);
    if (!comp) return 0;
    uint8_t *asset = ptr_prop(a, mesh, "SkinnedAsset"); uint8_t reinit = 1;
    const char *n2[2] = { "NewMesh", "bReinitPose" }; const void *v2[2] = { &asset, &reinit }; int s2[2] = { 8, 1 };
    a->call_fn(comp, "SkinnedMeshComponent", "SetSkinnedAssetAndUpdate", 2, n2, v2, s2, NULL, 0);
    uint8_t snap = 2, weld = 0; uint8_t *par = mesh;
    const char *n3[6] = { "Parent", "SocketName", "LocationRule", "RotationRule", "ScaleRule", "bWeldSimulatedBodies" };
    const void *v3[6] = { &par, "None", &snap, &snap, &snap, &weld }; int s3[6] = { 8, -1, 1, 1, 1, 1 };
    a->call_fn(comp, "SceneComponent", "K2_AttachToComponent", 6, n3, v3, s3, NULL, 0);
    uint8_t none = 0, ignore = 0, off = 0;
    { const char *nn[1] = { "NewType" }; const void *vv[1] = { &none }; int ss[1] = { 1 }; a->call_fn(comp, "PrimitiveComponent", "SetCollisionEnabled", 1, nn, vv, ss, NULL, 0); }
    { const char *nn[1] = { "NewResponse" }; const void *vv[1] = { &ignore }; int ss[1] = { 1 }; a->call_fn(comp, "PrimitiveComponent", "SetCollisionResponseToAllChannels", 1, nn, vv, ss, NULL, 0); }
    { const char *nn[1] = { "bInGenerateOverlapEvents" }; const void *vv[1] = { &off }; int ss[1] = { 1 }; a->call_fn(comp, "PrimitiveComponent", "SetGenerateOverlapEvents", 1, nn, vv, ss, NULL, 0); }
    { int lod0 = 1; const char *nn[1] = { "InNewForcedLOD" }; const void *vv[1] = { &lod0 }; int ss[1] = { 4 }; a->call_fn(comp, "SkinnedMeshComponent", "SetForcedLOD", 1, nn, vv, ss, NULL, 0); }
    set_visible(a, comp, 1); set_visible(a, mesh, 0);
    {   /* the original mesh must not compete with the bone-driven one: no animation tick, no physics (ragdoll), no collision; it only keeps the sockets the items hang on */
        uint8_t col = 3; a->call_fn(mesh, "PrimitiveComponent", "GetCollisionEnabled", 0, NULL, NULL, NULL, &col, 1); g->orig_col = col;
        uint8_t no = 0, ten = 0;
        { const char *nn[1] = { "bSimulate" }; const void *vv[1] = { &no }; int ss[1] = { 1 }; a->call_fn(mesh, "PrimitiveComponent", "SetSimulatePhysics", 1, nn, vv, ss, NULL, 0); }
        { const char *nn[1] = { "NewType" }; const void *vv[1] = { &no }; int ss[1] = { 1 }; a->call_fn(mesh, "PrimitiveComponent", "SetCollisionEnabled", 1, nn, vv, ss, NULL, 0); }
        { const char *nn[1] = { "bEnabled" }; const void *vv[1] = { &ten }; int ss[1] = { 1 }; a->call_fn(mesh, "ActorComponent", "SetComponentTickEnabled", 1, nn, vv, ss, NULL, 0); }
    }
    g->comp = comp; g->mesh = mesh; g->nbn = 0; g->napplied = 0; g->nparent = 0;
    { int mv = reparent_items(a, mesh, comp, m->actor); if (mv) a->log("N guest: %d item(s) of NPC %p moved to its bone-driven mesh", mv, m->actor); }
    a->log("N guest: NPC %p now bone-driven (poseable %p)", m->actor, comp);
    return 1;
}
static void gp_apply(CoreAPI *a, Mirror *m)
{
    GPose *g = m->gp; if (!g || g->bad) return;
    if (g->comp && g->mesh && GetTickCount() - g->titems > 1000) { g->titems = GetTickCount(); if (a->obj_alive(g->comp, NULL) && a->obj_alive(g->mesh, NULL)) { int mv = reparent_items(a, g->mesh, g->comp, m->actor); if (mv) a->log("N guest: %d new item(s) of NPC %p moved to its bone-driven mesh", mv, m->actor); } }
    if (g->dirty) { g->dirty = 0; g->pending = g->napplied ? 2 : 3; }       /* the bone hierarchy is ~15 levels deep and each pass fixes one more level */                          /* a pose is applied over several frames in a row: SetBoneTransformByName converts a bone through its parent's pose of the LAST refresh, */
    if (g->pending <= 0) return;                                           /* so after a big jump (standing -> sitting) the first pass leaves the children wrong and the next passes pull them in */
    g->pending--;
    if (GetTickCount() - N.t_hero < 8000 && !g->comp) { g->dirty = 1; return; }   /* a poseable component added to an NPC right after a save was loaded crashed the game: wait until the world has settled */
    if (!gp_ensure(a, m, g)) return;
    if (g->nbn != g->nb) {
        int n = 0; a->call_fn(g->mesh, "SkinnedMeshComponent", "GetNumBones", 0, NULL, NULL, NULL, &n, 4);
        if (n != g->nb || n > MAXB) { g->bad = 1; a->log("N guest: NPC bone count mismatch (mine %d, host %d), no pose copy for it", n, g->nb); return; }
        for (int i = 0; i < n; i++) { const char *nn[1] = { "BoneIndex" }; const void *vv[1] = { &i }; int ss[1] = { 4 }; a->call_fn(g->mesh, "SkinnedMeshComponent", "GetBoneName", 1, nn, vv, ss, g->fnm[i], 8); a->fname_to_str(g->fnm[i], g->names[i], 48); }
        g->nbn = n;
    }
    /* Exact application. SetBoneTransformByName(ComponentSpace) turns a bone into a local transform through its parent's pose of the LAST refresh (the cache).
     * So for every bone we hand it  T_i * T_parent^-1 * C_parent  (T = wanted pose, C = what the mesh shows now): the engine's conversion then yields exactly the wanted local
     * transform, and after one refresh every bone sits where the host had it, however big the jump (falling, getting up). Bones that are not set follow their parent: their wanted pose is predicted. */
    if (!g->nparent) {
        for (int i = 0; i < g->nb; i++) {
            uint8_t pf[8] = { 0 }; char pn[64] = ""; g->parent[i] = -1;
            const char *n1[1] = { "BoneName" }; const void *v1[1] = { g->fnm[i] }; int s1[1] = { -2 };
            a->call_fn(g->mesh, "SkinnedMeshComponent", "GetParentBone", 1, n1, v1, s1, pf, 8); a->fname_to_str(pf, pn, sizeof pn);
            if (i > 0) for (int j = 0; j < i; j++) if (strcmp(g->names[j], pn) == 0) { g->parent[i] = (int16_t)j; break; }
        }
        g->nparent = 1;
    }
    static Xf C[MAXB], T[MAXB]; static uint8_t given[MAXB];
    for (int i = 0; i < g->nb; i++) {                                         /* what the bone-driven mesh shows now (component space) */
        Transform rb; memset(&rb, 0, sizeof rb); rb.q[3] = 1; uint8_t sp2 = 2; const char *nn[2] = { "InSocketName", "TransformSpace" }; const void *vv[2] = { g->fnm[i], &sp2 }; int ss[2] = { -2, 1 };
        a->call_fn(g->comp, "SceneComponent", "GetSocketTransform", 2, nn, vv, ss, &rb, (int)sizeof rb);
        C[i] = xf_from(rb.q, rb.t);
    }
    for (int i = 0; i < g->nb; i++) {                                         /* the wanted pose: the host's value, or (not set) the current local transform carried onto the wanted parent */
        int16_t v[6]; memcpy(v, g->data + i * 12, 12);
        int skip = strncmp(g->names[i], "ik_", 3) == 0 || npc_skip_has(g->names[i]) || (i > 0 && !(v[0] | v[1] | v[2] | v[3] | v[4] | v[5]));
        given[i] = (uint8_t)!skip;
        if (!skip) {
            double x = v[0] / 32767.0, y = v[1] / 32767.0, z = v[2] / 32767.0, w2 = 1.0 - x * x - y * y - z * z, q[4] = { x, y, z, w2 > 0 ? sqrt(w2) : 0 }, t[3] = { v[3] / 50.0, v[4] / 50.0, v[5] / 50.0 };
            T[i] = xf_from(q, t);
        } else if (g->parent[i] < 0) T[i] = C[i];
        else { Xf pinv = xf_inv(&C[g->parent[i]]); Xf local = xf_mul(&C[i], &pinv); T[i] = xf_mul(&local, &T[g->parent[i]]); }
    }
    for (int i = 0; i < g->nb; i++) {
        if (!given[i]) continue;
        Xf out = T[i];
        if (g->parent[i] >= 0) { Xf pinv = xf_inv(&T[g->parent[i]]); Xf loc = xf_mul(&T[i], &pinv); out = xf_mul(&loc, &C[g->parent[i]]); }
        double q[4]; m2q(out.r, q);
        Transform tr; memset(&tr, 0, sizeof tr); tr.q[0] = q[0]; tr.q[1] = q[1]; tr.q[2] = q[2]; tr.q[3] = q[3]; tr.t[0] = out.t[0]; tr.t[1] = out.t[1]; tr.t[2] = out.t[2];
        tr.s[0] = g->sc[i][0] / 1000.0; tr.s[1] = g->sc[i][1] / 1000.0; tr.s[2] = g->sc[i][2] / 1000.0;
        uint8_t space = 1;                                                    /* EBoneSpaces::ComponentSpace */
        const char *names[3] = { "BoneName", "InTransform", "BoneSpace" }; const void *vals[3] = { g->fnm[i], &tr, &space }; int sizes[3] = { -2, (int)sizeof tr, 1 };
        a->call_fn(g->comp, "PoseableMeshComponent", "SetBoneTransformByName", 3, names, vals, sizes, NULL, 0);
    }
    memcpy(g->applied, g->data, (size_t)g->nb * 12); g->napplied = g->nb;
    if (g->pending == 0 && !g->checked) {   /* diagnosis, once per NPC: read the bones back from the bone-driven mesh and name the ones that are far from the target */
        g->checked = 1; struct { double e; int i; } top[8]; int nt = 0;
        for (int i = 1; i < g->nb; i++) {
            int16_t v[6]; memcpy(v, g->data + i * 12, 12); if (!(v[0] | v[1] | v[2] | v[3] | v[4] | v[5])) continue;
            Transform rb; memset(&rb, 0, sizeof rb); uint8_t sp2 = 2; const char *nn[2] = { "InSocketName", "TransformSpace" }; const void *vv[2] = { g->fnm[i], &sp2 }; int ss[2] = { -2, 1 };
            if (!a->call_fn(g->comp, "SceneComponent", "GetSocketTransform", 2, nn, vv, ss, &rb, (int)sizeof rb)) continue;
            double dx = rb.t[0] - v[3] / 50.0, dy = rb.t[1] - v[4] / 50.0, dz = rb.t[2] - v[5] / 50.0, e = sqrt(dx * dx + dy * dy + dz * dz);
            int pos = nt; while (pos > 0 && top[pos - 1].e < e) pos--; if (pos >= 8) continue;
            for (int k = (nt < 8 ? nt : 7); k > pos; k--) top[k] = top[k - 1]; top[pos].e = e; top[pos].i = i; if (nt < 8) nt++;
        }
        {   /* where do the bones that the host could not report (face, ribs, scapula, radius ...) end up in the bone-driven mesh, next to the head and the spine? */
            const char *want[] = { "face_C_faceRoot", "face_C_neckRoot", "face_C_adamsApple", "face_L_neck_01", "Head", "neck_03", "face_C_skull", "Jaw", "helmet", "eye_l", "radius_r", "scapula_r", "chestpiece", "rib_01_l", "belt_01", "bag_r_01", "spine_05" };
            for (size_t w = 0; w < sizeof want / sizeof want[0]; w++) for (int i = 1; i < g->nb; i++) if (!strcmp(g->names[i], want[w])) {
                Transform rb; memset(&rb, 0, sizeof rb); uint8_t sp2 = 2; const char *nn[2] = { "InSocketName", "TransformSpace" }; const void *vv[2] = { g->fnm[i], &sp2 }; int ss[2] = { -2, 1 };
                a->call_fn(g->comp, "SceneComponent", "GetSocketTransform", 2, nn, vv, ss, &rb, (int)sizeof rb);
                int16_t v[6]; memcpy(v, g->data + i * 12, 12);
                uint8_t pf[8] = { 0 }; char pn[64] = "?"; { const char *n1[1] = { "BoneName" }; const void *v1[1] = { g->fnm[i] }; int s1[1] = { -2 }; a->call_fn(g->mesh, "SkinnedMeshComponent", "GetParentBone", 1, n1, v1, s1, pf, 8); a->fname_to_str(pf, pn, sizeof pn); }
                a->log("N guest: bone #%d %s (parent %s) now at %.1f %.1f %.1f (host reported %s)", i, g->names[i], pn, rb.t[0], rb.t[1], rb.t[2], (v[0] | v[1] | v[2] | v[3] | v[4] | v[5]) ? "a transform" : "nothing"); break;
            }
        }
        for (int k = 0; k < nt; k++) { int i = top[k].i; int16_t v[6]; memcpy(v, g->data + i * 12, 12); a->log("N guest: after applying, bone #%d %s is %.1f off (target loc %.1f %.1f %.1f)", i, g->names[i], top[k].e, v[3] / 50.0, v[4] / 50.0, v[5] / 50.0); }
    }
    N.n_posed++;
}

static void freeze(CoreAPI *a, Mirror *m)
{
    /* the controller stays possessed: with a null controller the game's own perception code (PerceptionConditions, other NPCs looking at this one) crashed with a null read.
     * The local AI keeps running, but the NPC is placed and animated from the host's data every frame. build
pc_unpossess.txt = the old way (UnPossess). */
    m->ctrl = ptr_prop(a, m->actor, "Controller");
    m->unposs = GetFileAttributesA(BUILD "npc_unpossess.txt") != INVALID_FILE_ATTRIBUTES;
    {   /* the guest hero must not hit a mirrored NPC: the host alone decides what happens to it (a local hit made the guest's own AI play a second, different reaction on top of the host's pose) */
        uint8_t off = 0; const char *nn[1] = { "bNewActorEnableCollision" }; const void *vv[1] = { &off }; int ss[1] = { 1 };
        a->call_fn(m->actor, "Actor", "SetActorEnableCollision", 1, nn, vv, ss, NULL, 0);
    }
    if (m->unposs && m->ctrl) a->call_fn(m->ctrl, "Controller", "UnPossess", 0, NULL, NULL, NULL, NULL, 0);
    m->frozen = 1;
}
static void release(CoreAPI *a, Mirror *m)
{
    gp_drop(a, m);
    if (m->frozen && m->unposs && m->ctrl && m->actor && a->obj_alive(m->ctrl, NULL) && a->obj_alive(m->actor, NULL)) {
        const char *n[1] = { "InPawn" }; const void *v[1] = { &m->actor }; int s[1] = { 8 };
        a->call_fn(m->ctrl, "Controller", "Possess", 1, n, v, s, NULL, 0);
    }
    if (m->frozen && m->actor && a->obj_alive(m->actor, NULL)) { uint8_t on = 1; const char *nn[1] = { "bNewActorEnableCollision" }; const void *vv[1] = { &on }; int ss[1] = { 1 }; a->call_fn(m->actor, "Actor", "SetActorEnableCollision", 1, nn, vv, ss, NULL, 0); }
    m->frozen = 0; m->have_cur = 0;
    if (m->anim[0] && m->actor && a->obj_alive(m->actor, NULL)) { m->anim[0] = 0; set_anim(a, m); }
}



/* The mirror's AI decides on its own where the NPC carries its weapon: put the melee weapon / bow into the hand or back into its holster as the owner has it.
 * The holster socket is remembered the first time an item is seen without the owner having it in hand. */
static void sync_weapon_sockets(CoreAPI *a, Mirror *m, unsigned long now)
{
    if (now - m->t_wp < 700) return; m->t_wp = now;
    uint8_t *mesh = ptr_prop(a, m->actor, "Mesh"); if (!mesh) return;
    uint8_t *parent = (m->gp && m->gp->comp && a->obj_alive(m->gp->comp, NULL)) ? m->gp->comp : mesh;
    int oat = a->find_prop_offset(a->obj_class(parent), "AttachChildren"); if (oat < 0) return;
    uint8_t *kids = NULL; int nk = 0; a->read_mem(parent + oat, &kids, 8); a->read_mem(parent + oat + 8, &nk, 4);
    if (!kids || nk <= 0 || nk > 100) return;
    uint8_t *list[100]; int n = 0; for (int i = 0; i < nk; i++) { uint8_t *c = NULL; a->read_mem(kids + (size_t)i * 8, &c, 8); if (c) list[n++] = c; }
    for (int i = 0; i < n; i++) {
        uint8_t *comp = list[i], *o = NULL; a->read_mem(comp + 0x20, &o, 8);
        if (!o || o == m->actor || !a->obj_alive(o, NULL) || ptr_prop(a, o, "RootComponent") != comp) continue;
        char cn[120] = ""; a->obj_name(a->obj_class(o), cn, sizeof cn);
        int melee = strstr(cn, "ItMw") != NULL, bow = strstr(cn, "ItRw") != NULL && strstr(cn, "Quiver") == NULL; if (!melee && !bow) continue;
        int os = a->find_prop_offset(a->obj_class(comp), "AttachSocketName"); char cur[96] = "None"; if (os >= 0) a->fname_to_str(comp + os, cur, sizeof cur);
        int slot = -1; for (int k = 0; k < m->nwp; k++) if (m->wp[k].item == o) slot = k;
        int inhand = (m->want.flags & (melee ? 4 : 8)) != 0;
        const char *hand = melee ? "weapon_r" : "weapon_l";
        if (slot < 0 && m->nwp < 4) { slot = m->nwp++; m->wp[slot].item = o; m->wp[slot].holster[0] = 0; }
        if (slot < 0) continue;
        if (strcmp(cur, hand) != 0 && !m->wp[slot].holster[0]) snprintf(m->wp[slot].holster, sizeof m->wp[slot].holster, "%s", cur);   /* first sight: this is the holster */
        const char *want = inhand ? hand : (m->wp[slot].holster[0] ? m->wp[slot].holster : NULL);
        if (!want || strcmp(cur, want) == 0) continue;
        uint8_t snap = 2, weld = 0; uint8_t *par = parent;
        const char *n3[6] = { "Parent", "SocketName", "LocationRule", "RotationRule", "ScaleRule", "bWeldSimulatedBodies" };
        const void *v3[6] = { &par, want, &snap, &snap, &snap, &weld }; int s3[6] = { 8, -1, 1, 1, 1, 1 };
        a->call_fn(comp, "SceneComponent", "K2_AttachToComponent", 6, n3, v3, s3, NULL, 0);
        a->log("N mirror: %s of NPC %p %s -> %s", cn, m->actor, cur, want);
    }
}
static void guest_tick(CoreAPI *a, unsigned long now)
{
    double c1[3] = { 0 }, c2[3] = { 0 }; int have1 = a->hero && loc_of(a, a->hero, c1), have2 = a->puppet && loc_of(a, a->puppet, c2);
    if (a->remote_blob2_seq != N.last_seq && a->remote_blob2_len >= 5) {          /* a new list from the host */
        N.last_seq = a->remote_blob2_seq;
        const uint8_t *B = a->remote_blob2; int len = a->remote_blob2_len;
        int cnt = B[0] | (B[1] << 8), nstr = B[2], strpos = B[3] | (B[4] << 8);
        if (cnt > MAXN || 5 + cnt * (int)sizeof(Rec) + strpos > len) cnt = 0;
        const char *strv[40]; int ns = 0; { const char *q = (const char *)B + 5 + cnt * sizeof(Rec); const char *end = q + strpos; while (q < end && ns < nstr && ns < 40) { strv[ns++] = q; q += strlen(q) + 1; } }
        N.n_missing = 0;
        for (int k = 0; k < cnt; k++) {
            Rec r; memcpy(&r, B + 5 + k * sizeof(Rec), sizeof r);
            {   /* the other copy claims this NPC: accept unless my hero is clearly nearer to it (then my own simulation stays authoritative) */
                double rp[3] = { r.x, r.y, r.z };
                if (own_get(r.id) == N.seat && have1 && have2 && dist3(rp, c1) + HYST < dist3(rp, c2)) continue;
                if (own_get(r.id) == N.seat) own_set(a, r.id, 1 - N.seat, "the other copy claims it");
            }
            Mirror *m = mirror_for(a, r.id); if (!m) { N.n_missing++; continue; }
            m->want = r; m->last_seen = now;
            if (!m->frozen) freeze(a, m);
            const char *want_anim = (r.anim > 0 && r.anim <= ns) ? strv[r.anim - 1] : "";
            if (strcmp(want_anim, m->anim) != 0) { snprintf(m->anim, sizeof m->anim, "%s", want_anim); set_anim(a, m); }
        }
        {   /* the pose section: one NPC's bones */
            int po = 5 + cnt * (int)sizeof(Rec) + strpos;
            if (po + 7 <= len && B[po] == 1) {
                uint32_t pid; memcpy(&pid, B + po + 1, 4); int nb = B[po + 5] | (B[po + 6] << 8);
                int nsp = 0; if (nb > 0 && nb <= MAXB && po + 7 + nb * 12 + 2 <= len) nsp = B[po + 7 + nb * 12] | (B[po + 8 + nb * 12] << 8);
                if (nb > 0 && nb <= MAXB && nsp >= 0 && nsp <= 160 && po + 9 + nb * 12 + nsp * 8 <= len) {
                    Mirror *m = own_get(pid) == N.seat ? NULL : mirror_for(a, pid);
                    if (m) {
                        if (!m->gp) m->gp = (GPose *)calloc(1, sizeof(GPose));
                        if (m->gp) {
                            GPose *g = m->gp; g->nb = nb; memcpy(g->data, B + po + 7, (size_t)nb * 12);
                            for (int i = 0; i < nb; i++) g->sc[i][0] = g->sc[i][1] = g->sc[i][2] = 1000;
                            unsigned h = 2166136261u;
                            for (int k2 = 0; k2 < nsp; k2++) { int16_t e[4]; memcpy(e, B + po + 9 + nb * 12 + k2 * 8, 8); if (e[0] >= 0 && e[0] < nb) { g->sc[e[0]][0] = e[1]; g->sc[e[0]][1] = e[2]; g->sc[e[0]][2] = e[3]; } for (int c2 = 0; c2 < 4; c2++) { h ^= (uint16_t)e[c2]; h *= 16777619u; } }
                            if (h != g->shash) { g->shash = h; g->napplied = 0; }                 /* the scales changed: apply every bone again */
                            g->have = 1; g->dirty = 1; g->t = now;
                        }
                    }
                }
            }
        }
    }
    double dt = N.t_prev ? (now - N.t_prev) / 1000.0 : 0.016; N.t_prev = now; if (dt > 0.1) dt = 0.1;
    int applied = 0;
    for (int i = 0; i < N.nm; i++) {
        Mirror *m = &N.m[i];
        if (!actor_ok(a, m->actor)) { m->frozen = 0; continue; }
        if (own_get(m->id) == N.seat) { if (m->frozen) release(a, m); continue; }     /* simulated here */
        if (!m->frozen) continue;
        if (m->gp) {
            if (now - m->gp->t > 5000) { gp_drop(a, m); set_anim(a, m); }          /* no longer one of the host's focus NPCs: the original mesh and its animation come back */
            else { if (now - m->gp->topt > 3000) { m->gp->topt = now; tickopt_off(a, m->actor); } gp_apply(a, m); }
        }
        if (now - m->last_seen > 5000) { release(a, m); continue; }              /* the host no longer reports it (far away): give the AI back */
        if (m->want.pad != 255 && now - m->t_hp > 400) {                           /* the owner's health numbers (and a death) on the mirror */
            m->t_hp = now; int mine = npc_health(a, m->actor);
            if (mine >= 0 && mine != m->want.pad) npc_set_health(a, m->actor, m->want.pad);
        }
        sync_weapon_sockets(a, m, now);
        double want[3] = { m->want.x, m->want.y, m->want.z };
        if (!m->have_cur) { memcpy(m->cur, want, sizeof want); m->have_cur = 1; }
        double dx = want[0] - m->cur[0], dy = want[1] - m->cur[1], dz = want[2] - m->cur[2], dist = sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > 400) memcpy(m->cur, want, sizeof want);                         /* teleport on big jumps */
        else { double k = 1.0 - exp(-dt * 12.0); m->cur[0] += dx * k; m->cur[1] += dy * k; m->cur[2] += dz * k; }
        uint8_t sw = 0, tp = 1;
        const char *nl[3] = { "NewLocation", "bSweep", "bTeleport" }; const void *vl[3] = { m->cur, &sw, &tp }; int sl[3] = { 24, 1, 1 };
        if (!a->call_fn(m->actor, "Actor", "K2_SetActorLocation", 3, nl, vl, sl, NULL, 0)) { m->actor = NULL; m->frozen = 0; m->gp = NULL; continue; }     /* faulted: the actor is gone */
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
    if (a->hero != N.last_hero) {                                            /* a new world (save loaded): nothing of the old one may be touched, and nothing is created while it is still settling */
        N.last_hero = a->hero; N.t_hero = now; N.nm = 0; N.nknown = 0; N.scanning = 0; N.last_scan = 0; memset(N.m, 0, sizeof N.m);
    }
    if (N.off || !a->have_remote || !a->hero) return;
    if (N.scanning || !N.last_scan || now - N.last_scan > 4000) rescan_slice(a);
    N.seat = N.role == 1 ? 0 : 1;
    host_tick(a, now); guest_tick(a, now);                                    /* send what this copy simulates, mirror what the other one simulates */
    if (now - N.last_stat > 5000) {
        N.last_stat = now;
        a->log("N seat %d: %d characters known, simulating %d, mirroring %d, missing %d, bone-driven here %d, bone-driven mirrors applied %d", N.seat, N.nknown, N.n_sent, N.n_applied, N.n_missing, N.nfocus, N.n_posed);
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) { (void)h; (void)r; (void)x; return TRUE; }
