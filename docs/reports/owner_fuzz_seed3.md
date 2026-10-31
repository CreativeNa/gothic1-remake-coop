# Ownership fuzz 2026-10-31 13:42 (20 rounds, seed 3)

7 / 8 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | setup: the camp NPCs and both hero positions are known | 2 NPCs |
| PASS | world: heroes were put back after falling through unloaded ground (test artefact, informational) | 17 rescues |
| PASS | owners: both copies agree who owns every NPC they both changed | 25 NPCs changed owner, 0 disagree |
| PASS | owners: every ownership change was noticed by both copies (allowing one lost at a teleport) | 0 seen by one copy only |
| PASS | owners: no flapping (no ownership change while nobody moves) | 0 changes in the quiet 8 s |
| PASS | sync: positions and health match after the fuzzing (at least 2 NPCs compared) | NPCs compared: 7, mismatching: 0 |
| FAIL | stability: no fault inside our code | seat 0 [14384] AV   our code at that moment: call=K2_GetActorLocation module=npc seat 0 [14384] AV   our code at that moment: call=K2_GetActorLocation module=npc |
| PASS | stability: both copies still running |  |

