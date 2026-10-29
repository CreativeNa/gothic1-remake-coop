        if (T.have_remote && T.marker && T.puppet_char == 2 && T.skcomp && T.playanim.fn) {
            unsigned long now = GetTickCount();
            int acting = strcmp(T.ract, "-") != 0;
            int airborne = T.rmm == 3;
            if (acting) T.jump_phase = 0;

            /* ---- jump: In clip -> loop while rising/falling -> landing clip. A fall without upward speed (ledge) only loops. ---- */
            if (!acting) {
                int gait = T.speed < 25 ? 0 : (T.speed < 300 ? 1 : (T.speed < 560 ? 2 : 3));
                const char *in_clip[4]   = {"A_Man_N_In_Place_Jump_In", "A_Man_N_Walk_Jump_In", "A_Man_N_Run_Jump_In", "A_Man_N_Sprint_Jump_In"};
                const char *land_clip[4] = {"A_Man_N_In_Place_Jump_Landing", "A_Man_N_Walk_Jump_Landing", "A_Man_N_Run_Jump_Landing", "A_Man_N_Sprint_Jump_Landing"};
                if (T.jump_phase == 0 && airborne && T.vz > 120) {
                    play_named(in_clip[gait], 0); T.jump_phase = 1; T.jump_t = now; T.anim_state = 5;
                    logf_("J jump start vz=%.0f speed=%.0f -> %s", T.vz, T.speed, in_clip[gait]);
                } else if (T.jump_phase == 0 && airborne && now - T.fall_since > 300) {
                    play_named("A_Man_N_Run_Jump_Loop", 1); T.jump_phase = 2; T.jump_t = now; T.anim_state = 5;
                    logf_("J fall (no jump) -> loop");
                } else if (T.jump_phase == 1 && airborne && now - T.jump_t > 450) {
                    play_named("A_Man_N_Run_Jump_Loop", 1); T.jump_phase = 2; T.jump_t = now;
                } else if ((T.jump_phase == 1 || T.jump_phase == 2) && !airborne) {
                    play_named(land_clip[gait], 0); T.jump_phase = 3; T.jump_t = now;
                    logf_("J land -> %s", land_clip[gait]);
                } else if (T.jump_phase == 3 && now - T.jump_t > 600) {
                    T.jump_phase = 0; T.anim_state = -1;
                }
            }

            if (acting) {
                if (strcmp(T.ract, T.cur_act) != 0) {     /* a one-shot action (attack, dodge, eat, draw ...) */
                    uint8_t *seq = clip_by_name(T.ract, T.rpkg);
                    if (seq) {
                        play_seq(seq, 0);
                        logf_("A play action '%s'", T.ract);
                    }
                    if (strstr(T.ract, "S1H_Tr_Draw")) { T.weapon_drawn = 1; logf_("S weapon drawn"); }
                    if (strstr(T.ract, "S1H_Tr_Shelve")) { T.weapon_drawn = 0; logf_("S weapon sheathed"); }
                    snprintf(T.cur_act, sizeof T.cur_act, "%s", T.ract);
                }
            } else if (T.cur_act[0]) { T.cur_act[0] = 0; T.anim_state = -1; }

            if (!acting && T.jump_phase == 0) {
                /* locomotion from the remote speed (u/s); switch only after 6 consecutive frames agree */
                int want = T.speed < 25 ? 0 : (T.speed < 300 ? 1 : (T.speed < 560 ? 2 : 3));
                if (want == T.cand_state) T.cand_n++; else { T.cand_state = want; T.cand_n = 1; }
                if (T.cand_n >= 6 && want != T.anim_state) {
                    if (want == 0 && T.weapon_drawn) {
                        /* sword in hand and standing: hold the last pose of the draw clip */
                        uint8_t *dseq = clip_by_name("A_Man_S1H_Tr_Draw", "/Game/AnimLibrary/Humanoid/Man/Items/Sword1H/Draw/Trained/A_Man_S1H_Tr_Draw");
                        if (dseq) { freeze_at_end(dseq); logf_("S standing with weapon: hold draw pose"); T.anim_state = 0; }
                    } else {
                        if (T.anim[want] && !obj_alive(T.anim[want], T.anim_names[want])) { T.anim[want] = find_anim(T.anim_names[want]); root_obj(T.anim[want]); }
                        int eff = want;
                        while (eff > 0 && !T.anim[eff]) eff--;        /* missing sprint/run clip: use the next slower one */
                        if (T.anim[eff]) {
                            play_seq(T.anim[eff], 1);
                            logf_("N6 speed %.0f u/s -> state %d (clip %s)", T.speed, want, T.anim_names[eff]);
                            T.anim_state = want;
                        }
                    }
                }
            }
        }
