/*
 * probe module: a live recon console for the running game. Always active (also in the main menu).
 *   write build\probe_cmd.txt  (first line "#<id>", then one command per line); every game process answers in build\probe_out_<pid>.txt
 *   (first line "#<id>", then the output). tools\dev.ps1 probe "<command>" does both and prints the answers.
 * Commands: see "help" below. Pointers can be written as 0x..., hero, puppet, mesh, world, cdo:<Class>, or $alias.
 * Everything is guarded; a bad pointer prints an error instead of crashing. Hot-reloadable.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "coreapi.h"

#define OUTCAP 120000
static char *g_out; static int g_olen;
static char g_dir[MAX_PATH];
static unsigned long g_last_poll; static char g_last_id[64];
static struct { char name[40]; uint8_t *ptr; } g_alias[32]; static int g_nalias;
static CoreAPI *A;

static void P(const char *fmt, ...)
{
    if (g_olen > OUTCAP - 600) return;
    va_list ap; va_start(ap, fmt);
    g_olen += vsnprintf(g_out + g_olen, (size_t)(OUTCAP - g_olen), fmt, ap);
    va_end(ap);
    g_out[g_olen++] = '\n'; g_out[g_olen] = 0;
}

static int ci_has(const char *s, const char *sub)
{
    if (!sub || !sub[0]) return 1;
    size_t n = strlen(sub);
    for (; *s; s++) if (_strnicmp(s, sub, n) == 0) return 1;
    return 0;
}

__declspec(dllexport) void mod_init(CoreAPI *a)
{
    A = a;
    if (!g_out) g_out = (char *)malloc(OUTCAP + 16);
    char path[MAX_PATH]; HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&mod_init, &self);
    GetModuleFileNameA(self, path, MAX_PATH);                              /* ...\build\modules\probe_<pid>_<n>.dll */
    char *s = strrchr(path, '\\'); if (s) *s = 0;
    s = strrchr(path, '\\'); if (s) *s = 0;                                /* ...\build */
    snprintf(g_dir, sizeof g_dir, "%s", path);
    g_last_id[0] = 0;
    a->log("PR probe ready, dir %s", g_dir);
}
__declspec(dllexport) void mod_shutdown(CoreAPI *a) { (void)a; }

/* ---------- pointers ---------- */
static uint8_t *find_world(void)
{
    static uint8_t *w;
    char nm[128], cn[64];
    if (w && A->obj_alive(w, NULL)) return w;
    w = NULL;
    int n = A->num_objects();
    for (int i = 0; i < n && !w; i++) {
        uint8_t *o = A->object_at(i); if (!o) continue;
        A->obj_name(A->obj_class(o), cn, sizeof cn);
        if (strcmp(cn, "World") != 0) continue;
        A->obj_name(o, nm, sizeof nm);
        if (strncmp(nm, "Default__", 9) != 0) w = o;
    }
    return w;
}
static uint8_t *ptr_of(const char *t)
{
    if (!t || !t[0]) return NULL;
    if (strcmp(t, "hero") == 0) return A->hero;
    if (strcmp(t, "puppet") == 0) return A->puppet;
    if (strcmp(t, "mesh") == 0) return A->puppet_mesh;
    if (strcmp(t, "world") == 0) return find_world();
    if (t[0] == '$') { for (int i = 0; i < g_nalias; i++) if (strcmp(g_alias[i].name, t + 1) == 0) return g_alias[i].ptr; return NULL; }
    if (strncmp(t, "cdo:", 4) == 0) { char nm[100]; snprintf(nm, sizeof nm, "Default__%s", t + 4); return A->find_object(NULL, nm, NULL); }
    return (uint8_t *)(uintptr_t)strtoull(t, NULL, 0);
}
static int readable(const void *p) { char b; return p && A->read_mem(p, &b, 1); }

