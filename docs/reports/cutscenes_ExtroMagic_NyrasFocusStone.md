# Cutscenes on two copies 2026-10-31 23:50

7 / 11 checks passed. Pictures of both windows during every scene: `docs/img/cutscenes/`.

| Result | Check | Detail |
|---|---|---|
| PASS | setup: both copies have a cinematic manager | 0000019461573C40 000002B497EE8D40 |
| PASS | ExtroMagic (started on the host) plays on the copy that started it | 6 s |
| PASS | ExtroMagic (started on the host) plays on the other copy as well |  |
| FAIL | ExtroMagic (started on the host) skipping it on one copy ends it on both |  |
| PASS | ExtroMagic (started on the host) both copies still running, available memory 6623 MB |  |
| FAIL | NyrasFocusStone (started on the host) plays on the copy that started it | 65 s |
| FAIL | NyrasFocusStone (started on the host) plays on the other copy as well |  |
| FAIL | NyrasFocusStone (started on the host) skipping it on one copy ends it on both |  |
| PASS | NyrasFocusStone (started on the host) both copies still running, available memory 6647 MB |  |
| PASS | stability: both copies still running 30 s after the last scene | available 6920 MB |
| PASS | stability: no crash at the audio predicate (exe+0x5be3977) | 0 faults (caught or not); 4 access violations logged in all |

