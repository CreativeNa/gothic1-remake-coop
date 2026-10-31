/*
 * menu module: the "Co-op" button of the main menu.
 *  - finds the main menu widget (W_MainMenu_C), creates one more button of the same class (W_MainMenuButton2_C) right after "Load game", gives it a label in the language of the game
 *    (all 10 languages the game ships: english german french spanish schinese italian japanese polish brazilian russian) and watches whether it is pressed
 *  - build\menu_lang.txt (one language key, e.g. "russian") forces a language for tests; build\menu_off.txt switches the module off.
 * In the main menu the core does not poll for new module builds: restart the game to load a new build there.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "coreapi.h"

#define BUILD "C:/Program Files (x86)/Steam/steamapps/common/Gothic1R-coop/native/build/"

static const struct { const char *key, *cult; const wchar_t *label; } LANG[] = {
    { "english",   "en", L"Co-op" },
    { "german",    "de", L"Koop" },
    { "french",    "fr", L"Coop" },
    { "spanish",   "es", L"Cooperativo" },
    { "schinese",  "zh", L"\u5408\u4f5c\u6a21\u5f0f" },
    { "italian",   "it", L"Cooperativa" },
    { "japanese",  "ja", L"\u5354\u529b\u30d7\u30ec\u30a4" },
    { "polish",    "pl", L"Kooperacja" },
    { "brazilian", "pt", L"Cooperativo" },
    { "russian",   "ru", L"\u041a\u043e\u043e\u043f\u0435\u0440\u0430\u0442\u0438\u0432" },
};

static struct {
    uint8_t *menu, *box, *btn, *label; uint32_t menu_fn[2];
    unsigned long last_find, last_tick; int created, tries, was_pressed, lang; char cur_lang[16];
    int clicks;
} M;

__declspec(dllexport) void mod_init(CoreAPI *a) { (void)a; memset(&M, 0, sizeof M); M.lang = -1; }

static int same_obj(CoreAPI *a, uint8_t *o, const uint32_t *fn)
{
    if (!o || !a->obj_alive(o, NULL)) return 0;
    uint32_t f[2]; if (!a->read_mem(o + 0x18, f, 8)) return 0; return f[0] == fn[0] && f[1] == fn[1];
}
static uint8_t *ptr_prop(CoreAPI *a, uint8_t *o, const char *name)
{
    if (!o) return NULL; int off = a->find_prop_offset(a->obj_class(o), name); uint8_t *p = NULL; if (off >= 0) a->read_mem(o + off, &p, 8); return p;
}

/* the language of the game: build\menu_lang.txt overrides it; otherwise the current language of the engine ("ru", "en-US", "zh-Hans", "pt-BR", ...) */
static int language_index(CoreAPI *a)
{
    char key[40] = "";
    FILE *f = fopen(BUILD "menu_lang.txt", "r"); if (f) { if (fgets(key, sizeof key, f)) { char *e = strpbrk(key, "\r\n "); if (e) *e = 0; } fclose(f); }
    if (key[0]) { for (int i = 0; i < 10; i++) if (!strcmp(key, LANG[i].key)) return i; }
    uint8_t *cdo = a->find_object(NULL, "Default__KismetInternationalizationLibrary", NULL);
    uint8_t ret[16] = { 0 };
    if (cdo && a->call_fn(cdo, "KismetInternationalizationLibrary", "GetCurrentLanguage", 0, NULL, NULL, NULL, ret, 16)) {
        wchar_t *w = NULL; int32_t n = 0; memcpy(&w, ret, 8); memcpy(&n, ret + 8, 4);
        char c[16] = ""; if (w && n > 1 && n < 16) { uint8_t tmp[64]; if (a->read_mem(w, tmp, (size_t)n * 2)) { for (int i = 0; i < n && i < 15; i++) c[i] = (char)tmp[i * 2]; c[15] = 0; } }
        snprintf(M.cur_lang, sizeof M.cur_lang, "%s", c);
        for (int i = 0; i < 10; i++) if (!strncmp(c, LANG[i].cult, 2)) return i;
    }
    return 0;
}