/* ---------- value formatting ---------- */
static void fmt_value(uint8_t *obj, const char *name, const char *type, int off, int size, char *out, int cap)
{
    out[0] = 0;
    if (!strcmp(type, "ObjectProperty") || !strcmp(type, "ObjectPtrProperty") || !strcmp(type, "WeakObjectProperty") || !strcmp(type, "LazyObjectProperty")) {
        uint8_t *t = NULL; A->read_mem(obj + off, &t, 8);
        if (!t) snprintf(out, (size_t)cap, "null");
        else { char on[160] = "", cn[96] = ""; A->obj_name(t, on, sizeof on); A->obj_name(A->obj_class(t), cn, sizeof cn); snprintf(out, (size_t)cap, "%p %s (%s)", t, on, cn); }
    } else if (!strcmp(type, "BoolProperty")) snprintf(out, (size_t)cap, "%d", A->read_bool(obj, name));
    else if (!strcmp(type, "IntProperty") || !strcmp(type, "UInt32Property")) { int v = 0; A->read_mem(obj + off, &v, 4); snprintf(out, (size_t)cap, "%d", v); }
    else if (!strcmp(type, "Int64Property") || !strcmp(type, "UInt64Property")) { long long v = 0; A->read_mem(obj + off, &v, 8); snprintf(out, (size_t)cap, "%lld", v); }
    else if (!strcmp(type, "FloatProperty")) { float v = 0; A->read_mem(obj + off, &v, 4); snprintf(out, (size_t)cap, "%g", v); }
    else if (!strcmp(type, "DoubleProperty")) { double v = 0; A->read_mem(obj + off, &v, 8); snprintf(out, (size_t)cap, "%g", v); }
    else if (!strcmp(type, "ByteProperty") || !strcmp(type, "EnumProperty")) { unsigned v = 0; A->read_mem(obj + off, &v, size > 4 ? 4 : (size < 1 ? 1 : size)); snprintf(out, (size_t)cap, "%u", v); }
    else if (!strcmp(type, "NameProperty")) { char nm[160] = ""; A->fname_to_str(obj + off, nm, sizeof nm); snprintf(out, (size_t)cap, "'%s'", nm); }
    else if (!strcmp(type, "StrProperty")) {
        uint8_t *d = NULL; int n = 0; A->read_mem(obj + off, &d, 8); A->read_mem(obj + off + 8, &n, 4);
        if (!d || n <= 0 || n > 4000) snprintf(out, (size_t)cap, "\"\""); else {
            wchar_t w[130]; int m = n < 128 ? n : 128; memset(w, 0, sizeof w); A->read_mem(d, w, (size_t)m * 2);
            char u[300]; WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL); snprintf(out, (size_t)cap, "\"%s\"", u);
        }
    } else if (!strcmp(type, "ArrayProperty")) { int n = 0; A->read_mem(obj + off + 8, &n, 4); snprintf(out, (size_t)cap, "num=%d", n); }
    else {
        unsigned char b[24] = { 0 }; int m = size < 24 ? size : 24; if (m < 0) m = 0; A->read_mem(obj + off, b, (size_t)m);
        char *o = out; o += snprintf(o, (size_t)cap, "%d bytes:", size);
        for (int i = 0; i < m && o - out < cap - 4; i++) o += sprintf(o, " %02x", b[i]);
    }
}

