# Chest loot replay 2026-10-31 19:25

10 / 10 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | host->guest setup: the same chest is loaded in both copies | 00000223576395C0 00000225BDD84B60 |
| PASS | host->guest setup: the chest has a container module | 0000022375634D80 |
| PASS | host->guest the take is noticed on the copy that made it (once) | [18120] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 1 x1 taken here |
| PASS | host->guest the other copy replays it on the same chest | [20308] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 1 x1 taken by the other player (replay ok) |
| PASS | host->guest no echo back |  |
| PASS | guest->host setup: the same chest is loaded in both copies | 00000225BDD84B60 00000223576395C0 |
| PASS | guest->host setup: the chest has a container module | 00000225D9E7BD80 |
| PASS | guest->host the take is noticed on the copy that made it (once) | [20308] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 2 x1 taken here |
| PASS | guest->host the other copy replays it on the same chest | [18120] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 2 x1 taken by the other player (replay ok) |
| PASS | guest->host no echo back |  |

