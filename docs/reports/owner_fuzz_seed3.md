# Ownership fuzz 2026-10-31 14:11 (15 rounds, seed 3)

6 / 8 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | setup: the camp NPCs and both hero positions are known | 2 NPCs |
| PASS | world: heroes were put back after falling through unloaded ground (test artefact, informational) | 11 rescues |
| PASS | owners: both copies agree who owns every NPC they both changed | 14 NPCs changed owner, 0 disagree |
| PASS | owners: every ownership change was noticed by both copies (allowing one lost at a teleport) | 0 seen by one copy only |
| PASS | owners: no flapping (no ownership change while nobody moves) | 0 changes in the quiet 8 s |
| FAIL | sync: positions and health match after the fuzzing (at least 2 NPCs compared) | NPCs compared: 0, mismatching: 0 |
| PASS | stability: no unhandled fault inside our code (caught faults on streamed-out actors are expected) |  |
| FAIL | stability: both copies still running |  |

