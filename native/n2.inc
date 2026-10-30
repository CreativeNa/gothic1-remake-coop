/* ================= N2: call UFunctions through ProcessEvent on the game thread =================
 * Included by coopdll.c after the N1 helpers. UNTESTED. */
#define RVA_PROCESSEVENT 0x13A2920u
typedef void (*ProcessEvent_t)(void *self, void *fn, void *params);
typedef void (*EngineTick_t)(void *self, float dt, unsigned char idle);

typedef struct { char name[64]; int off; int size; uint64_t flags; } ParamInfo;
typedef struct { uint8_t *fn; char name[64]; int n; ParamInfo p[24]; int parms_size; } FnInfo;

static ProcessEvent_t g_pe;
static EngineTick_t g_orig_tick;

static uint8_t *find_object(const char *cls_name, const char *name, const char *outer_name)
{
    char nm[256], cn[256], on[256];
    for (int32_t i = 0; i < g_num_elements; i++) {
        uint8_t *o = get_object(i);
        if (!o) continue;
        obj_name(o, nm, sizeof nm);
        if (strcmp(nm, name) != 0) continue;
        if (cls_name) { obj_name(obj_class(o), cn, sizeof cn); if (strcmp(cn, cls_name) != 0) continue; }
        if (outer_name) { uint8_t *out = (uint8_t *)rd64(o + 0x20); obj_name(out, on, sizeof on); if (strcmp(on, outer_name) != 0) continue; }
        return o;
    }
    return NULL;
}

static int load_fn(const char *outer, const char *name, FnInfo *fi)
{
    memset(fi, 0, sizeof *fi);
    fi->fn = find_object("Function", name, outer);
    strncpy(fi->name, name, sizeof fi->name - 1);
    if (!fi->fn) { logf_("fn %s:%s NOT FOUND", outer, name); return 0; }
    char nm[256];
    uint8_t *prop = (uint8_t *)rd64(fi->fn + 0x50);
    int maxend = 0;
    for (int g = 0; prop && g < 24; g++) {
        fname_str(prop + g_off_name, nm, sizeof nm);
        ParamInfo *pi = &fi->p[fi->n++];
        strncpy(pi->name, nm, sizeof pi->name - 1);
        pi->off = rd32(prop + g_off_offint);
        pi->size = rd32(prop + g_off_name + 0x14);
        pi->flags = (uint64_t)rd64(prop + g_off_name + 0x18);
        if (pi->off + pi->size > maxend) maxend = pi->off + pi->size;
        prop = (uint8_t *)rd64(prop + g_off_next);
    }
    fi->parms_size = (maxend + 15) & ~15;
    char line[400]; line[0] = 0;
    for (int i = 0; i < fi->n; i++) sprintf(line + strlen(line), "%s@%d/%d ", fi->p[i].name, fi->p[i].off, fi->p[i].size);
    logf_("fn %s:%s @%p parms=%d : %s", outer, name, fi->fn, fi->parms_size, line);
    return 1;
}

static ParamInfo *pfind(FnInfo *fi, const char *name)
{
    for (int i = 0; i < fi->n; i++) if (strcmp(fi->p[i].name, name) == 0) return &fi->p[i];
    return NULL;
}

/* params buffer: pnew+pzero, fill with pset, call with pcall, read results with pget */
static uint8_t *pnew(FnInfo *fi) { uint8_t *b = (uint8_t *)_aligned_malloc((size_t)fi->parms_size + 64, 16); memset(b, 0, (size_t)fi->parms_size + 64); return b; }
static int pset(FnInfo *fi, uint8_t *buf, const char *name, const void *data, int n)
{
    ParamInfo *p = pfind(fi, name);
    if (!p) { logf_("  param %s missing in %s", name, fi->name); return 0; }
    memcpy(buf + p->off, data, (size_t)(n < p->size ? n : p->size));
    return 1;
}
static void *pget(FnInfo *fi, uint8_t *buf, const char *name) { ParamInfo *p = pfind(fi, name); return p ? buf + p->off : NULL; }
/* what our code is doing right now (single game thread): the crash recorder prints it, so a crash tells whether one of our calls was on the stack */
static volatile const char *g_in_call, *g_in_mod; static volatile DWORD g_game_tid;
static int pcall(void *self, FnInfo *fi, uint8_t *buf)
{
    g_in_call = fi->name;
    __try { g_pe(self, fi->fn, buf); g_in_call = NULL; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_in_call = NULL; logf_("EXCEPTION 0x%lx in ProcessEvent(%s)", GetExceptionCode(), fi->name); return 0; }
}

typedef struct { double x, y, z; } Vec3;
typedef struct { double q[4]; double t[3]; double pad0; double s[3]; double pad1; } Transform;  /* 96 bytes, FQuat first */

/* ---- N2 test state machine, advanced once per engine frame ---- */
static struct {
    int step, frame;
    uint8_t *hero, *root;
    int off_root, off_loc, off_smc;
    FnInfo getloc, setloc, begin_spawn, finish_spawn, setmesh, setmob, setscale;
    uint8_t *gs_cdo, *smactor_class, *mesh;
    uint8_t *marker;
    int moves;
    /* puppet = a second PlayerCharacterBP_C instead of the cube */
    int puppet_char; uint8_t *hero_class; int unhides; uint8_t *skm_class;
    /* puppet animation: 0 idle, 1 walk, 2 run, 3 sprint */
    char sent_act[96], ract[96], cur_act[96], rpkg[256], sent_pkg[256]; int rmm; uint8_t *anim_jump;
    FnInfo loadasset; uint8_t *ksl_cdo; unsigned long fall_since; char anim_names[4][96];
    FnInfo setpos, setrate; double vz; int jump_phase, weapon_drawn; unsigned long jump_t; double mesh_yaw;
    double rspd, rvz; int rspd_valid, rcrouch; int off_vel; int last_cr;
    char local_extra[900], remote_extra[900]; FnInfo attach_fn, destroy_fn;
    struct { char name[96]; uint8_t *ptr; } act_cache[64]; int act_cache_n;
    FnInfo getmontage; int watch_mode; uint8_t *last_montage; int last_movemode; double last_vz;
    FnInfo playanim; uint8_t *skcomp; uint8_t *anim[4]; int anim_state, cand_state, cand_n;
    double last_rp[3]; unsigned long last_rt; double speed;
    FnInfo sethidden, setcoll, setrot, setskinned, setvis, sethidcomp;
    int off_mesh_asset; uint8_t *last_asset;
    /* net */
    SOCKET sock; struct sockaddr_in peer; int net_ok; char id[16];
    double rpos[3], ryaw; int have_remote; int seq; int hero_idx;
    double shown[3];
} T;

/* cfg_<pid>.txt next to the dll: "<localPort> <peerHost> <peerPort> <id>" */
static int net_init(void)
{
    char path[MAX_PATH], line[256], host[64];
    int lport = 0, pport = 0;
    snprintf(path, sizeof path, "%s\\cfg_%lu.txt", g_dir, GetCurrentProcessId());
    FILE *f = fopen(path, "r");
    if (!f) { logf_("net: no cfg file %s", path); return 0; }
    if (!fgets(line, sizeof line, f)) { fclose(f); return 0; }
    fclose(f);
    if (sscanf(line, "%d %63s %d %15s", &lport, host, &pport, T.id) != 4) { logf_("net: bad cfg '%s'", line); return 0; }
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    T.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((u_short)lport);
    if (bind(T.sock, (struct sockaddr *)&a, sizeof a) != 0) { logf_("net: bind %d failed %d", lport, WSAGetLastError()); return 0; }
    memset(&T.peer, 0, sizeof T.peer);
    T.peer.sin_family = AF_INET; T.peer.sin_port = htons((u_short)pport);
    if (inet_pton(AF_INET, host, &T.peer.sin_addr) != 1) {              /* not a dotted address: resolve the host name */
        struct addrinfo hints, *res = NULL; memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) { logf_("net: cannot resolve peer host '%s'", host); return 0; }
        T.peer.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr; freeaddrinfo(res);
    }
    u_long nb = 1; ioctlsocket(T.sock, FIONBIO, &nb);
    char ipbuf[64]; inet_ntop(AF_INET, &T.peer.sin_addr, ipbuf, sizeof ipbuf);
    logf_("net: id=%s local :%d -> %s (%s):%d; packets from other addresses are ignored", T.id, lport, host, ipbuf, pport);
    return 1;
}

static void read_hero_pos(double v[3]) { safe_read(T.root + T.off_loc, v, 24); }

/* FProperty* by name along the struct chain */
static uint8_t *find_prop_ptr(uint8_t *ustruct, const char *want)
{
    find_prop_offset(ustruct, want);       /* fills the cache and g_last_prop */
    return g_last_prop;
}
/* read a bool/bitfield UPROPERTY: FBoolProperty keeps {FieldSize, ByteOffset, ByteMask, FieldMask} right after FProperty; locate it by pattern */
static int read_bool(uint8_t *obj, const char *prop_name)
{
    uint8_t *p = find_prop_ptr(obj_class(obj), prop_name);
    if (!p) return -2;
    int off = rd32(p + g_off_offint);
    for (int o = 0x58; o <= 0xB0; o++) {
        uint8_t b[4]; if (!safe_read(p + o, b, 4)) return -3;
        if (b[0] == 1 && b[2] != 0 && (b[2] & (b[2] - 1)) == 0 && b[3] == b[2] && b[1] < 8) {
            uint8_t v = 0; safe_read(obj + off + b[1], &v, 1);
            return (v & b[2]) ? 1 : 0;
        }
    }
    return -4;
}