/* ---------- members ---------- */
struct MemCtx { uint8_t *obj; const char *filter; int props, fns, max; int want_fns; };
static void member_cb(const char *kind, const char *sname, const char *name, const char *type, int off, int size, void *ud)
{
    struct MemCtx *c = (struct MemCtx *)ud;
    if (!ci_has(name, c->filter)) return;
    if (!strcmp(kind, "fn")) { if (!c->want_fns || c->fns >= c->max) return; c->fns++; P("  fn   %s::%s", sname, name); return; }
    if (c->props >= c->max) return;
    c->props++;
    char val[400] = "";
    if (c->obj) fmt_value(c->obj, name, type, off, size, val, sizeof val);
    P("  prop %s::%s [%s @%d/%d]%s%s", sname, name, type, off, size, c->obj ? " = " : "", val);
}
static uint8_t *class_by_name(const char *n)
{
    uint8_t *c = A->find_object("Class", n, NULL);
    if (!c) c = A->find_object("BlueprintGeneratedClass", n, NULL);
    if (c) return c;
    int cnt = A->num_objects(); char nm[160], cn[96];                 /* any other kind of class (ASClass, WidgetBlueprintGeneratedClass ...) */
    for (int i = 0; i < cnt; i++) {
        uint8_t *o = A->object_at(i); if (!o) continue;
        A->obj_name(o, nm, sizeof nm); if (strcmp(nm, n) != 0) continue;
        A->obj_name(A->obj_class(o), cn, sizeof cn);
        size_t l = strlen(cn); if (l >= 5 && strcmp(cn + l - 5, "Class") == 0) return o;
    }
    return NULL;
}

struct InfoCtx { const char *want; char type[64]; int off, size, found; };
static void info_cb(const char *kind, const char *sname, const char *name, const char *type, int off, int size, void *ud)
{
    struct InfoCtx *c = (struct InfoCtx *)ud; (void)sname;
    if (!c->found && !strcmp(kind, "prop") && !strcmp(name, c->want)) { snprintf(c->type, sizeof c->type, "%s", type); c->off = off; c->size = size; c->found = 1; }
}

/* ---------- commands ---------- */
static void cmd_help(void)
{
    P("commands (pointers: 0x.., hero, puppet, mesh, world, cdo:<Class>, $alias):");
    P("  help | echo <text> | alias <name> <ptr> | aliases");
    P("  find <Class> <Name>            first object of that class and exact name");
    P("  findall <classSub> [nameSub] [max]   objects whose class/name contain the text");
    P("  members <ptr> [filter] [max]   properties (with values) of the object's class chain; add 'fns' as the last word to list functions too");
    P("  class <Name> [filter] [max]    properties and functions of a class by name (no values)");
    P("  get <ptr> <Prop>               one property value");
    P("  set <ptr> <Prop> <value>       write an int/float/bool/byte property");
    P("  fn <Outer> <Function>          parameters of a function (no call)");
    P("  call <ptr> <Outer> <Function> [Param=type:value ...]   types: i int, f float, d double, b bool, p pointer, n name, v3 x,y,z");
    P("  cc <console command>           KismetSystemLibrary.ExecuteConsoleCommand in the current world");
    P("  frames                          engine frame counter and object count");
}

