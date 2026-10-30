/*
 * pose module: the other player's puppet copies the hero's real skeleton pose instead of playing guessed clips.
 *  - sender: every call reads the component-space transform of every bone of the hero mesh (SceneComponent.GetSocketTransform),
 *    packs them (12 bytes per bone) and hands the blob to the core, which sends it next to the normal packet
 *  - receiver: a PoseableMeshComponent with the same mesh is added to the puppet; every new blob is applied with SetBoneTransformByName.
 *    The old skeletal mesh stays on the puppet (hidden) as the carrier of position/rotation; equipment is attached to the poseable mesh.
 * Kill switch: create build\pose_off.txt -> the old clip-driven puppet is shown again.
 * Hot-reloadable. UNTESTED.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "coreapi.h"

#define MAXB 480
typedef struct { double q[4]; double t[3]; double pad0; double s[3]; double pad1; } Transform;   /* FTransform: FQuat, translation, scale */

static struct {
    uint8_t *hmesh; int nb; char names[MAXB][64]; uint8_t fnm[MAXB][8]; int parent[MAXB]; int frame;
    uint8_t *comp, *comp_puppet, *pclass; unsigned long last_try, last_chk, last_vis;
    int last_seq, off, failed, applied, nohide, nocomp;
    int mode;                      /* build\pose_mode.txt: 0 component space both ways, 1 sender parent-bone space + applied as component, 2 parent-bone space + applied as world */
    int16_t last[MAXB][9];         /* what was applied last (rotation, location, scale), to skip unchanged bones */
    unsigned long last_count; int diag, diag2, nbad;
    uint8_t skip[MAXB]; char skip_txt[300];
    unsigned long drawn_t, snap_t; int dumped, snap_nb; int16_t snap[MAXB][6];
} P;

static uint8_t *ptr_prop(CoreAPI *a, uint8_t *obj, const char *name)
{
    if (!obj) return NULL;
    int off = a->find_prop_offset(a->obj_class(obj), name);
    uint8_t *p = NULL;
    if (off >= 0) a->read_mem(obj + off, &p, 8);
    return p;
}

static int call1(CoreAPI *a, uint8_t *obj, const char *outer, const char *fn, const char *pn, const void *v, int sz, void *ret, int retcap)
{
    const char *names[1] = { pn }; const void *vals[1] = { v }; int sizes[1] = { sz };
    return a->call_fn(obj, outer, fn, pn ? 1 : 0, names, vals, sizes, ret, retcap);
}

static void set_visible(CoreAPI *a, uint8_t *comp, int vis)
{
    uint8_t v = (uint8_t)vis, prop = 0;
    const char *names[2] = { "bNewVisibility", "bPropagateToChildren" }; const void *vals[2] = { &v, &prop }; int sizes[2] = { 1, 1 };
    a->call_fn(comp, "SceneComponent", "SetVisibility", 2, names, vals, sizes, NULL, 0);
}

__declspec(dllexport) void mod_init(CoreAPI *a) { (void)a; memset(&P, 0, sizeof P); }

static void destroy_comp(CoreAPI *a)
{
    if (P.comp && a->obj_alive(P.comp, NULL)) call1(a, P.comp, "ActorComponent", "K2_DestroyComponent", "Object", &P.comp, 8, NULL, 0);
    P.comp = NULL; P.comp_puppet = NULL;
    a->set_attach_target(NULL);
}

__declspec(dllexport) void mod_shutdown(CoreAPI *a)
{
    if (a->puppet_mesh && a->obj_alive(a->puppet_mesh, NULL)) set_visible(a, a->puppet_mesh, 1);
    destroy_comp(a);
}

/* ---------------- sender ---------------- */
static int16_t q16(double v, double k) { double r = floor(v * k + 0.5); if (r > 32767) r = 32767; if (r < -32767) r = -32767; return (int16_t)r; }

