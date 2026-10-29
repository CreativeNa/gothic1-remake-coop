/* ---- animation scan: which AnimSequences exist, and for which skeleton? (runs only when scan.txt exists next to the dll) ---- */
static int ci_contains(const char *h, const char *n)
{
    size_t hl = strlen(h), nl = strlen(n);
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t k = 0;
        while (k < nl && tolower((unsigned char)h[i + k]) == tolower((unsigned char)n[k])) k++;
        if (k == nl) return 1;
    }
    return 0;
}

static void scan_anims(void)
{
    char nm[256], sk[256], cn[128];
    int total = 0, shown = 0;
    const char *keys[] = {"S1H", "_STL_", "Sneak", "Crouch", "Jump_In", "Landing", "_Land"};
    uint8_t *heroClass = NULL;
    for (int32_t i = 0; i < g_num_elements && !heroClass; i++) {
        uint8_t *o = get_object(i); if (!o) continue;
        obj_name(o, nm, sizeof nm);
        if (strcmp(nm, "PlayerCharacterBP_C") == 0) heroClass = o;
    }
    if (heroClass) {
        for (int32_t j = 0; j < g_num_elements; j++) {
            uint8_t *x = get_object(j);
            if (!x || obj_class(x) != heroClass) continue;
            obj_name(x, nm, sizeof nm);
            if (strncmp(nm, "Default__", 9) == 0) continue;
            uint8_t *mesh = (uint8_t *)rd64(x + 792);
            int oa = find_prop_offset(obj_class(mesh), "SkinnedAsset");
            uint8_t *asset = (uint8_t *)rd64(mesh + oa);
            int osk = find_prop_offset(obj_class(asset), "Skeleton");
            uint8_t *skel = osk >= 0 ? (uint8_t *)rd64(asset + osk) : NULL;
            if (skel) obj_name(skel, sk, sizeof sk); else strcpy(sk, "?");
            logf_("SCAN hero mesh skeleton: %s (%p)", sk, skel);
            int oai = find_prop_offset(obj_class(mesh), "AnimClass");
            uint8_t *ac = oai >= 0 ? (uint8_t *)rd64(mesh + oai) : NULL;
            if (ac) { obj_name(ac, nm, sizeof nm); logf_("SCAN hero AnimClass: %s", nm); }
            break;
        }
    }
    for (int32_t i = 0; i < g_num_elements; i++) {
        uint8_t *o = get_object(i);
        if (!o) continue;
        obj_name(obj_class(o), cn, sizeof cn);
        if (strcmp(cn, "AnimSequence") != 0) continue;
        total++;
        obj_name(o, nm, sizeof nm);
        int match = 0;
        for (int k = 0; k < 7; k++) if (ci_contains(nm, keys[k])) match = 1;
        if (match) {
            const char *need[] = {"idle", "walk", "run", "move", "strafe", "stance", "ready", "fight", "jump", "land", "loop", "crouch", "sneak", "block"};
            int okn = 0;
            for (int k = 0; k < 14; k++) if (ci_contains(nm, need[k])) okn = 1;
            if (!okn || ci_contains(nm, "IdleBreaker") || ci_contains(nm, "Training") || ci_contains(nm, "StartTurns") || ci_contains(nm, "Plants") || ci_contains(nm, "StartStop") || ci_contains(nm, "Stop_")) match = 0;
        }
        if (!match || shown >= 500) continue;
        int osk = find_prop_offset(obj_class(o), "Skeleton");
        uint8_t *skel = osk >= 0 ? (uint8_t *)rd64(o + osk) : NULL;
        if (skel) obj_name(skel, sk, sizeof sk); else strcpy(sk, "?");
        logf_("SCAN anim %p %s  skeleton=%s", o, nm, sk);
        shown++;
    }
    logf_("SCAN done: %d AnimSequences loaded, %d listed", total, shown);
}
