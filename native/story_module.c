/*
 * story module: story flags and quest states are the same in both copies.
 *  - story: the StoryG1R object is one flat block of ints / time values (only the chapter lives in its parent, GameStory). Every second the block is compared with
 *    "what both copies last agreed on"; changed words are sent on the third channel (delta list) and stay in the list until the other copy acknowledges the blob.
 *  - quests: every Quest object in QuestSubsystem.AllQuestInstances has a one byte State; same delta mechanism, keyed by a hash of the quest's object name.
 *  - first contact: the host sends everything, the guest takes over (the host leads, like the rest of the world). A conflict (both changed the same word) is won by the host.
 * Kill switch: build\story_off.txt. Hot-reloadable.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "coreapi.h"

#define BUILD "C:/Program Files (x86)/Steam/steamapps/common/Gothic1R-coop/native/build/"
#define MAXW 1024
#define MAXQ 1024

#define MAXI 400
typedef struct { uint8_t *ptr; char name[96]; uint8_t id[16]; double pos[3]; uint32_t fname[2]; int chest; uint8_t *mod; int base; } It;
typedef struct { uint8_t id[16]; unsigned sent; } Gone;
typedef struct { uint32_t id; uint8_t *ptr; uint8_t base, cur, sentv; int pend; unsigned sent; } Q;

static struct {
    int role, off;
    uint8_t *story, *qsys; uint32_t story_fn[2], qsys_fn[2];
    uint8_t *cls_story, *cls_qsys;
    int blk_off, blk_len, nw, chap_off, state_off, arr_off;
    int32_t base[MAXW]; int32_t cur[MAXW]; uint8_t pend[MAXW]; unsigned sent[MAXW]; int32_t sentv[MAXW];
    Q q[MAXQ]; int nq, last_nq;
    int scan_pos, scanning; unsigned long last_scan, last_tick, last_stat, last_hero_t; uint8_t *last_hero;
    unsigned my_seq; int last_remote_seq; unsigned acked_by_remote, remote_last_seq_u16; int ack_dirty;
    int full_pending, have_contact;
    int n_sent_w, n_sent_q, n_applied_w, n_applied_q;
    int hist[256]; int histlogged;
    /* world items: picked up here -> removed there (matched by the persistent id every placed actor has) */
    It it[MAXI]; int nit; Gone gone[128]; int ngone; uint8_t removed[2048][16]; int nremoved;
    uint8_t *cls_cont; int keep_birds, n_birds; int n_chest_sent, n_chest_applied; uint8_t *cls_ok[300]; uint8_t cls_has[300]; int ncls; int it_off_cache; int isc_pos; int n_pick_sent, n_pick_applied;
} S;

__declspec(dllexport) void mod_init(CoreAPI *a) { (void)a; memset(&S, 0, sizeof S); }

static int role_of(void)
{
    char line[40] = ""; FILE *f = fopen(BUILD "role.txt", "r");
    if (f) { if (fgets(line, sizeof line, f)) { if (strstr(line, "guest")) { fclose(f); return 2; } if (strstr(line, "host")) { fclose(f); return 1; } } fclose(f); }
    const char *c = strstr(GetCommandLineA(), "-CoopSeat=");
    return (c && atoi(c + 10) > 0) ? 2 : 1;
}

/* the core's obj_alive(ptr, name) compares a cached name per FName index, which is wrong for objects that differ only by the number suffix: identity is checked on the raw FName (index + number) instead */
static int same_obj(CoreAPI *a, uint8_t *o, const uint32_t *fname)
{
    if (!a->obj_alive(o, NULL)) return 0;
    uint32_t f[2]; if (!a->read_mem(o + 0x18, f, 8)) return 0;
    return f[0] == fname[0] && f[1] == fname[1];
}
static uint32_t fnv(const char *s) { uint32_t h = 2166136261u; for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; } return h; }