static void send_pose(CoreAPI *a)
{
    uint8_t *mesh = ptr_prop(a, a->hero, "Mesh");
    if (!mesh) return;
    if (mesh != P.hmesh) { P.hmesh = mesh; P.nb = 0; }
    if (P.nb == 0) {
        int n = 0;
        a->call_fn(mesh, "SkinnedMeshComponent", "GetNumBones", 0, NULL, NULL, NULL, &n, 4);
        if (n <= 0) return;
        if (n > MAXB) { a->log("P hero mesh has %d bones, sending the first %d", n, MAXB); n = MAXB; }
        for (int i = 0; i < n; i++) {
            call1(a, mesh, "SkinnedMeshComponent", "GetBoneName", "BoneIndex", &i, 4, P.fnm[i], 8);
            a->fname_to_str(P.fnm[i], P.names[i], 64);
        }
        for (int i = 0; i < n; i++) {                       /* parent index of every bone, to know which bones must follow a moving parent */
            uint8_t pf[8] = { 0 }; char pn[64] = ""; P.parent[i] = -1;
            call1(a, mesh, "SkinnedMeshComponent", "GetParentBone", "BoneName", P.fnm[i], -2, pf, 8);
            a->fname_to_str(pf, pn, 64);
            for (int j = 0; j < i; j++) if (strcmp(P.names[j], pn) == 0) { P.parent[i] = j; break; }
        }
        {   /* recon: every component hanging on the hero mesh, with its class and bone count (the face is probably a separate skinned mesh) */
            int oat = a->find_prop_offset(a->obj_class(mesh), "AttachChildren");
            uint8_t *kids = NULL; int nk = 0;
            if (oat >= 0) { a->read_mem(mesh + oat, &kids, 8); a->read_mem(mesh + oat + 8, &nk, 4); }
            if (nk < 0 || nk > 100) nk = 0;
            for (int k = 0; k < nk; k++) {
                uint8_t *c = NULL; a->read_mem(kids + (size_t)k * 8, &c, 8);
                if (!c) continue;
                char cn[96] = "", cc[96] = ""; a->obj_name(c, cn, sizeof cn); a->obj_name(a->obj_class(c), cc, sizeof cc);
                a->log("P child #%d %p '%s' class %s", k, c, cn, cc);
            }
        }
        P.nb = n;
        a->log("P sender: %d bones, first '%s' '%s' '%s', parent of #2 = %d", n, P.names[0], P.names[1], P.names[2], P.parent[2]);
    }
    if (P.frame++ % 3) return;                              /* the core sends every 3rd frame */
    if (GetTickCount() - P.last_count > 1000) {             /* the hero mesh is rebuilt when the outfit changes: a different bone count means a different skeleton */
        P.last_count = GetTickCount();
        int n = 0; a->call_fn(mesh, "SkinnedMeshComponent", "GetNumBones", 0, NULL, NULL, NULL, &n, 4);
        if (n > MAXB) n = MAXB;
        if (n != P.nb) { a->log("P hero bone count changed %d -> %d, rebuilding", P.nb, n); P.nb = 0; return; }
    }
    uint8_t buf[2 + MAXB * 12 + 2 + MAXB * 8]; int len = 2;
    int16_t sparse[MAXB][4]; int nsparse = 0;                /* bones whose scale is not 1: index, sx, sy, sz (x1000) */
    buf[0] = (uint8_t)(P.nb & 255); buf[1] = (uint8_t)(P.nb >> 8);
    for (int i = 0; i < P.nb; i++) {
        Transform tr; memset(&tr, 0, sizeof tr); tr.q[3] = 1;
        uint8_t space = (P.mode == 0 || P.mode == 3) ? 2 : 3;   /* RTS_Component / RTS_ParentBoneSpace */
        const char *names[2] = { "InSocketName", "TransformSpace" }; const void *vals[2] = { P.fnm[i], &space }; int sizes[2] = { -2, 1 };
        a->call_fn(mesh, "SceneComponent", "GetSocketTransform", 2, names, vals, sizes, &tr, (int)sizeof tr);
        int bad = 0;                                         /* NaN / infinite / absurd values would be packed into garbage: send such a bone as "unresolved" (zeros) */
        for (int c = 0; c < 4; c++) if (!isfinite(tr.q[c])) bad = 1;
        for (int c = 0; c < 3; c++) if (!isfinite(tr.t[c]) || fabs(tr.t[c]) > 600 || !isfinite(tr.s[c])) bad = 1;
        if (bad) {
            if (P.nbad < 40) { P.nbad++; a->log("P bad bone #%d '%s' loc %g %g %g quat %g %g %g %g scale %g", i, P.names[i], tr.t[0], tr.t[1], tr.t[2], tr.q[0], tr.q[1], tr.q[2], tr.q[3], tr.s[0]); }
            memset(&tr, 0, sizeof tr); tr.q[3] = 1; tr.s[0] = tr.s[1] = tr.s[2] = 1;
        }
        double nq = sqrt(tr.q[0] * tr.q[0] + tr.q[1] * tr.q[1] + tr.q[2] * tr.q[2] + tr.q[3] * tr.q[3]);
        if (nq < 1e-6) { tr.q[0] = tr.q[1] = tr.q[2] = 0; tr.q[3] = 1; nq = 1; }
        double sg = tr.q[3] < 0 ? -1.0 / nq : 1.0 / nq;       /* w >= 0, so w is rebuilt from x, y, z */
        int16_t v[6] = { q16(tr.q[0] * sg, 32767), q16(tr.q[1] * sg, 32767), q16(tr.q[2] * sg, 32767),
                         q16(tr.t[0], 50), q16(tr.t[1], 50), q16(tr.t[2], 50) };
        memcpy(buf + len, v, 12); len += 12;
        if (P.frame % 300 == 1 && i > 0 && P.diag < 60 && strncmp(P.names[i], "face_", 5) != 0) {      /* bones that sit at the origin or very far away stretch the skin: name them */
            double d = sqrt(tr.t[0] * tr.t[0] + tr.t[1] * tr.t[1] + tr.t[2] * tr.t[2]);
            if (d < 0.01 || d > 400) { P.diag++; a->log("P diag bone #%d '%s' loc %.1f %.1f %.1f quat %.2f %.2f %.2f %.2f scale %.2f", i, P.names[i], tr.t[0], tr.t[1], tr.t[2], tr.q[0], tr.q[1], tr.q[2], tr.q[3], tr.s[0]); }
        }
        if (fabs(tr.s[0] - 1) > 0.002 || fabs(tr.s[1] - 1) > 0.002 || fabs(tr.s[2] - 1) > 0.002) {
            sparse[nsparse][0] = (int16_t)i; sparse[nsparse][1] = q16(tr.s[0], 1000); sparse[nsparse][2] = q16(tr.s[1], 1000); sparse[nsparse][3] = q16(tr.s[2], 1000); nsparse++;
        }
    }
    if (P.frame % 300 == 1 && P.diag2 < 40) {                /* a bone far from its parent cannot be right (hand-elbow is ~30): name those */
        for (int i = 1; i < P.nb && P.diag2 < 40; i++) {
            int pi = P.parent[i]; if (pi < 0) continue;
            int16_t a6[3], b6[3]; memcpy(a6, buf + 2 + i * 12 + 6, 6); memcpy(b6, buf + 2 + pi * 12 + 6, 6);
            if (!(a6[0] | a6[1] | a6[2]) || !(b6[0] | b6[1] | b6[2])) continue;   /* skip the unresolved zeros */
            double dx = (a6[0] - b6[0]) / 50.0, dy = (a6[1] - b6[1]) / 50.0, dz = (a6[2] - b6[2]) / 50.0, d = sqrt(dx * dx + dy * dy + dz * dz);
            if (d > 80) { P.diag2++; a->log("P diag2 bone #%d '%s' is %.0f from parent #%d '%s' (loc %.0f %.0f %.0f)", i, P.names[i], d, pi, P.names[pi], a6[0] / 50.0, a6[1] / 50.0, a6[2] / 50.0); }
        }
    }
    {   /* recon: pose with the sword sheathed vs drawn. Keep a snapshot while sheathed; 2 s after the draw list the bones that moved most */
        uint8_t *carry = ptr_prop(a, a->hero, "m_CarryComponent");
        int weapon = ptr_prop(a, carry, "m_EquippedItem") != NULL;
        unsigned long tnow = GetTickCount();
        if (!weapon) { P.drawn_t = 0; P.dumped = 0; if (tnow - P.snap_t > 1000) { P.snap_t = tnow; memcpy(P.snap, buf + 2, (size_t)P.nb * 12); P.snap_nb = P.nb; } }
        else {
            if (!P.drawn_t) P.drawn_t = tnow;
            if (!P.dumped && tnow - P.drawn_t > 2000 && P.snap_nb == P.nb) {
                P.dumped = 1;
                struct { double d; int i; } top[MAXB]; int nt = 0;
                for (int i = 1; i < P.nb; i++) {
                    int16_t *c = (int16_t *)(buf + 2 + i * 12), *s = (int16_t *)((uint8_t *)P.snap + i * 12);
                    if (!(c[3] | c[4] | c[5]) || !(s[3] | s[4] | s[5])) continue;
                    double dx = (c[3] - s[3]) / 50.0, dy = (c[4] - s[4]) / 50.0, dz = (c[5] - s[5]) / 50.0;
                    top[nt].d = sqrt(dx * dx + dy * dy + dz * dz); top[nt].i = i; nt++;
                }
                for (int k = 0; k < 30 && k < nt; k++) {
                    int m = k; for (int j = k + 1; j < nt; j++) if (top[j].d > top[m].d) m = j;
                    if (m != k) { double td = top[k].d; int ti = top[k].i; top[k] = top[m]; top[m].d = td; top[m].i = ti; }
                    a->log("P drawn-vs-sheathed #%d '%s' moved %.1f", top[k].i, P.names[top[k].i], top[k].d);
                }
                a->log("P drawn-vs-sheathed: %d bones with scale != 1", nsparse);
                for (int k = 0; k < nsparse && k < 20; k++) a->log("P scaled bone #%d '%s' scale %.2f %.2f %.2f", sparse[k][0], P.names[sparse[k][0]], sparse[k][1] / 1000.0, sparse[k][2] / 1000.0, sparse[k][3] / 1000.0);
            }
        }
    }
    buf[len++] = (uint8_t)(nsparse & 255); buf[len++] = (uint8_t)(nsparse >> 8);
    memcpy(buf + len, sparse, (size_t)nsparse * 8); len += nsparse * 8;
    if (P.frame % 900 == 1) a->log("P sender: blob %d bytes, %d bones with scale != 1", len, nsparse);
    a->set_local_blob(buf, len);
}

