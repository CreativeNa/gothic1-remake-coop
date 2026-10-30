/*
 * equip module: everything the hero has attached to its body (quiver, weapons in hand or on the back, an apple being eaten ...),
 * shown on the other player's puppet.
 *  - sender side: every 0.5 s finds all ItemVisual actors owned by the hero and attached to its mesh, describes them
 *    ("package,class,socket,arrows") and sends the list through set_local_extra
 *  - receiver side: spawns the same Blueprint actors and attaches them to the puppet's mesh sockets, removes the ones that went away
 * "W=<0/1>" in the same text tells whether a weapon is in the hand (used by the anim module).
 * Hot-reloadable. UNTESTED.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coreapi.h"

#define MAXV 8

static struct {
    uint8_t *puppet;
    struct { char key[300]; uint8_t *actor; int arrows; } pv[MAXV];
    unsigned long last_scan, last_try, last_arrows;
    int last_weapon, gen, off; unsigned long last_off, last_hide;
    struct { char key[300]; int n; } fail[8]; int nfail;            /* visuals whose spawn failed (a half-built actor stays in the world each time) */
    char local[900];
    char seen[16][300]; int nseen;
    struct { uint8_t *cls; int v; int off_owner; } cc[512]; int ncc;
} E;

/* "package,class,socket[,arrows]" -> "package,class,socket" */
static void entry_key(const char *ent, char *key, size_t cap)
{
    snprintf(key, cap, "%s", ent);
    int commas = 0;
    for (char *p = key; *p; p++) if (*p == ',' && ++commas == 3) { *p = 0; break; }
}

__declspec(dllexport) void mod_init(CoreAPI *a) { (void)a; memset(&E, 0, sizeof E); }
__declspec(dllexport) void mod_shutdown(CoreAPI *a) { for (int i = 0; i < MAXV; i++) if (E.pv[i].actor) a->destroy_actor(E.pv[i].actor); }

static uint8_t *ptr_prop(CoreAPI *a, uint8_t *obj, const char *name)
{
    if (!obj) return NULL;
    int off = a->find_prop_offset(a->obj_class(obj), name);
    uint8_t *p = NULL;
    if (off >= 0) a->read_mem(obj + off, &p, 8);
    return p;
}

static int arrow_count(CoreAPI *a, uint8_t *vis)
{
    uint8_t *ism = ptr_prop(a, vis, "InstancedStaticMeshArrows");
    int n = -1;
    if (!ism || !a->call_int(ism, "InstancedStaticMeshComponent", "GetInstanceCount", 0, NULL, &n)) return -1;
    return n;
}

/* is this class (or a parent) "ItemVisual"? cached per class; also remembers the Owner property offset */
static int class_info(CoreAPI *a, uint8_t *cls, int *off_owner)
{
    for (int i = 0; i < E.ncc; i++) if (E.cc[i].cls == cls) { *off_owner = E.cc[i].off_owner; return E.cc[i].v; }
    int v = 0; char nm[96];
    uint8_t *c = cls;
    for (int d = 0; c && d < 12; d++) {
        a->obj_name(c, nm, sizeof nm);
        if (strcmp(nm, "ItemVisual") == 0) { v = 1; break; }
        uint8_t *sup = NULL; a->read_mem(c + 0x40, &sup, 8); c = sup;
    }
    int oo = v ? a->find_prop_offset(cls, "Owner") : -1;
    if (E.ncc < 512) { E.cc[E.ncc].cls = cls; E.cc[E.ncc].v = v; E.cc[E.ncc].off_owner = oo; E.ncc++; }
    *off_owner = oo;
    return v;
}