struct Blk { int lo, hi; };
static void blk_cb(const char *kind, const char *sname, const char *name, const char *type, int off, int size, void *ud)
{
    struct Blk *b = (struct Blk *)ud; (void)name; (void)type;
    if (strcmp(kind, "prop") || strcmp(sname, "StoryG1R")) return;
    if (off < b->lo) b->lo = off;
    if (off + size > b->hi) b->hi = off + size;
}

static void reset_story(void) { S.story = NULL; S.nw = 0; S.blk_len = 0; }

/* the sliced search for the two objects; returns when both are found */
static void find_objects(CoreAPI *a)
{
    if (!S.cls_story) { uint8_t *d = a->find_object(NULL, "Default__StoryG1R", NULL); if (d) S.cls_story = a->obj_class(d); }
    if (!S.cls_qsys) { uint8_t *d = a->find_object(NULL, "Default__QuestSubsystem", NULL); if (d) S.cls_qsys = a->obj_class(d); }
    if (!S.cls_story || !S.cls_qsys) { static unsigned long lg; if (GetTickCount() - lg > 10000) { lg = GetTickCount(); a->log("S classes not found yet (story %p, quest subsystem %p)", S.cls_story, S.cls_qsys); } return; }
    int n = a->num_objects();
    if (!S.scanning) { S.scanning = 1; S.scan_pos = 0; }
    int end = S.scan_pos + 12000; if (end > n) end = n;
    for (int i = S.scan_pos; i < end; i++) {
        uint8_t *o = a->object_at(i); if (!o) continue;
        uint8_t *c = a->obj_class(o);
        if (c != S.cls_story && c != S.cls_qsys) continue;
        char nm[96]; a->obj_name(o, nm, sizeof nm);
        if (!strncmp(nm, "Default__", 9)) continue;
        if (c == S.cls_story) { S.story = o; a->read_mem(o + 0x18, S.story_fn, 8); }
        else { S.qsys = o; a->read_mem(o + 0x18, S.qsys_fn, 8); }
    }
    S.scan_pos = end;
    if (end >= n) S.scanning = 0;
}

static void init_story(CoreAPI *a)
{
    struct Blk b = { 1 << 30, 0 };
    a->list_members(S.cls_story, blk_cb, &b);
    S.blk_off = b.lo; S.blk_len = (b.hi - b.lo) & ~3; S.nw = S.blk_len / 4;
    S.chap_off = a->find_prop_offset(S.cls_story, "Chapter");
    if (S.nw + 1 > MAXW || S.nw <= 0) { a->log("S story block %d..%d does not fit", b.lo, b.hi); S.nw = 0; return; }
    a->read_mem(S.story + S.blk_off, S.cur, (size_t)S.blk_len);
    if (S.chap_off >= 0) a->read_mem(S.story + S.chap_off, &S.cur[S.nw], 4);
    for (int i = 0; i <= S.nw; i++) { S.base[i] = S.role == 1 ? ~S.cur[i] : S.cur[i]; S.pend[i] = 0; S.sent[i] = 0; }      /* the host starts with "everything changed" = a full sync */
    a->log("S story %s: block @%d, %d words, chapter @%d (role %s)", "object", S.blk_off, S.nw, S.chap_off, S.role == 1 ? "host" : "guest");
}

static void index_quests(CoreAPI *a)
{
    S.arr_off = a->find_prop_offset(a->obj_class(S.qsys), "AllQuestInstances");
    uint8_t *data = NULL; int num = 0;
    if (S.arr_off < 0 || !a->read_mem(S.qsys + S.arr_off, &data, 8) || !a->read_mem(S.qsys + S.arr_off + 8, &num, 4) || !data || num <= 0) { S.nq = 0; return; }
    Q old[MAXQ]; int nold = S.nq; memcpy(old, S.q, sizeof old);
    S.nq = 0;
    for (int i = 0; i < num && S.nq < MAXQ; i++) {
        uint8_t *q = NULL; if (!a->read_mem(data + (size_t)i * 8, &q, 8) || !q) continue;
        char nm[160]; a->obj_name(q, nm, sizeof nm);
        Q *e = &S.q[S.nq++]; memset(e, 0, sizeof *e); e->id = fnv(nm); e->ptr = q;
        if (S.state_off <= 0) S.state_off = a->find_prop_offset(a->obj_class(q), "State");
        uint8_t st = 0; if (S.state_off > 0) a->read_mem(q + S.state_off, &st, 1);
        e->cur = st; e->base = S.role == 1 ? (uint8_t)(st ^ 0xff) : st;
        for (int k = 0; k < nold; k++) if (old[k].id == e->id) { e->base = old[k].base; e->pend = old[k].pend; e->sent = old[k].sent; break; }
    }
    S.last_nq = num;
    a->log("S quests: %d instances indexed, State @%d", S.nq, S.state_off);
}

