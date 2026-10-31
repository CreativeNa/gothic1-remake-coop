# Cutscenes on two copies 2026-10-31 23:33

7 / 7 checks passed. Pictures of both windows during every scene: `docs/img/cutscenes/`.

| Result | Check | Detail |
|---|---|---|
| PASS | setup: both copies have a cinematic manager | 000001F864CEA180 0000018BE8658680 |
| PASS | IntroPt2 (started on the host) plays on the copy that started it | 6 s |
| PASS | IntroPt2 (started on the host) plays on the other copy as well |  |
| PASS | IntroPt2 (started on the host) skipping it on one copy ends it on both |  |
| PASS | IntroPt2 (started on the host) both copies still running, available memory 7976 MB |  |
| PASS | stability: both copies still running 30 s after the last scene | available 9120 MB |
| PASS | stability: no crash at the audio predicate (exe+0x5be3977) | 0 faults (caught or not); 0 access violations logged in all |