/* ---------------- receiver ---------------- */
/* crash bisecting: build\pose_variant.txt = "seat0=minimal seat1=noextra". The seat comes from the "-CoopSeat=N" argument Couchlink passes.
 *   minimal  = only add the PoseableMeshComponent to the puppet, nothing else (no mesh, no attach, no collision calls, no LOD, no bones)
 *   noextra  = mesh + attach, but no collision calls and no forced LOD
 *   full     = everything (default) */
static int my_seat_no(void)
{
    static int seat = -2;
    if (seat == -2) { const char *c = strstr(GetCommandLineA(), "-CoopSeat="); seat = c ? atoi(c + 10) : -1; }
    return seat;
}
static int variant_of_seat(void)                              /* 0 full, 1 minimal, 2 noextra */
{
    static unsigned long t; static int v;
    unsigned long now = GetTickCount();
    if (now - t < 1000) return v;
    t = now; v = 0;
    FILE *f = fopen("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Gothic1R-coop\\native\\build\\pose_variant.txt", "r");
    if (f) {
        char line[200] = ""; if (fgets(line, sizeof line, f)) {
            char key[24]; snprintf(key, sizeof key, "seat%d=", my_seat_no());
            char *p = strstr(line, key);
            if (p) { p += strlen(key); if (!strncmp(p, "minimal", 7)) v = 1; else if (!strncmp(p, "noextra", 7)) v = 2; }
        }
        fclose(f);
    }
    return v;
}

