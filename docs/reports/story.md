# Story sync 2026-10-31 15:56

4 / 6 checks passed.

| Result | Check | Detail |
|---|---|---|
| PASS | setup: story and quest objects found in both copies | 000001E770299A00 000002266DCB5A40 000001E4168F8D40 00000225DB85CE80 |
| PASS | story flag: host change reaches the guest | host set 1, guest 1 |
| FAIL | story flag: guest change reaches the host | guest set 0, host 1 |
| PASS | quest: started on the host, running on the guest | host 2, guest 2 |
| FAIL | quest: finished on the guest, finished on the host | guest 2, host 2 |
| PASS | quest: reset on the host is reset on the guest | guest 1 (wanted 1) |