static FnInfo g_isvis, g_lastrender, g_timeseconds;
static void dump_vis(const char *tag, uint8_t *actor)
{
    uint8_t *mesh = (uint8_t *)rd64(actor + 792);
    double now = 0, last = 0; int vis = -1;
    if (g_timeseconds.fn) { uint8_t *b = pnew(&g_timeseconds); pset(&g_timeseconds, b, "WorldContextObject", &T.hero, 8); pcall(T.gs_cdo, &g_timeseconds, b); void *r = pget(&g_timeseconds, b, "ReturnValue"); if (r) memcpy(&now, r, 8); _aligned_free(b); }
    if (g_lastrender.fn) { uint8_t *b = pnew(&g_lastrender); pcall(actor, &g_lastrender, b); void *r = pget(&g_lastrender, b, "ReturnValue"); if (r) memcpy(&last, r, 8); _aligned_free(b); }
    if (g_isvis.fn) { uint8_t *b = pnew(&g_isvis); pcall(mesh, &g_isvis, b); void *r = pget(&g_isvis, b, "ReturnValue"); if (r) vis = *(uint8_t *)r; _aligned_free(b); }
    logf_("VIS %s actor: bHidden=%d | mesh: bVisible=%d bHiddenInGame=%d bOwnerNoSee=%d bOnlyOwnerSee=%d bRenderInMainPass=%d IsVisible()=%d | now=%.2f lastRender=%.2f (%.2fs ago)",
          tag, read_bool(actor, "bHidden"), read_bool(mesh, "bVisible"), read_bool(mesh, "bHiddenInGame"),
          read_bool(mesh, "bOwnerNoSee"), read_bool(mesh, "bOnlyOwnerSee"), read_bool(mesh, "bRenderInMainPass"), vis, now, last, now - last);
}

/* diagnostic: why is the spawned character puppet not visible? compare with the hero */
static void dump_char(const char *tag, uint8_t *actor)
{
    char nm[256];
    uint8_t *mesh = (uint8_t *)rd64(actor + 792);          /* Character::Mesh */
    uint8_t *ctrl = NULL;
    int off_ctrl = find_prop_offset(obj_class(actor), "Controller");
    if (off_ctrl >= 0) ctrl = (uint8_t *)rd64(actor + off_ctrl);
    obj_name(obj_class(mesh), nm, sizeof nm);
    logf_("DUMP %s actor=%p Mesh=%p (%s) Controller=%p", tag, actor, mesh, nm, ctrl);
    const char *assets[] = {"SkinnedAsset", "SkeletalMeshAsset", "SkeletalMesh"};
    for (int k = 0; k < 3; k++) {
        int off = find_prop_offset(obj_class(mesh), assets[k]);
        if (off < 0) continue;
        uint8_t *a = (uint8_t *)rd64(mesh + off);
        if (a) obj_name(a, nm, sizeof nm); else strcpy(nm, "NULL");
        logf_("DUMP %s %s@%d = %p %s", tag, assets[k], off, a, nm);
    }
    int off_attach = find_prop_offset(obj_class(mesh), "AttachChildren");
    if (off_attach >= 0) {
        uint8_t *data = (uint8_t *)rd64(mesh + off_attach);
        int32_t num = rd32(mesh + off_attach + 8);
        logf_("DUMP %s AttachChildren@%d count=%d", tag, off_attach, num);
        for (int i = 0; i < num && i < 40; i++) {
            uint8_t *c = (uint8_t *)rd64(data + (size_t)i * 8);
            if (!c) continue;
            char cn[128]; obj_name(c, nm, sizeof nm); obj_name(obj_class(c), cn, sizeof cn);
            int ao = find_prop_offset(obj_class(c), "SkinnedAsset");
            uint8_t *ca = ao >= 0 ? (uint8_t *)rd64(c + ao) : NULL;
            char an[128] = "-"; if (ca) obj_name(ca, an, sizeof an);
            logf_("DUMP %s   child[%d] %s (%s) asset=%s", tag, i, nm, cn, an);
        }
    }
    double l[3] = {0}, sc[3] = {0};
    int off_sc = find_prop_offset(obj_class(mesh), "RelativeScale3D");
    safe_read(mesh + find_prop_offset(obj_class(mesh), "RelativeLocation"), l, 24);
    if (off_sc >= 0) safe_read(mesh + off_sc, sc, 24);
    logf_("DUMP %s mesh relloc %.1f %.1f %.1f scale %.2f %.2f %.2f", tag, l[0], l[1], l[2], sc[0], sc[1], sc[2]);
}

