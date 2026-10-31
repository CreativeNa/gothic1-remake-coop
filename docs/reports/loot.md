# Chest loot replay 2026-10-31 22:52

10 / 10 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | host->guest setup: the same chest is loaded in both copies | 000001C63DAAAB00 0000023C528EEAC0 |
| PASS | host->guest setup: the chest has a container module | 000001C66867CF00 |
| PASS | host->guest the take is noticed on the copy that made it (once) | [21472] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 1 x1 taken here |
| PASS | host->guest the other copy replays it on the same chest | [16892] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 1 x1 taken by the other player (replay ok) |
| PASS | host->guest no echo back |  |
| PASS | guest->host setup: the same chest is loaded in both copies | 0000023C528EEAC0 000001C63DAAAB00 |
| PASS | guest->host setup: the chest has a container module | 0000023C215E9380 |
| PASS | guest->host the take is noticed on the copy that made it (once) | [16892] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 2 x1 taken here |
| PASS | guest->host the other copy replays it on the same chest | [21472] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 2 x1 taken by the other player (replay ok) |
| PASS | guest->host no echo back |  |