/* collect the hero's attached item visuals into "W=<weapon>;V=e1&e2..." with e = package,class,socket,arrows */
static void describe_local(CoreAPI *a)
{
    char entries[820] = ""; int count = 0;
    uint8_t *hero_mesh = ptr_prop(a, a->hero, "Mesh");
    /* the visuals hang on the hero mesh: walk its attached components (about 20) instead of every object in the game (450k) */
    char nm[200];
    int oat = hero_mesh ? a->find_prop_offset(a->obj_class(hero_mesh), "AttachChildren") : -1;
    uint8_t *kids = NULL; int nk = 0;
    if (oat >= 0) { a->read_mem(hero_mesh + oat, &kids, 8); a->read_mem(hero_mesh + oat + 8, &nk, 4); }
    if (nk < 0 || nk > 200) nk = 0;
    for (int i = 0; i < nk && count < MAXV; i++) {
        uint8_t *comp = NULL; a->read_mem(kids + (size_t)i * 8, &comp, 8);
        if (!comp) continue;
        uint8_t *o = NULL; a->read_mem(comp + 0x20, &o, 8);          /* a component's outer is the actor that owns it */
        if (!o || o == a->hero) continue;
        uint8_t *cls = a->obj_class(o);
        if (!cls) continue;
        int oo; if (!class_info(a, cls, &oo) || oo < 0) continue;
        uint8_t *owner = NULL; a->read_mem(o + oo, &owner, 8);
        if (owner != a->hero) continue;
        a->obj_name(o, nm, sizeof nm);
        if (strncmp(nm, "Default__", 9) == 0) continue;
        if (a->read_bool(o, "bHidden") == 1 || a->read_bool(o, "bActorIsBeingDestroyed") == 1) continue;
        uint8_t *root = ptr_prop(a, o, "RootComponent");
        if (!root) continue;
        uint8_t *parent = ptr_prop(a, root, "AttachParent");
        if (parent != hero_mesh) continue;
        int os = a->find_prop_offset(a->obj_class(root), "AttachSocketName");
        char sock[96] = "";
        if (os >= 0) a->fname_to_str(root + os, sock, sizeof sock);
        char cn[160] = "", pn[256] = "";
        a->obj_name(cls, cn, sizeof cn);
        uint8_t *pkg = NULL; a->read_mem(cls + 0x20, &pkg, 8);
        if (pkg) a->obj_name(pkg, pn, sizeof pn);
        int arrows = strstr(cn, "Quiver") ? arrow_count(a, o) : -1;
        char ent[600]; snprintf(ent, sizeof ent, "%s,%s,%s,%d", pn, cn, sock[0] ? sock : "None", arrows);
        if (strlen(entries) + strlen(ent) + 2 < sizeof entries) { if (count) strcat(entries, "&"); strcat(entries, ent); count++; }
        int known = 0;                                    /* log every distinct visual once, for the recon */
        for (int k = 0; k < E.nseen; k++) if (strcmp(E.seen[k], cn) == 0) known = 1;
        if (!known && E.nseen < 16) { snprintf(E.seen[E.nseen++], 300, "%s", cn); a->log("E visual: %s socket=%s package=%s", cn, sock, pn); }
    }    uint8_t *carry = ptr_prop(a, a->hero, "m_CarryComponent");
    int weapon = ptr_prop(a, carry, "m_EquippedItem") != NULL;
    if (weapon != E.last_weapon) { E.last_weapon = weapon; a->log("E local weapon in hand -> %d", weapon); }
    char desc[900]; snprintf(desc, sizeof desc, "W=%d;V=%s", weapon, entries[0] ? entries : "-");
    if (strcmp(desc, E.local) != 0) {
        snprintf(E.local, sizeof E.local, "%s", desc);
        a->set_local_extra(desc);
        a->log("E local equipment: %s", desc);
    }
}

