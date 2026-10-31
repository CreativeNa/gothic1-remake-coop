# Cutscenes on two copies 2026-10-31 23:59

9 / 11 checks passed. Pictures of both windows during every scene: `docs/img/cutscenes/`.

| Result | Check | Detail |
|---|---|---|
| PASS | setup: both copies have a cinematic manager | 000002B365174C00 0000020F845C8F80 |
| FAIL | none (started on the host) plays on the copy that started it | 61 s |
| FAIL | none (started on the host) plays on the other copy as well |  |
| PASS | none (started on the host) skipping it on one copy ends it on both |  |
| PASS | none (started on the host) both copies still running, available memory 9986 MB |  |
| PASS | Prophecy (started on the guest) plays on the copy that started it | 10 s |
| PASS | Prophecy (started on the guest) plays on the other copy as well |  |
| PASS | Prophecy (started on the guest) skipping it on one copy ends it on both |  |
| PASS | Prophecy (started on the guest) both copies still running, available memory 2186 MB |  |
| PASS | stability: both copies still running 12 s after the last scene | available 2246 MB |
| PASS | stability: no crash at the audio predicate (exe+0x5be3977) | 0 faults (caught or not); 0 access violations logged in all |

