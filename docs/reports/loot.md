# Chest loot replay 2026-10-31 22:39

10 / 10 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | host->guest setup: the same chest is loaded in both copies | 000002D2C6390100 000002B9AE07B5A0 |
| PASS | host->guest setup: the chest has a container module | 000002D2EBD2B300 |
| PASS | host->guest the take is noticed on the copy that made it (once) | [7700] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 1 x1 taken here |
| PASS | host->guest the other copy replays it on the same chest | [13968] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 1 x1 taken by the other player (replay ok) |
| PASS | host->guest no echo back |  |
| PASS | guest->host setup: the same chest is loaded in both copies | 000002B9AE07B5A0 000002D2C6390100 |
| PASS | guest->host setup: the chest has a container module | 000002B9777CFC80 |
| PASS | guest->host the take is noticed on the copy that made it (once) | [13968] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 2 x1 taken here |
| PASS | guest->host the other copy replays it on the same chest | [7700] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 2 x1 taken by the other player (replay ok) |
| PASS | guest->host no echo back |  |

