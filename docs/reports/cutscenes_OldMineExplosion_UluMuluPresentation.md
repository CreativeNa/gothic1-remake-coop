# Cutscenes on two copies 2026-11-01 00:05

11 / 11 checks passed. Pictures of both windows during every scene: `docs/img/cutscenes/`.

| Result | Check | Detail |
|---|---|---|
| PASS | setup: both copies have a cinematic manager | 0000024E61869880 0000023EEAEBC7C0 |
| PASS | OldMineExplosion (started on the host) plays on the copy that started it | 6 s |
| PASS | OldMineExplosion (started on the host) plays on the other copy as well |  |
| PASS | OldMineExplosion (started on the host) skipping it on one copy ends it on both |  |
| PASS | OldMineExplosion (started on the host) both copies still running, available memory 8678 MB |  |
| PASS | UluMuluPresentation (started on the host) plays on the copy that started it | 6 s |
| PASS | UluMuluPresentation (started on the host) plays on the other copy as well |  |
| PASS | UluMuluPresentation (started on the host) skipping it on one copy ends it on both |  |
| PASS | UluMuluPresentation (started on the host) both copies still running, available memory 7269 MB |  |
| PASS | stability: both copies still running 12 s after the last scene | available 8443 MB |
| PASS | stability: no crash at the audio predicate (exe+0x5be3977) | 0 faults (caught or not); 8 access violations logged in all |

