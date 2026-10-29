/* ================= hot-reloadable modules (modules\<name>.dll) ================= */
#include "coreapi.h"
static CoreAPI g_api;
#define MAX_MODS 4
static struct ModSlot {
    const char *name;
    HMODULE h; ModUpdate_t update; ModShutdown_t shutdown; ModInit_t init;
    FILETIME mtime; int serial; int need_init; char loaded_path[MAX_PATH];
} g_mods[MAX_MODS] = { { "anim" }, { "equip" } };
static int g_nmods = 2;

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
                if (t) { obj_name(t, on, sizeof on); obj_name(obj_class(t), cn, sizeof cn); snprintf(val, sizeof val, "%p %s (%s)", t, on, cn); } else strcpy(val, "null");
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

static void module_frame(void)
{
    if (!g_api.version) api_init();
    g_api.have_remote = T.have_remote;
    g_api.speed = T.rspd_valid ? T.rspd : T.speed; g_api.vz = T.rspd_valid ? T.rvz : T.vz; g_api.movemode = T.rmm;
    g_api.crouched = T.rcrouch;
    g_api.action = T.ract; g_api.action_pkg = T.rpkg; g_api.fall_since = T.fall_since;
    g_api.hero = T.hero; g_api.puppet = T.marker; g_api.puppet_mesh = T.skcomp;
    for (int i = 0; i < g_nmods; i++) {
        struct ModSlot *m = &g_mods[i];
        if (T.frame % 30 == (i * 7) % 30) module_poll(m);
        if (!m->h || !m->update) continue;
        if (m->need_init) {
            m->need_init = 0;
            __try { m->init(&g_api); } __except (EXCEPTION_EXECUTE_HANDLER) { logf_("M exception in %s mod_init, module disabled", m->name); m->update = NULL; continue; }
        }
        __try { m->update(&g_api); } __except (EXCEPTION_EXECUTE_HANDLER) { logf_("M exception 0x%lx in %s mod_update, disabled until reload", GetExceptionCode(), m->name); m->update = NULL; }
    }
}