__declspec(dllexport) void mod_update(CoreAPI *a)
{
    unsigned long now = GetTickCount();
    if (a->hero && now - E.last_scan > 500) { E.last_scan = now; describe_local(a); }
    if (!a->have_remote || !a->puppet) return;

    /* kill switch for isolating problems: build\equip_off.txt removes all visuals from the puppet and stops creating them */
    if (now - E.last_off > 1000) {
        E.last_off = now;
        int off = GetFileAttributesA("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Gothic1R-coop\\native\\build\\equip_off.txt") != INVALID_FILE_ATTRIBUTES;
        if (off != E.off) { E.off = off; a->log("E equipment on puppet %s", off ? "OFF" : "ON"); }
    }
    if (E.off) {
        for (int i = 0; i < MAXV; i++) { if (E.pv[i].actor && a->obj_alive(E.pv[i].actor, NULL)) a->destroy_actor(E.pv[i].actor); E.pv[i].actor = NULL; E.pv[i].key[0] = 0; }
        return;
    }
    if (a->puppet != E.puppet) { E.puppet = a->puppet; memset(E.pv, 0, sizeof E.pv); }   /* new puppet: the old visuals died with the old world */
    if (a->attach_gen != E.gen) {                           /* equipment now belongs on another mesh (bone-driven puppet on/off): attach everything again */
        E.gen = a->attach_gen;
        for (int i = 0; i < MAXV; i++) { if (E.pv[i].actor && a->obj_alive(E.pv[i].actor, NULL)) a->destroy_actor(E.pv[i].actor); E.pv[i].actor = NULL; E.pv[i].key[0] = 0; }
    }

    {   /* hide the puppet's items while the friend sends no poses (main menu, loading), the same rule as the pose module uses for the puppet */
        static int last_seen = -1; static unsigned long seen_t; static int hidden_state = -1;
        if (a->remote_blob_seq != last_seen) { last_seen = a->remote_blob_seq; seen_t = now; }
        int away = a->remote_blob_seq == 0 || now - seen_t > 1500;
        if (away != hidden_state || (away && now - E.last_hide > 2000)) {         /* also re-apply while away: items may be spawned meanwhile */
            hidden_state = away; E.last_hide = now;
            for (int i = 0; i < MAXV; i++) {
                if (!E.pv[i].actor || !a->obj_alive(E.pv[i].actor, NULL)) continue;
                uint8_t hid = (uint8_t)away; const char *n1[1] = { "bNewHidden" }; const void *v1[1] = { &hid }; int s1[1] = { 1 };
                a->call_fn(E.pv[i].actor, "Actor", "SetActorHiddenInGame", 1, n1, v1, s1, NULL, 0);
            }
        }
    }

    const char *ex = a->remote_extra ? a->remote_extra : "-";
    const char *v = strstr(ex, "V=");
    char list[900] = "";
    if (v && v[2] != '-') snprintf(list, sizeof list, "%s", v + 2);

    /* parse the remote entries */
    char ents[MAXV][600]; int ne = 0;
    for (char *tok = strtok(list, "&"); tok && ne < MAXV; tok = strtok(NULL, "&")) snprintf(ents[ne++], 600, "%s", tok);

    /* a visual that only moved to another socket (the sword: hand <-> holder) is re-attached, never destroyed and created again:
     * destroying/spawning owner-less item visuals is what crashed the game (access violation at 0xd50) after every draw/sheathe */
    for (int k = 0; k < ne; k++) {
        char key[600]; entry_key(ents[k], key, sizeof key);
        int exact = 0;
        for (int i = 0; i < MAXV; i++) if (E.pv[i].actor && strcmp(E.pv[i].key, key) == 0) exact = 1;
        if (exact) continue;
        char pre[600]; snprintf(pre, sizeof pre, "%s", key); { char *last = strrchr(pre, ','); if (last) last[1] = 0; }   /* "package,class," */
        const char *sock = strrchr(key, ','); sock = sock ? sock + 1 : "None";
        for (int i = 0; i < MAXV; i++) {
            if (!E.pv[i].actor || strncmp(E.pv[i].key, pre, strlen(pre)) != 0 || !a->obj_alive(E.pv[i].actor, NULL)) continue;
            int wanted = 0;                                   /* is this visual's current socket still wanted by another entry? */
            for (int j = 0; j < ne; j++) { char kj[600]; entry_key(ents[j], kj, sizeof kj); if (strcmp(kj, E.pv[i].key) == 0) wanted = 1; }
            if (wanted) continue;
            int ok = a->attach_to_puppet(E.pv[i].actor, sock);
            a->log("E re-attach %s: '%s' -> '%s' ok=%d", E.pv[i].key, E.pv[i].key, sock, ok);
            snprintf(E.pv[i].key, sizeof E.pv[i].key, "%s", key);
            break;
        }
    }

    /* remove puppet visuals that the other player no longer has */
    for (int i = 0; i < MAXV; i++) {
        if (!E.pv[i].actor) continue;
        int keep = 0;
        for (int k = 0; k < ne; k++) { char key[600]; snprintf(key, sizeof key, "%s", ents[k]); char *c3 = key; int commas = 0; for (char *p = key; *p; p++) if (*p == ',' && ++commas == 3) { *p = 0; break; } (void)c3; if (strcmp(E.pv[i].key, key) == 0) keep = 1; }
        if (!keep || !a->obj_alive(E.pv[i].actor, NULL)) { a->log("E remove visual %s", E.pv[i].key); if (a->obj_alive(E.pv[i].actor, NULL)) a->destroy_actor(E.pv[i].actor); E.pv[i].actor = NULL; E.pv[i].key[0] = 0; }
    }

    /* create what is missing, and mirror the arrow count of quivers */
    for (int k = 0; k < ne; k++) {
        char buf[600]; snprintf(buf, sizeof buf, "%s", ents[k]);
        char *pkg = strtok(buf, ","), *cls = strtok(NULL, ","), *sock = strtok(NULL, ","), *cnt = strtok(NULL, ",");
        if (!pkg || !cls || !sock) continue;
        int target = cnt ? atoi(cnt) : -1;
        char key[600]; snprintf(key, sizeof key, "%s,%s,%s", pkg, cls, sock);
        int slot = -1;
        for (int i = 0; i < MAXV; i++) if (E.pv[i].actor && strcmp(E.pv[i].key, key) == 0) slot = i;
        if (slot >= 0) {
            if (target >= 0 && now - E.last_arrows > 500) {
                uint8_t *ism = ptr_prop(a, E.pv[slot].actor, "InstancedStaticMeshArrows");
                int own = arrow_count(a, E.pv[slot].actor);
                for (int guard = 0; ism && own > target && own > 0 && guard < 40; guard++) {
                    int idx = own - 1, ok = 0;
                    a->call_int(ism, "InstancedStaticMeshComponent", "RemoveInstance", 1, &idx, &ok);
                    int now_own = arrow_count(a, E.pv[slot].actor);
                    if (now_own >= own) break;
                    own = now_own;
                }
                if (own != E.pv[slot].arrows) { a->log("E puppet %s arrows now %d (remote %d)", cls, own, target); E.pv[slot].arrows = own; }
            }
            continue;
        }
        { int given_up = 0; for (int i = 0; i < E.nfail; i++) if (strcmp(E.fail[i].key, key) == 0 && E.fail[i].n >= 2) given_up = 1; if (given_up) continue; }
        if (now - E.last_try < 1000) continue;
        E.last_try = now;
        for (int i = 0; i < MAXV && slot < 0; i++) if (!E.pv[i].actor) slot = i;
        if (slot < 0) continue;
        uint8_t *uclass = a->find_object("BlueprintGeneratedClass", cls, NULL);
        if (!uclass) uclass = a->load_asset(pkg, cls);
        a->log("E class %s -> %p", cls, uclass);
        if (!uclass) continue;
        uint8_t *actor = a->spawn_actor(uclass);
        a->log("E spawned %s %p", cls, actor);
        if (!actor) {                                           /* a failed spawn leaves a half-built actor behind in the world: after two failures stop trying this visual */
            E.last_try = now + 4000;
            int f = -1; for (int i = 0; i < E.nfail; i++) if (strcmp(E.fail[i].key, key) == 0) f = i;
            if (f < 0 && E.nfail < 8) { f = E.nfail++; snprintf(E.fail[f].key, sizeof E.fail[f].key, "%s", key); E.fail[f].n = 0; }
            if (f >= 0 && ++E.fail[f].n >= 2) a->log("E giving up on %s (spawn keeps failing)", cls);
            continue;
        }
        {   /* the other player's items must be invisible to hit traces: a sword swing that hits an owner-less item visual sends the game looking for its character (null) */
            uint8_t off = 0; const char *n1[1] = { "bNewActorEnableCollision" }; const void *v1[1] = { &off }; int s1[1] = { 1 };
            a->call_fn(actor, "Actor", "SetActorEnableCollision", 1, n1, v1, s1, NULL, 0);
            uint8_t *root = ptr_prop(a, actor, "RootComponent");
            if (root) {
                uint8_t none = 0, ignore = 0;
                const char *n2[1] = { "NewType" }; const void *v2[1] = { &none }; int s2[1] = { 1 };
                a->call_fn(root, "PrimitiveComponent", "SetCollisionEnabled", 1, n2, v2, s2, NULL, 0);
                const char *n3[1] = { "NewResponse" }; const void *v3[1] = { &ignore }; int s3[1] = { 1 };
                a->call_fn(root, "PrimitiveComponent", "SetCollisionResponseToAllChannels", 1, n3, v3, s3, NULL, 0);
            }
        }
        int ok = a->attach_to_puppet(actor, sock);
        a->log("E attach %s to socket '%s' ok=%d", cls, sock, ok);
        E.pv[slot].actor = actor; snprintf(E.pv[slot].key, sizeof E.pv[slot].key, "%s", key); E.pv[slot].arrows = -2;
    }
    if (now - E.last_arrows > 500) E.last_arrows = now;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) { (void)h; (void)r; (void)x; return TRUE; }
