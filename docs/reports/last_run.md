# Automated run 2026-10-31 11:41

11 / 14 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | boot: both copies reach the world and see each other | after 37 s |
| PASS | boot: two game processes | host 25888, guest 23364 |
| FAIL | npc sync: every NPC of the host is at the same place in the guest | NPCs compared: 12, mismatching: 0 |
| PASS | knock-out: NPC Ratford found in the host |  |
| PASS | knock-out: the host reports the NPC defeated | after 1 attacks |
| PASS | knock-out: the guest switched an NPC to the bone-driven mesh |  |
| FAIL | knock-out: the lying NPC is at the same place in both copies | NPCs compared: 1, mismatching: 0 |
| PASS | quick load #1: both copies survive |  |
| PASS | quick load #2: both copies survive |  |
| PASS | quick load: both copies see each other again |  |
| PASS | fps: seat 0 median of the last samples >= 15 | 59,3 fps |
| PASS | fps: seat 1 median of the last samples >= 15 | 59,2 fps |
| FAIL | stability: no access violation or exception inside our code (this run) | seat 1 [436] EXCEPTION 0xc0000005 in ProcessEvent(SetActorEnableCollision) seat 1 [436] EXCEPTION 0xc0000005 in ProcessEvent(SetActorEnableCollision) |
| PASS | stability: no new crash dump |  |