static void run_command(char *line)
{
    char *tok[24]; int nt = 0;
    for (char *p = strtok(line, " \t\r"); p && nt < 24; p = strtok(NULL, " \t\r")) tok[nt++] = p;
    if (!nt) return;
    const char *c = tok[0];
    P("> %s%s%s%s%s", tok[0], nt > 1 ? " " : "", nt > 1 ? tok[1] : "", nt > 2 ? " " : "", nt > 2 ? tok[2] : "");

    if (!strcmp(c, "help")) { cmd_help(); return; }
    if (!strcmp(c, "echo")) { char b[300] = ""; for (int i = 1; i < nt; i++) { strcat(b, tok[i]); strcat(b, " "); } P("%s", b); return; }
    if (!strcmp(c, "frames")) { P("objects=%d hero=%p puppet=%p have_remote=%d", A->num_objects(), A->hero, A->puppet, A->have_remote); return; }
    if (!strcmp(c, "aliases")) { for (int i = 0; i < g_nalias; i++) P("  $%s = %p", g_alias[i].name, g_alias[i].ptr); return; }
    if (!strcmp(c, "alias") && nt >= 3) {
        uint8_t *p = ptr_of(tok[2]); int i;
        for (i = 0; i < g_nalias; i++) if (!strcmp(g_alias[i].name, tok[1])) break;
        if (i == g_nalias && g_nalias < 32) g_nalias++;
        if (i < 32) { snprintf(g_alias[i].name, sizeof g_alias[i].name, "%s", tok[1]); g_alias[i].ptr = p; P("$%s = %p", tok[1], p); }
        return;
    }
    if (!strcmp(c, "find") && nt >= 3) {
        uint8_t *o = A->find_object(strcmp(tok[1], "*") ? tok[1] : NULL, tok[2], NULL);
        if (!o) P("not found"); else { char cn[96] = ""; A->obj_name(A->obj_class(o), cn, sizeof cn); P("%p %s (%s)", o, tok[2], cn); }
        return;
    }
    if (!strcmp(c, "findall") && nt >= 2) {
        const char *csub = tok[1], *nsub = nt > 2 && strcmp(tok[2], "*") ? tok[2] : NULL; int max = nt > 3 ? atoi(tok[3]) : 30, shown = 0, total = 0;
        int n = A->num_objects(); char nm[160], cn[96];
        for (int i = 0; i < n; i++) {
            uint8_t *o = A->object_at(i); if (!o) continue;
            A->obj_name(A->obj_class(o), cn, sizeof cn);
            if (!ci_has(cn, csub)) continue;
            A->obj_name(o, nm, sizeof nm);
            if (!ci_has(nm, nsub)) continue;
            total++;
            if (shown < max) { shown++; P("  %p %s (%s)", o, nm, cn); }
        }
        P("%d matches, %d shown", total, shown);
        return;
    }
    if (!strcmp(c, "members") && nt >= 2) {
        uint8_t *o = ptr_of(tok[1]);
        if (!readable(o)) { P("bad pointer %s", tok[1]); return; }
        struct MemCtx ctx = { o, NULL, 0, 0, 80, 0 };
        for (int i = 2; i < nt; i++) { if (!strcmp(tok[i], "fns")) ctx.want_fns = 1; else if (atoi(tok[i]) > 0) ctx.max = atoi(tok[i]); else ctx.filter = tok[i]; }
        char cn[96] = ""; A->obj_name(A->obj_class(o), cn, sizeof cn); P("%p class %s", o, cn);
        A->list_members(A->obj_class(o), member_cb, &ctx);
        P("%d props, %d fns shown", ctx.props, ctx.fns);
        return;
    }
    if (!strcmp(c, "class") && nt >= 2) {
        uint8_t *k = class_by_name(tok[1]);
        if (!k) { P("class %s not found", tok[1]); return; }
        struct MemCtx ctx = { NULL, nt > 2 && atoi(tok[2]) == 0 && strcmp(tok[2], "*") ? tok[2] : NULL, 0, 0, 200, 1 };
        for (int i = 2; i < nt; i++) if (atoi(tok[i]) > 0) ctx.max = atoi(tok[i]);
        A->list_members(k, member_cb, &ctx);
        P("%d props, %d fns shown", ctx.props, ctx.fns);
        return;
    }
    if (!strcmp(c, "get") && nt >= 3) {
        uint8_t *o = ptr_of(tok[1]);
        if (!readable(o)) { P("bad pointer %s", tok[1]); return; }
        struct InfoCtx ic = { tok[2], "", 0, 0, 0 };
        A->list_members(A->obj_class(o), info_cb, &ic);
        if (!ic.found) { P("no property %s", tok[2]); return; }
        char val[400]; fmt_value(o, tok[2], ic.type, ic.off, ic.size, val, sizeof val);
        P("%s [%s @%d/%d] = %s", tok[2], ic.type, ic.off, ic.size, val);
        return;
    }
    if (!strcmp(c, "set") && nt >= 4) {
        uint8_t *o = ptr_of(tok[1]);
        if (!readable(o)) { P("bad pointer %s", tok[1]); return; }
        struct InfoCtx ic = { tok[2], "", 0, 0, 0 };
        A->list_members(A->obj_class(o), info_cb, &ic);
        if (!ic.found) { P("no property %s", tok[2]); return; }
        int ok = 0;
        if (!strcmp(ic.type, "IntProperty")) { int v = atoi(tok[3]); ok = A->write_mem(o + ic.off, &v, 4); }
        else if (!strcmp(ic.type, "FloatProperty")) { float v = (float)atof(tok[3]); ok = A->write_mem(o + ic.off, &v, 4); }
        else if (!strcmp(ic.type, "DoubleProperty")) { double v = atof(tok[3]); ok = A->write_mem(o + ic.off, &v, 8); }
        else if (!strcmp(ic.type, "ByteProperty") || !strcmp(ic.type, "EnumProperty")) { unsigned char v = (unsigned char)atoi(tok[3]); ok = A->write_mem(o + ic.off, &v, 1); }
        else if (!strcmp(ic.type, "BoolProperty")) { P("bool bitfields are not writable here"); return; }
        else { P("cannot write a %s", ic.type); return; }
        P("%s written: %d", tok[2], ok);
        return;
    }
    if (!strcmp(c, "fn") && nt >= 3) {
        char d[900]; if (!A->describe_fn(tok[1], tok[2], d, sizeof d)) P("function %s:%s not found", tok[1], tok[2]); else P("%s:%s(%s)", tok[1], tok[2], d);
        return;
    }
    if ((!strcmp(c, "call") && nt >= 4) || !strcmp(c, "cc")) {
        uint8_t *o; const char *outer, *fn; int first;
        char cmdtext[400] = "";
        if (!strcmp(c, "cc")) {
            o = A->find_object(NULL, "Default__KismetSystemLibrary", NULL); outer = "KismetSystemLibrary"; fn = "ExecuteConsoleCommand"; first = 99;
            for (int i = 1; i < nt; i++) { strcat(cmdtext, tok[i]); if (i + 1 < nt) strcat(cmdtext, " "); }
        } else { o = ptr_of(tok[1]); outer = tok[2]; fn = tok[3]; first = 4; }
        if (!readable(o)) { P("bad target %s", tok[1]); return; }
        const char *names[16]; const void *vals[16]; int sizes[16]; int na = 0;
        unsigned char store[16][32]; char nstr[16][100];
        uint8_t *world = NULL; uint8_t nullp[8] = { 0 };
        if (!strcmp(c, "cc")) {
            world = find_world();
            if (!world) { P("no world object"); return; }
            names[na] = "WorldContextObject"; vals[na] = &world; sizes[na++] = 8;
            names[na] = "Command"; vals[na] = cmdtext; sizes[na++] = -3;     /* FString: handled below */
            names[na] = "SpecificPlayer"; vals[na] = nullp; sizes[na++] = 8;
            /* FString parameters are not supported by call_fn: build a small FString by hand */
            static wchar_t wbuf[400]; MultiByteToWideChar(CP_UTF8, 0, cmdtext, -1, wbuf, 400);
            static struct { wchar_t *data; int num, max; } fs; fs.data = wbuf; fs.num = fs.max = (int)wcslen(wbuf) + 1;
            vals[1] = &fs; sizes[1] = 16;
        }
        for (int i = first; i < nt && na < 16; i++) {
            char *eq = strchr(tok[i], '='); if (!eq) continue; *eq = 0;
            const char *tv = eq + 1;
            names[na] = tok[i]; vals[na] = store[na]; memset(store[na], 0, 32);
            if (!strncmp(tv, "i:", 2)) { int v = atoi(tv + 2); memcpy(store[na], &v, 4); sizes[na] = 4; }
            else if (!strncmp(tv, "f:", 2)) { float v = (float)atof(tv + 2); memcpy(store[na], &v, 4); sizes[na] = 4; }
            else if (!strncmp(tv, "d:", 2)) { double v = atof(tv + 2); memcpy(store[na], &v, 8); sizes[na] = 8; }
            else if (!strncmp(tv, "b:", 2)) { store[na][0] = (unsigned char)atoi(tv + 2); sizes[na] = 1; }
            else if (!strncmp(tv, "p:", 2)) { uint8_t *p = ptr_of(tv + 2); memcpy(store[na], &p, 8); sizes[na] = 8; }
            else if (!strncmp(tv, "n:", 2)) { snprintf(nstr[na], sizeof nstr[na], "%s", tv + 2); vals[na] = nstr[na]; sizes[na] = -1; }
            else if (!strncmp(tv, "v3:", 3)) { double v[3] = { 0 }; sscanf(tv + 3, "%lf,%lf,%lf", &v[0], &v[1], &v[2]); memcpy(store[na], v, 24); sizes[na] = 24; }
            else { P("bad value '%s' (use i: f: d: b: p: n: v3:)", tv); return; }
            na++;
        }
        char d[900]; if (A->describe_fn(outer, fn, d, sizeof d)) P("%s:%s(%s)", outer, fn, d); else { P("function %s:%s not found", outer, fn); return; }
        unsigned char ret[64]; memset(ret, 0, sizeof ret);
        int ok = A->call_fn(o, outer, fn, na, names, vals, sizes, ret, sizeof ret);
        P("call ran: %d", ok);
        int iv; float fv; uint8_t *pv; memcpy(&iv, ret, 4); memcpy(&fv, ret, 4); memcpy(&pv, ret, 8);
        P("ReturnValue bytes: %02x %02x %02x %02x %02x %02x %02x %02x | int %d float %g ptr %p", ret[0], ret[1], ret[2], ret[3], ret[4], ret[5], ret[6], ret[7], iv, fv, pv);
        if (pv && readable(pv)) { char on[160] = "", cn[96] = ""; A->obj_name(pv, on, sizeof on); A->obj_name(A->obj_class(pv), cn, sizeof cn); P("  as object: %s (%s)", on, cn); }
        return;
    }
    P("unknown or incomplete command (try: help)");
}