/* an FText from a wide string (KismetTextLibrary.Conv_StringToText); the FString is built here and stays ours */
static int make_text(CoreAPI *a, const wchar_t *w, uint8_t out[16])
{
    uint8_t *cdo = a->find_object(NULL, "Default__KismetTextLibrary", NULL); if (!cdo) return 0;
    int len = (int)wcslen(w) + 1; wchar_t *buf = (wchar_t *)calloc((size_t)len + 1, 2); memcpy(buf, w, (size_t)len * 2);
    uint8_t fs[16] = { 0 }; memcpy(fs, &buf, 8); int32_t n = len, mx = len; memcpy(fs + 8, &n, 4); memcpy(fs + 12, &mx, 4);
    const char *nn[1] = { "InString" }; const void *vv[1] = { fs }; int sz[1] = { 16 };
    return a->call_fn(cdo, "KismetTextLibrary", "Conv_StringToText", 1, nn, vv, sz, out, 16);
}

static void set_label(CoreAPI *a, const wchar_t *w)
{
    uint8_t ft[16] = { 0 }; if (!M.btn || !make_text(a, w, ft)) return;
    int ot = a->find_prop_offset(a->obj_class(M.btn), "Button Text"); if (ot >= 0) a->write_mem(M.btn + ot, ft, 16);         /* what PreConstruct copies into the label */
    if (M.label) { const char *nn[1] = { "InText" }; const void *vv[1] = { ft }; int sz[1] = { 16 }; a->call_fn(M.label, "TextBlock", "SetText", 1, nn, vv, sz, NULL, 0); }
}

static uint8_t *find_menu(CoreAPI *a)
{
    uint8_t *d = a->find_object(NULL, "Default__W_MainMenu_C", NULL); uint8_t *k = d ? a->obj_class(d) : NULL; if (!k) return NULL;
    int n = a->num_objects();
    for (int i = 0; i < n; i++) { uint8_t *o = a->object_at(i); if (o && o != d && a->obj_class(o) == k) return o; }
    return NULL;
}