/* a live change goes through the game's own functions (journal entry, notification, rewards for this hero); a bulk change (first sync) and anything the functions refuse is written directly */
static int write_state(CoreAPI *a, Q *q, uint8_t st, int bulk)
{
    if (!bulk) {
        const char *fn = st == 2 ? "StartQuest" : st == 4 ? "SucceedQuest" : st == 1 ? "MakeQuestAvailable" : NULL;
        uint8_t now = 0;
        if (fn) {
            a->call_fn(q->ptr, "Quest", fn, 0, NULL, NULL, NULL, NULL, 0);
            if (a->read_mem(q->ptr + S.state_off, &now, 1) && now == st) return 1;
        }
    }
    return a->write_mem(q->ptr + S.state_off, &st, 1);
}

static int cls_has_item(CoreAPI *a, uint8_t *k, int *idoff)
{
    for (int i = 0; i < S.ncls; i++) if (S.cls_ok[i] == k) { if (S.cls_has[i] == 1 || S.cls_has[i] == 2) *idoff = a->find_prop_offset(k, "m_PersistentId"); return S.cls_has[i]; }
    int has = 0;
    { char cn[96]; a->obj_name(k, cn, sizeof cn); for (char *c = cn; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
      if (strstr(cn, "pidgeon_bp") || strstr(cn, "crow_bp") || strstr(cn, "ambientlifewarningflock")) has = 3; }          /* ambient birds: their audio emitters crash the game (fmod, exe+0x5be3977) */
    if (!has && a->find_prop_offset(k, "m_PersistentId") >= 0) {
        if (a->find_prop_offset(k, "m_WorldItemDefinition") >= 0) has = 1;
        else if (a->find_prop_offset(k, "m_DataModuleComponent") >= 0) { char cn[96]; a->obj_name(k, cn, sizeof cn); for (char *c = cn; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32; if (strstr(cn, "chest")) has = 2; }
    }
    if (S.ncls < 300) { S.cls_ok[S.ncls] = k; S.cls_has[S.ncls] = (uint8_t)has; S.ncls++; }
    if (has == 1 || has == 2) *idoff = a->find_prop_offset(k, "m_PersistentId");
    return has;
}
static int loc_of(CoreAPI *a, uint8_t *o, double *v) { double r[3] = { 0, 0, 0 }; if (!a->call_fn(o, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, r, 24)) return 0; v[0] = r[0]; v[1] = r[1]; v[2] = r[2]; return 1; }
static uint8_t *chest_module(CoreAPI *a, uint8_t *actor)
{
    if (!S.cls_cont) { uint8_t *d = a->find_object(NULL, "Default__DataModule_Container", NULL); if (d) S.cls_cont = a->obj_class(d); }
    int oc = a->find_prop_offset(a->obj_class(actor), "m_DataModuleComponent"); uint8_t *comp = NULL; if (oc < 0 || !a->read_mem(actor + oc, &comp, 8) || !comp) return NULL;
    int oa = a->find_prop_offset(a->obj_class(comp), "m_DataModules"); uint8_t *arr = NULL; int num = 0;
    if (oa < 0 || !a->read_mem(comp + oa, &arr, 8) || !a->read_mem(comp + oa + 8, &num, 4) || !arr || num <= 0 || num > 32) return NULL;
    for (int i = 0; i < num; i++) { uint8_t *m = NULL; if (a->read_mem(arr + (size_t)i * 8, &m, 8) && m && a->obj_class(m) == S.cls_cont) return m; }
    return NULL;
}
static int chest_count(CoreAPI *a, uint8_t *mod)
{
    int oi = a->find_prop_offset(S.cls_cont, "m_Inventory"); int n0 = 0, n1 = 0;
    if (oi < 0 || !a->read_mem(mod + oi + 8, &n0, 4) || !a->read_mem(mod + oi + 24, &n1, 4)) return -1;
    if (n0 < 0 || n1 < 0 || n0 > 10000 || n1 > 10000) return -1;
    return n0 + n1;
}
static void chest_empty(CoreAPI *a, It *t)
{
    if (t->mod) {
        int oi = a->find_prop_offset(S.cls_cont, "m_Inventory"), z = 0;                    /* the game's own Server_Debug_EmptyInventory faults without authority: both item lists are given length 0 */
        a->write_mem(t->mod + oi + 8, &z, 4); a->write_mem(t->mod + oi + 24, &z, 4);
    }
    t->base = 0;
}
static int is_removed(const uint8_t *id) { for (int i = 0; i < S.nremoved; i++) if (!memcmp(S.removed[i], id, 16)) return 1; return 0; }
static void add_removed(const uint8_t *id) { if (is_removed(id)) return; if (S.nremoved < 2048) memcpy(S.removed[S.nremoved++], id, 16); }
static void idstr(const uint8_t *id, char *o) { sprintf(o, "%02x%02x%02x%02x-%02x%02x%02x%02x", id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]); }

/* a few slices of the object table per second: placed world items near this hero get tracked */
static void scan_items(CoreAPI *a, double *hp)
{
    int n = a->num_objects(); if (S.isc_pos >= n) S.isc_pos = 0;
    int end = S.isc_pos + 30000; if (end > n) end = n;
    for (int i = S.isc_pos; i < end; i++) {
        uint8_t *o = a->object_at(i); if (!o) continue;
        int idoff = -1; int kind = cls_has_item(a, a->obj_class(o), &idoff); if (!kind) continue;
        if (kind == 3) {
            if (S.keep_birds) continue;
            uint32_t fl = 0; a->read_mem(o + 8, &fl, 4); if ((fl & 0x8000) || a->read_bool(o, "bActorIsBeingDestroyed") == 1) continue;
            char bn[96]; a->obj_name(o, bn, sizeof bn); if (!strncmp(bn, "Default__", 9)) continue;
            a->destroy_actor(o); S.n_birds++; if (S.n_birds <= 5 || S.n_birds % 50 == 0) a->log("S ambient bird/flock removed: %s (%d so far)", bn, S.n_birds); continue;
        }
        if (idoff < 0) continue;
        int known = 0; for (int j = 0; j < S.nit; j++) if (S.it[j].ptr == o) { known = 1; break; }
        if (known || S.nit >= MAXI) continue;
        { uint32_t fl = 0; a->read_mem(o + 8, &fl, 4); if ((fl & 0x8000) || a->read_bool(o, "bActorIsBeingDestroyed") == 1) continue; }          /* already destroyed, waiting for the garbage collector */
        char nm[96]; a->obj_name(o, nm, sizeof nm); if (!strncmp(nm, "Default__", 9)) continue;
        double p[3]; if (!loc_of(a, o, p)) continue;
        double dx = p[0] - hp[0], dy = p[1] - hp[1]; if (dx * dx + dy * dy > 3500.0 * 3500.0) continue;
        It *t = &S.it[S.nit]; memset(t, 0, sizeof *t); t->ptr = o; t->chest = kind == 2; snprintf(t->name, sizeof t->name, "%s", nm); a->read_mem(o + 0x18, t->fname, 8); a->read_mem(o + idoff, t->id, 16); memcpy(t->pos, p, sizeof p);
        { int zero = 1; for (int b = 0; b < 16; b++) if (t->id[b]) zero = 0;
          if (zero) {                                                                         /* spawned by the level, no persistent id: class + position (deterministic in both copies) is the key */
              char cn[96]; a->obj_name(a->obj_class(o), cn, sizeof cn); uint32_t h1 = fnv(cn), h2 = 2166136261u;
              long q[3] = { (long)(p[0] / 25.0 + (p[0] < 0 ? -0.5 : 0.5)), (long)(p[1] / 25.0 + (p[1] < 0 ? -0.5 : 0.5)), (long)(p[2] / 25.0 + (p[2] < 0 ? -0.5 : 0.5)) };
              for (int b = 0; b < 3; b++) { h2 ^= (uint32_t)q[b]; h2 *= 16777619u; h1 ^= (uint32_t)(q[b] >> 3); h1 *= 16777619u; }
              memcpy(t->id, &h1, 4); memcpy(t->id + 4, &h2, 4); t->id[15] = 0xA5; } }
        if (t->chest) { t->id[14] ^= 0xC3; t->mod = chest_module(a, o); if (!t->mod) continue; t->base = chest_count(a, t->mod); if (t->base < 0) continue; }
        if (t->chest && is_removed(t->id)) { chest_empty(a, t); S.n_chest_applied++; a->log("S chest %s emptied on arrival (looted by the other player)", nm); S.nit++; continue; }
        if (is_removed(t->id)) { a->destroy_actor(o); a->log("S item %s removed on arrival (picked up by the other player)", nm); continue; }
        if (t->chest) a->log("S chest tracked: %s, %d items", nm, t->base);
        S.nit++;
    }
    S.isc_pos = end;
}

static void check_items(CoreAPI *a, double *hp)
{
    static double last_hp[3]; static unsigned long quiet_until;
    double mx = hp[0] - last_hp[0], my = hp[1] - last_hp[1]; memcpy(last_hp, hp, sizeof last_hp);
    if (mx * mx + my * my > 1500.0 * 1500.0) quiet_until = GetTickCount() + 15000;                  /* a teleport / load: the world around is rebuilt, nothing that vanishes now was picked up */
    int cand[MAXI], nc = 0;
    for (int j = 0; j < S.nit; ) {
        It *t = &S.it[j]; int dead = !same_obj(a, t->ptr, t->fname);
        if (!dead) { uint32_t fl = 0; a->read_mem(t->ptr + 8, &fl, 4); if (fl & 0x8000) dead = 1; }               /* RF_BeginDestroyed */
        if (!dead && a->read_bool(t->ptr, "bActorIsBeingDestroyed") == 1) dead = 1;                  /* Destroy() was called, the object waits for the garbage collector */
        if (!dead && t->chest) {
            int cnt = chest_count(a, t->mod); double dx = t->pos[0] - hp[0], dy = t->pos[1] - hp[1];
            if (cnt == 0 && t->base > 0 && dx * dx + dy * dy < 2500.0 * 2500.0 && S.ngone < 128 && !is_removed(t->id)) {          /* this hero emptied it */
                memcpy(S.gone[S.ngone].id, t->id, 16); S.gone[S.ngone].sent = 0; S.ngone++; add_removed(t->id); S.n_chest_sent++;
                char b[24]; idstr(t->id, b); a->log("S chest %s emptied here (id %s)", t->name, b);
            }
            if (cnt >= 0) t->base = cnt;
            j++; continue;
        }
        if (!dead) { loc_of(a, t->ptr, t->pos); j++; continue; }
        if (t->chest) { S.it[j++].ptr = NULL; continue; }
        double dx = t->pos[0] - hp[0], dy = t->pos[1] - hp[1];
        if (dx * dx + dy * dy < 2500.0 * 2500.0 && !is_removed(t->id) && nc < MAXI) cand[nc++] = j;
        S.it[j++].ptr = NULL;                                                                   /* mark: forget below */
    }
    int streaming = nc > 2 || GetTickCount() < quiet_until;
    for (int k = 0; k < nc; k++) {
        It *t = &S.it[cand[k]];
        if (streaming || S.ngone >= 128) continue;                                                 /* many at once = a sub-level was unloaded; a pickup is one item at a time */
        memcpy(S.gone[S.ngone].id, t->id, 16); S.gone[S.ngone].sent = 0; S.ngone++; add_removed(t->id); S.n_pick_sent++;
        char b[24]; idstr(t->id, b); a->log("S item %s picked up here (id %s)", t->name, b);
    }
    if (streaming && nc) { a->log("S %d items vanished at once near this hero: treated as streaming, not as pickups%s", nc, GetTickCount() < quiet_until ? " (quiet after a teleport)" : "");
        for (int k = 0; k < nc && k < 4; k++) a->log("S   vanished: %s", S.it[cand[k]].name); }
    for (int j = 0; j < S.nit; ) { if (!S.it[j].ptr) S.it[j] = S.it[--S.nit]; else j++; }
}

static void apply_remote(CoreAPI *a, const uint8_t *B, int len)
{
    if (len < 11) return;
    unsigned seq = B[1] | (B[2] << 8), ack = B[3] | (B[4] << 8); int nwd = B[5] | (B[6] << 8), nq = B[7] | (B[8] << 8); int full = B[9];
    S.have_contact = 1; S.remote_last_seq_u16 = seq; if (nwd + nq > 0) S.ack_dirty = 1;
    /* what the other copy has confirmed */
    for (int i = 0; i <= S.nw; i++) if (S.pend[i] && S.sent[i] && (int16_t)(uint16_t)(ack - S.sent[i]) >= 0) { S.base[i] = S.sentv[i]; S.pend[i] = 0; S.sent[i] = 0; }
    for (int i = 0; i < S.nq; i++) if (S.q[i].pend && S.q[i].sent && (int16_t)(uint16_t)(ack - S.q[i].sent) >= 0) { S.q[i].base = S.q[i].sentv; S.q[i].pend = 0; S.q[i].sent = 0; }
    int p = 11;
    int nit = B[9] | (B[10] << 8); if (nit > 0) S.ack_dirty = 1;
    for (int i = 0; i < S.ngone; ) { if (S.gone[i].sent && (int16_t)(uint16_t)(ack - S.gone[i].sent) >= 0) S.gone[i] = S.gone[--S.ngone]; else i++; }
    if (p + nwd * 6 + nq * 5 + nit * 16 > len) return;
    for (int k = 0; k < nwd; k++, p += 6) {
        int idx = B[p] | (B[p + 1] << 8); int32_t v; memcpy(&v, B + p + 2, 4);
        if (idx > S.nw || !S.story) continue;
        if (S.pend[idx] && S.role == 1) continue;                                        /* conflict: the host wins */
        int off = idx == S.nw ? S.chap_off : S.blk_off + idx * 4; if (off < 0) continue;
        if (a->write_mem(S.story + off, &v, 4)) { S.cur[idx] = v; S.base[idx] = v; S.pend[idx] = 0; S.n_applied_w++; }
    }
    for (int k = 0; k < nq; k++, p += 5) {
        uint32_t id; memcpy(&id, B + p, 4); uint8_t st = B[p + 4];
        for (int i = 0; i < S.nq; i++) if (S.q[i].id == id) {
            Q *e = &S.q[i]; if (e->pend && S.role == 1) break;
            if (write_state(a, e, st, nq > 20)) { e->cur = st; e->base = st; e->pend = 0; S.n_applied_q++; if (nq <= 20) { char nm[160]; a->obj_name(e->ptr, nm, sizeof nm); a->log("S quest %s -> state %d (from the other copy)", nm, st); } }
            break;
        }
    }
    for (int k = 0; k < nit; k++, p += 16) {
        const uint8_t *id = B + p; add_removed(id);
        for (int j = 0; j < S.nit; j++) if (!memcmp(S.it[j].id, id, 16)) {
            if (S.it[j].chest && S.it[j].base == 0) break;                                             /* already empty here: the other copy is only repeating itself until it hears our acknowledgement */
            char b[24]; idstr(id, b); a->log("S %s %s: the other player %s (id %s)", S.it[j].chest ? "chest" : "item", S.it[j].name, S.it[j].chest ? "emptied it" : "picked it up", b);
            if (S.it[j].chest) { chest_empty(a, &S.it[j]); S.n_chest_applied++; break; }
            a->destroy_actor(S.it[j].ptr); S.it[j] = S.it[--S.nit]; S.n_pick_applied++; break;
        }
    }
    (void)full; (void)ack;
}

static unsigned s_ack_seen;
static void send_blob(CoreAPI *a)
{
    uint8_t buf[8800]; int p = 11, nwd = 0, nq = 0;
    S.my_seq = (S.my_seq + 1) & 0xffff; if (!S.my_seq) S.my_seq = 1;
    for (int i = 0; i <= S.nw && p + 6 < 4400; i++) if (S.pend[i]) { buf[p] = (uint8_t)i; buf[p + 1] = (uint8_t)(i >> 8); memcpy(buf + p + 2, &S.cur[i], 4); p += 6; nwd++; S.sent[i] = S.my_seq; S.sentv[i] = S.cur[i]; }
    for (int i = 0; i < S.nq && p + 5 < 8700; i++) if (S.q[i].pend) { memcpy(buf + p, &S.q[i].id, 4); buf[p + 4] = S.q[i].cur; p += 5; nq++; S.q[i].sent = S.my_seq; S.q[i].sentv = S.q[i].cur; }
    int ni = 0; for (int i = 0; i < S.ngone && p + 16 < 8800; i++) { memcpy(buf + p, S.gone[i].id, 16); p += 16; ni++; S.gone[i].sent = S.my_seq; }
    buf[9] = (uint8_t)ni; buf[10] = (uint8_t)(ni >> 8);
    buf[0] = 1; buf[1] = (uint8_t)S.my_seq; buf[2] = (uint8_t)(S.my_seq >> 8); buf[3] = (uint8_t)S.remote_last_seq_u16; buf[4] = (uint8_t)(S.remote_last_seq_u16 >> 8);
    buf[5] = (uint8_t)nwd; buf[6] = (uint8_t)(nwd >> 8); buf[7] = (uint8_t)nq; buf[8] = (uint8_t)(nq >> 8); 
    a->set_local_blob3(buf, p);
    S.n_sent_w += nwd; S.n_sent_q += nq; S.ack_dirty = 0;
    if (nwd || nq || ni) a->log("S sent %d story words, %d quests (seq %u, size %d)", nwd, nq, S.my_seq, p);
}

__declspec(dllexport) void mod_update(CoreAPI *a)
{
    unsigned long now = GetTickCount();
    if (S.scanning && !S.story) goto go;
    if (now - S.last_tick < 1000 && a->remote_blob3_seq == S.last_remote_seq) return;
    go:;
    int fresh = now - S.last_tick >= 1000; if (fresh) S.last_tick = now;
    if (GetFileAttributesA(BUILD "story_off.txt") != INVALID_FILE_ATTRIBUTES) { S.off = 1; return; }
    S.off = 0; S.role = role_of(); S.keep_birds = GetFileAttributesA(BUILD "ambient_keep.txt") != INVALID_FILE_ATTRIBUTES;
    if (a->hero != S.last_hero) { S.last_hero = a->hero; S.last_hero_t = now; reset_story(); S.qsys = NULL; S.nq = 0; S.scanning = 0; S.have_contact = 0; }
    if (!a->hero || !a->have_remote || now - S.last_hero_t < 10000) return;               /* a new world has to settle first */
    if (S.story && !same_obj(a, S.story, S.story_fn)) reset_story();
    if (S.qsys && !same_obj(a, S.qsys, S.qsys_fn)) { S.qsys = NULL; S.nq = 0; }
    if (!S.story || !S.qsys) { find_objects(a); if (S.story && !S.nw) init_story(a); if (S.qsys && !S.nq) index_quests(a); return; }
    if (!S.nw) init_story(a);
    if (!S.nq) index_quests(a);
    if (!S.nw) return;
    /* the other copy's changes first */
    if (a->remote_blob3_seq != S.last_remote_seq) {
        S.last_remote_seq = a->remote_blob3_seq;
        if (a->remote_blob3_len >= 11 && a->remote_blob3[0] == 1) {
            /* refresh the local view first so a change made this very second is not mistaken for a remote one */
            apply_remote(a, a->remote_blob3, a->remote_blob3_len);
        }
    }
    if (!fresh) return;
    /* local changes */
    int32_t cur[MAXW];
    a->read_mem(S.story + S.blk_off, cur, (size_t)S.blk_len);
    if (S.chap_off >= 0) a->read_mem(S.story + S.chap_off, &cur[S.nw], 4); else cur[S.nw] = S.base[S.nw];
    int changed = 0;
    for (int i = 0; i <= S.nw; i++) { S.cur[i] = cur[i]; if (cur[i] != S.base[i] && !S.pend[i]) { S.pend[i] = 1; S.sent[i] = 0; changed++; } else if (cur[i] != S.base[i] && S.pend[i]) { S.sent[i] = 0; } }
    int numq = 0; { uint8_t *d; (void)d; a->read_mem(S.qsys + S.arr_off + 8, &numq, 4); }
    if (numq != S.last_nq) index_quests(a);
    for (int i = 0; i < S.nq; i++) {
        uint8_t st = 0; a->read_mem(S.q[i].ptr + S.state_off, &st, 1); S.q[i].cur = st;
        if (st != S.q[i].base) { if (!S.q[i].pend || S.q[i].sent) S.q[i].sent = 0; S.q[i].pend = 1; changed++; }
    }
    if (!S.histlogged) {                                                                  /* one-time recon: which State values exist */
        S.histlogged = 1; memset(S.hist, 0, sizeof S.hist);
        for (int i = 0; i < S.nq; i++) S.hist[S.q[i].cur]++;
        char ln[400] = "S quest state histogram:"; for (int v = 0; v < 256; v++) if (S.hist[v]) { char t[32]; snprintf(t, sizeof t, " %d:%d", v, S.hist[v]); strcat(ln, t); }
        a->log("%s", ln);
        for (int v = 0; v < 256; v++) { int shown = 0; for (int i = 0; i < S.nq && shown < 3; i++) if (S.q[i].cur == v) {
            uint8_t r[8][1] = {{0}}; const char *fn[6] = { "IsRunning", "IsAvailable", "HasSucceeded", "HasFailed", "HasBeenStarted", "IsCompleted" };
            char res[120] = "", nm[160]; for (int k = 0; k < 6; k++) { uint8_t b = 9; a->call_fn(S.q[i].ptr, "Quest", fn[k], 0, NULL, NULL, NULL, &b, 1); char t[24]; snprintf(t, sizeof t, " %s=%d", fn[k], b); strcat(res, t); }
            a->obj_name(S.q[i].ptr, nm, sizeof nm); a->log("S state %d example %s:%s", v, nm, res); shown++; (void)r; } }
    }
    { double hp[3]; if (loc_of(a, a->hero, hp)) { scan_items(a, hp); check_items(a, hp); } }
    int anyp = S.ngone > 0; for (int i = 0; i <= S.nw && !anyp; i++) anyp = S.pend[i]; for (int i = 0; i < S.nq && !anyp; i++) anyp = S.q[i].pend;
    if (anyp || S.ack_dirty) send_blob(a);
    if (now - S.last_stat > 10000) {
        S.last_stat = now;
        a->log("S role %s: sent %d words / %d quests, applied %d words / %d quests, contact %d, pending %s", S.role == 1 ? "host" : "guest", S.n_sent_w, S.n_sent_q, S.n_applied_w, S.n_applied_q, S.have_contact, anyp ? "yes" : "no");
    }
    (void)changed; (void)s_ack_seen;
}
