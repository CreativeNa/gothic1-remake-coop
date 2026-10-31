# Body loot replay 2026-10-31 22:39

10 / 10 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | host->guest setup: an NPC with an inventory exists in both copies | Character_OC_WOC_Prisoner02_112-WorldPointActor_Prisoner02 |
| PASS | host->guest the NPC is tracked in both copies | Character_OC_WOC_Prisoner02_112-WorldPointActor_Prisoner02 |
| PASS | host->guest the take is noticed on the copy that made it (once) | [7700] S loot: chest Character_OC_WOC_Prisoner02_112-WorldPointActor_Prisoner02, item id 1 x1 taken here |
| PASS | host->guest the other copy replays it on the same NPC | [13968] S loot: chest Character_OC_WOC_Prisoner02_112-WorldPointActor_Prisoner02, item id 1 x1 taken by the other player (replay ok) |
| PASS | host->guest no echo back |  |
| PASS | guest->host setup: an NPC with an inventory exists in both copies | Character_OC_WOC_Prisoner02_112-WorldPointActor_Prisoner02 |
| PASS | guest->host the NPC is tracked in both copies | Character_OC_WOC_Prisoner02_112-WorldPointActor_Prisoner02 |
| PASS | guest->host the take is noticed on the copy that made it (once) | [13968] S loot: chest Character_OC_WOC_Prisoner02_112-WorldPointActor_Prisoner02, item id 1 x1 taken here |
| PASS | guest->host the other copy replays it on the same NPC | [7700] S loot: chest Character_OC_WOC_Prisoner02_112-WorldPointActor_Prisoner02, item id 1 x1 taken by the other player (replay ok) |
| PASS | guest->host no echo back |  |