static void create_button(CoreAPI *a)
{
    uint8_t *cont = ptr_prop(a, M.menu, "Continue"), *load = ptr_prop(a, M.menu, "LoadGame"); M.box = ptr_prop(a, M.menu, "VerticalBox_Menu");
    if (!cont || !load || !M.box) { a->log("MENU: menu widgets not complete yet (continue %p load %p box %p)", cont, load, M.box); return; }
    uint8_t *lib = a->find_object(NULL, "Default__WidgetBlueprintLibrary", NULL), *gs = a->find_object(NULL, "Default__GameplayStatics", NULL);
    if (!lib || !gs) { a->log("MENU: no widget library"); return; }
    uint8_t *pc = NULL;
    { int32_t idx = 0; uint8_t *ctx = M.menu; const char *nn[2] = { "WorldContextObject", "PlayerIndex" }; const void *vv[2] = { &ctx, &idx }; int sz[2] = { 8, 4 };
      a->call_fn(gs, "GameplayStatics", "GetPlayerController", 2, nn, vv, sz, &pc, 8); }
    uint8_t *cls = a->obj_class(cont), *nb = NULL;
    { uint8_t *ctx = M.menu; const char *nn[3] = { "WorldContextObject", "WidgetType", "OwningPlayer" }; const void *vv[3] = { &ctx, &cls, &pc }; int sz[3] = { 8, 8, 8 };
      a->call_fn(lib, "WidgetBlueprintLibrary", "Create", 3, nn, vv, sz, &nb, 8); }
    if (!nb) { a->log("MENU: Create returned no widget (player controller %p)", pc); return; }
    M.btn = nb; M.label = ptr_prop(a, nb, "Text_Label");
    /* the new button goes right after "Load game", in the panel that holds the menu buttons (the parent of "Load game"): the siblings that come after it are taken out and added again behind the
     * new button, in their old order */
    {
        uint8_t *par = NULL; a->call_fn(load, "Widget", "GetParent", 0, NULL, NULL, NULL, &par, 8); if (!par) par = M.box; M.box = par;
        int32_t count = 0; a->call_fn(par, "PanelWidget", "GetChildrenCount", 0, NULL, NULL, NULL, &count, 4);
        uint8_t *kids[32]; int nk = 0, load_at = -1;
        for (int32_t i = 0; i < count && nk < 32; i++) {
            uint8_t *c = NULL; const char *nn[1] = { "Index" }; const void *vv[1] = { &i }; int sz[1] = { 4 };
            if (a->call_fn(par, "PanelWidget", "GetChildAt", 1, nn, vv, sz, &c, 8) && c) { if (c == load) load_at = nk; kids[nk++] = c; }
        }
        const char *cn[1] = { "Content" }; int csz[1] = { 8 }; uint8_t *slot = NULL;
        { const void *v[1] = { &nb }; a->call_fn(par, "PanelWidget", "AddChild", 1, cn, v, csz, &slot, 8); }
        for (int k = load_at + 1; load_at >= 0 && k < nk; k++) {
            const void *v[1] = { &kids[k] }; uint8_t ok = 0; a->call_fn(par, "PanelWidget", "RemoveChild", 1, cn, v, csz, &ok, 1);
            a->call_fn(par, "PanelWidget", "AddChild", 1, cn, v, csz, &slot, 8);
        }
        char pn[80] = ""; a->obj_name(a->obj_class(par), pn, sizeof pn);
        a->log("MENU: parent %p (%s) holds %d widgets, 'Load game' is number %d, %d moved behind the new button", par, pn, nk, load_at, load_at >= 0 ? nk - load_at - 1 : 0);
    }
    M.lang = language_index(a);
    set_label(a, LANG[M.lang].label);
    M.created = 1; a->log("MENU: co-op button created (%p), language %s (engine language '%s'), label set", nb, LANG[M.lang].key, M.cur_lang);
}

__declspec(dllexport) void mod_update(CoreAPI *a)
{
    unsigned long now = GetTickCount();
    if (GetFileAttributesA(BUILD "menu_off.txt") != INVALID_FILE_ATTRIBUTES) return;
    if (M.menu && !same_obj(a, M.menu, M.menu_fn)) { M.menu = M.btn = M.label = M.box = NULL; M.created = 0; M.tries = 0; M.was_pressed = 0; }
    if (!M.menu) {
        if (now - M.last_find < 1500) return; M.last_find = now;
        M.menu = find_menu(a); if (!M.menu) return;
        a->read_mem(M.menu + 0x18, M.menu_fn, 8); a->log("MENU: main menu widget found (%p)", M.menu);
    }
    if (!M.created) {
        if (now - M.last_tick < 700 || M.tries > 12) return; M.last_tick = now; M.tries++;
        __try { create_button(a); } __except (EXCEPTION_EXECUTE_HANDLER) { a->log("MENU: exception while creating the button"); M.tries = 99; }
        return;
    }
    { static unsigned long t_lang; if (now - t_lang > 1000) { t_lang = now; int li = language_index(a); if (li != M.lang) { M.lang = li; set_label(a, LANG[li].label); a->log("MENU: label changed to %s", LANG[li].key); } } }      /* build\menu_lang.txt can change the language while the menu is open (tests) */
    /* a click: the button is pressed and then released */
    uint8_t pr = 0; if (M.btn && a->obj_alive(M.btn, NULL)) a->call_fn(M.btn, "ButtonWidget", "IsPressed", 0, NULL, NULL, NULL, &pr, 1);
    if (pr && !M.was_pressed) { M.was_pressed = 1; }
    else if (!pr && M.was_pressed) { M.was_pressed = 0; M.clicks++; a->log("MENU: the co-op button was clicked (#%d)", M.clicks); }
}
