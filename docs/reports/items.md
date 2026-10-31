# World item sync 2026-10-31 17:06

7 / 8 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | host->guest setup: hero next to the item | target at 116413 -99481 -4941 |
| PASS | host->guest an item is still lying within 450 units of the hero | 0 units |
| FAIL | host->guest the pickup is noticed on the picking copy | [19788] S item BP_ItAI_Plank_World_C_2147439469 picked up here (id 0e17f412-e3aa2b1d) |
| PASS | host->guest the other copy removes the same item | [9288] S item BP_ItAI_Plank_World_C_2147339626 removed on arrival (picked up by the other player) |
| PASS | guest->host setup: hero next to the item | target at 116500 -99596 -4953 |
| PASS | guest->host an item is still lying within 450 units of the hero | 0 units |
| PASS | guest->host the pickup is noticed on the picking copy | [9288] S item BP_ItAI_Plank_World_C_2147439464 picked up here (id 0e17f412-296a66b3) |
| PASS | guest->host the other copy removes the same item | [19788] S item BP_ItAI_Plank_World_C_2147329944 removed on arrival (picked up by the other player) |