static void poll(void)
{
    char cmdp[MAX_PATH], outp[MAX_PATH];
    snprintf(cmdp, sizeof cmdp, "%s\\probe_cmd.txt", g_dir);
    FILE *f = fopen(cmdp, "rb");
    if (!f) return;
    static char buf[16000];
    size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f);
    buf[n] = 0;
    char id[64] = ""; char *body = buf;
    if (buf[0] == '#') { char *e = strpbrk(buf, "\r\n"); size_t l = e ? (size_t)(e - buf) : n; if (l > 63) l = 63; memcpy(id, buf, l); id[l] = 0; body = e ? e + 1 : buf + n; }
    if (!id[0] || !strcmp(id, g_last_id)) return;                  /* only a new "#<id>" line triggers a run */
    snprintf(g_last_id, sizeof g_last_id, "%s", id);
    g_olen = 0; g_out[0] = 0;
    P("%s pid %lu", id, GetCurrentProcessId());
    char *ctx = NULL;                                                /* own context: run_command uses strtok itself */
    for (char *line = strtok_s(body, "\n", &ctx); line; line = strtok_s(NULL, "\n", &ctx)) {
        char tmp[800]; snprintf(tmp, sizeof tmp, "%s", line);
        __try { run_command(tmp); } __except (EXCEPTION_EXECUTE_HANDLER) { P("EXCEPTION 0x%lx while running: %s", GetExceptionCode(), line); }
    }
    snprintf(outp, sizeof outp, "%s\\probe_out_%lu.txt", g_dir, GetCurrentProcessId());
    FILE *o = fopen(outp, "wb");
    if (o) { fwrite(g_out, 1, (size_t)g_olen, o); fclose(o); }
}

__declspec(dllexport) void mod_update(CoreAPI *a)
{
    A = a;
    unsigned long now = GetTickCount();
    if (now - g_last_poll < 250) return;
    g_last_poll = now;
    poll();
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) { (void)h; (void)r; (void)x; return TRUE; }
