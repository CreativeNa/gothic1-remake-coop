# Chest loot replay 2026-10-31 22:13

10 / 10 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | host->guest setup: the same chest is loaded in both copies | 000001E6559B6B40 0000011E3629CAE0 |
| PASS | host->guest setup: the chest has a container module | 000001E65F13C100 |
| PASS | host->guest the take is noticed on the copy that made it (once) | [21740] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 1 x1 taken here |
| PASS | host->guest the other copy replays it on the same chest | [8700] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 1 x1 taken by the other player (replay ok) |
| PASS | host->guest no echo back |  |
| PASS | guest->host setup: the same chest is loaded in both copies | 0000011E3629CAE0 000001E6559B6B40 |
| PASS | guest->host setup: the chest has a container module | 0000011E0C1DBA00 |
| PASS | guest->host the take is noticed on the copy that made it (once) | [8700] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 2 x1 taken here |
| PASS | guest->host the other copy replays it on the same chest | [21740] S loot: chest Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755, item id 2 x1 taken by the other player (replay ok) |
| PASS | guest->host no echo back |  |