static int ensure_comp(CoreAPI *a)
{
    if (P.comp && P.comp_puppet == a->puppet && a->obj_alive(P.comp, NULL)) return 1;
    unsigned long now = GetTickCount();
    if (now - P.last_try < 1000) return 0;
    P.last_try = now;
    P.comp = NULL; P.comp_puppet = NULL;
    if (!P.pclass) P.pclass = a->find_object("Class", "PoseableMeshComponent", "/Script/Engine");
    if (!P.pclass) { a->log("P PoseableMeshComponent class not found"); P.failed++; return 0; }
    Transform id; memset(&id, 0, sizeof id); id.q[3] = 1; id.s[0] = id.s[1] = id.s[2] = 1;
    uint8_t manual = 1, defer = 0; uint8_t *comp = NULL;
    const char *names[4] = { "Class", "bManualAttachment", "RelativeTransform", "bDeferredFinish" };
    const void *vals[4] = { &P.pclass, &manual, &id, &defer }; int sizes[4] = { 8, 1, (int)sizeof id, 1 };
    a->call_fn(a->puppet, "Actor", "AddComponentByClass", 4, names, vals, sizes, &comp, 8);
    a->log("P AddComponentByClass -> %p", comp);
    if (!comp) { P.failed++; return 0; }
    int variant = variant_of_seat();
    if (variant == 1) {                                     /* minimal: the bare component, nothing else */
        P.comp = comp; P.comp_puppet = a->puppet;
        a->log("P variant MINIMAL (seat %d): poseable component %p added and left alone", my_seat_no(), comp);
        return 1;
    }
    uint8_t *asset = ptr_prop(a, a->puppet_mesh, "SkinnedAsset");
    uint8_t reinit = 1;
    const char *n2[2] = { "NewMesh", "bReinitPose" }; const void *v2[2] = { &asset, &reinit }; int s2[2] = { 8, 1 };
    a->call_fn(comp, "SkinnedMeshComponent", "SetSkinnedAssetAndUpdate", 2, n2, v2, s2, NULL, 0);
    uint8_t snap = 2, weld = 0; uint8_t *par = a->puppet_mesh;
    const char *n3[6] = { "Parent", "SocketName", "LocationRule", "RotationRule", "ScaleRule", "bWeldSimulatedBodies" };
    const void *v3[6] = { &par, "None", &snap, &snap, &snap, &weld }; int s3[6] = { 8, -1, 1, 1, 1, 1 };
    a->call_fn(comp, "SceneComponent", "K2_AttachToComponent", 6, n3, v3, s3, NULL, 0);
    if (variant != 2) {   /* no collision at all on the puppet mesh: the local hero's sword must not be able to hit it */
        uint8_t none = 0, ignore = 0;
        call1(a, comp, "PrimitiveComponent", "SetCollisionEnabled", "NewType", &none, 1, NULL, 0);
        call1(a, comp, "PrimitiveComponent", "SetCollisionResponseToAllChannels", "NewResponse", &ignore, 1, NULL, 0);
        uint8_t off = 0;
        call1(a, comp, "PrimitiveComponent", "SetGenerateOverlapEvents", "bInGenerateOverlapEvents", &off, 1, NULL, 0);
    }
    int lod0 = 1;                                           /* forced LOD 0 (1-based): distant LODs drop bones and the skin then stretches */
    if (variant != 2) call1(a, comp, "SkinnedMeshComponent", "SetForcedLOD", "InNewForcedLOD", &lod0, 4, NULL, 0);
    if (variant == 2) a->log("P variant NOEXTRA (seat %d): mesh + attach only, no collision calls, no forced LOD", my_seat_no());
    set_visible(a, comp, 1);
    if (!P.nohide) set_visible(a, a->puppet_mesh, 0);
    P.comp = comp; P.comp_puppet = a->puppet; P.last_seq = 0; P.applied = 0;
    a->set_attach_target(comp);
    a->log("P puppet now bone-driven: comp %p asset %p", comp, asset);
    return 1;
}

