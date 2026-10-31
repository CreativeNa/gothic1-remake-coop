# Automated run [network: loss=10 delay=150 jitter=60] 2026-10-31 13:02

17 / 17 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | boot: both copies reach the world and see each other | after 38 s |
| PASS | boot: two game processes | host 17544, guest 25152 |
| PASS | npc sync: every NPC of the host is at the same place in the guest | NPCs compared: 12, mismatching: 0 |
| PASS | knock-out: NPC Ratford found in the host |  |
| PASS | knock-out: the host reports the NPC defeated | after 1 attacks |
| PASS | knock-out: the guest switched an NPC to the bone-driven mesh |  |
| PASS | knock-out: the lying NPC is at the same place in both copies | NPCs compared: 1, mismatching: 0 |
| PASS | ownership: the guest takes the NPC over when its hero is clearly nearer |  |
| PASS | ownership: the guest's own hits knock the NPC out (it simulates it) | after 1 attacks |
| PASS | ownership: the host mirrors the NPC with the bone-driven mesh |  |
| PASS | ownership: the NPC is at the same place in both copies | NPCs compared: 1, mismatching: 0 |
| PASS | health: the mirror shows the owner's health 77 | mirror shows '77' |
| PASS | health: the mirror shows the owner's health 0 | mirror shows '0' |
| PASS | fps: seat 0 median of the last samples >= 15 | 57,9 fps |
| PASS | fps: seat 1 median of the last samples >= 15 | 57,8 fps |
| PASS | stability: no access violation or exception inside our code (this run) |  |
| PASS | stability: no new crash dump |  |

Proxy: in=19012 dropped=1912 dup=0 out=17065

