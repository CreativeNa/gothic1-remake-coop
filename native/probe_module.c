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
#include <math.h>
#include <stdarg.h>
#include "coreapi.h"
#pragma comment(lib, "user32.lib")

#define OUTCAP 120000
static char *g_out; static int g_olen;
static char g_dir[MAX_PATH];
static unsigned long g_last_poll; static char g_last_id[64];
static struct { char name[40]; uint8_t *ptr; } g_alias[32]; static int g_nalias;
static CoreAPI *A;
static struct { LARGE_INTEGER last, freq; double acc; int frames; double worst; double fps, worst_ms; int fg; unsigned long log_t; } F;   /* see frame_stats */

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

/* ---- crash context: when the game thread faults on a near-null pointer inside the exe, name every game object found in the registers and on the stack ---- */
static PVOID g_veh;
static int is_uobject(uint8_t *p)
{
    if (!p || ((uintptr_t)p & 7) || (uintptr_t)p < 0x10000 || (uintptr_t)p > 0x7fffffffffffULL) return 0;
    int idx = -1; if (!A->read_mem(p + 0xC, &idx, 4) || idx < 0 || idx >= A->num_objects()) return 0;
    return A->object_at(idx) == p;
}
static void describe_obj(const char *tag, uint8_t *p)
{
    char nm[160] = "", cn[96] = "", on[160] = "";
    A->obj_name(p, nm, sizeof nm); A->obj_name(A->obj_class(p), cn, sizeof cn);
    uint8_t *outer = NULL; A->read_mem(p + 0x20, &outer, 8); if (outer) A->obj_name(outer, on, sizeof on);
    A->log("PR crash-context %s = %p %s (%s) outer=%s", tag, p, nm, cn, on);
}
static LONG CALLBACK crash_context(EXCEPTION_POINTERS *ep)
{
    static volatile LONG n;
    if (ep->ExceptionRecord->ExceptionCode != 0xC0000005 || ep->ExceptionRecord->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    ULONG_PTR addr = ep->ExceptionRecord->ExceptionInformation[1];
    uint8_t *rip = (uint8_t *)ep->ExceptionRecord->ExceptionAddress, *base = (uint8_t *)GetModuleHandleA(NULL);
    if (addr >= 0x10000 || rip < base || rip > base + 0x8000000 || InterlockedIncrement(&n) > 6) return EXCEPTION_CONTINUE_SEARCH;
    A->log("PR crash-context: near-null fault at exe+0x%llx, fault address 0x%llx", (unsigned long long)(rip - base), (unsigned long long)addr);
    CONTEXT *c = ep->ContextRecord;
    struct { const char *n; DWORD64 v; } regs[] = { {"rcx", c->Rcx}, {"rdx", c->Rdx}, {"r8", c->R8}, {"r9", c->R9}, {"rbx", c->Rbx}, {"rsi", c->Rsi}, {"rdi", c->Rdi}, {"rbp", c->Rbp}, {"r12", c->R12}, {"r13", c->R13}, {"r14", c->R14}, {"r15", c->R15} };
    for (int i = 0; i < 12; i++) if (is_uobject((uint8_t *)regs[i].v)) describe_obj(regs[i].n, (uint8_t *)regs[i].v);
    uint64_t *sp = (uint64_t *)c->Rsp; int shown = 0; uint8_t *seen[40]; int ns = 0;
    for (int i = 0; i < 600 && shown < 30; i++) {
        uint64_t v = 0; if (!A->read_mem(sp + i, &v, 8)) break;
        uint8_t *p = (uint8_t *)v; if (!is_uobject(p)) continue;
        int dup = 0; for (int k = 0; k < ns; k++) if (seen[k] == p) dup = 1; if (dup) continue;
        if (ns < 40) seen[ns++] = p;
        char tag[32]; snprintf(tag, sizeof tag, "stack+0x%x", i * 8); describe_obj(tag, p); shown++;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

__declspec(dllexport) void mod_init(CoreAPI *a)
{
    A = a;
    if (!g_veh) g_veh = AddVectoredExceptionHandler(1, crash_context);
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
__declspec(dllexport) void mod_shutdown(CoreAPI *a) { (void)a; if (g_veh) { RemoveVectoredExceptionHandler(g_veh); g_veh = NULL; } }

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
/* readable memory is not enough: a dead object's memory stays mapped, and a function called on it faults INSIDE ProcessEvent, which can leave the game's
 * internal state (input handling included) damaged. A target must be a live UObject (its InternalIndex points back at it). */
static int live(const void *p)
{
    int idx = -1;
    if (!readable(p) || ((uintptr_t)p & 7) || !A->read_mem((const uint8_t *)p + 0xC, &idx, 4) || idx < 0 || idx >= A->num_objects()) return 0;
    return A->object_at(idx) == (uint8_t *)p;
}

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
    P("  purge <classSub> [max]         destroy actors whose class contains the text, that are not CDOs and have NO Owner (leftovers of failed spawns)");
}

static int al_seat(void);
static void run_command(char *line)
{
    char *tok[24]; int nt = 0;
    for (char *p = strtok(line, " \t\r"); p && nt < 24; p = strtok(NULL, " \t\r")) tok[nt++] = p;
    if (!nt) return;
    if (tok[0][0] == '@') {                                              /* "@0 <command>": run only in the copy of that seat (the command file is read by every copy) */
        if (atoi(tok[0] + 1) != al_seat()) return;
        for (int i = 1; i < nt; i++) tok[i - 1] = tok[i];
        if (--nt == 0) return;
    }
    const char *c = tok[0];
    P("> %s%s%s%s%s", tok[0], nt > 1 ? " " : "", nt > 1 ? tok[1] : "", nt > 2 ? " " : "", nt > 2 ? tok[2] : "");

    if (!strcmp(c, "help")) { cmd_help(); return; }
    if (!strcmp(c, "echo")) { char b[300] = ""; for (int i = 1; i < nt; i++) { strcat(b, tok[i]); strcat(b, " "); } P("%s", b); return; }
    if (!strcmp(c, "fps")) { P("fps %.1f, worst frame %.0f ms, window %s (last 2 s)", F.fps, F.worst_ms, F.fg ? "ACTIVE" : "background"); return; }
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
    if (!strcmp(c, "nativefn") && nt >= 2) {
        /* which game functions (UFunctions with a native body) start just below the given exe offsets? e.g. nativefn 0x52b478f 0x363dfff */
        uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
        IMAGE_NT_HEADERS *nth = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
        size_t image = nth->OptionalHeader.SizeOfImage;
        uint8_t *ref = A->find_object("Function", "K2_GetActorLocation", "Actor");
        int foff = -1;
        for (int o = 0x80; ref && o < 0x140 && foff < 0; o += 8) { uint8_t *v = NULL; A->read_mem(ref + o, &v, 8); if (v >= base + 0x1000 && v < base + 0x6000000 && (size_t)(v - base) < image) foff = o; }
        if (foff < 0) { P("could not find the Func offset in UFunction"); return; }
        P("UFunction::Func at +0x%x", foff);
        int n = A->num_objects(); char nm[160], cn[96], on[160];
        for (int t = 1; t < nt; t++) {
            uint64_t rva = strtoull(tok[t], NULL, 0); uint8_t *target = base + rva; uint8_t *best = NULL; uint8_t *bestf = NULL;
            for (int i = 0; i < n; i++) {
                uint8_t *o = A->object_at(i); if (!o) continue;
                A->obj_name(A->obj_class(o), cn, sizeof cn); if (strcmp(cn, "Function") != 0) continue;
                uint8_t *f = NULL; A->read_mem(o + foff, &f, 8);
                if (f && f <= target && target - f < 0x4000 && (!bestf || f > bestf)) { bestf = f; best = o; }
            }
            if (!best) { P("  0x%llx: no UFunction native within 16 KB below", (unsigned long long)rva); continue; }
            uint8_t *outer = NULL; A->read_mem(best + 0x20, &outer, 8); A->obj_name(best, nm, sizeof nm); A->obj_name(outer, on, sizeof on);
            P("  0x%llx: %s::%s  (native starts at +0x%llx, %lld bytes before)", (unsigned long long)rva, on, nm, (unsigned long long)(bestf - base), (long long)(target - bestf));
        }
        return;
    }
    if (!strcmp(c, "npchp") && nt >= 3) {                              /* npchp <npc ptr> <health>: set the Health attribute (base and current) of an NPC; finds its AttributeSet_Health by outer */
        uint8_t *npc = ptr_of(tok[1]); float val = (float)atof(tok[2]); int n = A->num_objects(), done = 0; char cn[96];
        if (!live(npc)) { P("%s is not a live game object", tok[1]); return; }
        uint8_t *pst = NULL; { int ops = A->find_prop_offset(A->obj_class(npc), "PlayerState"); if (ops >= 0) A->read_mem(npc + ops, &pst, 8); }   /* an NPC's attribute sets belong to its PlayerState */
        for (int i = 0; i < n; i++) {
            uint8_t *o = A->object_at(i); if (!o) continue;
            A->obj_name(A->obj_class(o), cn, sizeof cn); if (strcmp(cn, "AttributeSet_Health") != 0) continue;
            uint8_t *outer = NULL; A->read_mem(o + 0x20, &outer, 8); if (!outer) continue;
            uint8_t *outer2 = NULL; A->read_mem(outer + 0x20, &outer2, 8);
            if (outer != npc && outer2 != npc && !(pst && outer == pst)) continue;
            float before = 0; A->read_mem(o + 64 + 12, &before, 4);
            A->write_mem(o + 64 + 8, &val, 4); A->write_mem(o + 64 + 12, &val, 4);
            P("health set %p: %.1f -> %.1f", o, before, val); done++;
        }
        if (!done) P("no AttributeSet_Health belongs to that NPC");
        return;
    }
    if (!strcmp(c, "tp") && nt >= 5) {                                  /* tp <ptr> x y z: move an actor (teleport) */
        uint8_t *o = ptr_of(tok[1]); if (!live(o)) { P("%s is not a live game object", tok[1]); return; }
        double v[3] = { atof(tok[2]), atof(tok[3]), atof(tok[4]) }; uint8_t sw = 0, tpf = 1;
        const char *nl[3] = { "NewLocation", "bSweep", "bTeleport" }; const void *vl[3] = { v, &sw, &tpf }; int sl[3] = { 24, 1, 1 };
        P("tp ran: %d", A->call_fn(o, "Actor", "K2_SetActorLocation", 3, nl, vl, sl, NULL, 0)); return;
    }
    if (!strcmp(c, "tpto") && nt >= 3) {                                 /* tpto <ptr> <target ptr> [dx dy dz]: teleport an actor next to another one */
        uint8_t *o = ptr_of(tok[1]), *t = ptr_of(tok[2]); if (!live(o) || !live(t)) { P("not live objects"); return; }
        double l[3] = { 0, 0, 0 }; if (!A->call_fn(t, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, l, 24)) { P("no location"); return; }
        double v[3] = { l[0] + (nt > 3 ? atof(tok[3]) : 150), l[1] + (nt > 4 ? atof(tok[4]) : 0), l[2] + (nt > 5 ? atof(tok[5]) : 20) }; uint8_t sw = 0, tpf = 1;
        const char *nl[3] = { "NewLocation", "bSweep", "bTeleport" }; const void *vl[3] = { v, &sw, &tpf }; int sl[3] = { 24, 1, 1 };
        P("tpto ran: %d (target at %.0f %.0f %.0f)", A->call_fn(o, "Actor", "K2_SetActorLocation", 3, nl, vl, sl, NULL, 0), l[0], l[1], l[2]); return;
    }
    if (!strcmp(c, "lookat") && nt >= 2) {                               /* lookat <target ptr>: turn the hero's camera (control rotation) towards an actor */
        uint8_t *t = ptr_of(tok[1]); if (!A->hero || !live(t)) { P("not live objects"); return; }
        double hl[3] = { 0 }, tl[3] = { 0 };
        A->call_fn(A->hero, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, hl, 24); A->call_fn(t, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, tl, 24);
        double dx = tl[0] - hl[0], dy = tl[1] - hl[1], dz = tl[2] - hl[2], hz = sqrt(dx * dx + dy * dy);
        double rot[3] = { -atan2(dz, hz) * 57.29578 - 5.0, atan2(dy, dx) * 57.29578 + (nt > 2 ? atof(tok[2]) : 0), 0 };
        if (nt > 3) rot[0] = atof(tok[3]);                                   /* optional pitch override (e.g. -55 = looking steeply down) */      /* pitch, yaw, roll */
        int oc = A->find_prop_offset(A->obj_class(A->hero), "Controller"); uint8_t *ctl = NULL; if (oc >= 0) A->read_mem(A->hero + oc, &ctl, 8);
        if (!ctl) { P("no controller"); return; }
        const char *n[1] = { "NewRotation" }; const void *v[1] = { rot }; int sz[1] = { 24 };
        P("lookat ran: %d (yaw %.0f pitch %.0f)", A->call_fn(ctl, "Controller", "SetControlRotation", 1, n, v, sz, NULL, 0), rot[1], rot[0]); return;
    }
    if (!strcmp(c, "inject") && nt >= 2) {                               /* inject <InputActionName> [value]: press an Enhanced Input action as the local player (e.g. IA_Ability_Action_EquipWeaponAuto, IA_Combat_Attack_Top) */
        if (!A->hero) { P("no hero"); return; }
        uint8_t *act = NULL; int n = A->num_objects(); char cn[96], nm[160];
        for (int i = 0; i < n && !act; i++) { uint8_t *o = A->object_at(i); if (!o) continue; A->obj_name(A->obj_class(o), cn, sizeof cn); if (strcmp(cn, "InputAction") != 0) continue; A->obj_name(o, nm, sizeof nm); if (!strcmp(nm, tok[1])) act = o; }
        if (!act) { P("input action %s not found", tok[1]); return; }
        int oc = A->find_prop_offset(A->obj_class(A->hero), "Controller"); uint8_t *ctl = NULL; if (oc >= 0) A->read_mem(A->hero + oc, &ctl, 8);
        uint8_t *scls = A->find_object("Class", "EnhancedInputLocalPlayerSubsystem", NULL), *lib = A->find_object(NULL, "Default__SubsystemBlueprintLibrary", NULL), *sub = NULL;
        if (!ctl || !scls || !lib) { P("no controller / subsystem class / library"); return; }
        { const char *n2[2] = { "ContextObject", "Class" }; const void *v2[2] = { &ctl, &scls }; int s2[2] = { 8, 8 }; A->call_fn(lib, "SubsystemBlueprintLibrary", "GetLocalPlayerSubsystem", 2, n2, v2, s2, &sub, 8); }
        if (!sub) { P("no Enhanced Input subsystem"); return; }
        double raw[4] = { nt > 2 ? atof(tok[2]) : 1.0, 0, 0, 0 }; uint8_t empty[16] = { 0 };
        const char *n3[4] = { "Action", "RawValue", "Modifiers", "Triggers" }; const void *v3[4] = { &act, raw, empty, empty }; int s3[4] = { 8, 32, 16, 16 };
        P("inject %s ran: %d", tok[1], A->call_fn(sub, "EnhancedInputSubsystemInterface", "InjectInputForAction", 4, n3, v3, s3, NULL, 0)); return;
    }
    if (!strcmp(c, "attached") && nt >= 2) {                            /* attached <npc ptr>: components hanging on its mesh (and on a bone-driven poseable child) with the owner actor's world position */
        uint8_t *npc = ptr_of(tok[1]); if (!live(npc)) { P("not a live object"); return; }
        double nl[3] = { 0 }; A->call_fn(npc, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, nl, 24); P("npc at %.0f %.0f %.0f", nl[0], nl[1], nl[2]);
        int om = A->find_prop_offset(A->obj_class(npc), "Mesh"); uint8_t *mesh = NULL; if (om >= 0) A->read_mem(npc + om, &mesh, 8);
        uint8_t *lists[2] = { mesh, NULL };
        for (int pass = 0; pass < 2 && lists[pass]; pass++) {
            int oat = A->find_prop_offset(A->obj_class(lists[pass]), "AttachChildren"); uint8_t *kids = NULL; int nk = 0;
            if (oat >= 0) { A->read_mem(lists[pass] + oat, &kids, 8); A->read_mem(lists[pass] + oat + 8, &nk, 4); }
            if (nk < 0 || nk > 60) nk = 0;
            for (int i = 0; i < nk; i++) {
                uint8_t *comp = NULL, *o = NULL; A->read_mem(kids + (size_t)i * 8, &comp, 8); if (!comp) continue; A->read_mem(comp + 0x20, &o, 8);
                char cc[96] = "", oc[96] = "", sock[96] = "-"; A->obj_name(A->obj_class(comp), cc, sizeof cc); if (o) A->obj_name(A->obj_class(o), oc, sizeof oc);
                int os = A->find_prop_offset(A->obj_class(comp), "AttachSocketName"); if (os >= 0) A->fname_to_str(comp + os, sock, sizeof sock);
                double ol[3] = { 0 }; if (o && o != npc) A->call_fn(o, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, ol, 24);
                double d = sqrt((ol[0]-nl[0])*(ol[0]-nl[0]) + (ol[1]-nl[1])*(ol[1]-nl[1]) + (ol[2]-nl[2])*(ol[2]-nl[2]));
                P("  %s child: %s of %s, socket %s, dist to npc %.0f  (z %.0f)", pass ? "poseable" : "mesh", cc, oc, sock, d, ol[2] - nl[2]);
                if (!pass && strstr(cc, "Poseable") && !lists[1]) lists[1] = comp;
            }
        }
        return;
    }
    if (!strcmp(c, "npcinfo") && nt >= 2) {                              /* npcinfo <name part>: position, yaw, defeated flag, health, speed of every NPC whose name contains the text (numbers for automated comparison between the two copies) */
        int n = A->num_objects(), shown = 0; char nm[160], cn[96], chain[40];
        for (int i = 0; i < n && shown < 12; i++) {
            uint8_t *o = A->object_at(i); if (!o) continue;
            uint8_t *cls = A->obj_class(o); if (!cls) continue;
            int is = 0; uint8_t *k = cls; for (int d = 0; k && d < 14; d++) { A->obj_name(k, cn, sizeof cn); if (!strcmp(cn, "AIAgentCharacter")) { is = 1; break; } uint8_t *sup = NULL; A->read_mem(k + 0x40, &sup, 8); k = sup; }
            if (!is) continue;
            A->obj_name(o, nm, sizeof nm); if (!strncmp(nm, "Default__", 9) || !strstr(nm, tok[1])) continue;
            double lp[3] = { 0 }, rot[3] = { 0 }; uint8_t df = 0; A->call_fn(o, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, lp, 24); A->call_fn(o, "Actor", "K2_GetActorRotation", 0, NULL, NULL, NULL, rot, 24); A->call_fn(o, "GothicCharacter", "IsDefeated", 0, NULL, NULL, NULL, &df, 1);
            float hp = -1; { int ops = A->find_prop_offset(A->obj_class(o), "PlayerState"); uint8_t *pst = NULL; if (ops >= 0) A->read_mem(o + ops, &pst, 8);
                if (pst) for (int j = 0; j < n; j++) { uint8_t *q = A->object_at(j); if (!q) continue; A->obj_name(A->obj_class(q), chain, sizeof chain); if (strcmp(chain, "AttributeSet_Health")) continue; uint8_t *ou = NULL; A->read_mem(q + 0x20, &ou, 8); if (ou == pst) { A->read_mem(q + 64 + 12, &hp, 4); break; } } }
            int ocm = A->find_prop_offset(A->obj_class(o), "Controller"); uint8_t *ctl = NULL; if (ocm >= 0) A->read_mem(o + ocm, &ctl, 8);
            P("NPCINFO %s pos %.0f %.0f %.0f yaw %.0f defeated %d health %.0f controller %d", nm, lp[0], lp[1], lp[2], rot[1], df & 1, hp, ctl != NULL); shown++;
        }
        if (!shown) P("NPCINFO none matching %s", tok[1]);
        return;
    }
    if (!strcmp(c, "gedamage") && nt >= 3) {                              /* gedamage <npc ptr> <GameplayEffect class name> [level]: the hero's ability system applies that effect to the NPC's (what a weapon hit does) */
        uint8_t *npc = ptr_of(tok[1]); if (!live(npc) || !A->hero) { P("not live objects"); return; }
        char cdoname[200]; snprintf(cdoname, sizeof cdoname, "Default__%s", tok[2]);
        uint8_t *cdo = A->find_object(NULL, cdoname, NULL); uint8_t *ge = cdo ? A->obj_class(cdo) : A->find_object("Class", tok[2], NULL);
        if (!ge) { P("effect class %s not found", tok[2]); return; }
        uint8_t *owners[2][2] = { { A->hero, NULL }, { npc, NULL } }; uint8_t *asc[2] = { NULL, NULL }; char cn[96];
        for (int w = 0; w < 2; w++) { int ops = A->find_prop_offset(A->obj_class(owners[w][0]), "PlayerState"); if (ops >= 0) A->read_mem(owners[w][0] + ops, &owners[w][1], 8); }
        int n = A->num_objects();
        for (int i = 0; i < n; i++) {
            uint8_t *o = A->object_at(i); if (!o) continue; A->obj_name(A->obj_class(o), cn, sizeof cn); if (!strstr(cn, "AbilitySystemComponent")) continue;
            uint8_t *ou = NULL; A->read_mem(o + 0x20, &ou, 8); if (!ou) continue;
            for (int w = 0; w < 2; w++) if (ou == owners[w][0] || (owners[w][1] && ou == owners[w][1])) asc[w] = o;
        }
        P("hero ability system %p, npc ability system %p, effect %p", asc[0], asc[1], ge);
        if (!asc[0] || !asc[1]) return;
        float lvl = nt > 3 ? (float)atof(tok[3]) : 1.0f; uint8_t ctx[24] = { 0 }; uint8_t ret[8] = { 0 };
        const char *nn[4] = { "GameplayEffectClass", "Target", "Level", "Context" }; const void *vv[4] = { &ge, &asc[1], &lvl, ctx }; int ss[4] = { 8, 8, 4, 24 };
        int ok = A->call_fn(asc[0], "AbilitySystemComponent", "BP_ApplyGameplayEffectToTarget", 4, nn, vv, ss, ret, 8);
        P("gedamage ran: %d, handle %02x%02x%02x%02x", ok, ret[0], ret[1], ret[2], ret[3]); return;
    }
    if (!strcmp(c, "ioscan")) {                                          /* ioscan [classSub] [max]: live actors that have a persistent id (items lying around, chests, doors ...): count per class, and the first few with id and position */
        const char *sub = nt > 1 && strcmp(tok[1], "*") ? tok[1] : NULL; int max = nt > 2 ? atoi(tok[2]) : 12, shown = 0;
        struct { uint8_t *cls; int n; char name[80]; } cl[200]; int ncl = 0; static uint8_t *no[256]; static int nno;       /* classes known to have no id */
        int total = 0, n = A->num_objects(); char nm[160];
        for (int i = 0; i < n; i++) {
            uint8_t *o = A->object_at(i); if (!o) continue; uint8_t *k = A->obj_class(o); int skip = 0;
            for (int j = 0; j < nno; j++) if (no[j] == k) { skip = 1; break; }
            if (skip) continue;
            int oid = A->find_prop_offset(k, "m_PersistentId");
            if (oid < 0) { if (nno < 256) no[nno++] = k; continue; }
            { uint32_t fl = 0; A->read_mem(o + 8, &fl, 4); if (fl & 0x8000) continue; }          /* destroyed, waiting for the garbage collector */
            A->obj_name(o, nm, sizeof nm); if (!strncmp(nm, "Default__", 9)) continue;
            char cn[96]; A->obj_name(k, cn, sizeof cn); if (sub && !ci_has(cn, sub)) continue;
            total++;
            int f = -1; for (int j = 0; j < ncl; j++) if (cl[j].cls == k) { f = j; break; }
            if (f < 0 && ncl < 200) { f = ncl++; cl[f].cls = k; cl[f].n = 0; snprintf(cl[f].name, sizeof cl[f].name, "%s", cn); }
            if (f >= 0) cl[f].n++;
            if (shown < max) {
                shown++; uint8_t id[16] = {0}; A->read_mem(o + oid, id, 16);
                double lp[3] = {0,0,0}; A->call_fn(o, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, lp, 24);
                P("  %p %s (%s) pos %.0f %.0f %.0f id %02x%02x%02x%02x-%02x%02x%02x%02x-%02x%02x%02x%02x-%02x%02x%02x%02x", o, nm, cn, lp[0], lp[1], lp[2], id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9], id[10], id[11], id[12], id[13], id[14], id[15]);
            }
        }
        for (int j = 0; j < ncl; j++) if (cl[j].n >= 2) P("  class %s x%d", cl[j].name, cl[j].n);
        P("%d actors with a persistent id", total);
        return;
    }
    if (!strcmp(c, "contscan")) {                                        /* contscan [max]: DataModule_Container objects (chests, bodies, characters) whose m_Inventory struct is not all zero: first 96 bytes and the owning actor */
        int max = nt > 1 ? atoi(tok[1]) : 6, shown = 0, total = 0, nonzero = 0, n = A->num_objects(); char nm[160];
        uint8_t *dc = A->find_object(NULL, "Default__DataModule_Container", NULL); uint8_t *kc = dc ? A->obj_class(dc) : NULL; if (!kc) { P("no DataModule_Container class"); return; }
        int oi = A->find_prop_offset(kc, "m_Inventory");
        for (int i = 0; i < n; i++) {
            uint8_t *o = A->object_at(i); if (!o || A->obj_class(o) != kc) continue;
            A->obj_name(o, nm, sizeof nm); if (!strncmp(nm, "Default__", 9)) continue;
            total++; uint8_t b[96]; if (!A->read_mem(o + oi, b, 96)) continue;
            int nz = 0; for (int k = 0; k < 96; k++) if (b[k]) { nz = 1; break; }
            if (!nz) continue; nonzero++;
            if (shown < max) { shown++; char h[300] = ""; for (int k = 0; k < 96; k++) { char t[4]; sprintf(t, "%02x", b[k]); strcat(h, t); if (k % 8 == 7) strcat(h, " "); }
                P("  %p %s: %s", o, nm, h); }
        }
        P("%d container modules, %d with a non-empty inventory struct", total, nonzero);
        return;
    }
    if (!strcmp(c, "emptychest") && nt >= 2) {                           /* emptychest <chest actor ptr>: removes everything from its container module (what taking all the loot does), test helper */
        uint8_t *ch = ptr_of(tok[1]); if (!live(ch)) { P("not a live object"); return; }
        uint8_t *dc = A->find_object(NULL, "Default__DataModule_Container", NULL); uint8_t *kc = dc ? A->obj_class(dc) : NULL;
        int oc = A->find_prop_offset(A->obj_class(ch), "m_DataModuleComponent"); uint8_t *comp = NULL; if (oc >= 0) A->read_mem(ch + oc, &comp, 8);
        int oa = comp ? A->find_prop_offset(A->obj_class(comp), "m_DataModules") : -1; uint8_t *arr = NULL; int num = 0;
        if (oa >= 0) { A->read_mem(comp + oa, &arr, 8); A->read_mem(comp + oa + 8, &num, 4); }
        for (int i = 0; arr && i < num && i < 32; i++) { uint8_t *m = NULL; A->read_mem(arr + (size_t)i * 8, &m, 8);
            if (m && A->obj_class(m) == kc) { int oi = A->find_prop_offset(kc, "m_Inventory"), n0 = 0, n1 = 0; A->read_mem(m + oi + 8, &n0, 4); A->read_mem(m + oi + 24, &n1, 4);
                P("container %p: %d + %d items before", m, n0, n1);
                if (nt > 2 && !strcmp(tok[2], "dry")) return;
                int ok = 0;                                                          /* the game's Server_Debug_EmptyInventory faults here: the item lists are given length 0 instead */
                A->read_mem(m + oi + 8, &n0, 4); A->read_mem(m + oi + 24, &n1, 4);
                if (n0 + n1 > 0) { int z = 0; A->write_mem(m + oi + 8, &z, 4); A->write_mem(m + oi + 24, &z, 4); A->read_mem(m + oi + 8, &n0, 4); A->read_mem(m + oi + 24, &n1, 4); P("function refused, item lists set to length 0"); }
                P("empty call %d, now %d + %d items", ok, n0, n1); return; } }
        P("no container module found");
        return;
    }
    if (!strcmp(c, "contdump") && nt >= 2) {                             /* contdump <chest actor ptr>: both item arrays of its container module, raw element bytes, and what the first pointer in an element is */
        uint8_t *ch = ptr_of(tok[1]); if (!live(ch)) { P("not a live object"); return; }
        uint8_t *dc = A->find_object(NULL, "Default__DataModule_Container", NULL); uint8_t *kc = dc ? A->obj_class(dc) : NULL;
        int oc = A->find_prop_offset(A->obj_class(ch), "m_DataModuleComponent"); uint8_t *comp = NULL; if (oc >= 0) A->read_mem(ch + oc, &comp, 8);
        int oa = comp ? A->find_prop_offset(A->obj_class(comp), "m_DataModules") : -1; uint8_t *arr = NULL; int num = 0;
        if (oa >= 0) { A->read_mem(comp + oa, &arr, 8); A->read_mem(comp + oa + 8, &num, 4); }
        for (int i = 0; arr && i < num && i < 32; i++) { uint8_t *m = NULL; A->read_mem(arr + (size_t)i * 8, &m, 8);
            if (!m || A->obj_class(m) != kc) continue;
            int oi = A->find_prop_offset(kc, "m_Inventory"); P("container %p, m_Inventory @%d", m, oi);
            for (int a = 0; a < 2; a++) {
                uint8_t *data = NULL; int n = 0, mx = 0; A->read_mem(m + oi + a * 16, &data, 8); A->read_mem(m + oi + a * 16 + 8, &n, 4); A->read_mem(m + oi + a * 16 + 12, &mx, 4);
                P("  array %d: data %p num %d max %d", a, data, n, mx);
                for (int e = 0; data && e < n && e < 4; e++) {
                    uint8_t b[96]; if (!A->read_mem(data + e * 16, b, 96)) break;
                    char h[300] = ""; for (int k = 0; k < 96; k++) { char t[4]; sprintf(t, "%02x", b[k]); strcat(h, t); if (k % 8 == 7) strcat(h, " "); }
                    P("    elem %d @%d: %s", e, e * 16, h);
                    for (int q = 0; q < 2; q++) for (int mk = 0; mk < 3; mk++) { uint8_t *pp; memcpy(&pp, b + q * 8, 8); pp = (uint8_t *)((uintptr_t)pp & ~(mk == 0 ? 0 : mk == 1 ? 1 : 15)); if (mk == 0) pp = (uint8_t *)((uintptr_t)pp); if ((uintptr_t)pp > 0x10000 && A->read_mem(pp + 0xc, &num, 4)) { char nn[120] = "", cn[80] = ""; if (A->obj_alive(pp, NULL)) { A->obj_name(pp, nn, sizeof nn); A->obj_name(A->obj_class(pp), cn, sizeof cn); P("      qword %d mask %d -> %p %s (%s)", q, mk, pp, nn, cn); } } }
                }
            }
            return; }
        P("no container module found");
        return;
    }
    if (!strcmp(c, "peek") && nt >= 2) {                                 /* peek <addr> [bytes]: raw memory (guarded) as qwords, with the object name where a qword is a live game object */
        uint8_t *a0 = ptr_of(tok[1]); int nb = nt > 2 ? atoi(tok[2]) : 64; if (nb > 512) nb = 512;
        for (int off = 0; off < nb; off += 16) {
            uint64_t q[2] = {0, 0}; if (!A->read_mem(a0 + off, q, 16)) { P("  +%d unreadable", off); break; }
            char ex[200] = ""; for (int k = 0; k < 2; k++) { uint8_t *pp = (uint8_t *)q[k]; int32_t idx = 0; if ((uintptr_t)pp > 0x10000 && A->read_mem(pp + 0xc, &idx, 4) && idx > 0 && idx < A->num_objects() && A->object_at(idx) == pp) { char nn[100]; A->obj_name(pp, nn, sizeof nn); char t[120]; snprintf(t, sizeof t, " [q%d=%s]", k, nn); strcat(ex, t); } }
            P("  +%03d: %016llx %016llx%s", off, (unsigned long long)q[0], (unsigned long long)q[1], ex);
        }
        return;
    }
    if (!strcmp(c, "structinfo") && nt >= 2) {                           /* structinfo <ScriptStruct name> [max]: properties of a USTRUCT (offset, type, size) */
        uint8_t *k = A->find_object("ScriptStruct", tok[1], NULL); if (!k) { P("ScriptStruct %s not found", tok[1]); return; }
        struct MemCtx ctx = { NULL, NULL, 0, 0, nt > 2 ? atoi(tok[2]) : 60, 1 };
        A->list_members(k, member_cb, &ctx); P("%d props", ctx.props); return;
    }
    if (!strcmp(c, "fninfo") && nt >= 3) {                               /* fninfo <Outer> <Function>: the UFunction, its flags, where its native body is (exe RVA; the same RVA for many functions = script / blueprint thunk) and its parameters */
        uint8_t *fn = A->find_object("Function", tok[2], tok[1]); if (!fn) { P("function %s::%s not found", tok[1], tok[2]); return; }
        uint8_t *base = (uint8_t *)GetModuleHandleA(NULL); IMAGE_NT_HEADERS *nth = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew); size_t image = nth->OptionalHeader.SizeOfImage;
        uint8_t *ref = A->find_object("Function", "K2_GetActorLocation", "Actor"); int foff = -1;
        for (int o = 0x80; ref && o < 0x140 && foff < 0; o += 8) { uint8_t *v = NULL; A->read_mem(ref + o, &v, 8); if (v >= base + 0x1000 && v < base + 0x6000000 && (size_t)(v - base) < image) foff = o; }
        uint8_t *f = NULL; if (foff >= 0) A->read_mem(fn + foff, &f, 8);
        uint32_t flags = 0; int ofl = A->find_prop_offset(A->obj_class(fn), "FunctionFlags"); (void)ofl;
        for (int o = 0x80; o < 0xb0; o += 4) { uint32_t v = 0; A->read_mem(fn + o, &v, 4); if (v & 0x400) { flags = v; P("  flags-like dword at +0x%x = 0x%x (0x400 = native)", o, v); break; } }
        char d[300] = ""; A->describe_fn(tok[1], tok[2], d, sizeof d);
        P("%s::%s ufunction %p, Func at +0x%x = %p (exe+0x%llx), params: %s", tok[1], tok[2], fn, foff, f, (unsigned long long)(f ? f - base : 0), d);
        (void)flags; return;
    }
    if (!strcmp(c, "miscan") && nt >= 2) {                               /* miscan <npc ptr>: where does the NPC's AnimInstance keep its list of active montage instances? (TArray of pointers to FAnimMontageInstance, whose Montage field points to a UAnimMontage) */
        uint8_t *npc = ptr_of(tok[1]); if (!live(npc)) { P("not a live object"); return; }
        int om = A->find_prop_offset(A->obj_class(npc), "Mesh"); uint8_t *mesh = NULL; if (om >= 0) A->read_mem(npc + om, &mesh, 8);
        uint8_t *ai = NULL; if (mesh) { int oa = A->find_prop_offset(A->obj_class(mesh), "AnimScriptInstance"); if (oa >= 0) A->read_mem(mesh + oa, &ai, 8); }
        if (!ai) { P("no anim instance"); return; }
        uint8_t *cur = NULL; A->call_fn(ai, "AnimInstance", "GetCurrentActiveMontage", 0, NULL, NULL, NULL, &cur, 8);
        char cn[120] = "none"; if (cur) A->obj_name(cur, cn, sizeof cn); P("AnimInstance %p, current active montage %p %s", ai, cur, cn);
        for (int off = 0x40; off < 0xA00; off += 8) {
            uint8_t *data = NULL; int32_t num = 0, mx = 0; A->read_mem(ai + off, &data, 8); A->read_mem(ai + off + 8, &num, 4); A->read_mem(ai + off + 12, &mx, 4);
            if (!data || num < 1 || num > 12 || mx < num || mx > 64 || (uintptr_t)data < 0x10000) continue;
            for (int e = 0; e < num; e++) {
                uint8_t *inst = NULL; if (!A->read_mem(data + e * 8, &inst, 8) || (uintptr_t)inst < 0x10000) continue;
                for (int f = 0; f < 0x40; f += 8) {
                    uint8_t *mo = NULL; if (!A->read_mem(inst + f, &mo, 8) || (uintptr_t)mo < 0x10000) continue;
                    int32_t idx = 0; if (!A->read_mem(mo + 0xc, &idx, 4) || idx <= 0 || idx >= A->num_objects() || A->object_at(idx) != mo) continue;
                    char mn[120], mc[60]; A->obj_name(mo, mn, sizeof mn); A->obj_name(A->obj_class(mo), mc, sizeof mc);
                    if (strstr(mc, "Montage")) P("  array @+0x%x elem %d/%d inst %p field +0x%x -> %s (%s)", off, e, num, inst, f, mn, mc);
                }
            }
        }
        P("scan done"); return;
    }
    if (!strcmp(c, "chestlist")) {                                       /* chestlist [max] [minItems]: chest actors (class name contains Chest, with a container module) in ONE call: pointer, name, the two list lengths, position */
        int max = nt > 1 ? atoi(tok[1]) : 60, minit = nt > 2 ? atoi(tok[2]) : 0, shown = 0, n = A->num_objects(); char nm[160], cn[96];
        uint8_t *dc = A->find_object(NULL, "Default__DataModule_Container", NULL); uint8_t *kc = dc ? A->obj_class(dc) : NULL; if (!kc) { P("no container class"); return; }
        int oi = A->find_prop_offset(kc, "m_Inventory");
        for (int i = 0; i < n && shown < max; i++) {
            uint8_t *o = A->object_at(i); if (!o) continue; uint8_t *k = A->obj_class(o);
            int oc = A->find_prop_offset(k, "m_DataModuleComponent"); if (oc < 0 || A->find_prop_offset(k, "m_PersistentId") < 0) continue;
            A->obj_name(k, cn, sizeof cn); { char l[96]; strncpy(l, cn, sizeof l - 1); l[95] = 0; for (char *q = l; *q; q++) if (*q >= 'A' && *q <= 'Z') *q += 32; if (!strstr(l, "chest")) continue; }
            A->obj_name(o, nm, sizeof nm); if (!strncmp(nm, "Default__", 9) || A->read_bool(o, "bActorIsBeingDestroyed") == 1) continue;
            uint8_t *comp = NULL; A->read_mem(o + oc, &comp, 8); int oa = comp ? A->find_prop_offset(A->obj_class(comp), "m_DataModules") : -1; uint8_t *arr = NULL; int num = 0;
            if (oa >= 0) { A->read_mem(comp + oa, &arr, 8); A->read_mem(comp + oa + 8, &num, 4); }
            for (int e = 0; arr && e < num && e < 32; e++) { uint8_t *m = NULL; A->read_mem(arr + (size_t)e * 8, &m, 8); if (!m || A->obj_class(m) != kc) continue;
                int n0 = 0, n1 = 0; A->read_mem(m + oi + 8, &n0, 4); A->read_mem(m + oi + 24, &n1, 4); if (n0 + n1 < minit) break;
                double lp[3] = {0,0,0}; A->call_fn(o, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, lp, 24);
                P("CHEST %p %s items %d + %d pos %.0f %.0f %.0f", o, nm, n0, n1, lp[0], lp[1], lp[2]); shown++; break; }
        }
        P("%d chests listed", shown); return;
    }
    if (!strcmp(c, "heroinfo")) {                                        /* heroinfo: the hero's position (to put it back after a test) */
        double l[3] = { 0 }; if (!A->hero || !A->call_fn(A->hero, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, l, 24)) { P("HEROINFO none"); return; }
        P("HEROINFO pos %.0f %.0f %.0f", l[0], l[1], l[2]); return;
    }
    if (!strcmp(c, "npcs")) {                                           /* characters in the world with their class chain and distance to the hero */
        double rad = nt > 1 ? atof(tok[1]) : 3000, hp[3] = {0,0,0}; int max = nt > 2 ? atoi(tok[2]) : 20, shown = 0, total = 0;
        if (A->hero) A->call_fn(A->hero, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, hp, 24);
        int n = A->num_objects(); char nm[160], cn[96], chain[300];
        for (int i = 0; i < n; i++) {
            uint8_t *o = A->object_at(i); if (!o || o == A->hero || o == A->puppet) continue;
            uint8_t *cls = A->obj_class(o); if (!cls) continue;
            int is = 0; chain[0] = 0; uint8_t *k = cls;
            for (int d = 0; k && d < 14; d++) { A->obj_name(k, cn, sizeof cn); if (d < 6) { strcat(chain, cn); strcat(chain, ">"); } if (!strcmp(cn, "Character")) { is = 1; break; } uint8_t *sup = NULL; A->read_mem(k + 0x40, &sup, 8); k = sup; }
            if (!is) continue;
            A->obj_name(o, nm, sizeof nm); if (!strncmp(nm, "Default__", 9)) continue;
            if (A->read_bool(o, "bActorIsBeingDestroyed") == 1) continue;
            double lp[3] = {0,0,0}; if (!A->call_fn(o, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, lp, 24)) continue;
            double d2 = sqrt((lp[0]-hp[0])*(lp[0]-hp[0]) + (lp[1]-hp[1])*(lp[1]-hp[1]) + (lp[2]-hp[2])*(lp[2]-hp[2]));
            total++; if (d2 > rad || shown >= max) continue;
            shown++; P("  %p %s dist %.0f  %s", o, nm, d2, chain);
        }
        P("%d characters in total, %d shown (radius %.0f)", total, shown, rad); return;
    }
    if (!strcmp(c, "npcanim")) {                                        /* npcanim [radius] [max]: per NPC (same names in both copies): active montage clip, slot, carried item class, speed: to compare what the two copies show */
        double rad = nt > 1 ? atof(tok[1]) : 2500, hp[3] = {0,0,0}; int max = nt > 2 ? atoi(tok[2]) : 40, shown = 0;
        if (A->hero) A->call_fn(A->hero, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, hp, 24);
        int n = A->num_objects(); char nm[160], cn[96];
        uint8_t *gc = A->find_object(NULL, "Default__GothicCharacter", NULL); uint8_t *gcls = gc ? A->obj_class(gc) : NULL;
        for (int i = 0; i < n && shown < max; i++) {
            uint8_t *o = A->object_at(i); if (!o || o == A->hero || o == A->puppet) continue;
            uint8_t *cls = A->obj_class(o); int is = 0; uint8_t *k = cls;
            for (int d = 0; k && d < 14; d++) { if (k == gcls) { is = 1; break; } uint8_t *sup = NULL; A->read_mem(k + 0x40, &sup, 8); k = sup; }
            if (!is) continue;
            A->obj_name(o, nm, sizeof nm); if (!strncmp(nm, "Default__", 9) || A->read_bool(o, "bActorIsBeingDestroyed") == 1) continue;
            double lp[3] = {0,0,0}; if (!A->call_fn(o, "Actor", "K2_GetActorLocation", 0, NULL, NULL, NULL, lp, 24)) continue;
            double dd = sqrt((lp[0]-hp[0])*(lp[0]-hp[0]) + (lp[1]-hp[1])*(lp[1]-hp[1])); if (dd > rad) continue;
            char mont[160] = "-", slot[60] = "-", carry[96] = "-";
            int om = A->find_prop_offset(cls, "Mesh"); uint8_t *mesh = NULL; if (om >= 0) A->read_mem(o + om, &mesh, 8);
            uint8_t *ai = NULL; if (mesh) { int oa = A->find_prop_offset(A->obj_class(mesh), "AnimScriptInstance"); if (oa >= 0) A->read_mem(mesh + oa, &ai, 8); }
            uint8_t *mo = NULL; if (ai) A->call_fn(ai, "AnimInstance", "GetCurrentActiveMontage", 0, NULL, NULL, NULL, &mo, 8);
            if (mo) { A->obj_name(mo, mont, sizeof mont);
                int ost = A->find_prop_offset(A->obj_class(mo), "SlotAnimTracks"); uint8_t *tr = NULL; int32_t nt2 = 0; if (ost >= 0) { A->read_mem(mo + ost, &tr, 8); A->read_mem(mo + ost + 8, &nt2, 4); }
                if (tr && nt2 > 0) { A->fname_to_str(tr, slot, sizeof slot); uint8_t *segs = NULL; int32_t ns = 0; A->read_mem(tr + 8, &segs, 8); A->read_mem(tr + 16, &ns, 4);
                    if (segs && ns > 0) { uint8_t *ref = NULL; A->read_mem(segs, &ref, 8); if (ref && A->obj_alive(ref, NULL)) { char sq[100]; A->obj_name(ref, sq, sizeof sq); strcat(mont, " seq="); strcat(mont, sq); } } } }
            if (ai) { uint8_t *data = NULL; int32_t num = 0; A->read_mem(ai + 0x88, &data, 8); A->read_mem(ai + 0x90, &num, 4); mont[0] = 0; strcpy(slot, "-");
                for (int e = 0; data && num > 0 && num < 12 && e < num; e++) { uint8_t *inst = NULL, *mo2 = NULL; int32_t ix = 0; if (!A->read_mem(data + e * 8, &inst, 8) || !inst || !A->read_mem(inst, &mo2, 8) || !mo2) continue;
                    if (!A->read_mem(mo2 + 0xc, &ix, 4) || ix <= 0 || ix >= A->num_objects() || A->object_at(ix) != mo2) continue;
                    char mn[100]; A->obj_name(mo2, mn, sizeof mn); if (!strncmp(mn, "AnimMontage_", 12)) continue; if (mont[0]) strcat(mont, "+"); strncat(mont, mn, 60); }
                if (!mont[0]) strcpy(mont, "-"); }
            int oc = A->find_prop_offset(cls, "m_CarryComponent"); uint8_t *cc = NULL; if (oc >= 0) A->read_mem(o + oc, &cc, 8);
            if (cc) { int oe = A->find_prop_offset(A->obj_class(cc), "m_EquippedItem"); uint8_t *eq = NULL; if (oe >= 0) A->read_mem(cc + oe, &eq, 8); if (eq) A->obj_name(A->obj_class(eq), carry, sizeof carry); }
            shown++; P("NPCANIM %s dist %.0f montage=%s slot=%s carry=%s", nm, dd, mont, slot, carry);
        }
        P("%d NPCs shown", shown); return;
    }
    if (!strcmp(c, "purge") && nt >= 2) {
        int max = nt > 2 ? atoi(tok[2]) : 1000, done = 0, skipped = 0, hidden = 0;
        int n = A->num_objects(); char nm[160], cn[96];
        for (int i = 0; i < n && done + hidden < max; i++) {
            uint8_t *o = A->object_at(i); if (!o) continue;
            A->obj_name(A->obj_class(o), cn, sizeof cn);
            if (!ci_has(cn, tok[1])) continue;
            A->obj_name(o, nm, sizeof nm);
            if (!strncmp(nm, "Default__", 9)) continue;
            struct InfoCtx ic = { "Owner", "", 0, 0, 0 };
            A->list_members(A->obj_class(o), info_cb, &ic);
            if (!ic.found) { skipped++; continue; }                     /* not an actor */
            uint8_t *owner = NULL; A->read_mem(o + ic.off, &owner, 8);
            if (owner) { skipped++; continue; }                         /* has an owner: a real one (e.g. the hero's own bow) */
            if (A->read_bool(o, "bActorIsBeingDestroyed") == 1) {        /* stuck half-destroyed actor: still rendered. Hide it, switch its collision off and destroy the root component */
                uint8_t hid = 1, off = 0, prop = 1; uint8_t *root = NULL;
                int orr = A->find_prop_offset(A->obj_class(o), "RootComponent"); if (orr >= 0) A->read_mem(o + orr, &root, 8);
                const char *n1[1] = { "bNewHidden" }; const void *v1[1] = { &hid }; int s1[1] = { 1 };
                A->call_fn(o, "Actor", "SetActorHiddenInGame", 1, n1, v1, s1, NULL, 0);
                const char *n2[1] = { "bNewActorEnableCollision" }; const void *v2[1] = { &off }; int s2[1] = { 1 };
                A->call_fn(o, "Actor", "SetActorEnableCollision", 1, n2, v2, s2, NULL, 0);
                if (root && live(root)) {
                    const char *n3[2] = { "bNewVisibility", "bPropagateToChildren" }; const void *v3[2] = { &off, &prop }; int s3[2] = { 1, 1 };
                    A->call_fn(root, "SceneComponent", "SetVisibility", 2, n3, v3, s3, NULL, 0);
                    const char *n4[1] = { "Object" }; const void *v4[1] = { &root }; int s4[1] = { 8 };
                    A->call_fn(root, "ActorComponent", "K2_DestroyComponent", 1, n4, v4, s4, NULL, 0);
                }
                hidden++; continue;
            }
            const char *nn[1] = { NULL };
            A->call_fn(o, "Actor", "K2_DestroyActor", 0, nn, NULL, NULL, NULL, 0);
            done++;
        }
        P("destroyed %d, hid stuck half-destroyed %d, kept %d (with an owner)", done, hidden, skipped);
        return;
    }
    if (!strcmp(c, "findfn") && nt >= 2) {                           /* findfn <nameSub> [outerSub] [max]: UFunctions whose name contains the text, with their class */
        const char *osub = nt > 2 && strcmp(tok[2], "*") ? tok[2] : NULL; int max = nt > 3 ? atoi(tok[3]) : 40, shown = 0, total = 0;
        int n = A->num_objects(); char nm[200], cn[96], on[200];
        for (int i = 0; i < n; i++) {
            uint8_t *o = A->object_at(i); if (!o) continue;
            A->obj_name(A->obj_class(o), cn, sizeof cn); if (strcmp(cn, "Function") != 0) continue;
            A->obj_name(o, nm, sizeof nm); if (!ci_has(nm, tok[1])) continue;
            uint8_t *outer = NULL; A->read_mem(o + 0x20, &outer, 8); on[0] = 0; if (outer) A->obj_name(outer, on, sizeof on);
            if (!ci_has(on, osub)) continue;
            total++; if (shown < max) { shown++; P("  %s::%s", on, nm); }
        }
        P("%d matches, %d shown", total, shown);
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
        if (!live(o)) { P("%s is not a live game object (null, dead or stale)", tok[1]); return; }
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
        if (!live(o)) { P("%s is not a live game object (null, dead or stale)", tok[1]); return; }
        struct InfoCtx ic = { tok[2], "", 0, 0, 0 };
        A->list_members(A->obj_class(o), info_cb, &ic);
        if (!ic.found) { P("no property %s", tok[2]); return; }
        char val[400]; fmt_value(o, tok[2], ic.type, ic.off, ic.size, val, sizeof val);
        P("%s [%s @%d/%d] = %s", tok[2], ic.type, ic.off, ic.size, val);
        return;
    }
    if (!strcmp(c, "set") && nt >= 4) {
        uint8_t *o = ptr_of(tok[1]);
        if (!live(o)) { P("%s is not a live game object (null, dead or stale)", tok[1]); return; }
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
        if (!live(o)) { P("%s is not a live game object (null, dead or stale)", tok[1]); return; }
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

/* ---- autoload: press "Continue" in the main menu by itself, so a restarted copy is back in its save without a click.
 * On when build\autoload.txt exists. Optional lines in it:  "gap=25"  seconds added per Couchlink seat (the second copy waits longer, two loads at once
 * would fill the GPU); "wait=6"  seconds the menu must have been up. The game's own MainMenuWidget::OnContinueClicked is called (it loads the newest save). ---- */
static struct { uint8_t *menu; unsigned long seen_t, last_try, scan_t; int tries; int reported_disabled; unsigned long hero_t; int keys; } AL;
static int al_seat(void) { static int s = -2; if (s == -2) { const char *c = strstr(GetCommandLineA(), "-CoopSeat="); s = c ? atoi(c + 10) : 0; } return s; }
static ULONGLONG proc_start_ft(void) { FILETIME c, e, k, u; if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0; return ((ULONGLONG)c.dwHighDateTime << 32) | c.dwLowDateTime; }
static double proc_age_s(void) { FILETIME now; GetSystemTimeAsFileTime(&now); ULONGLONG n = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime; ULONGLONG s = proc_start_ft(); return s ? (double)(n - s) / 1e7 : 0; }
/* the previous seat has finished loading when its marker file is newer than this process; the markers are written below when a hero exists */
static int al_prev_seat_done(void)
{
    int seat = al_seat(); if (seat <= 0) return 1;
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\autoload_done_%d.txt", g_dir, seat - 1);
    WIN32_FILE_ATTRIBUTE_DATA fa; if (!GetFileAttributesExA(p, GetFileExInfoStandard, &fa)) return 0;
    ULONGLONG m = ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
    return m >= proc_start_ft();
}
static void al_mark_done(void)
{
    static int marked; if (marked) return; marked = 1;
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\autoload_done_%d.txt", g_dir, al_seat());
    FILE *f = fopen(p, "w"); if (f) { fputs("done\n", f); fclose(f); }
}
static BOOL CALLBACK al_find_wnd(HWND h, LPARAM l) { DWORD pid = 0; GetWindowThreadProcessId(h, &pid); if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) { *(HWND *)l = h; return FALSE; } return TRUE; }
/* after the save is loaded the game waits for "press any key" on its loading screen: post a key nobody uses (F13) to the game window, which works without focus */
static void al_press_any_key(void)
{
    HWND w = NULL; EnumWindows(al_find_wnd, (LPARAM)&w); if (!w) return;
    PostMessageA(w, WM_KEYDOWN, VK_F13, 0x00010001); PostMessageA(w, WM_KEYUP, VK_F13, 0xC0010001);
    if (!AL.keys++) A->log("PR autoload: sending a key to pass the 'press any key' loading screen");
}
static void autoload_tick(void)
{
    static unsigned long t; unsigned long now = GetTickCount();
    if (now - t < 1000) return; t = now;
    char path[MAX_PATH]; snprintf(path, sizeof path, "%s\\autoload.txt", g_dir);
    FILE *f = fopen(path, "r"); if (!f) return;
    int gap = 60; double wait = 2; char line[100];                                 /* gap: how long a later seat waits for the previous one at most */
    while (fgets(line, sizeof line, f)) { if (!strncmp(line, "gap=", 4)) gap = atoi(line + 4); else if (!strncmp(line, "wait=", 5)) wait = atof(line + 5); }
    fclose(f);
    if (A->hero) {                                                           /* already in a save (and tell the next seat) */
        if (!AL.hero_t) AL.hero_t = now;
        if (AL.tries && now - AL.hero_t < 20000) al_press_any_key();         /* we loaded it ourselves: the loading screen still waits for a key */
        AL.seen_t = 0; al_mark_done(); return;
    }
    if (AL.tries && AL.tries < 99 && now - AL.last_try > 4000 && now - AL.last_try < 150000) al_press_any_key();   /* the loading screen can show before the hero exists */
    if (AL.menu && !live(AL.menu)) AL.menu = NULL;
    if (!AL.menu) {
        if (now - AL.scan_t < 2000) return; AL.scan_t = now;
        int n = A->num_objects(); char nm[160], cn[96];
        for (int i = 0; i < n && !AL.menu; i++) {
            uint8_t *o = A->object_at(i); if (!o) continue;
            A->obj_name(A->obj_class(o), cn, sizeof cn); if (strcmp(cn, "W_MainMenu_C") != 0) continue;
            A->obj_name(o, nm, sizeof nm); if (strncmp(nm, "Default__", 9) != 0) AL.menu = o;
        }
        if (!AL.menu) { AL.seen_t = 0; return; }
    }
    if (!AL.seen_t) { AL.seen_t = now; A->log("PR autoload: main menu widget %p found %.1f s after the process started; waits %.1f s%s", AL.menu, proc_age_s(), wait, al_seat() > 0 ? " and for the previous seat to finish loading" : ""); }
    if (now - AL.seen_t < (unsigned long)(wait * 1000)) return;
    if (al_seat() > 0 && !strstr(GetCommandLineA(), "-NoLoadGap") && !al_prev_seat_done() && now - AL.seen_t < (unsigned long)((wait + gap) * 1000)) return;   /* one load at a time (the GPU memory); give up waiting after gap seconds */
    int act = 0; A->call_fn(AL.menu, "CommonActivatableWidget", "IsActivated", 0, NULL, NULL, NULL, &act, 1);
    if (!(act & 255)) return;                                                /* the menu must be the active page (not the intro / disclaimer); the bitfield itself reads as garbage */
    int en = 0; A->call_fn(AL.menu, "MainMenuWidget", "IsContinueEnabled", 0, NULL, NULL, NULL, &en, 1);
    if (!(en & 255)) { if (!AL.reported_disabled) { AL.reported_disabled = 1; A->log("PR autoload: 'Continue' is not enabled (no save to continue?)"); } return; }
    if (AL.tries >= 3 || (AL.tries && now - AL.last_try < 90000)) return;    /* retry only if the hero did not appear within 90 s */
    A->call_fn(AL.menu, "MainMenuWidget", "OnContinueClicked", 0, NULL, NULL, NULL, NULL, 0);
    AL.tries++; AL.last_try = now; A->log("PR autoload: pressed 'Continue' (try %d) %.1f s after the process started", AL.tries, proc_age_s());
}

#if defined(__has_include)
#if __has_include("local_god.inc")
#include "local_god.inc"
#define HAVE_LOCAL_GOD 1
#endif
#endif


/* ---- performance for two copies on one GPU ----
 *  - build\perf_cmds.txt: console commands (one per line, e.g. "r.ScreenPercentage 80") run once per world, as soon as the hero exists */
static int console_cmd(const char *text)
{
    uint8_t *o = A->find_object(NULL, "Default__KismetSystemLibrary", NULL); uint8_t *world = find_world();
    if (!o || !world) return 0;
    static wchar_t wbuf[400]; MultiByteToWideChar(CP_UTF8, 0, text, -1, wbuf, 400);
    static struct { wchar_t *data; int num, max; } fs; fs.data = wbuf; fs.num = fs.max = (int)wcslen(wbuf) + 1;
    uint8_t nullp[8] = { 0 };
    const char *names[3] = { "WorldContextObject", "Command", "SpecificPlayer" }; const void *vals[3] = { &world, &fs, nullp }; int sizes[3] = { 8, 16, 8 };
    return A->call_fn(o, "KismetSystemLibrary", "ExecuteConsoleCommand", 3, names, vals, sizes, NULL, 0);
}
static void perf_tick(void)
{
    static unsigned long t; static uint8_t *last_hero;
    unsigned long now = GetTickCount();
    if (now - t < 1000) return; t = now;
    if (!A->hero) { last_hero = NULL; return; }
    if (A->hero != last_hero) {                                              /* a new world: run the profile once */
        last_hero = A->hero;
        char path[MAX_PATH]; snprintf(path, sizeof path, "%s\\perf_cmds.txt", g_dir);
        FILE *f = fopen(path, "r");
        if (f) { char line[300]; int n = 0; while (fgets(line, sizeof line, f)) { char *e = line + strlen(line); while (e > line && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0; if (!line[0] || line[0] == '#') continue; console_cmd(line); n++; } fclose(f); A->log("PR perf: ran %d console commands from perf_cmds.txt", n); }
    }
}

/* frame statistics of this game process: engine ticks per second and the worst frame, with a flag whether its window is the foreground one */
static void frame_stats(void)
{
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    if (!F.freq.QuadPart) { QueryPerformanceFrequency(&F.freq); F.last = t; return; }
    double dt = (double)(t.QuadPart - F.last.QuadPart) / (double)F.freq.QuadPart; F.last = t;
    F.acc += dt; F.frames++; if (dt > F.worst) F.worst = dt;
    if (F.acc >= 2.0) {
        F.fps = F.frames / F.acc; F.worst_ms = F.worst * 1000.0;
        DWORD fpid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &fpid); F.fg = fpid == GetCurrentProcessId();
        F.acc = 0; F.frames = 0; F.worst = 0;
        unsigned long now = GetTickCount();
        if (now - F.log_t > 10000) { F.log_t = now; A->log("PR fps %.1f worst frame %.0f ms, window %s", F.fps, F.worst_ms, F.fg ? "ACTIVE" : "background"); }
    }
}

__declspec(dllexport) void mod_update(CoreAPI *a)
{
    A = a;
    frame_stats();
    __try { perf_tick(); } __except (EXCEPTION_EXECUTE_HANDLER) { }
#ifdef HAVE_LOCAL_GOD
    __try { local_god_tick(); } __except (EXCEPTION_EXECUTE_HANDLER) { }
#endif
    __try { autoload_tick(); } __except (EXCEPTION_EXECUTE_HANDLER) { a->log("PR autoload: exception 0x%lx, skipped", GetExceptionCode()); }
    unsigned long now = GetTickCount();
    if (now - g_last_poll < 250) return;
    g_last_poll = now;
    poll();
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) { (void)h; (void)r; (void)x; return TRUE; }
