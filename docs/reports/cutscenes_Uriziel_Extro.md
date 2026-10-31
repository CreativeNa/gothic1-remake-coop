# Cutscenes on two copies 2026-10-31 23:45

10 / 11 checks passed. Pictures of both windows during every scene: `docs/img/cutscenes/`.

| Result | Check | Detail |
|---|---|---|
| PASS | setup: both copies have a cinematic manager | 000001FAFCCD40C0 000002098E8DF040 |
| PASS | Uriziel (started on the host) plays on the copy that started it | 6 s |
| PASS | Uriziel (started on the host) plays on the other copy as well |  |
| PASS | Uriziel (started on the host) skipping it on one copy ends it on both |  |
| PASS | Uriziel (started on the host) both copies still running, available memory 3733 MB |  |
| PASS | Extro (started on the host) plays on the copy that started it | 7 s |
| PASS | Extro (started on the host) plays on the other copy as well |  |
| FAIL | Extro (started on the host) skipping it on one copy ends it on both |  |
| PASS | Extro (started on the host) both copies still running, available memory 4304 MB |  |
| PASS | stability: both copies still running 30 s after the last scene | available 12872 MB |
| PASS | stability: no crash at the audio predicate (exe+0x5be3977) | 0 faults (caught or not); 10 access violations logged in all |

