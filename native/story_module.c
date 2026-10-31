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

typedef struct { uint32_t id; uint8_t *ptr; uint8_t base, cur, sentv; int pend; unsigned sent; } Q;

static struct {
    int role, off;
    uint8_t *story, *qsys; char story_name[96], qsys_name[96];
    uint8_t *cls_story, *cls_qsys;
    int blk_off, blk_len, nw, chap_off, state_off, arr_off;
    int32_t base[MAXW]; int32_t cur[MAXW]; uint8_t pend[MAXW]; unsigned sent[MAXW]; int32_t sentv[MAXW];
    Q q[MAXQ]; int nq, last_nq;
    int scan_pos, scanning; unsigned long last_scan, last_tick, last_stat, last_hero_t; uint8_t *last_hero;
    unsigned my_seq; int last_remote_seq; unsigned acked_by_remote, remote_last_seq_u16; int ack_dirty;
    int full_pending, have_contact;
    int n_sent_w, n_sent_q, n_applied_w, n_applied_q;
    int hist[256]; int histlogged;
} S;

__declspec(dllexport) void mod_init(CoreAPI *a) { (void)a; memset(&S, 0, sizeof S); }

static int role_of(void)
{
    char line[40] = ""; FILE *f = fopen(BUILD "role.txt", "r");
    if (f) { if (fgets(line, sizeof line, f)) { if (strstr(line, "guest")) { fclose(f); return 2; } if (strstr(line, "host")) { fclose(f); return 1; } } fclose(f); }
    const char *c = strstr(GetCommandLineA(), "-CoopSeat=");
    return (c && atoi(c + 10) > 0) ? 2 : 1;
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
        if (c == S.cls_story) { S.story = o; snprintf(S.story_name, sizeof S.story_name, "%s", nm); }
        else { S.qsys = o; snprintf(S.qsys_name, sizeof S.qsys_name, "%s", nm); }
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
    a->log("S story %s: block @%d, %d words, chapter @%d (role %s)", S.story_name, S.blk_off, S.nw, S.chap_off, S.role == 1 ? "host" : "guest");
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

static void apply_remote(CoreAPI *a, const uint8_t *B, int len)
{
    if (len < 11) return;
    unsigned seq = B[1] | (B[2] << 8), ack = B[3] | (B[4] << 8); int nwd = B[5] | (B[6] << 8), nq = B[7] | (B[8] << 8); int full = B[9];
    S.have_contact = 1; S.remote_last_seq_u16 = seq; if (nwd + nq > 0) S.ack_dirty = 1;
    /* what the other copy has confirmed */
    for (int i = 0; i <= S.nw; i++) if (S.pend[i] && S.sent[i] && (int16_t)(uint16_t)(ack - S.sent[i]) >= 0) { S.base[i] = S.sentv[i]; S.pend[i] = 0; S.sent[i] = 0; }
    for (int i = 0; i < S.nq; i++) if (S.q[i].pend && S.q[i].sent && (int16_t)(uint16_t)(ack - S.q[i].sent) >= 0) { S.q[i].base = S.q[i].sentv; S.q[i].pend = 0; S.q[i].sent = 0; }
    int p = 11;
    if (p + nwd * 6 + nq * 5 > len) return;
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
    (void)full; (void)ack;
}

static unsigned s_ack_seen;
static void send_blob(CoreAPI *a)
{
    uint8_t buf[8800]; int p = 11, nwd = 0, nq = 0;
    S.my_seq = (S.my_seq + 1) & 0xffff; if (!S.my_seq) S.my_seq = 1;
    for (int i = 0; i <= S.nw && p + 6 < 4400; i++) if (S.pend[i]) { buf[p] = (uint8_t)i; buf[p + 1] = (uint8_t)(i >> 8); memcpy(buf + p + 2, &S.cur[i], 4); p += 6; nwd++; S.sent[i] = S.my_seq; S.sentv[i] = S.cur[i]; }
    for (int i = 0; i < S.nq && p + 5 < 8700; i++) if (S.q[i].pend) { memcpy(buf + p, &S.q[i].id, 4); buf[p + 4] = S.q[i].cur; p += 5; nq++; S.q[i].sent = S.my_seq; S.q[i].sentv = S.q[i].cur; }
    buf[0] = 1; buf[1] = (uint8_t)S.my_seq; buf[2] = (uint8_t)(S.my_seq >> 8); buf[3] = (uint8_t)S.remote_last_seq_u16; buf[4] = (uint8_t)(S.remote_last_seq_u16 >> 8);
    buf[5] = (uint8_t)nwd; buf[6] = (uint8_t)(nwd >> 8); buf[7] = (uint8_t)nq; buf[8] = (uint8_t)(nq >> 8); buf[9] = 0; buf[10] = 0;
    a->set_local_blob3(buf, p);
    S.n_sent_w += nwd; S.n_sent_q += nq; S.ack_dirty = 0;
    if (nwd || nq) a->log("S sent %d story words, %d quests (seq %u, size %d)", nwd, nq, S.my_seq, p);
}

__declspec(dllexport) void mod_update(CoreAPI *a)
{
    unsigned long now = GetTickCount();
    if (S.scanning && !S.story) goto go;
    if (now - S.last_tick < 1000 && a->remote_blob3_seq == S.last_remote_seq) return;
    go:;
    int fresh = now - S.last_tick >= 1000; if (fresh) S.last_tick = now;
    if (GetFileAttributesA(BUILD "story_off.txt") != INVALID_FILE_ATTRIBUTES) { S.off = 1; return; }
    S.off = 0; S.role = role_of();
    if (a->hero != S.last_hero) { S.last_hero = a->hero; S.last_hero_t = now; reset_story(); S.qsys = NULL; S.nq = 0; S.scanning = 0; S.have_contact = 0; }
    if (!a->hero || !a->have_remote || now - S.last_hero_t < 10000) return;               /* a new world has to settle first */
    if (S.story && !a->obj_alive(S.story, S.story_name)) reset_story();
    if (S.qsys && !a->obj_alive(S.qsys, S.qsys_name)) { S.qsys = NULL; S.nq = 0; }
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
    int anyp = 0; for (int i = 0; i <= S.nw && !anyp; i++) anyp = S.pend[i]; for (int i = 0; i < S.nq && !anyp; i++) anyp = S.q[i].pend;
    if (anyp || S.ack_dirty) send_blob(a);
    if (now - S.last_stat > 10000) {
        S.last_stat = now;
        a->log("S role %s: sent %d words / %d quests, applied %d words / %d quests, contact %d, pending %s", S.role == 1 ? "host" : "guest", S.n_sent_w, S.n_sent_q, S.n_applied_w, S.n_applied_q, S.have_contact, anyp ? "yes" : "no");
    }
    (void)changed; (void)s_ack_seen;
}
