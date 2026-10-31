# Chest loot replay 2026-10-31 18:04

10 / 10 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | host->guest setup: the same chest is loaded in both copies | 0000013BCFE1D580 0000026E32140BA0 |
| PASS | host->guest setup: the chest has a container module | 0000013BCC502700 |
| PASS | host->guest the take is noticed on the copy that made it (once) | [4244] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 1 x1 taken here |
| PASS | host->guest the other copy replays it on the same chest | [15164] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 1 x1 taken by the other player (replay ok) |
| PASS | host->guest no echo back |  |
| PASS | guest->host setup: the same chest is loaded in both copies | 0000026E32140BA0 0000013BCFE1D580 |
| PASS | guest->host setup: the chest has a container module | 0000026DDD111C80 |
| PASS | guest->host the take is noticed on the copy that made it (once) | [15164] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 2 x1 taken here |
| PASS | guest->host the other copy replays it on the same chest | [4244] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 2 x1 taken by the other player (replay ok) |
| PASS | guest->host no echo back |  |