/* ---- actions: what the hero's AnimInstance is playing as a montage, reduced to the AnimSequence of its first segment ---- */
/* Assets we load or look up can be garbage collected later: root them and re-validate before every use. */
static void root_obj(uint8_t *o)
{
    if (!o) return;
    int32_t idx = rd32(o + 0xC);
    if (idx < 0 || idx >= g_num_elements) return;
    uint8_t *chunkp = (uint8_t *)rd64(g_objs_chunks + (size_t)(idx / 65536) * 8);
    if (!chunkp) return;
    __try { InterlockedOr((volatile LONG *)(chunkp + (size_t)(idx % 65536) * 24 + 8), 1 << 30); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static int obj_alive(uint8_t *o, const char *name)
{
    if (!o) return 0;
    int32_t idx = rd32(o + 0xC);
    if (idx < 0 || idx >= g_num_elements || get_object(idx) != o) return 0;
    if (name) {
        static struct { int32_t idx; char name[96]; } nc[256];     /* FName index -> verified name: avoids ToString (allocation) on every check */
        int32_t ni = rd32(o + 0x18);
        unsigned h = (unsigned)ni & 255u;
        if (nc[h].name[0] && nc[h].idx == ni) return strcmp(nc[h].name, name) == 0;
        char nm[256]; obj_name(o, nm, sizeof nm);
        if (strlen(nm) < 96) { nc[h].idx = ni; strcpy(nc[h].name, nm); }
        if (strcmp(nm, name) != 0) return 0;
    }
    return 1;
}

static uint8_t *g_anim_class;
static uint8_t *find_anim(const char *name)
{
    char nm[256];
    if (!g_anim_class) g_anim_class = find_object("Class", "AnimSequence", "/Script/Engine");
    if (!g_anim_class) return NULL;
    for (int32_t i = 0; i < g_num_elements; i++) {
        uint8_t *o = get_object(i);
        if (!o || obj_class(o) != g_anim_class) continue;
        obj_name(o, nm, sizeof nm);
        if (strcmp(nm, name) == 0) return o;
    }
    return NULL;
}

/* Clips the other player used may not be loaded here (weapon, food ...). Load them by package path:
 * KismetSystemLibrary::LoadAsset_Blocking(TSoftObjectPtr): {FWeakObjectPtr 8, FTopLevelAssetPath{PackageName, AssetName} 16, SubPath FString 16} */
typedef void (*FNameCtor_t)(void *self, const wchar_t *name, int find_type);
static uint8_t *load_asset(const char *pkg, const char *asset)
{
    if (!T.loadasset.fn || !T.ksl_cdo) return NULL;
    wchar_t wpkg[256], wasset[128];
    MultiByteToWideChar(CP_UTF8, 0, pkg, -1, wpkg, 256);
    MultiByteToWideChar(CP_UTF8, 0, asset, -1, wasset, 128);
    FNameCtor_t ctor = (FNameCtor_t)(g_base + 0x11AFA80);
    uint8_t *b = pnew(&T.loadasset);
    ParamInfo *pa = pfind(&T.loadasset, "Asset");
    if (!pa) { _aligned_free(b); return NULL; }
    uint8_t *sp = b + pa->off;
    int32_t none = -1; memcpy(sp, &none, 4);                 /* weak ptr: invalid */
    __try { ctor(sp + 8, wpkg, 1); ctor(sp + 16, wasset, 1); } __except (EXCEPTION_EXECUTE_HANDLER) { logf_("FName ctor exception"); _aligned_free(b); return NULL; }
    int ok = pcall(T.ksl_cdo, &T.loadasset, b);
    void *r = pget(&T.loadasset, b, "ReturnValue");
    uint8_t *res = r ? *(uint8_t **)r : NULL;
    _aligned_free(b);
    logf_("A LoadAsset_Blocking(%s, %s) ok=%d -> %p", pkg, asset, ok, res);
    return res;
}

static char g_act_pkg[256];
static void local_action(char *out, int cap, int *movemode)
{
    strcpy(out, "-"); g_act_pkg[0] = 0;
    uint8_t *cm = (uint8_t *)rd64(T.hero + 800);
    int omm = find_prop_offset(obj_class(cm), "MovementMode");
    uint8_t mm = 1; if (omm >= 0) safe_read(cm + omm, &mm, 1);
    *movemode = mm;
    uint8_t *mesh = (uint8_t *)rd64(T.hero + 792);
    int oai = find_prop_offset(obj_class(mesh), "AnimScriptInstance");
    uint8_t *ai = oai >= 0 ? (uint8_t *)rd64(mesh + oai) : NULL;
    if (!ai || !T.getmontage.fn) return;
    uint8_t *b = pnew(&T.getmontage);
    pcall(ai, &T.getmontage, b);
    void *r = pget(&T.getmontage, b, "ReturnValue");
    uint8_t *mont = r ? *(uint8_t **)r : NULL;
    _aligned_free(b);
    if (!mont) return;
    char nm[128] = "";
    int ost = find_prop_offset(obj_class(mont), "SlotAnimTracks");
    if (ost >= 0) {
        uint8_t *tracks = (uint8_t *)rd64(mont + ost);
        int32_t nt = rd32(mont + ost + 8);
        if (tracks && nt > 0) {
            uint8_t *segs = (uint8_t *)rd64(tracks + 8);        /* FSlotAnimationTrack: SlotName(8), AnimTrack.AnimSegments */
            int32_t ns = rd32(tracks + 16);
            if (segs && ns > 0) {
                uint8_t *ref = (uint8_t *)rd64(segs);
                if (ref) {
                    obj_name(ref, nm, sizeof nm);
                    uint8_t *pkg = (uint8_t *)rd64(ref + 0x20);             /* outer = UPackage, whose name is the asset path */
                    if (pkg) obj_name(pkg, g_act_pkg, sizeof g_act_pkg);
                }
            }
        }
    }
    if (!nm[0]) obj_name(mont, nm, sizeof nm);
    for (char *c = nm; *c; c++) if (*c == '|' || *c == ' ') *c = '_';
    snprintf(out, (size_t)cap, "%s", nm);
}

/* ---- one-shot clip helpers used by the puppet animation code ---- */
static uint8_t *clip_by_name(const char *name, const char *pkg)
{
    uint8_t *seq = NULL;
    int slot = -1;
    for (int k = 0; k < T.act_cache_n; k++) if (strcmp(T.act_cache[k].name, name) == 0) slot = k;
    if (slot >= 0 && obj_alive(T.act_cache[slot].ptr, name)) return T.act_cache[slot].ptr;
    seq = find_anim(name);
    if (!seq && pkg && strcmp(pkg, "-") != 0) seq = load_asset(pkg, name);
    if (seq && !obj_alive(seq, name)) { logf_("A resolved pointer %p failed validation", seq); seq = NULL; }
    if (seq) {
        root_obj(seq);
        if (slot < 0 && T.act_cache_n < 64) { slot = T.act_cache_n++; snprintf(T.act_cache[slot].name, 96, "%s", name); }
        if (slot >= 0) T.act_cache[slot].ptr = seq;
    }
    logf_("A resolve '%s' -> %p", name, seq);
    return seq;
}

static int play_seq(uint8_t *seq, int loop)
{
    if (!seq || !T.skcomp) return 0;
    if (T.setrate.fn) {                                  /* a frozen pose left the rate at 0 */
        float one = 1.0f; uint8_t *rb = pnew(&T.setrate);
        pset(&T.setrate, rb, "NewRate", &one, 4); pcall(T.skcomp, &T.setrate, rb); _aligned_free(rb);
    }
    uint8_t lp = (uint8_t)loop;
    uint8_t *pb = pnew(&T.playanim);
    pset(&T.playanim, pb, "NewAnimToPlay", &seq, 8);
    pset(&T.playanim, pb, "bLooping", &lp, 1);
    int ok = pcall(T.skcomp, &T.playanim, pb);
    _aligned_free(pb);
    return ok;
}

static int play_named(const char *name, int loop)
{
    uint8_t *seq = clip_by_name(name, "-");
    return seq ? play_seq(seq, loop) : 0;
}

/* play a clip once, jump to its last frame and stop the playback there */
static void freeze_at_end(uint8_t *seq)
{
    if (!play_seq(seq, 0)) return;
    int osl = find_prop_offset(obj_class(seq), "SequenceLength");
    float len = 0; if (osl >= 0) safe_read(seq + osl, &len, 4);
    if (len > 0.05f && T.setpos.fn) {
        float pos = len - 0.03f; uint8_t fire = 0;
        uint8_t *pb = pnew(&T.setpos);
        pset(&T.setpos, pb, "NewPos", &pos, 4); pset(&T.setpos, pb, "bFireNotifies", &fire, 1);
        pcall(T.skcomp, &T.setpos, pb); _aligned_free(pb);
    }
    if (T.setrate.fn) {
        float zero = 0.0f; uint8_t *rb = pnew(&T.setrate);
        pset(&T.setrate, rb, "NewRate", &zero, 4); pcall(T.skcomp, &T.setrate, rb); _aligned_free(rb);
    }
}

/* ================= hot-reloadable modules (modules\<name>.dll) ================= */
#include "coreapi.h"
static CoreAPI g_api;
#define MAX_MODS 4
static struct ModSlot {
    const char *name;
    HMODULE h; ModUpdate_t update; ModShutdown_t shutdown; ModInit_t init;
    FILETIME mtime; int serial; int need_init; char loaded_path[MAX_PATH];
    int always;                 /* 1: called every engine tick (menu, loading screens, no friend needed); 0: only while the puppet exists */
} g_mods[MAX_MODS] = { { .name = "anim" }, { .name = "equip" }, { .name = "pose" }, { .name = "probe", .always = 1 } };
static int g_nmods = 4;

static double seq_length_w(uint8_t *seq)
{
    if (!seq) return 0;
    int osl = find_prop_offset(obj_class(seq), "SequenceLength");
    float len = 0; if (osl >= 0) safe_read(seq + osl, &len, 4);
    return len;
}

static void list_anims_w(const char *sub, int max)
{
    char nm[256]; int shown = 0;
    if (!g_anim_class) g_anim_class = find_object("Class", "AnimSequence", "/Script/Engine");
    for (int32_t i = 0; i < g_num_elements && shown < max; i++) {
        uint8_t *o = get_object(i);
        if (!o || obj_class(o) != g_anim_class) continue;
        obj_name(o, nm, sizeof nm);
        if (ci_contains(nm, sub)) { logf_("M list[%s] %s", sub, nm); shown++; }
    }
    logf_("M list[%s]: %d shown", sub, shown);
}

/* log the properties of an object (object pointers with the target's class, bools, ints, floats, names, array sizes) */
static void dump_props_w(uint8_t *obj, const char *tag, int max)
{
    if (!obj) { logf_("D %s: null", tag); return; }
    char nm[160], tn[96], on[160], cn[96], sn[96];
    int n = 0;
    obj_name(obj_class(obj), cn, sizeof cn);
    logf_("D %s: %p class %s", tag, obj, cn);
    for (uint8_t *st = obj_class(obj); st && n < max; st = (uint8_t *)rd64(st + 0x40)) {
        obj_name(st, sn, sizeof sn);
        if (strcmp(sn, "Object") == 0) continue;
        for (uint8_t *p = (uint8_t *)rd64(st + 0x50); p && n < max; p = (uint8_t *)rd64(p + g_off_next)) {
            fname_str(p + g_off_name, nm, sizeof nm);
            uint8_t *fc = (uint8_t *)rd64(p + 8);
            if (fc) fname_str(fc, tn, sizeof tn); else strcpy(tn, "?");
            int off = rd32(p + g_off_offint), size = rd32(p + g_off_name + 0x14);
            char val[200] = "";
            if (strcmp(tn, "ObjectProperty") == 0 || strcmp(tn, "ObjectPtrProperty") == 0) {
                uint8_t *t = (uint8_t *)rd64(obj + off);
                if (t) {
                    obj_name(t, on, sizeof on); obj_name(obj_class(t), cn, sizeof cn); snprintf(val, sizeof val, "%p %s (%s)", t, on, cn);
                    static int depth = 0;       /* look one level into the interesting components (quiver, visuals, carry, item visuals) */
                    if (depth == 0 && (ci_contains(cn, "Quiver") || ci_contains(cn, "Visuals") || ci_contains(cn, "Carry") || ci_contains(cn, "ItemVisual"))) {
                        char sub[200]; snprintf(sub, sizeof sub, "%s.%s", tag, nm);
                        depth = 1; logf_("D   %s::%s [%s @%d/%d] = %s", sn, nm, tn, off, size, val);
                        dump_props_w(t, sub, 120); depth = 0;
                        continue;
                    }
                } else strcpy(val, "null");
            } else if (strcmp(tn, "BoolProperty") == 0) snprintf(val, sizeof val, "%d", read_bool(obj, nm));
            else if (strcmp(tn, "IntProperty") == 0) snprintf(val, sizeof val, "%d", rd32(obj + off));
            else if (strcmp(tn, "FloatProperty") == 0) { float f = 0; safe_read(obj + off, &f, 4); snprintf(val, sizeof val, "%.3f", f); }
            else if (strcmp(tn, "DoubleProperty") == 0) { double d = 0; safe_read(obj + off, &d, 8); snprintf(val, sizeof val, "%.3f", d); }
            else if (strcmp(tn, "ByteProperty") == 0 || strcmp(tn, "EnumProperty") == 0) { uint8_t b = 0; safe_read(obj + off, &b, 1); snprintf(val, sizeof val, "%u", b); }
            else if (strcmp(tn, "NameProperty") == 0) { fname_str(obj + off, on, sizeof on); snprintf(val, sizeof val, "'%s'", on); }
            else if (strcmp(tn, "ArrayProperty") == 0) snprintf(val, sizeof val, "num=%d", rd32(obj + off + 8));
            else continue;
            logf_("D   %s::%s [%s @%d/%d] = %s", sn, nm, tn, off, size, val);
            n++;
        }
    }
}

/* ---- actors the modules attach to the puppet (quiver, weapons ...) ---- */
static void destroy_actor_w(uint8_t *actor);
static uint8_t *spawn_actor_w(uint8_t *cls)
{
    if (!(cls && T.begin_spawn.fn && T.finish_spawn.fn && T.gs_cdo && T.hero)) return NULL;
    Transform tr; memset(&tr, 0, sizeof tr);
    tr.q[3] = 1.0; tr.t[0] = T.shown[0]; tr.t[1] = T.shown[1]; tr.t[2] = T.shown[2]; tr.s[0] = tr.s[1] = tr.s[2] = 1.0;
    uint8_t *b = pnew(&T.begin_spawn); uint8_t col = 1;
    pset(&T.begin_spawn, b, "WorldContextObject", &T.hero, 8);
    pset(&T.begin_spawn, b, "ActorClass", &cls, 8);
    pset(&T.begin_spawn, b, "SpawnTransform", &tr, sizeof tr);
    pset(&T.begin_spawn, b, "CollisionHandlingOverride", &col, 1);
    pcall(T.gs_cdo, &T.begin_spawn, b);
    void *rv = pget(&T.begin_spawn, b, "ReturnValue");
    uint8_t *actor = rv ? *(uint8_t **)rv : NULL;
    _aligned_free(b);
    if (!actor) return NULL;
    uint8_t *b2 = pnew(&T.finish_spawn);
    pset(&T.finish_spawn, b2, "Actor", &actor, 8);
    pset(&T.finish_spawn, b2, "SpawnTransform", &tr, sizeof tr);
    int okf = pcall(T.gs_cdo, &T.finish_spawn, b2);
    _aligned_free(b2);
    if (!okf) {                                                         /* the half-built actor would stay in the world (a trail of items): remove it */
        logf_("spawn_actor: FinishSpawningActor failed, destroying actor %p", actor);
        destroy_actor_w(actor);
        return NULL;
    }
    root_obj(actor);
    return actor;
}

/* equipment goes to g_attach_target (the bone-driven puppet mesh) when a module set one, else to the puppet's skeletal mesh */
static uint8_t *g_attach_target; static int g_attach_gen;
static void set_attach_target_w(uint8_t *comp) { if (comp != g_attach_target) { g_attach_target = comp; g_attach_gen++; } }

/* attach the actor's root component to the puppet's skeletal mesh at a socket, snapping to it */
static int attach_to_puppet_w(uint8_t *actor, const char *socket)
{
    uint8_t *parent_comp = g_attach_target ? g_attach_target : T.skcomp;
    if (!actor || !parent_comp) return 0;
    int orc = find_prop_offset(obj_class(actor), "RootComponent");
    uint8_t *root = orc >= 0 ? (uint8_t *)rd64(actor + orc) : NULL;
    if (!root) return 0;
    if (!T.attach_fn.fn) load_fn("SceneComponent", "K2_AttachToComponent", &T.attach_fn);
    if (!T.attach_fn.fn) return 0;
    wchar_t wsock[128]; MultiByteToWideChar(CP_UTF8, 0, socket, -1, wsock, 128);
    FNameCtor_t ctor = (FNameCtor_t)(g_base + 0x11AFA80);
    uint8_t *b = pnew(&T.attach_fn);
    ParamInfo *ps = pfind(&T.attach_fn, "SocketName");
    if (!ps) { _aligned_free(b); return 0; }
    __try { ctor(b + ps->off, wsock, 1); } __except (EXCEPTION_EXECUTE_HANDLER) { _aligned_free(b); return 0; }
    uint8_t snap = 2, weld = 0;   /* EAttachmentRule::SnapToTarget */
    pset(&T.attach_fn, b, "Parent", &parent_comp, 8);
    pset(&T.attach_fn, b, "LocationRule", &snap, 1);
    pset(&T.attach_fn, b, "RotationRule", &snap, 1);
    pset(&T.attach_fn, b, "ScaleRule", &snap, 1);
    pset(&T.attach_fn, b, "bWeldSimulatedBodies", &weld, 1);
    int ok = pcall(root, &T.attach_fn, b);
    _aligned_free(b);
    return ok;
}

static void destroy_actor_w(uint8_t *actor)
{
    if (!actor) return;
    if (!T.destroy_fn.fn) load_fn("Actor", "K2_DestroyActor", &T.destroy_fn);
    if (!T.destroy_fn.fn) return;
    uint8_t *b = pnew(&T.destroy_fn); pcall(actor, &T.destroy_fn, b); _aligned_free(b);
}

static void set_local_extra_w(const char *text) { snprintf(T.local_extra, sizeof T.local_extra, "%s", text && text[0] ? text : "-"); }
static uint8_t *find_object_w(const char *cls, const char *name, const char *outer) { return find_object(cls, name, outer); }
static uint8_t *load_asset_w(const char *pkg, const char *asset) { return load_asset(pkg, asset); }
/* ---- generic helper: call a UFunction whose parameters are plain ints/bools; returns the int/bool result ---- */
static struct { char key[100]; FnInfo fi; } g_fcache[64];
static int g_nfcache;
static int call_int_w(uint8_t *obj, const char *outer, const char *fn, int nargs, const int *args, int *ret)
{
    if (!obj) return 0;
    char key[100]; snprintf(key, sizeof key, "%s:%s", outer, fn);
    FnInfo *fi = NULL;
    for (int i = 0; i < g_nfcache; i++) if (strcmp(g_fcache[i].key, key) == 0) fi = &g_fcache[i].fi;
    if (!fi) {
        if (g_nfcache >= 64) return 0;
        snprintf(g_fcache[g_nfcache].key, sizeof g_fcache[0].key, "%s", key);
        load_fn(outer, fn, &g_fcache[g_nfcache].fi);
        fi = &g_fcache[g_nfcache++].fi;
    }
    if (!fi->fn) return 0;
    uint8_t *b = pnew(fi);
    int ai = 0;
    for (int i = 0; i < fi->n; i++) {
        if (strcmp(fi->p[i].name, "ReturnValue") == 0 || !(fi->p[i].flags & 0x80)) continue;
        if (ai < nargs) { int sz = fi->p[i].size < 4 ? fi->p[i].size : 4; memcpy(b + fi->p[i].off, &args[ai], (size_t)sz); }
        ai++;
    }
    int ok = pcall(obj, fi, b);
    if (ret) { *ret = 0; void *r = pget(fi, b, "ReturnValue"); ParamInfo *rp = pfind(fi, "ReturnValue"); if (r && rp) memcpy(ret, r, (size_t)(rp->size < 4 ? rp->size : 4)); }
    _aligned_free(b);
    return ok;
}

/* ---- generic call with named parameters (see coreapi.h); shares the FnInfo cache with call_int ---- */
static FnInfo *fcache_get(const char *outer, const char *fn)
{
    char key[100]; snprintf(key, sizeof key, "%s:%s", outer, fn);
    for (int i = 0; i < g_nfcache; i++) if (strcmp(g_fcache[i].key, key) == 0) return &g_fcache[i].fi;
    if (g_nfcache >= 64) return NULL;
    snprintf(g_fcache[g_nfcache].key, sizeof g_fcache[0].key, "%s", key);
    load_fn(outer, fn, &g_fcache[g_nfcache].fi);
    return &g_fcache[g_nfcache++].fi;
}
static int call_fn_w(uint8_t *obj, const char *outer, const char *fn, int n, const char **names, const void **vals, const int *sizes, void *ret, int retcap)
{
    if (!obj) return 0;
    FnInfo *fi = fcache_get(outer, fn);
    if (!fi || !fi->fn) return 0;
    uint8_t *b = pnew(fi);
    for (int i = 0; i < n; i++) {
        ParamInfo *p = pfind(fi, names[i]);
        if (!p) continue;
        if (sizes[i] == -2) memcpy(b + p->off, vals[i], 8);          /* an FName already built (its raw 8 bytes), saves the name lookup */
        else if (sizes[i] < 0) {
            wchar_t w[128]; MultiByteToWideChar(CP_UTF8, 0, (const char *)vals[i], -1, w, 128);
            FNameCtor_t ctor = (FNameCtor_t)(g_base + 0x11AFA80);
            __try { ctor(b + p->off, w, 1); } __except (EXCEPTION_EXECUTE_HANDLER) { _aligned_free(b); return 0; }
        } else memcpy(b + p->off, vals[i], (size_t)(sizes[i] < p->size ? sizes[i] : p->size));
    }
    int ok = pcall(obj, fi, b);
    ParamInfo *rp = pfind(fi, "ReturnValue");
    if (ret && rp) memcpy(ret, b + rp->off, (size_t)(retcap < rp->size ? retcap : rp->size));
    _aligned_free(b);
    return ok;
}

/* ---- live recon helpers (probe module) ---- */
typedef void (*MemberCb)(const char *kind, const char *sname, const char *name, const char *type, int off, int size, void *ud);
static void list_members_w(uint8_t *ustruct, MemberCb cb, void *ud)
{
    char sn[96], nm[160], tn[96];
    int depth = 0;
    for (uint8_t *st = ustruct; st && depth < 12; st = (uint8_t *)rd64(st + 0x40), depth++) {
        obj_name(st, sn, sizeof sn);
        int guard = 0;
        for (uint8_t *p = (uint8_t *)rd64(st + 0x50); p && guard < 4000; p = (uint8_t *)rd64(p + g_off_next), guard++) {
            fname_str(p + g_off_name, nm, sizeof nm);
            uint8_t *fc = (uint8_t *)rd64(p + 8);
            if (fc) fname_str(fc, tn, sizeof tn); else strcpy(tn, "?");
            cb("prop", sn, nm, tn, rd32(p + g_off_offint), rd32(p + g_off_name + 0x14), ud);
        }
        guard = 0;
        for (uint8_t *f = (uint8_t *)rd64(st + 0x48); f && guard < 4000; f = (uint8_t *)rd64(f + 0x28), guard++) {   /* UStruct::Children (UField chain, Next at +0x28) */
            obj_name(f, nm, sizeof nm);
            cb("fn", sn, nm, "Function", 0, 0, ud);
        }
    }
}
static int describe_fn_w(const char *outer, const char *fn, char *out, int cap)
{
    out[0] = 0;
    FnInfo *fi = fcache_get(outer, fn);
    if (!fi || !fi->fn) return 0;
    for (int i = 0; i < fi->n && (int)strlen(out) + 80 < cap; i++)
        sprintf(out + strlen(out), "%s%s@%d/%d%s", i ? " " : "", fi->p[i].name, fi->p[i].off, fi->p[i].size, (fi->p[i].flags & 0x400) ? "(out)" : "");   /* 0x400 = CPF_OutParm */
    return 1;
}
static int write_mem_w(void *dst, const void *src, size_t n)
{
    __try { memcpy(dst, src, n); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* ---- binary pose channel ---- */
#define BLOB_MAX 9000
#define BLOB_CHUNK 1400
static uint8_t g_lblob[BLOB_MAX]; static int g_lblob_len;
static uint8_t g_rblob[BLOB_MAX], g_rasm[BLOB_MAX]; static int g_rblob_len, g_rblob_seq, g_rexpect;
static void set_local_blob_w(const void *d, int len) { if (len < 0 || len > (int)sizeof g_lblob) len = 0; memcpy(g_lblob, d, (size_t)len); g_lblob_len = len; }

/* ---- which AnimSequences got loaded since the last call? (the first call only records the baseline) ---- */
static uint8_t *g_seen_tab[32768];
static int g_seen_baselined;
static int seen_has_or_add(uint8_t *p)
{
    size_t h = (size_t)((uintptr_t)p >> 4) & 32767;
    for (int k = 0; k < 32768; k++, h = (h + 1) & 32767) {
        if (g_seen_tab[h] == p) return 1;
        if (!g_seen_tab[h]) { g_seen_tab[h] = p; return 0; }
    }
    return 1;
}
static void scan_new_anims_w(void (*cb)(const char *name, const char *pkg, void *ud), void *ud)
{
    char nm[256], pn[256];
    if (!g_anim_class) g_anim_class = find_object("Class", "AnimSequence", "/Script/Engine");
    for (int32_t i = 0; i < g_num_elements; i++) {
        uint8_t *o = get_object(i);
        if (!o || obj_class(o) != g_anim_class) continue;
        if (seen_has_or_add(o)) continue;
        if (g_seen_baselined && cb) {
            obj_name(o, nm, sizeof nm);
            uint8_t *pk = (uint8_t *)rd64(o + 0x20); pn[0] = 0; if (pk) obj_name(pk, pn, sizeof pn);
            cb(nm, pn, ud);
        }
    }
    g_seen_baselined = 1;
}
static int num_objects_w(void) { return (int)g_num_elements; }
static uint8_t *object_at_w(int i) { return (i >= 0 && i < g_num_elements) ? get_object(i) : NULL; }
static void fname_to_str_w(const void *p, char *out, int cap) { if (!fname_str(p, out, cap)) out[0] = 0; }
static void api_init(void)
{
    memset(&g_api, 0, sizeof g_api);
    g_api.version = COREAPI_VERSION;
    g_api.log = logf_;
    g_api.find_anim = find_anim;
    g_api.clip_by_name = clip_by_name;
    g_api.play_seq = play_seq;
    g_api.play_named = play_named;
    g_api.freeze_at_end = freeze_at_end;
    g_api.seq_length = seq_length_w;
    g_api.obj_alive = obj_alive;
    g_api.root_obj = root_obj;
    g_api.list_anims = list_anims_w;
    g_api.dump_props = dump_props_w;
    g_api.find_prop_offset = find_prop_offset;
    g_api.obj_class = obj_class;
    g_api.obj_name = obj_name;
    g_api.read_mem = safe_read;
    g_api.read_bool = read_bool;
    g_api.call_int = call_int_w;
    g_api.scan_new_anims = scan_new_anims_w;
    g_api.num_objects = num_objects_w;
    g_api.object_at = object_at_w;
    g_api.fname_to_str = fname_to_str_w;
    g_api.find_object = find_object_w;
    g_api.load_asset = load_asset_w;
    g_api.spawn_actor = spawn_actor_w;
    g_api.attach_to_puppet = attach_to_puppet_w;
    g_api.destroy_actor = destroy_actor_w;
    g_api.set_local_extra = set_local_extra_w;
    g_api.remote_extra = T.remote_extra;
    g_api.call_fn = call_fn_w;
    g_api.set_local_blob = set_local_blob_w;
    g_api.set_attach_target = set_attach_target_w;
    g_api.remote_blob = g_rblob;
    g_api.list_members = list_members_w;
    g_api.describe_fn = describe_fn_w;
    g_api.write_mem = write_mem_w;
    g_api.action = "-"; g_api.action_pkg = "-";
}

/* load / reload modules\<name>.dll when its file changes (loaded through a private copy so the original can be rebuilt any time) */
static void module_poll(struct ModSlot *m)
{
    char src[MAX_PATH], dst[MAX_PATH];
    snprintf(src, sizeof src, "%s\\modules\\%s.dll", g_dir, m->name);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(src, GetFileExInfoStandard, &fa) || fa.nFileSizeLow == 0) return;
    if (m->h && CompareFileTime(&fa.ftLastWriteTime, &m->mtime) == 0) return;
    snprintf(dst, sizeof dst, "%s\\modules\\%s_%lu_%d.dll", g_dir, m->name, GetCurrentProcessId(), ++m->serial);
    if (!CopyFileA(src, dst, FALSE)) return;                    /* still being written: try again next poll */
    HMODULE h = LoadLibraryA(dst);
    if (!h) { logf_("M load failed for %s (%lu)", dst, GetLastError()); m->mtime = fa.ftLastWriteTime; return; }
    ModUpdate_t upd = (ModUpdate_t)GetProcAddress(h, "mod_update");
    ModInit_t init = (ModInit_t)GetProcAddress(h, "mod_init");
    if (!upd || !init) { logf_("M %s lacks mod_init/mod_update", dst); FreeLibrary(h); m->mtime = fa.ftLastWriteTime; return; }
    if (m->h) {
        if (m->shutdown) __try { m->shutdown(&g_api); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        FreeLibrary(m->h);
        DeleteFileA(m->loaded_path);
    }
    m->h = h; m->update = upd; m->init = init; m->shutdown = (ModShutdown_t)GetProcAddress(h, "mod_shutdown");
    m->mtime = fa.ftLastWriteTime; m->need_init = 1;
    snprintf(m->loaded_path, sizeof m->loaded_path, "%s", dst);
    logf_("M module '%s' loaded: %s", m->name, dst);
}

static void module_run(int always)
{
    if (!always) {                                          /* debug switch: build\modules_off.txt keeps the puppet actor but runs none of the puppet modules (pose, equip, anim) */
        static unsigned long chk; static int off; unsigned long now = GetTickCount();
        if (now - chk > 1000) {
            chk = now;
            char mp[MAX_PATH]; snprintf(mp, sizeof mp, "%s\\modules_off.txt", g_dir);
            int v = GetFileAttributesA(mp) != INVALID_FILE_ATTRIBUTES;
            if (v != off) { off = v; logf_("M puppet modules %s (modules_off.txt)", v ? "OFF" : "ON"); }
        }
        if (off) return;
    }
    if (!g_api.version) api_init();
    g_api.have_remote = T.have_remote;
    g_api.speed = T.rspd_valid ? T.rspd : T.speed; g_api.vz = T.rspd_valid ? T.rvz : T.vz; g_api.movemode = T.rmm;
    g_api.crouched = T.rcrouch;
    g_api.action = T.ract; g_api.action_pkg = T.rpkg; g_api.fall_since = T.fall_since;
    g_api.hero = T.hero; g_api.puppet = T.marker; g_api.puppet_mesh = T.skcomp;
    g_api.remote_blob_len = g_rblob_len; g_api.remote_blob_seq = g_rblob_seq; g_api.attach_gen = g_attach_gen;
    for (int i = 0; i < g_nmods; i++) {
        struct ModSlot *m = &g_mods[i];
        if (m->always != always) continue;
        if (T.frame % 30 == (i * 7) % 30) module_poll(m);
        if (!m->h || !m->update) continue;
        if (m->need_init) {
            m->need_init = 0;
            __try { m->init(&g_api); } __except (EXCEPTION_EXECUTE_HANDLER) { logf_("M exception in %s mod_init, module disabled", m->name); m->update = NULL; continue; }
        }
        g_in_mod = m->name;
        __try { m->update(&g_api); } __except (EXCEPTION_EXECUTE_HANDLER) { logf_("M exception 0x%lx in %s mod_update, disabled until reload", GetExceptionCode(), m->name); m->update = NULL; }
        g_in_mod = NULL;
    }
}

static void module_frame(void)  { module_run(0); }     /* modules that need the puppet */
static void module_always(void)  { module_run(1); }     /* modules that run every tick */

static void frame_work(void)
{
    T.frame++;
    if (g_arr) g_num_elements = rd32(g_arr + 0x10 + 0x14);   /* objects keep being created (loaded assets): do not use a stale count */
    /* the hero object can die on level load: re-resolve (marker belonged to the old level too) */
    if (T.step >= 1 && T.step != 99 && get_object(T.hero_idx) != T.hero) {
        logf_("N3 hero changed, re-resolving");
        T.hero = NULL; T.marker = NULL; T.step = 0; T.have_remote = 0;
    }
    switch (T.step) {
    case 0: {   /* resolve everything */
        char nm[128];
        if (T.frame % 60 != 1) return;                                   /* looking for the hero scans every object: once a second is enough (main menu, loading) */
        uint8_t *hc = find_object("BlueprintGeneratedClass", "PlayerCharacterBP_C", NULL);
        if (!hc) { if (T.frame % 1800 == 1) logf_("N2: hero class not found yet"); return; }
        for (int32_t j = 0; j < g_num_elements; j++) {
            uint8_t *x = get_object(j);
            if (!x || obj_class(x) != hc) continue;
            obj_name(x, nm, sizeof nm);
            if (strncmp(nm, "Default__", 9) != 0) { T.hero = x; T.hero_idx = j; break; }
        }
        if (!T.hero) { if (T.frame % 1800 == 1) logf_("N2: no hero instance yet (no save loaded)"); return; }
        logf_("N2: hero instance found at frame %d", T.frame);
        T.hero_class = hc; T.puppet_char = 2;   /* 1 = spawn PlayerCharacterBP_C (the game keeps it hidden), 2 = plain SkeletalMeshActor with the hero's mesh */
        T.skm_class = find_object("Class", "SkeletalMeshActor", "/Script/Engine");
        load_fn("SkeletalMeshComponent", "PlayAnimation", &T.playanim);
        load_fn("SkeletalMeshComponent", "SetPosition", &T.setpos);
        load_fn("SkeletalMeshComponent", "SetPlayRate", &T.setrate);
        load_fn("AnimInstance", "GetCurrentActiveMontage", &T.getmontage);
        strcpy(T.ract, "-"); strcpy(T.sent_act, "-"); T.rmm = 1;
        logf_("N5 SkeletalMeshActor class=%p", T.skm_class);
        load_fn("SkinnedMeshComponent", "SetSkinnedAssetAndUpdate", &T.setskinned);
        load_fn("SceneComponent", "SetVisibility", &T.setvis);
        load_fn("SceneComponent", "SetHiddenInGame", &T.sethidcomp);
        load_fn("Actor", "SetActorHiddenInGame", &T.sethidden);
        load_fn("Actor", "SetActorEnableCollision", &T.setcoll);
        load_fn("Actor", "K2_SetActorRotation", &T.setrot);
        T.off_root = find_prop_offset(hc, "RootComponent");
        T.root = (uint8_t *)rd64(T.hero + T.off_root);
        T.off_loc = find_prop_offset(obj_class(T.root), "RelativeLocation");
        load_fn("Actor", "K2_GetActorLocation", &T.getloc);
        load_fn("Actor", "K2_SetActorLocation", &T.setloc);
        load_fn("GameplayStatics", "BeginDeferredActorSpawnFromClass", &T.begin_spawn);
        load_fn("GameplayStatics", "FinishSpawningActor", &T.finish_spawn);
        load_fn("StaticMeshComponent", "SetStaticMesh", &T.setmesh);
        load_fn("SceneComponent", "SetMobility", &T.setmob);
        load_fn("SceneComponent", "SetWorldScale3D", &T.setscale);
        T.gs_cdo = find_object(NULL, "Default__GameplayStatics", NULL);
        T.smactor_class = find_object("Class", "StaticMeshActor", "/Script/Engine");
        const char *meshes[] = {"Cube", "Sphere", "Cylinder"};
        for (int k = 0; k < 3 && !T.mesh; k++) T.mesh = find_object("StaticMesh", meshes[k], NULL);
        logf_("N2 resolved: hero=%p root=%p gs_cdo=%p StaticMeshActor class=%p mesh=%p", T.hero, T.root, T.gs_cdo, T.smactor_class, T.mesh);
        {   char wp[MAX_PATH]; snprintf(wp, sizeof wp, "%s\\watch.txt", g_dir);
            if (GetFileAttributesA(wp) != INVALID_FILE_ATTRIBUTES) {
                T.watch_mode = 1;
                load_fn("AnimInstance", "GetCurrentActiveMontage", &T.getmontage);
                logf_("W watch mode: logging hero montage / movement mode changes (do actions in the game now)");
                T.step = 50; T.frame = 0;
                break;
            }
        }
        T.step = 1; T.frame = 0;
        break;
    }
    case 50: {  /* recon: what does the hero's AnimInstance play during jump / dodge / attack / eat? */
        {   /* once: list state-like properties of the hero, its AnimInstance and movement component, then log every change */
            static int built = 0;
            static struct { char tag[24], name[80]; uint8_t *obj; int off, size, kind; uint8_t prev[8]; } tr[160];
            static int ntr = 0;
            if (!built) {
                built = 1;
                uint8_t *mesh0 = (uint8_t *)rd64(T.hero + 792);
                int oai0 = find_prop_offset(obj_class(mesh0), "AnimScriptInstance");
                uint8_t *ai0 = oai0 >= 0 ? (uint8_t *)rd64(mesh0 + oai0) : NULL;
                uint8_t *objs[3] = { T.hero, ai0, (uint8_t *)rd64(T.hero + 800) };
                const char *tags[3] = { "hero", "anim", "move" };
                const char *keys[] = {"stance", "weapon", "sneak", "crouch", "combat", "armed", "draw", "hold", "sheath", "aim", "sprint", "walk", "run", "mode", "state", "speed", "fight", "equip", "stealth", "wield", "drawn", "ready", "item"};
                char nm[160], tn[96];
                for (int w = 0; w < 3; w++) {
                    uint8_t *obj = objs[w];
                    if (!obj) continue;
                    for (uint8_t *st = obj_class(obj); st && ntr < 150; st = (uint8_t *)rd64(st + 0x40)) {
                        char sn[96]; obj_name(st, sn, sizeof sn);
                        if (strcmp(sn, "Object") == 0 || strcmp(sn, "Actor") == 0 || strcmp(sn, "Pawn") == 0) continue;
                        for (uint8_t *p = (uint8_t *)rd64(st + 0x50); p && ntr < 150; p = (uint8_t *)rd64(p + g_off_next)) {
                            fname_str(p + g_off_name, nm, sizeof nm);
                            int hit = 0;
                            for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) if (ci_contains(nm, keys[k])) hit = 1;
                            if (!hit) continue;
                            uint8_t *fc = (uint8_t *)rd64(p + 8);
                            if (fc) fname_str(fc, tn, sizeof tn); else strcpy(tn, "?");
                            int size = rd32(p + g_off_name + 0x14), off = rd32(p + g_off_offint);
                            int kind = 0;   /* 1 bool, 2 byte/enum, 3 int, 4 float */
                            if (strcmp(tn, "BoolProperty") == 0) kind = 1;
                            else if (strcmp(tn, "ByteProperty") == 0 || strcmp(tn, "EnumProperty") == 0) kind = 2;
                            else if (strcmp(tn, "IntProperty") == 0) kind = 3;
                            else if (strcmp(tn, "FloatProperty") == 0) kind = 4;
                            logf_("W prop %s.%s::%s type=%s off=%d size=%d", tags[w], sn, nm, tn, off, size);
                            if (kind) { snprintf(tr[ntr].tag, 24, "%s", tags[w]); snprintf(tr[ntr].name, 80, "%s", nm); tr[ntr].obj = obj; tr[ntr].off = off; tr[ntr].size = size; tr[ntr].kind = kind; ntr++; }
                        }
                    }
                }
                logf_("W tracking %d properties", ntr);
            }
            if (T.frame % 4 == 0) {
                for (int i = 0; i < ntr; i++) {
                    uint8_t cur[8] = {0};
                    if (tr[i].kind == 1) { int b = read_bool(tr[i].obj, tr[i].name); cur[0] = (uint8_t)(b & 0xff); }
                    else safe_read(tr[i].obj + tr[i].off, cur, tr[i].size > 8 ? 8 : tr[i].size);
                    if (memcmp(cur, tr[i].prev, 8) != 0) {
                        if (tr[i].kind == 4) { float a, b2; memcpy(&a, tr[i].prev, 4); memcpy(&b2, cur, 4); if (fabsf(a - b2) > 0.5f) logf_("W change %s.%s: %.2f -> %.2f", tr[i].tag, tr[i].name, a, b2); }
                        else { int a = 0, b2 = 0; memcpy(&a, tr[i].prev, tr[i].size < 4 ? tr[i].size : 4); memcpy(&b2, cur, tr[i].size < 4 ? tr[i].size : 4); logf_("W change %s.%s: %d -> %d", tr[i].tag, tr[i].name, a, b2); }
                        memcpy(tr[i].prev, cur, 8);
                    }
                }
            }
        }
        if (T.frame % 4) break;
        uint8_t *mesh = (uint8_t *)rd64(T.hero + 792);
        int oai = find_prop_offset(obj_class(mesh), "AnimScriptInstance");
        uint8_t *ai = oai >= 0 ? (uint8_t *)rd64(mesh + oai) : NULL;
        uint8_t *mont = NULL;
        if (ai && T.getmontage.fn) {
            uint8_t *b = pnew(&T.getmontage);
            pcall(ai, &T.getmontage, b);
            void *r = pget(&T.getmontage, b, "ReturnValue");
            if (r) mont = *(uint8_t **)r;
            _aligned_free(b);
        }
        uint8_t *cm = (uint8_t *)rd64(T.hero + 800);
        int omm = find_prop_offset(obj_class(cm), "MovementMode"), ovel = find_prop_offset(obj_class(cm), "Velocity");
        uint8_t mm = 0; if (omm >= 0) safe_read(cm + omm, &mm, 1);
        double vel[3] = {0}; if (ovel >= 0) safe_read(cm + ovel, vel, 24);
        if (mont != T.last_montage) {
            char nm[256] = "none", cn[128] = "";
            if (mont) { obj_name(mont, nm, sizeof nm); obj_name(obj_class(mont), cn, sizeof cn); }
            logf_("W montage -> %s (%s) | movemode %d vel %.0f %.0f %.0f", nm, cn, mm, vel[0], vel[1], vel[2]);
            T.last_montage = mont;
        }
        if ((int)mm != T.last_movemode) { logf_("W movemode %d -> %d vel %.0f %.0f %.0f", T.last_movemode, mm, vel[0], vel[1], vel[2]); T.last_movemode = mm; }
        break;
    }
    case 1: {   /* K2_GetActorLocation must equal the direct read */
        if (!T.getloc.fn) { T.step = 99; break; }
        uint8_t *b = pnew(&T.getloc);
        int ok = pcall(T.hero, &T.getloc, b);
        Vec3 *rv = (Vec3 *)pget(&T.getloc, b, "ReturnValue");
        double d[3]; read_hero_pos(d);
        logf_("N2 K2_GetActorLocation call ok=%d -> %.1f %.1f %.1f ; direct read %.1f %.1f %.1f", ok, rv ? rv->x : 0, rv ? rv->y : 0, rv ? rv->z : 0, d[0], d[1], d[2]);
        _aligned_free(b);
        if (!T.net_ok) T.net_ok = net_init();
        T.step = 2; T.frame = 0;
        break;
    }
    case 2: {   /* spawn a StaticMeshActor next to the hero */
        {   /* debug switch: no puppet at all (so no pose / equip / anim modules either); the network and the reads of the local hero keep running */
            char pp[MAX_PATH]; snprintf(pp, sizeof pp, "%s\\puppet_off.txt", g_dir);
            if (GetFileAttributesA(pp) != INVALID_FILE_ATTRIBUTES) {
                logf_("N2: puppet_off.txt present: no puppet is created (network and hero reads still run)");
                T.marker = NULL; T.step = 4; T.frame = 0; break;
            }
        }
        if (!(T.begin_spawn.fn && T.finish_spawn.fn && T.gs_cdo && T.smactor_class)) { logf_("N2: spawn pieces missing"); T.step = 99; break; }
        double h[3]; read_hero_pos(h);
        Transform tr; memset(&tr, 0, sizeof tr);
        tr.q[3] = 1.0; tr.t[0] = h[0] + 300; tr.t[1] = h[1]; tr.t[2] = h[2] + 100; tr.s[0] = tr.s[1] = tr.s[2] = 1.0;
        uint8_t *b = pnew(&T.begin_spawn);
        uint8_t col = 1;   /* AlwaysSpawn */
        pset(&T.begin_spawn, b, "WorldContextObject", &T.hero, 8);
        uint8_t *spawn_cls = T.puppet_char == 2 ? T.skm_class : (T.puppet_char ? T.hero_class : T.smactor_class);
        pset(&T.begin_spawn, b, "ActorClass", &spawn_cls, 8);
        pset(&T.begin_spawn, b, "SpawnTransform", &tr, sizeof tr);
        pset(&T.begin_spawn, b, "CollisionHandlingOverride", &col, 1);
        int ok = pcall(T.gs_cdo, &T.begin_spawn, b);
        void *rvp = pget(&T.begin_spawn, b, "ReturnValue");
        uint8_t *actor = rvp ? *(uint8_t **)rvp : NULL;
        logf_("N2 BeginDeferredActorSpawnFromClass ok=%d actor=%p", ok, actor);
        _aligned_free(b);
        if (!actor) { T.step = 99; break; }
        uint8_t *b2 = pnew(&T.finish_spawn);
        pset(&T.finish_spawn, b2, "Actor", &actor, 8);
        pset(&T.finish_spawn, b2, "SpawnTransform", &tr, sizeof tr);
        ok = pcall(T.gs_cdo, &T.finish_spawn, b2);
        logf_("N2 FinishSpawningActor ok=%d", ok);
        _aligned_free(b2);
        T.marker = actor;
        T.step = 3; T.frame = 0;
        break;
    }
    case 3: {   /* mesh, mobility, scale (cube) or unhide (character) */
        if (T.puppet_char == 2) {
            int off_c = find_prop_offset(obj_class(T.marker), "SkeletalMeshComponent");
            uint8_t *comp = off_c >= 0 ? (uint8_t *)rd64(T.marker + off_c) : NULL;
            uint8_t *hmesh = (uint8_t *)rd64(T.hero + 792);
            int off_a = find_prop_offset(obj_class(hmesh), "SkinnedAsset");
            uint8_t *hasset = (uint8_t *)rd64(hmesh + off_a);
            {   /* the hero's visual yaw = capsule yaw + its Mesh component's own relative yaw (typically -90): the puppet actor must carry that offset */
                int orr = find_prop_offset(obj_class(hmesh), "RelativeRotation");
                double mr[3] = {0}; if (orr >= 0) safe_read(hmesh + orr, mr, 24);
                T.mesh_yaw = mr[1];
                logf_("N7 hero Mesh relative rotation P/Y/R = %.1f %.1f %.1f -> puppet yaw offset %.1f", mr[0], mr[1], mr[2], T.mesh_yaw);
            }
            logf_("N5 SkeletalMeshActor %p comp=%p (off %d), hero asset=%p", T.marker, comp, off_c, hasset);
            if (!comp || !hasset) { T.step = 99; break; }
            uint8_t mob = 2, yes = 1;
            uint8_t *b = pnew(&T.setmob);
            pset(&T.setmob, b, "NewMobility", &mob, 1);
            logf_("N5 SetMobility ok=%d", pcall(comp, &T.setmob, b)); _aligned_free(b);
            b = pnew(&T.setskinned);
            pset(&T.setskinned, b, "NewMesh", &hasset, 8);
            pset(&T.setskinned, b, "bReinitPose", &yes, 1);
            logf_("N5 SetSkinnedAssetAndUpdate ok=%d", pcall(comp, &T.setskinned, b)); _aligned_free(b);
            /* looping locomotion clips that share the hero skeleton (SK_NH_Ch1_Skeleton) */
            T.ksl_cdo = find_object(NULL, "Default__KismetSystemLibrary", NULL);
            load_fn("KismetSystemLibrary", "LoadAsset_Blocking", &T.loadasset);
            /* locomotion cycles, first one that is loaded wins (NPC cycles first: distinct gaits, meant to loop) */
            const char *cand[4][4] = {
                {"A_Man_Idle_01", "A_Man_N_Idle_Breathe_Light", "A_Man_Idle_Breathe", "A_Man_MM_STL_Idle"},
                {"A_Man_NPC_WalkFwd_01", "A_Man_MM_N_Walk_Forward_v4", "", ""},
                {"A_Man_NPC_RunFwd_01", "A_Man_MM_N_Run_Forward_v4", "", ""},
                {"A_Man_NPC_SprintFwd_01", "A_Man_MM_N_Sprint_Forward_v6", "", ""}};
            for (int k = 0; k < 4; k++) {
                T.anim[k] = NULL; T.anim_names[k][0] = 0;
                for (int c = 0; c < 4 && !T.anim[k]; c++) {
                    if (!cand[k][c][0]) continue;
                    T.anim[k] = find_anim(cand[k][c]);
                    if (T.anim[k]) snprintf(T.anim_names[k], 96, "%s", cand[k][c]);
                }
                root_obj(T.anim[k]);
                logf_("N6 anim[%d] = %s %p", k, T.anim_names[k][0] ? T.anim_names[k] : "(none loaded)", T.anim[k]);
            }
            T.anim_jump = find_anim("A_Man_N_Run_Jump_Loop"); root_obj(T.anim_jump);
            logf_("N6 jump loop clip = %p", T.anim_jump);
            T.skcomp = comp; T.anim_state = -1; T.cand_state = 0;
            for (int mi = 0; mi < g_nmods; mi++) g_mods[mi].need_init = 1;                              /* a (re)created puppet: let the module look its clips up again */
            T.step = 4; T.frame = 0;
            break;
        }
        if (T.puppet_char) {
            uint8_t no = 0, nocoll = 0;
            uint8_t *b = pnew(&T.sethidden);
            pset(&T.sethidden, b, "bNewHidden", &no, 1);
            logf_("N4 SetActorHiddenInGame(false) ok=%d", pcall(T.marker, &T.sethidden, b)); _aligned_free(b);
            b = pnew(&T.setcoll);
            pset(&T.setcoll, b, "bNewActorEnableCollision", &nocoll, 1);
            logf_("N4 SetActorEnableCollision(false) ok=%d", pcall(T.marker, &T.setcoll, b)); _aligned_free(b);
            /* the spawned puppet only has the "Prebaked_NH" mesh; give it the hero's (Mutable generated) mesh and force visibility */
            uint8_t *pmesh = (uint8_t *)rd64(T.marker + 792), *hmesh = (uint8_t *)rd64(T.hero + 792);
            T.off_mesh_asset = find_prop_offset(obj_class(hmesh), "SkinnedAsset");
            uint8_t *hasset = (uint8_t *)rd64(hmesh + T.off_mesh_asset);
            if (T.setskinned.fn && hasset) {
                uint8_t yes = 1;
                b = pnew(&T.setskinned);
                pset(&T.setskinned, b, "NewMesh", &hasset, 8);
                pset(&T.setskinned, b, "bReinitPose", &yes, 1);
                logf_("N4 SetSkinnedAssetAndUpdate(%p) ok=%d", hasset, pcall(pmesh, &T.setskinned, b)); _aligned_free(b);
                T.last_asset = hasset;
            }
            uint8_t yes2 = 1, no2 = 0;
            b = pnew(&T.setvis);
            pset(&T.setvis, b, "bNewVisibility", &yes2, 1); pset(&T.setvis, b, "bPropagateToChildren", &yes2, 1);
            logf_("N4 SetVisibility(true) ok=%d", pcall(pmesh, &T.setvis, b)); _aligned_free(b);
            b = pnew(&T.sethidcomp);
            pset(&T.sethidcomp, b, "NewHidden", &no2, 1); pset(&T.sethidcomp, b, "bPropagateToChildren", &yes2, 1);
            logf_("N4 SetHiddenInGame(false) ok=%d", pcall(pmesh, &T.sethidcomp, b)); _aligned_free(b);
            T.step = 4; T.frame = 0;
            break;
        }
        T.off_smc = find_prop_offset(obj_class(T.marker), "StaticMeshComponent");
        uint8_t *comp = (uint8_t *)rd64(T.marker + T.off_smc);
        logf_("N2 marker %p StaticMeshComponent off=%d comp=%p", T.marker, T.off_smc, comp);
        if (!comp) { T.step = 99; break; }
        uint8_t mob = 2;   /* Movable */
        uint8_t *b = pnew(&T.setmob);
        pset(&T.setmob, b, "NewMobility", &mob, 1);
        logf_("N2 SetMobility ok=%d", pcall(comp, &T.setmob, b)); _aligned_free(b);
        if (T.mesh) {
            b = pnew(&T.setmesh);
            pset(&T.setmesh, b, "NewMesh", &T.mesh, 8);
            logf_("N2 SetStaticMesh ok=%d", pcall(comp, &T.setmesh, b)); _aligned_free(b);
        } else logf_("N2 no loaded Cube/Sphere/Cylinder mesh found");
        Vec3 sc = {0.6, 0.6, 1.8};
        b = pnew(&T.setscale);
        pset(&T.setscale, b, "NewScale", &sc, 24);
        logf_("N2 SetWorldScale3D ok=%d", pcall(comp, &T.setscale, b)); _aligned_free(b);
        T.step = 4; T.frame = 0;
        break;
    }
    case 4: {   /* N3: send my hero state, show the remote player as the marker */
        if (T.puppet_char && T.marker && T.sethidden.fn && read_bool(T.marker, "bHidden") == 1) {
            /* the game keeps re-hiding the spawned character; undo it at the end of every tick, before rendering */
            uint8_t no = 0;
            uint8_t *ub = pnew(&T.sethidden);
            pset(&T.sethidden, ub, "bNewHidden", &no, 1);
            pcall(T.marker, &T.sethidden, ub);
            _aligned_free(ub);
            T.unhides++;
            if (T.unhides <= 5 || T.unhides % 300 == 0) logf_("N4 re-unhide #%d at frame %d, bHidden now %d", T.unhides, T.frame, read_bool(T.marker, "bHidden"));
        }
        if (T.puppet_char && T.frame == 5) {
            load_fn("SceneComponent", "IsVisible", &g_isvis);
            load_fn("Actor", "GetLastRenderTime", &g_lastrender);
            load_fn("GameplayStatics", "GetTimeSeconds", &g_timeseconds);
        }
        if (T.puppet_char == 1 && (T.frame == 60 || T.frame == 300 || T.frame == 900)) { dump_vis("hero  ", T.hero); dump_vis("puppet", T.marker); }
        double h[3]; read_hero_pos(h);
        double r[3] = {0}; safe_read(T.root + 320, r, 24);   /* RelativeRotation: pitch, yaw, roll */
        if (T.net_ok && T.frame % 3 == 0) {
            char msg[1600], act[96]; int mm = 1;
            local_action(act, sizeof act, &mm);
            if (strcmp(act, T.sent_act) != 0) { logf_("A local action -> %s (movemode %d)", act, mm); snprintf(T.sent_act, sizeof T.sent_act, "%s", act); }
            uint8_t *cmv = (uint8_t *)rd64(T.hero + 800);
            if (T.off_vel <= 0) T.off_vel = find_prop_offset(obj_class(cmv), "Velocity");
            double vel[3] = {0}; if (T.off_vel > 0) safe_read(cmv + T.off_vel, vel, 24);
            double spd = sqrt(vel[0] * vel[0] + vel[1] * vel[1]);
            int crouch = read_bool(T.hero, "bIsCrouched"); if (crouch < 0) crouch = 0;
            if (crouch != T.last_cr) { logf_("C local crouch -> %d", crouch); T.last_cr = crouch; }
            if (T.frame % 30 == 0 && spd > 5) logf_("V local speed %.0f vz %.0f", spd, vel[2]);
            int n = snprintf(msg, sizeof msg, "P|%s|%.2f|%.2f|%.2f|%.1f|%d|%d|%s|%s|%.1f|%.1f|%d|%s", T.id, h[0], h[1], h[2], r[1], ++T.seq, mm, act, g_act_pkg[0] ? g_act_pkg : "-", spd, vel[2], crouch, T.local_extra[0] ? T.local_extra : "-");
            sendto(T.sock, msg, n, 0, (struct sockaddr *)&T.peer, sizeof T.peer);
            if (g_lblob_len > 0) {                      /* "B|" chunk index, chunk count, payload (at most BLOB_CHUNK bytes) */
                int count = (g_lblob_len + BLOB_CHUNK - 1) / BLOB_CHUNK;
                for (int k = 0; k < count; k++) {
                    uint8_t bm[BLOB_CHUNK + 8]; int pl = g_lblob_len - k * BLOB_CHUNK; if (pl > BLOB_CHUNK) pl = BLOB_CHUNK;
                    bm[0] = 'B'; bm[1] = '|'; bm[2] = (uint8_t)k; bm[3] = (uint8_t)count; memcpy(bm + 4, g_lblob + k * BLOB_CHUNK, (size_t)pl);
                    sendto(T.sock, (const char *)bm, pl + 4, 0, (struct sockaddr *)&T.peer, sizeof T.peer);
                }
            }
        }
        if (T.net_ok && !T.have_remote && T.frame % 600 == 0) logf_("net: still waiting for the first packet from the friend (check the address, the port and the firewall)");
        if (T.net_ok) {
            char rx[1700];
            for (;;) {
                struct sockaddr_in from; int fromlen = sizeof from;
                int n = recvfrom(T.sock, rx, sizeof rx - 1, 0, (struct sockaddr *)&from, &fromlen);
                if (n <= 0) break;
                if (from.sin_addr.s_addr != T.peer.sin_addr.s_addr) {           /* only the configured friend may talk to us */
                    static int rejected; if (rejected++ < 3) { char fb[64]; inet_ntop(AF_INET, &from.sin_addr, fb, sizeof fb); logf_("net: ignored a packet from %s", fb); }
                    continue;
                }
                rx[n] = 0;
                if (n > 4 && rx[0] == 'B' && rx[1] == '|') {
                    int k = (uint8_t)rx[2], count = (uint8_t)rx[3], pl = n - 4;
                    if (k == 0) g_rexpect = 0;
                    if (k == g_rexpect && (k + 1) * BLOB_CHUNK <= BLOB_MAX + BLOB_CHUNK - pl && k * BLOB_CHUNK + pl <= BLOB_MAX) {
                        memcpy(g_rasm + k * BLOB_CHUNK, rx + 4, (size_t)pl); g_rexpect++;
                        if (k == count - 1) { g_rblob_len = k * BLOB_CHUNK + pl; memcpy(g_rblob, g_rasm, (size_t)g_rblob_len); g_rblob_seq++; g_rexpect = 0; }
                    } else g_rexpect = 0;
                    continue;
                }
                char pid[16], ract[96] = "-", rpkg[256] = "-"; double x, y, z, yaw, rsp = -1, rvz = 0; int seq, rmm = 1, rcr = 0; char rextra[900] = "-";
                if (sscanf(rx, "P|%15[^|]|%lf|%lf|%lf|%lf|%d|%d|%95[^|]|%255[^|]|%lf|%lf|%d|%899[^\n]", pid, &x, &y, &z, &yaw, &seq, &rmm, ract, rpkg, &rsp, &rvz, &rcr, rextra) >= 6) {
                    snprintf(T.remote_extra, sizeof T.remote_extra, "%s", rextra);
                    if (rsp >= 0) { T.rspd = rsp; T.rvz = rvz; T.rspd_valid = 1; }
                    T.rcrouch = rcr;
                    unsigned long tnow = GetTickCount();
                    if (rmm == 3 && T.rmm != 3) T.fall_since = tnow;
                    T.rmm = rmm; snprintf(T.ract, sizeof T.ract, "%s", ract); snprintf(T.rpkg, sizeof T.rpkg, "%s", rpkg);
                    unsigned long nowt = GetTickCount();
                    if (T.have_remote && nowt > T.last_rt) {
                        double dx = x - T.last_rp[0], dy = y - T.last_rp[1];
                        double sp = sqrt(dx * dx + dy * dy) / ((nowt - T.last_rt) / 1000.0);
                        T.speed = T.speed * 0.6 + sp * 0.4;
                        double vzn = (z - T.last_rp[2]) / ((nowt - T.last_rt) / 1000.0);
                        T.vz = T.vz * 0.5 + vzn * 0.5;
                    }
                    T.last_rp[0] = x; T.last_rp[1] = y; T.last_rp[2] = z; T.last_rt = nowt;
                    T.rpos[0] = x; T.rpos[1] = y; T.rpos[2] = z; T.ryaw = yaw;
                    if (!T.have_remote) { T.shown[0] = x; T.shown[1] = y; T.shown[2] = z; logf_("N3 first packet from %s at %.0f %.0f %.0f", pid, x, y, z); }
                    T.have_remote = 1;
                }
            }
        }
        if (T.have_remote && T.marker && T.puppet_char == 2 && T.skcomp && T.playanim.fn) module_frame();
        if (T.have_remote && T.marker) {
            for (int k = 0; k < 3; k++) T.shown[k] += (T.rpos[k] - T.shown[k]) * 0.35;   /* smooth 20 Hz data */
            Vec3 np = { T.shown[0], T.shown[1], T.shown[2] + (T.puppet_char == 2 ? -90 : (T.puppet_char ? 0 : 90)) };
            uint8_t *b = pnew(&T.setloc);
            uint8_t sweep = 0, tele = 1;
            pset(&T.setloc, b, "NewLocation", &np, 24);
            pset(&T.setloc, b, "bSweep", &sweep, 1);
            pset(&T.setloc, b, "bTeleport", &tele, 1);
            int ok = pcall(T.marker, &T.setloc, b);
            if (T.puppet_char && T.setrot.fn) {
                Vec3 rot = { 0.0, T.ryaw + T.mesh_yaw, 0.0 };   /* FRotator: pitch, yaw, roll; + the hero mesh's own yaw inside its capsule */
                uint8_t tp = 1;
                uint8_t *rb = pnew(&T.setrot);
                pset(&T.setrot, rb, "NewRotation", &rot, 24);
                pset(&T.setrot, rb, "bTeleportPhysics", &tp, 1);
                pcall(T.marker, &T.setrot, rb);
                _aligned_free(rb);
            }
            if (T.moves++ % 120 == 0) logf_("N3 remote at %.0f %.0f %.0f shown ok=%d (packets seq=%d)", T.rpos[0], T.rpos[1], T.rpos[2], ok, T.seq);
            _aligned_free(b);
        }
        break;
    }
    default: break;
    }
}

static void hk_tick(void *self, float dt, unsigned char idle)
{
    g_orig_tick(self, dt, idle);
    g_game_tid = GetCurrentThreadId();
    __try { frame_work(); } __except (EXCEPTION_EXECUTE_HANDLER) { logf_("EXCEPTION 0x%lx in frame_work step %d", GetExceptionCode(), T.step); T.step = 99; }
    __try { module_always(); } __except (EXCEPTION_EXECUTE_HANDLER) { logf_("EXCEPTION 0x%lx in module_always", GetExceptionCode()); }
}

static void install_tick_hook(void)
{
    char nm[256];
    uint8_t *eng = NULL;
    for (int32_t i = 0; i < g_num_elements && !eng; i++) {
        uint8_t *o = get_object(i); if (!o) continue;
        obj_name(o, nm, sizeof nm);
        if (strncmp(nm, "GameEngine_", 11) == 0) eng = o;
    }
    if (!eng) { logf_("hook: no GameEngine"); return; }
    void **vt = (void **)rd64(eng);
    void **slot = &vt[95];
    g_pe = (ProcessEvent_t)(g_base + RVA_PROCESSEVENT);
    g_orig_tick = (EngineTick_t)*slot;
    DWORD old;
    if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) { logf_("hook: VirtualProtect failed"); return; }
    *slot = (void *)hk_tick;
    VirtualProtect(slot, 8, old, &old);
    logf_("hook installed: vtable slot 95 %p -> %p (orig %p)", (void *)slot, (void *)hk_tick, (void *)g_orig_tick);
}
