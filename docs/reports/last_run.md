# Automated run 2026-10-31 12:14

17 / 18 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | boot: both copies reach the world and see each other | after 37 s |
| PASS | boot: two game processes | host 23164, guest 18600 |
| FAIL | npc sync: every NPC of the host is at the same place in the guest | NPCs compared: 12, mismatching: 3 |
| PASS | knock-out: NPC Ratford found in the host |  |
| PASS | knock-out: the host reports the NPC defeated | after 1 attacks |
| PASS | knock-out: the guest switched an NPC to the bone-driven mesh |  |
| PASS | knock-out: the lying NPC is at the same place in both copies | NPCs compared: 1, mismatching: 0 |
| PASS | ownership: the guest takes the NPC over when its hero is clearly nearer |  |
| PASS | ownership: the guest's own hits knock the NPC out (it simulates it) | after 1 attacks |
| PASS | ownership: the host mirrors the NPC with the bone-driven mesh |  |
| PASS | ownership: the NPC is at the same place in both copies | NPCs compared: 1, mismatching: 0 |
| PASS | quick load #1: both copies survive |  |
| PASS | quick load #2: both copies survive |  |
| PASS | quick load: both copies see each other again |  |
| PASS | fps: seat 0 median of the last samples >= 15 | 57,7 fps |
| PASS | fps: seat 1 median of the last samples >= 15 | 55,3 fps |
| PASS | stability: no access violation or exception inside our code (this run) |  |
| PASS | stability: no new crash dump |  |

