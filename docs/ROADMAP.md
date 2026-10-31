# Roadmap: tasks for contributors

Status of each item is in [STATUS.md](../STATUS.md). Difficulty: S small, M medium, L large. "Start" says where to begin. Everything must be tested between two copies (see [CONTRIBUTING.md](../CONTRIBUTING.md)).

| Task | Size | Start |
|---|---|---|
| Hits on a mirror NPC do not reach the owner (guest cannot hurt enemies owned by the host, mirror body cannot be looted) | L | `npc_module.c`: send the hit/damage to the owner, or hand the ownership to the attacker at the first hit |
| Dialogues are not synchronised (an NPC talks to one player and walks past the other) | L | observe the dialogue start like the cutscenes in `story_module.c`; decide who owns a dialogue |
| Player death and shared save loading | M-L | observe death and the load-save action, send over `S|`, reload both; decide the rules (see STATUS.md) |
| Items put **into** a chest are not synchronised | M | observe the add function of `DataModule_Container` like `Server_ConsumeItem` |
| Chest lock while the friend uses it, and chest open/close animation | M | send open/close on `S|`, block/animate on the mirror, timeout on disconnect |
| Character bodies: breaks and jitter on mirrors | M | `npc_module.c` bone-driven bodies |
| Puppet visible in T-pose before the friend is in the world | S | hide the puppet component in the core at spawn (`SetVisibility`), see the pose module |
| Spawn zone next to the host (no spawning inside each other) | S-M | choose free offsets around the host at join/load |
| Menu button: open the port at click, show a localized status (waiting / host not ready / connected) | S-M | `menu_module.c` and a connect call in the core |
| Experience shared for kills | M | find the XP award function, replay it on the other copy |
| Falling through the ground after the UluMulu cutscene | M | find what teleports/moves the hero after the cutscene; see `docs/reports/` |
| Real network test (Radmin VPN or similar) between two PCs | S | `join`/`host` in the menu, report packet loss and latency |
| Unexplained crash around the cutscene sweep (exception `0x1d0`, no log) | M | dumps with `tools\dmpinfo.py` |

Ideas that need a design decision first: shared saves between different PCs, trade, a spectator mode after death.