static void apply_blob(CoreAPI *a)
{
    const uint8_t *b = a->remote_blob; int len = a->remote_blob_len;
    if (len < 2) return;
    int n = b[0] | (b[1] << 8);
    if (n > MAXB || len < 2 + n * 12 + 2) return;
    if (P.nb != n) return;                                    /* bone names/parents come from this process's own hero mesh (same asset); wait for them */
    static uint8_t chg[MAXB]; static Transform todo[MAXB]; static int todo_idx[MAXB]; int ntodo = 0;
    int16_t sc[MAXB][3];
    for (int i = 0; i < n; i++) sc[i][0] = sc[i][1] = sc[i][2] = 1000;
    int ns = b[2 + n * 12] | (b[3 + n * 12] << 8);
    if (ns > n || len < 4 + n * 12 + ns * 8) return;
    for (int k = 0; k < ns; k++) {
        int16_t e[4]; memcpy(e, b + 4 + n * 12 + k * 8, 8);
        if (e[0] >= 0 && e[0] < n) { sc[e[0]][0] = e[1]; sc[e[0]][1] = e[2]; sc[e[0]][2] = e[3]; }
    }
    for (int i = 0; i < n; i++) {
        int16_t v[9]; memcpy(v, b + 2 + i * 12, 12); memcpy(v + 6, sc[i], 6);
        int changed = memcmp(v, P.last[i], 18) != 0;
        memcpy(P.last[i], v, 18);
        if (P.parent[i] >= 0 && chg[P.parent[i]]) changed = 1;   /* a bone follows its parent: skip only when the whole chain is unchanged */
        chg[i] = (uint8_t)changed;
        if (!changed) continue;
        if (P.skip[i]) continue;                                     /* build\pose_skip.txt: "min=N;substr;substr" bones the receiver must leave alone (bisecting the sword-draw bug) */
        if (strncmp(P.names[i], "ik_", 3) == 0) continue;              /* IK helper bones (they come alive with a drawn sword): nothing is skinned to them, their poses only disturb the rest */
        if (i > 0 && !(v[0] | v[1] | v[2] | v[3] | v[4] | v[5])) continue;   /* the hero's mesh gives identity-at-origin for bones it does not animate (face bones): keep their reference pose, do not pull them to the feet */
        double x = v[0] / 32767.0, y = v[1] / 32767.0, z = v[2] / 32767.0;
        double w2 = 1.0 - x * x - y * y - z * z;
        Transform *tr = &todo[ntodo]; memset(tr, 0, sizeof *tr);
        tr->q[0] = x; tr->q[1] = y; tr->q[2] = z; tr->q[3] = w2 > 0 ? sqrt(w2) : 0;
        tr->t[0] = v[3] / 50.0; tr->t[1] = v[4] / 50.0; tr->t[2] = v[5] / 50.0;
        tr->s[0] = v[6] / 1000.0; tr->s[1] = v[7] / 1000.0; tr->s[2] = v[8] / 1000.0;
        todo_idx[ntodo++] = i;
    }
    uint8_t space = P.mode == 2 ? 0 : 1;                      /* EBoneSpaces: 0 world, 1 component */
    for (int pass = 0; pass < (P.mode == 3 ? 2 : 1); pass++)  /* mode 3: a second pass, in case parents were stale during the first */
        for (int k = 0; k < ntodo; k++) {
            const char *names[3] = { "BoneName", "InTransform", "BoneSpace" }; const void *vals[3] = { P.fnm[todo_idx[k]], &todo[k], &space }; int sizes[3] = { -2, (int)sizeof(Transform), 1 };
            a->call_fn(P.comp, "PoseableMeshComponent", "SetBoneTransformByName", 3, names, vals, sizes, NULL, 0);
        }
    if (P.applied++ % 200 == 0) a->log("P applied blob #%d (%d bones)", P.applied, n);
}

