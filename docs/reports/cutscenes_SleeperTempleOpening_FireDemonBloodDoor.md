# Cutscenes on two copies 2026-10-31 23:55

11 / 11 checks passed. Pictures of both windows during every scene: `docs/img/cutscenes/`.

| Result | Check | Detail |
|---|---|---|
| PASS | setup: both copies have a cinematic manager | 0000020A68EEACC0 000001A07A6AD780 |
| PASS | SleeperTempleOpening (started on the host) plays on the copy that started it | 6 s |
| PASS | SleeperTempleOpening (started on the host) plays on the other copy as well |  |
| PASS | SleeperTempleOpening (started on the host) skipping it on one copy ends it on both |  |
| PASS | SleeperTempleOpening (started on the host) both copies still running, available memory 5992 MB |  |
| PASS | FireDemonBloodDoor (started on the host) plays on the copy that started it | 8 s |
| PASS | FireDemonBloodDoor (started on the host) plays on the other copy as well |  |
| PASS | FireDemonBloodDoor (started on the host) skipping it on one copy ends it on both |  |
| PASS | FireDemonBloodDoor (started on the host) both copies still running, available memory 4279 MB |  |
| PASS | stability: both copies still running 30 s after the last scene | available 4859 MB |
| PASS | stability: no crash at the audio predicate (exe+0x5be3977) | 0 faults (caught or not); 2 access violations logged in all |

