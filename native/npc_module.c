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
typedef struct { uint8_t *comp, *mesh; int nb, nbn, bad; uint8_t fnm[MAXB][8]; char names[MAXB][48]; uint8_t data[MAXB * 12], applied[MAXB * 12]; int have, dirty, napplied; unsigned long t, topt, titems; } GPose;   /* guest: bone-driven copy of one NPC */
typedef struct { uint8_t *actor, *mesh; int nb; uint8_t fnm[MAXB][8]; uint8_t last[MAXB * 12]; unsigned long next_t; int interval; } HPose;                                                                                              /* host: cached bone names of one focus NPC */
typedef struct { uint8_t *actor; uint32_t id; } Known;
typedef struct { uint8_t *actor, *ctrl; uint32_t id; Rec want; unsigned long last_seen; int frozen; double cur[3]; int have_cur; int unposs; char anim[320]; GPose *gp; } Mirror;

static struct {
    Known known[2048]; int nknown; unsigned long last_scan, last_send, last_stat;
    struct { uint8_t *cls; int v; } cc[16384]; int ncc; int scan_pos, scanning, scan_n; Known tmp[2048]; int ntmp;
    Mirror m[MAXN]; int nm; int last_seq;
    int role, off; int off_vel; unsigned long t_prev;
    int n_sent, n_applied, n_missing;
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
    if (!N.scanning) { N.scanning = 1; N.scan_pos = 0; N.ntmp = 0; }
    int end = N.scan_pos + 25000; if (end > n) end = n;
    for (int i = N.scan_pos; i < end && N.ntmp < 2048; i++) {
        uint8_t *o = a->object_at(i); if (!o) continue;
        uint8_t *cls = a->obj_class(o); if (!cls || !is_character_class(a, cls)) continue;
        a->obj_name(o, nm, sizeof nm); if (strncmp(nm, "Default__", 9) == 0) continue;
        if (a->read_bool(o, "bActorIsBeingDestroyed") == 1) continue;
        N.tmp[N.ntmp].actor = o; N.tmp[N.ntmp].id = fnv(nm); N.ntmp++;
    }
    N.scan_pos = end;
    if (end >= n || N.ntmp >= 2048) { memcpy(N.known, N.tmp, (size_t)N.ntmp * sizeof(Known)); N.nknown = N.ntmp; N.scanning = 0; N.last_scan = GetTickCount(); }
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
static int read_pose(CoreAPI *a, uint8_t *actor, uint8_t *out, int *nb_out)
{
    uint8_t *mesh = ptr_prop(a, actor, "Mesh"); if (!mesh) return 0;
    HPose *h = hpose_for(actor);
    if (h->mesh != mesh || h->nb <= 0) {
        int n = 0; a->call_fn(mesh, "SkinnedMeshComponent", "GetNumBones", 0, NULL, NULL, NULL, &n, 4);
        if (n <= 0 || n > MAXB) return 0;
        for (int i = 0; i < n; i++) { const char *nn[1] = { "BoneIndex" }; const void *vv[1] = { &i }; int ss[1] = { 4 }; a->call_fn(mesh, "SkinnedMeshComponent", "GetBoneName", 1, nn, vv, ss, h->fnm[i], 8); }
        h->mesh = mesh; h->nb = n;
    }
    for (int i = 0; i < h->nb; i++) {
        Transform tr; memset(&tr, 0, sizeof tr); tr.q[3] = 1; uint8_t space = 2;   /* RTS_Component */
        const char *names[2] = { "InSocketName", "TransformSpace" }; const void *vals[2] = { h->fnm[i], &space }; int sizes[2] = { -2, 1 };
        a->call_fn(mesh, "SceneComponent", "GetSocketTransform", 2, names, vals, sizes, &tr, (int)sizeof tr);
        int bad = 0; for (int c = 0; c < 4; c++) if (!isfinite(tr.q[c])) bad = 1; for (int c = 0; c < 3; c++) if (!isfinite(tr.t[c]) || fabs(tr.t[c]) > 600) bad = 1;
        if (bad) { memset(&tr, 0, sizeof tr); tr.q[3] = 1; }
        double nq = sqrt(tr.q[0] * tr.q[0] + tr.q[1] * tr.q[1] + tr.q[2] * tr.q[2] + tr.q[3] * tr.q[3]); if (nq < 1e-6) { tr.q[0] = tr.q[1] = tr.q[2] = 0; tr.q[3] = 1; nq = 1; }
        double sg = tr.q[3] < 0 ? -1.0 / nq : 1.0 / nq;
        int16_t v[6] = { q16(tr.q[0] * sg, 32767), q16(tr.q[1] * sg, 32767), q16(tr.q[2] * sg, 32767), q16(tr.t[0], 50), q16(tr.t[1], 50), q16(tr.t[2], 50) };
        memcpy(out + i * 12, v, 12);
    }
    *nb_out = h->nb; return 1;
}
/* the characters closest to either player (a few at most): they are bone-driven on the guest */
static void pick_focus(CoreAPI *a, const double *c1, int have2, const double *c2)
{
    N.nfocus = 0; double best[NFOCUS];
    for (int i = 0; i < N.nknown; i++) {
        uint8_t *o = N.known[i].actor; if (!a->obj_alive(o, NULL)) continue;
        double p[3]; if (!loc_of(a, o, p)) continue;
        double d1 = sqrt((p[0] - c1[0]) * (p[0] - c1[0]) + (p[1] - c1[1]) * (p[1] - c1[1]) + (p[2] - c1[2]) * (p[2] - c1[2]));
        double d2 = have2 ? sqrt((p[0] - c2[0]) * (p[0] - c2[0]) + (p[1] - c2[1]) * (p[1] - c2[1]) + (p[2] - c2[2]) * (p[2] - c2[2])) : 1e18;
        double d = d1 < d2 ? d1 : d2; if (d > FOCUS_R) continue;
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
    if (now - N.last_focus > 500) { N.last_focus = now; pick_focus(a, c1, have2, c2); }
    static uint8_t buf[9000]; int cnt = 0, nstr = 0, strpos = 0;
    static char strs[40][320]; static char strtab[6000];
    static Rec recs[MAXN];
    for (int i = 0; i < N.nknown && cnt < MAXN; i++) {
        uint8_t *o = N.known[i].actor; if (!a->obj_alive(o, NULL)) continue;
        double p[3]; if (!loc_of(a, o, p)) continue;
        double d1 = sqrt((p[0] - c1[0]) * (p[0] - c1[0]) + (p[1] - c1[1]) * (p[1] - c1[1]) + (p[2] - c1[2]) * (p[2] - c1[2]));
        double d2 = have2 ? sqrt((p[0] - c2[0]) * (p[0] - c2[0]) + (p[1] - c2[1]) * (p[1] - c2[1]) + (p[2] - c2[2]) * (p[2] - c2[2])) : 1e18;
        if (d1 > RADIUS && d2 > RADIUS) continue;
        Rec r; memset(&r, 0, sizeof r);
        r.id = N.known[i].id; r.x = (float)p[0]; r.y = (float)p[1]; r.z = (float)p[2];
        r.yaw = (int16_t)(yaw_of(a, o) * 182.04); r.speed = (uint16_t)speed_of(a, o);
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
        static uint8_t pb[MAXB * 12]; int nb = 0;
        uint32_t id = 0; if (o) for (int i = 0; i < N.nknown; i++) if (N.known[i].actor == o) id = N.known[i].id;
        if (id && read_pose(a, o, pb, &nb) && blen + 6 + nb * 12 <= (int)sizeof buf) {
            int same = hp->nb == nb && memcmp(hp->last, pb, (size_t)nb * 12) == 0;
            memcpy(hp->last, pb, (size_t)nb * 12);
            hp->interval = same ? (hp->interval < 150 ? 300 : (hp->interval * 2 > 2500 ? 2500 : hp->interval * 2)) : 150;
            hp->next_t = now + (unsigned long)hp->interval;
            buf[blen - 1] = 1; memcpy(buf + blen, &id, 4); buf[blen + 4] = (uint8_t)(nb & 255); buf[blen + 5] = (uint8_t)(nb >> 8); memcpy(buf + blen + 6, pb, (size_t)nb * 12); blen += 6 + nb * 12;
        }
    }
    a->set_local_blob2(buf, blen);
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


/* play the host's montage clip on the frozen NPC (single-node animation), or give the animation blueprint back when the host plays none */
static void set_anim(CoreAPI *a, Mirror *m)
{
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

/* ---- guest: bone-driven NPC (a PoseableMeshComponent with the NPC's own mesh asset, the original mesh hidden) ---- */
static void gp_drop(CoreAPI *a, Mirror *m)
{
    if (!m->gp) return;
    GPose *g = m->gp;
    if (g->comp && a->obj_alive(g->comp, NULL) && g->mesh && a->obj_alive(g->mesh, NULL)) reparent_items(a, g->comp, g->mesh, m->actor);
    if (g->comp && a->obj_alive(g->comp, NULL)) { const char *n[1] = { "Object" }; const void *v[1] = { &g->comp }; int s[1] = { 8 }; a->call_fn(g->comp, "ActorComponent", "K2_DestroyComponent", 1, n, v, s, NULL, 0); }
    if (m->actor && a->obj_alive(m->actor, NULL)) { uint8_t *mesh = ptr_prop(a, m->actor, "Mesh"); if (mesh) set_visible(a, mesh, 1); }
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
    g->comp = comp; g->mesh = mesh; g->nbn = 0; g->napplied = 0;
    { int mv = reparent_items(a, mesh, comp, m->actor); if (mv) a->log("N guest: %d item(s) of NPC %p moved to its bone-driven mesh", mv, m->actor); }
    a->log("N guest: NPC %p now bone-driven (poseable %p)", m->actor, comp);
    return 1;
}
static void gp_apply(CoreAPI *a, Mirror *m)
{
    GPose *g = m->gp; if (!g || g->bad) return;
    if (g->comp && g->mesh && GetTickCount() - g->titems > 1000) { g->titems = GetTickCount(); if (a->obj_alive(g->comp, NULL) && a->obj_alive(g->mesh, NULL)) { int mv = reparent_items(a, g->mesh, g->comp, m->actor); if (mv) a->log("N guest: %d new item(s) of NPC %p moved to its bone-driven mesh", mv, m->actor); } }
    if (!g->dirty) return;
    g->dirty = 0;
    if (!gp_ensure(a, m, g)) return;
    if (g->nbn != g->nb) {
        int n = 0; a->call_fn(g->mesh, "SkinnedMeshComponent", "GetNumBones", 0, NULL, NULL, NULL, &n, 4);
        if (n != g->nb || n > MAXB) { g->bad = 1; a->log("N guest: NPC bone count mismatch (mine %d, host %d), no pose copy for it", n, g->nb); return; }
        for (int i = 0; i < n; i++) { const char *nn[1] = { "BoneIndex" }; const void *vv[1] = { &i }; int ss[1] = { 4 }; a->call_fn(g->mesh, "SkinnedMeshComponent", "GetBoneName", 1, nn, vv, ss, g->fnm[i], 8); a->fname_to_str(g->fnm[i], g->names[i], 48); }
        g->nbn = n;
    }
    for (int i = 0; i < g->nb; i++) {
        int16_t v[6]; memcpy(v, g->data + i * 12, 12);
        if (g->napplied == g->nb && memcmp(g->applied + i * 12, g->data + i * 12, 12) == 0) continue;     /* this bone did not change: no engine call */
        if (strncmp(g->names[i], "ik_", 3) == 0) continue;
        if (i > 0 && !(v[0] | v[1] | v[2] | v[3] | v[4] | v[5])) continue;
        double x = v[0] / 32767.0, y = v[1] / 32767.0, z = v[2] / 32767.0, w2 = 1.0 - x * x - y * y - z * z;
        Transform tr; memset(&tr, 0, sizeof tr); tr.q[0] = x; tr.q[1] = y; tr.q[2] = z; tr.q[3] = w2 > 0 ? sqrt(w2) : 0; tr.t[0] = v[3] / 50.0; tr.t[1] = v[4] / 50.0; tr.t[2] = v[5] / 50.0; tr.s[0] = tr.s[1] = tr.s[2] = 1;
        uint8_t space = 1;                                                    /* EBoneSpaces::ComponentSpace */
        const char *names[3] = { "BoneName", "InTransform", "BoneSpace" }; const void *vals[3] = { g->fnm[i], &tr, &space }; int sizes[3] = { -2, (int)sizeof tr, 1 };
        a->call_fn(g->comp, "PoseableMeshComponent", "SetBoneTransformByName", 3, names, vals, sizes, NULL, 0);
    }
    memcpy(g->applied, g->data, (size_t)g->nb * 12); g->napplied = g->nb;
    N.n_posed++;
}

static void freeze(CoreAPI *a, Mirror *m)
{
    /* the controller stays possessed: with a null controller the game's own perception code (PerceptionConditions, other NPCs looking at this one) crashed with a null read.
     * The local AI keeps running, but the NPC is placed and animated from the host's data every frame. build
pc_unpossess.txt = the old way (UnPossess). */
    m->ctrl = ptr_prop(a, m->actor, "Controller");
    m->unposs = GetFileAttributesA(BUILD "npc_unpossess.txt") != INVALID_FILE_ATTRIBUTES;
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
    m->frozen = 0; m->have_cur = 0;
    if (m->anim[0] && m->actor && a->obj_alive(m->actor, NULL)) { m->anim[0] = 0; set_anim(a, m); }
}

static void guest_tick(CoreAPI *a, unsigned long now)
{
    if (a->remote_blob2_seq != N.last_seq && a->remote_blob2_len >= 5) {          /* a new list from the host */
        N.last_seq = a->remote_blob2_seq;
        const uint8_t *B = a->remote_blob2; int len = a->remote_blob2_len;
        int cnt = B[0] | (B[1] << 8), nstr = B[2], strpos = B[3] | (B[4] << 8);
        if (cnt > MAXN || 5 + cnt * (int)sizeof(Rec) + strpos > len) cnt = 0;
        const char *strv[40]; int ns = 0; { const char *q = (const char *)B + 5 + cnt * sizeof(Rec); const char *end = q + strpos; while (q < end && ns < nstr && ns < 40) { strv[ns++] = q; q += strlen(q) + 1; } }
        N.n_missing = 0;
        for (int k = 0; k < cnt; k++) {
            Rec r; memcpy(&r, B + 5 + k * sizeof(Rec), sizeof r);
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
                if (nb > 0 && nb <= MAXB && po + 7 + nb * 12 <= len) {
                    Mirror *m = mirror_for(a, pid);
                    if (m) {
                        if (!m->gp) m->gp = (GPose *)calloc(1, sizeof(GPose));
                        if (m->gp) { m->gp->nb = nb; memcpy(m->gp->data, B + po + 7, (size_t)nb * 12); m->gp->have = 1; m->gp->dirty = 1; m->gp->t = now; }
                    }
                }
            }
        }
    }
    double dt = N.t_prev ? (now - N.t_prev) / 1000.0 : 0.016; N.t_prev = now; if (dt > 0.1) dt = 0.1;
    int applied = 0;
    for (int i = 0; i < N.nm; i++) {
        Mirror *m = &N.m[i];
        if (!m->actor || !a->obj_alive(m->actor, NULL)) { m->frozen = 0; continue; }
        if (!m->frozen) continue;
        if (m->gp) {
            if (now - m->gp->t > 8000) { gp_drop(a, m); set_anim(a, m); }          /* no longer one of the host's focus NPCs: the original mesh and its animation come back */
            else { if (now - m->gp->topt > 3000) { m->gp->topt = now; tickopt_off(a, m->actor); } gp_apply(a, m); }
        }
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
    if (N.scanning || !N.last_scan || now - N.last_scan > 4000) rescan_slice(a);
    if (N.role == 1) host_tick(a, now); else guest_tick(a, now);
    if (now - N.last_stat > 5000) {
        N.last_stat = now;
        a->log("N %s: %d characters known, %s %d, missing %d, %s %d", N.role == 1 ? "host" : "guest", N.nknown, N.role == 1 ? "sent" : "mirrored", N.role == 1 ? N.n_sent : N.n_applied, N.n_missing, N.role == 1 ? "focus" : "poses applied", N.role == 1 ? N.nfocus : N.n_posed);
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) { (void)h; (void)r; (void)x; return TRUE; }
