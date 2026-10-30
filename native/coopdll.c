/*
 * coopdll step N1: proof of life inside G1R-Win64-Shipping.exe.
 * Finds GUObjectArray via a fixed RVA (valid only for the exact exe build below), walks all UObjects,
 * finds the hero (PlayerCharacterBP_C instance) and prints its RootComponent->RelativeLocation.
 * Compare the printed position with what the Lua probe reads. Every memory read is guarded with __try.
 * UNTESTED. Log: coopdll.log next to this dll.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <ctype.h>
#include <malloc.h>
#include <string.h>

/* RVAs taken from UE4SS.log of this exact build (exe size 171704320) */
#define EXPECTED_EXE_SIZE 171704320u
#define RVA_GUOBJECTARRAY 0x9B9D440u
#define RVA_FNAME_TOSTRING 0x11CE2A0u

typedef struct { wchar_t *Data; int32_t Num; int32_t Max; } FString;
typedef void (*FNameToString_t)(const void *name, FString *out);

static FILE *g_log;
static uint8_t *g_base;
static FNameToString_t g_tostring;
static uint8_t *g_objs_chunks;      /* FUObjectItem** */
static uint8_t *g_arr;
static int32_t g_num_elements;

static void logf_(const char *fmt, ...)
{
    if (!g_log) return;
    fprintf(g_log, "[%lu] ", GetCurrentProcessId());
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

static int safe_read(const void *src, void *dst, size_t n)
{
    __try { memcpy(dst, src, n); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static uint64_t rd64(const void *p) { uint64_t v = 0; safe_read(p, &v, 8); return v; }
static int32_t rd32(const void *p) { int32_t v = 0; safe_read(p, &v, 4); return v; }

/* FName (8 bytes at name_ptr) -> utf8 */
static int fname_str(const void *name_ptr, char *out, int cap)
{
    FString s = {0};
    out[0] = 0;
    __try { g_tostring(name_ptr, &s); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!s.Data || s.Num <= 0 || s.Num > 1024) return 0;
    WideCharToMultiByte(CP_UTF8, 0, s.Data, -1, out, cap, NULL, NULL);
    return 1;
}

static uint8_t *get_object(int32_t i)
{
    int32_t chunk = i / 65536, idx = i % 65536;
    uint8_t *chunkp = (uint8_t *)rd64(g_objs_chunks + (size_t)chunk * 8);
    if (!chunkp) return NULL;
    return (uint8_t *)rd64(chunkp + (size_t)idx * 24);
}
static void obj_name(uint8_t *o, char *out, int cap) { out[0] = 0; if (o) fname_str(o + 0x18, out, cap); }
static uint8_t *obj_class(uint8_t *o) { return (uint8_t *)rd64(o + 0x10); }

/* FField layout differs between engine versions: detect where Next and NamePrivate live. */
static int g_off_next = 0x20, g_off_name = 0x28, g_off_offint = 0x4C;

static int plausible_ident(const char *s)
{
    if (!s[0]) return 0;
    for (const char *c = s; *c; c++) {
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) return 0;
    }
    return 1;
}

static void detect_ffield_layout(uint8_t *actorClass)
{
    uint8_t *prop = (uint8_t *)rd64(actorClass + 0x50);
    uint8_t dump[0x60];
    if (!prop || !safe_read(prop, dump, sizeof dump)) { logf_("detect: no first property"); return; }
    char hex[0x60 * 3 + 1];
    for (int i = 0; i < 0x60; i++) sprintf(hex + i * 3, "%02x ", dump[i]);
    logf_("first Actor property @%p: %s", prop, hex);
    int nexts[] = {0x18, 0x20, 0x28}, names[] = {0x20, 0x28, 0x30};
    char nm[256];
    for (int ni = 0; ni < 3; ni++) for (int mi = 0; mi < 3; mi++) {
        int no = nexts[ni], mo = names[mi];
        if (no == mo) continue;
        int good = 0; uint8_t *pr = prop;
        for (int k = 0; k < 8 && pr; k++) {
            fname_str(pr + mo, nm, sizeof nm);
            if (plausible_ident(nm)) good++;
            pr = (uint8_t *)rd64(pr + no);
        }
        logf_("  try next=0x%x name=0x%x -> %d plausible names", no, mo, good);
        if (good >= 6) { g_off_next = no; g_off_name = mo; g_off_offint = mo + 0x24; logf_("  using next=0x%x name=0x%x Offset_Internal=+0x%x", no, mo, g_off_offint); return; }
    }
    logf_("detect: no layout matched");
}

/* walk UStruct chain for a property by name; returns Offset_Internal or -1 */
/* Property lookups walk property lists and call FName::ToString (which allocates) for every property: far too slow to repeat every frame.
 * Results are cached per (struct, name). */
static struct { uint8_t *st; char name[48]; int off; uint8_t *prop; } g_pcache[4096];
static uint8_t *g_last_prop;
static unsigned pc_hash(uint8_t *st, const char *name)
{
    unsigned h = (unsigned)(((uintptr_t)st >> 4) * 2654435761u);
    for (const char *c = name; *c; c++) h = h * 31u + (unsigned char)*c;
    return h & 4095u;
}

static int find_prop_offset_v(uint8_t *ustruct, const char *want, int verbose)
{
    char nm[256], sn[256];
    uint8_t *origin = ustruct;
    unsigned h = pc_hash(origin, want);
    if (!verbose) {
        for (int k = 0; k < 8; k++) {
            unsigned i = (h + (unsigned)k) & 4095u;
            if (g_pcache[i].st == origin && strcmp(g_pcache[i].name, want) == 0) { g_last_prop = g_pcache[i].prop; return g_pcache[i].off; }
        }
    }
    int result = -1; uint8_t *found = NULL;
    for (int depth = 0; ustruct && depth < 16 && !found; depth++) {
        uint8_t *prop = (uint8_t *)rd64(ustruct + 0x50);      /* ChildProperties */
        if (verbose) obj_name(ustruct, sn, sizeof sn);
        for (int guard = 0; prop && guard < 4096; guard++) {
            fname_str(prop + g_off_name, nm, sizeof nm);
            if (verbose && guard < 3) logf_("  %s prop[%d] '%s' off=%d", sn, guard, nm, rd32(prop + g_off_offint));
            if (strcmp(nm, want) == 0) { result = rd32(prop + g_off_offint); found = prop; break; }
            prop = (uint8_t *)rd64(prop + g_off_next);
        }
        if (!found) ustruct = (uint8_t *)rd64(ustruct + 0x40);            /* SuperStruct */
    }
    g_last_prop = found;
    if (!verbose && origin && strlen(want) < 48) {
        for (int k = 0; k < 8; k++) {
            unsigned i = (h + (unsigned)k) & 4095u;
            if (!g_pcache[i].st) { g_pcache[i].st = origin; strcpy(g_pcache[i].name, want); g_pcache[i].off = result; g_pcache[i].prop = found; break; }
        }
    }
    return result;
}static int find_prop_offset(uint8_t *ustruct, const char *want) { return find_prop_offset_v(ustruct, want, 0); }


/* ---- N2a: where is GameEngine::Tick in the vtable? ---- */
#define RVA_GAMEENGINE_TICK_SCAN 0x379AD70u   /* from UE4SS.log scan address 0x7ff69a6aad70 - base */
static void analyze_gameengine(void)
{
    char nm[256];
    uint8_t *eng = NULL;
    for (int32_t i = 0; i < g_num_elements && !eng; i++) {
        uint8_t *o = get_object(i);
        if (!o) continue;
        obj_name(o, nm, sizeof nm);
        if (strncmp(nm, "GameEngine_", 11) == 0) eng = o;
    }
    if (!eng) { logf_("GameEngine instance not found"); return; }
    uint8_t *vt = (uint8_t *)rd64(eng);
    logf_("GameEngine %p vtable %p (rva 0x%llx)", eng, vt, (unsigned long long)(vt - g_base));
    uint8_t *want = g_base + RVA_GAMEENGINE_TICK_SCAN;
    for (int i = 0; i < 160; i++) {
        uint8_t *fn = (uint8_t *)rd64(vt + (size_t)i * 8);
        if (fn == want) logf_("  slot %d == scan address of Tick", i);
    }
    for (int i = 0; i < 160; i += 8) {
        char line[200]; line[0] = 0;
        for (int k = 0; k < 8; k++) { uint8_t *fn = (uint8_t *)rd64(vt + (size_t)(i + k) * 8); sprintf(line + strlen(line), "%05llx ", (unsigned long long)(fn - g_base) & 0xFFFFFFFull); }
        logf_("  vt[%3d..]: %s", i, line);
    }
    uint8_t b[32]; safe_read(want, b, 32);
    char hex[100]; for (int i = 0; i < 32; i++) sprintf(hex + i * 3, "%02x ", b[i]);
    logf_("bytes at Tick scan addr: %s", hex);
    safe_read(g_base + 0x13A2920, b, 32);
    for (int i = 0; i < 32; i++) sprintf(hex + i * 3, "%02x ", b[i]);
    logf_("bytes at ProcessEvent:   %s", hex);
}

static char g_dir[MAX_PATH];
#include "scan.inc"
#include "n2.inc"



static DWORD WINAPI worker(LPVOID arg)
{
    (void)arg;
    char path[MAX_PATH];
    HMODULE self;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&worker, &self);
    GetModuleFileNameA(self, path, MAX_PATH);
    char *slash = strrchr(path, '\\'); if (slash) *slash = 0;
    strcpy(g_dir, path);
    strcat(path, "\\coopdll.log");
    g_log = fopen(path, "a");
    logf_("==== coopdll N1 start, pid %lu", GetCurrentProcessId());

    g_base = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew);
    logf_("base %p, SizeOfImage %lu (expected-file-size check is on the exe: image size is not the file size)", g_base, nt->OptionalHeader.SizeOfImage);
    char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExA(exe, GetFileExInfoStandard, &fa) && fa.nFileSizeLow != EXPECTED_EXE_SIZE) {
        logf_("ABORT: exe size %lu != %u, RVAs do not apply", fa.nFileSizeLow, EXPECTED_EXE_SIZE);
        return 1;
    }
    g_tostring = (FNameToString_t)(g_base + RVA_FNAME_TOSTRING);

    uint8_t *arr = g_base + RVA_GUOBJECTARRAY; g_arr = arr;
    uint8_t hex[0x40]; safe_read(arr, hex, sizeof hex);
    char line[0x40 * 3 + 1]; for (int i = 0; i < 0x40; i++) sprintf(line + i * 3, "%02x ", hex[i]);
    logf_("GUObjectArray @%p: %s", arr, line);

    /* find FChunkedFixedUObjectArray inside it: {Objects**, PreAllocated*, Max, Num, MaxChunks, NumChunks} */
    int found = 0;
    for (int off = 0; off <= 0x30; off += 8) {
        uint8_t *chunks = (uint8_t *)rd64(arr + off);
        int32_t max = rd32(arr + off + 0x10), num = rd32(arr + off + 0x14);
        int32_t maxch = rd32(arr + off + 0x18), numch = rd32(arr + off + 0x1C);
        if (chunks && max > 1000 && num > 1000 && num <= max && numch >= 1 && numch <= maxch && rd64(chunks)) {
            g_objs_chunks = chunks; g_num_elements = num; found = 1;
            logf_("ObjObjects at +0x%x: Num=%d Max=%d chunks=%d/%d", off, num, max, numch, maxch);
            break;
        }
    }
    if (!found) { logf_("could not locate ObjObjects"); return 1; }

    /* The tick hook only needs the GameEngine object, which exists from the start. The hero is looked up on the game thread
     * (frame_work step 0) once a save is loaded, so modules (probe, auto-load) already work in the main menu. */
    char nm[256];
    {   /* property layout (FField offsets) is detected on the Actor class; it needs no hero */
        uint8_t *actor = NULL;
        for (int32_t i = 0; i < g_num_elements && !actor; i++) {
            uint8_t *o = get_object(i);
            if (!o) continue;
            obj_name(o, nm, sizeof nm);
            if (strcmp(nm, "Actor") != 0) continue;
            char cn[64]; obj_name(obj_class(o), cn, sizeof cn);
            if (strcmp(cn, "Class") == 0) actor = o;
        }
        if (!actor) { logf_("Actor class not found"); return 1; }
        detect_ffield_layout(actor);
    }
    analyze_gameengine();
    {   char sp[MAX_PATH]; snprintf(sp, sizeof sp, "%s\\scan.txt", g_dir); if (GetFileAttributesA(sp) != INVALID_FILE_ATTRIBUTES) { scan_anims(); return 0; } }
    install_tick_hook();
    logf_("==== N1 done, the hero is resolved on the game thread when a save is loaded");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(NULL, 0, worker, NULL, 0, NULL); }
    return TRUE;
}