__declspec(dllexport) void mod_update(CoreAPI *a)
{
    unsigned long now = GetTickCount();
    static int nosend; static unsigned long nosend_t;
    if (now - nosend_t > 1000) {                           /* build\pose_nosend.txt: do not read the hero's bones at all (bisecting a crash after sword swings) */
        nosend_t = now;
        int v = GetFileAttributesA("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Gothic1R-coop\\native\\build\\pose_nosend.txt") != INVALID_FILE_ATTRIBUTES;
        if (v != nosend) { nosend = v; a->log("P sending of bone poses %s", v ? "OFF (pose_nosend.txt)" : "ON"); }
    }
    if (a->hero && !nosend) send_pose(a);

    if (now - P.last_chk > 1000) {
        P.last_chk = now;
        {   /* more switches for bisecting the crash: pose_nohide.txt = never hide the original puppet mesh, pose_nocomp.txt = never create the poseable component */
            int nh = GetFileAttributesA("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Gothic1R-coop\\native\\build\\pose_nohide.txt") != INVALID_FILE_ATTRIBUTES;
            int nc = GetFileAttributesA("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Gothic1R-coop\\native\\build\\pose_nocomp.txt") != INVALID_FILE_ATTRIBUTES;
            if (nh != P.nohide || nc != P.nocomp) { P.nohide = nh; P.nocomp = nc; a->log("P switches: nohide=%d nocomp=%d", nh, nc); }
        }
        int off = GetFileAttributesA("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Gothic1R-coop\\native\\build\\pose_off.txt") != INVALID_FILE_ATTRIBUTES;
        FILE *mf = fopen("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Gothic1R-coop\\native\\build\\pose_mode.txt", "r");
        int mode = 0; if (mf) { if (fscanf(mf, "%d", &mode) != 1) mode = 0; fclose(mf); }
        {   /* skip mask */
            char line[300] = ""; FILE *sf = fopen("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Gothic1R-coop\\native\\build\\pose_skip.txt", "r");
            if (sf) { if (fgets(line, sizeof line, sf)) { size_t n = strlen(line); while (n && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' ')) line[--n] = 0; } fclose(sf); }
            if (strcmp(line, P.skip_txt) != 0) {
                snprintf(P.skip_txt, sizeof P.skip_txt, "%s", line);
                int minidx = 100000; char subs[8][40]; int ns = 0; char tmp[300]; snprintf(tmp, sizeof tmp, "%s", line);
                for (char *t = strtok(tmp, ";"); t && ns < 8; t = strtok(NULL, ";")) { if (strncmp(t, "min=", 4) == 0) minidx = atoi(t + 4); else if (t[0]) snprintf(subs[ns++], 40, "%s", t); }
                int cnt = 0;
                for (int i = 0; i < MAXB; i++) {
                    P.skip[i] = 0;
                    if (i >= minidx) P.skip[i] = 1;
                    for (int k = 0; k < ns && !P.skip[i]; k++) if (strstr(P.names[i], subs[k])) P.skip[i] = 1;
                    cnt += P.skip[i];
                }
                memset(P.last, 0, sizeof P.last);
                a->log("P skip mask '%s' -> %d bones", line, cnt);
            }
        }
        if (mode != P.mode) { P.mode = mode; memset(P.last, 0, sizeof P.last); a->log("P mode -> %d", mode); }
        if (off != P.off) {
            P.off = off;
            a->log("P pose driving %s", off ? "OFF (clips)" : "ON");
            if (off) { if (a->puppet_mesh) set_visible(a, a->puppet_mesh, 1); destroy_comp(a); }
        }
    }
    if (P.off || P.nocomp || P.failed > 3 || !a->have_remote || !a->puppet || !a->puppet_mesh) return;
    if (!ensure_comp(a)) return;
    if (now - P.last_vis > 1000) { P.last_vis = now; if (!P.nohide) set_visible(a, a->puppet_mesh, 0); }   /* keep the clip-driven mesh hidden */
    if (variant_of_seat() == 1) return;                     /* minimal variant: no bones are applied */
    if (a->remote_blob_seq != P.last_seq) { P.last_seq = a->remote_blob_seq; apply_blob(a); }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) { (void)h; (void)r; (void)x; return TRUE; }
