# Gothic 1 Remake Coop

A work-in-progress co-op mod for **Gothic 1 Remake** (Unreal Engine 5.4, Hazelight AngelScript). A native DLL is injected into two running game instances; each player sees the other as a puppet of the hero, driven by the other player's real skeleton dev.ps1 build [anim|equip|pose|npc|story|probe], and the two games stay in sync over UDP.

> Fan project. Not affiliated with Alkimia Interactive, THQ Nordic or the coop-anything project. No game files are included; you need your own copy of the game.
>
> **Status: working prototype, roughly two thirds of the way to a playable co-op.** Two game windows on one PC work (launched through [CouchLink](https://github.com/cranebeaker/couchlink) or the built-in test harness): the other player is visible with a real skeleton dev.ps1 build [anim|equip|pose|npc|story|probe], NPCs and fights are shared, and story flags, quest states, picked-up items and emptied chests are kept in sync. Connecting by IP (`tools\join.ps1`) is implemented but **not yet tested between two real PCs**. See [STATUS.md](STATUS.md) for the checklist and [docs/CHANGELOG.md](docs/CHANGELOG.md) for the detailed history.

*По-русски:* кооператив-прототип для Gothic 1 Remake. В два запущенных экземпляра игры внедряется DLL; каждый игрок видит другого как куклу героя с его настоящей позой скелета. Работает два окна на одном ПК (в том числе через CouchLink); подключение по IP есть, но проверено пока только на одном ПК; общие NPC и квесты ещё не сделаны.

## What works today

- Second player shown as a copy of the hero, position and rotation synced (20 Hz UDP, loopback).
- Pose of the whole skeleton (422 bones) copied to the puppet: walking, running, sprinting, crouching, jumping, sword draw and sheathe, attacks, eating. The puppet's dev.ps1 build [anim|equip|pose|npc|story|probe]ment (quiver, bow, sword, food in hand) is mirrored.
- Hot-reloadable modules, so the game logic can be rebuilt without restarting the game.
- **Shared NPCs:** every NPC is simulated by the copy whose hero is nearer (the other copy mirrors it), so both players can fight; position, health, death and the weapon in hand follow the owner. Knocked-out and dead NPCs are shown in their real dev.ps1 build [anim|equip|pose|npc|story|probe] (bone-driven) with their items.
- **Story and quests:** the whole story object (about 520 flags and the chapter) and the state of all 679 quests are kept equal in both copies; the host leads, changes are repeated until acknowledged.
- **World items and chests:** an item picked up next to one player disappears for the other; a chest emptied next to one player is empty for the other.
- **Robust network layer:** chunked blobs survive loss, reordering and duplicates (tested with 25 % loss and 300 ms jitter).
- **Automated tests** that need no human: a 20-check suite, ownership fuzz test, story / item / chest tests, network impairment scenarios (see below).

## What does not work yet

See [STATUS.md](STATUS.md) for the full list. The main gaps: IP transport untested between different PCs; no dialogue / trade / save sharing; partial looting of chests, loot of bodies and dropped items; arrows in flight; no facial animation on the puppet; occasional crashes inside the game (one family is traced to ambient bird audio and mitigated); journal / reward behaviour of synced quests not yet checked by eye. Everything was verified with two copies on one PC only.

## How it works

1. `native/inject.exe` loads `coopdll_N.dll` into each game process.
2. The core DLL finds engine functions by fixed addresses (read from a UE4SS log for one exact game build), hooks the game thread (`UGameEngine::Tick`) and calls UFunctions through `ProcessEvent` using parameter layouts read from the engine's own reflection data.
3. Modules in `modules\*.dll` (`anim`, `dev.ps1 build [anim|equip|pose|npc|story|probe]`, `pose`) are loaded through the core's API (`native/coreapi.h`) and reloaded when their file changes.
4. The puppet is an engine `SkeletalMeshActor` plus a `PoseableMeshComponent` that receives bone transforms every few frames.

More detail: [docs/NOTES.md](docs/NOTES.md) (findings log) and [docs/coop-anything-writeup.md](docs/coop-anything-writeup.md).

## Is it safe? What the injector does

The mod works by loading a DLL into the game process, so here is exactly what that means:

- `native/inject.exe` takes a process id and a DLL path and loads that DLL into **that one process** (the game). It is meant for the single-player game only; there is no anti-cheat or online mode involved and nothing here tries to bypass one.
- The only network traffic is UDP between you and the friend whose address you pass to `tools\join.ps1`. The DLL ignores packets from any other IP address. The traffic is not encrypted or authenticated yet, so play only with people you trust (or over a VPN). No telemetry, no other servers, no downloads.
- The code is open and small (C), and there are no prebuilt binaries in the repository: you build `inject.exe` and the DLLs yourself (see below). If a release is published, it lists SHA-256 hashes and is built from the tagged source.
- The DLL refuses to run if `G1R-Win64-Shipping.exe` is not exactly the supported build, so it cannot attach to a wrong version by accident.
- Nothing in the real game folder is modified; testing uses the copy created by `tools\setup_test.ps1`.

Your antivirus may flag any DLL injector, including this one. That is a false positive from its behaviour, not from its contents; read the source or do not run it.

## Play with a friend (experimental)

1. Build as described below.
2. Both start Gothic 1 Remake and load a save.
3. Each runs `tools\join.ps1 -Peer <the other player's IP>` (LAN/VPN address, or public IP with UDP port 40001 forwarded). Details are at the top of `join.ps1`.

This path is verified on loopback (two windows, one PC). Reports from real two-PC tests are very welcome: open an issue.

## Reporting bugs

Open an issue with: game build (exe size), what you did, and `native\build\coopdll.log`.

## Build and test (Windows)

Requirements: Visual Studio 2022 Build Tools, your own Gothic 1 Remake install. The addresses only match the game build with `G1R-Win64-Shipping.exe` of **171704320 bytes**; on any other size the DLL stops by design.

```
native\build_dll.bat <suffix>      -> native\build\coopdll_<suffix>.dll   (a loaded DLL cannot be overwritten, use a new suffix)
native\build_module.bat [anim|dev.ps1 build [anim|equip|pose|npc|story|probe]|pose]   -> native\build\modules\*.dll  (hot-reloaded within ~1 s)
native\build.bat                   -> inject.exe
tools\play_net.ps1 -Dll coopdll_<suffix>.dll   starts two windows and injects the DLL
```

### Connect by IP

```
tools\join.ps1 -Peer <friend's IP or host name>[:port]     (both players run it with the other player's address)
```

The DLL only accepts packets from that address. LAN or a VPN (Radmin VPN, ZeroTier, Hamachi) is the easiest; over the internet both sides use each other's public IP at about the same time (UDP hole punching) or forward UDP port 40001. Details are in the header of `tools\join.ps1`. Tested so far only with both copies on one PC.

### Two copies on one PC with CouchLink

[CouchLink](https://github.com/cranebeaker/couchlink) can start one copy of the game per player on its own monitor with its own input. `tools\couchlink\gothic1r-coop-test.json` is a recipe for it (copy it to `%LOCALAPPDATA%\Couchlink\recipes\`, check the `vars` paths inside). For each copy the recipe starts `couchlink_seat.vbs` (hidden window), which finds the copy by its `-CoopSeat=<n>` argument, writes the network config (UDP 40001 <-> 40002 on 127.0.0.1) and injects the newest `coopdll_*.dll`.

Set up a **copy** of the game in a separate folder (`tools\setup_test.ps1`) so the real installation is never touched. Debug switches (files in `native\build\`): `dev.ps1 build [anim|equip|pose|npc|story|probe]_off.txt`, `pose_mode.txt`, `pose_skip.txt`, `equip_off.txt`, `try_clip.txt`.

### Performance with two copies

Copy `tools\couchlink\perf_cmds.example.txt` to `native\build\perf_cmds.txt`: its console commands (the example only sets the render scale) run in every copy once its hero exists.

## Development tools

`tools\dev.ps1` is the single entry point (header of the file lists everything):

| Command | What it does |
|---|---|
| `dev.ps1 build [anim|equip|pose|npc|story|probe]\|equip\|pose\|probe]` | rebuild modules; running games hot-reload them within ~1 s, no restart, no save reload |
| `dev.ps1 core <n>` | build `coopdll_<n>.dll` (needs a game restart) |
| `dev.ps1 probe "<cmd>" ...` | run commands in the live game(s): `findall`, `class`, `members`, `get`, `set`, `fn`, `call`, `cc` (console command); `probe help` lists them. Works already in the main menu |
| `dev.ps1 shots` | screenshot all monitors |
| `dev.ps1 status` / `dev.ps1 log` | running games, recent problems, newest crash dump decoded (`tools\dmpinfo.py`) |

**Auto-continue:** create `native\build\autoload.txt` (optionally with the lines `wait=6` and `gap=25`) and every copy presses "Continue" in the main menu by itself, so a restarted game is back in its newest save without a click. The second Couchlink seat waits `gap` seconds longer so the two loads do not hit the GPU at once. Remove the file to switch it off. It calls the game's own `MainMenuWidget::OnContinueClicked` (found with the probe console).

The `probe` module (`native/probe_module.c`) reads `native\build\probe_cmd.txt` and answers in `probe_out_<pid>.txt`. Rule of thumb: put new game logic into modules, not into the core, so a change never costs a restart.

## Automated test harness (no human needed)

| Tool | What it does |
|---|---|
| `tools\harness_start.ps1` | starts both copies (seat 0 = host, seat 1 = guest) without CouchLink; autoload takes them into the newest save |
| `tools\winshot.ps1 -Seat N` | screenshot of one game window, even when it is covered |
| `tools\harness.ps1 near / hp / act / fight / shot / stop` | moves an NPC next to the heroes and aims the cameras, sets NPC health, presses game input actions, takes both screenshots |
| `tools\test_ko.ps1` | the host hero fights an NPC with real input actions until it is down, both windows are photographed into `docs\img\ko_*.png` (`-Attacker 1` lets the guest fight) |
| probe commands | `inject <InputAction>`, `tpto`, `lookat`, `npchp`, `npcs`, `attached`, `@<seat> <command>` (run in one copy only) |

| `tools\tests\run_all.ps1` | the full suite (boot, NPC sync by numbers, knock-out, two quick loads, fps, crashes): PASS / FAIL per check in `docs\reports\last_run.md`, about 7 minutes, no human needed |
| `tools\tests\test_owner_fuzz.ps1` | ownership fuzz test: random teleports of both heroes to NPCs, then both copies must agree on every owner, no flapping, positions / health match, no fault in our code |
| `tools\tests\test_story.ps1` | story flag and quest state sync, both directions |
| `tools\tests\test_items.ps1` | world item pickup sync, both directions |
| `tools\tests\test_chests.ps1` | chest emptied-state sync, both directions (run the three sync tests after a fresh start; see the note in the changelog) |
| `tools\tests\netproxy.py` | UDP impairment proxy (loss, delay, jitter, duplicates) for bad-network scenarios (`run_all.ps1 -Net`) |
| `tools\diff_npcs.ps1` | host vs guest NPC positions / defeated flags / health as numbers |
| `tools\logq.ps1 -Summary / -Crashes / -Seat N -Tag T` | query the log of the newest run by seat and tag (one log per harness run, older ones are in `native\build\logs\`) |

The game reads input only while its window has the focus; `tools\realclick.ps1 -Seat N -Click 0` brings a window to the front.

## Repository layout

| Path | What |
|---|---|
| `native/` | core DLL (`coopdll.c`, `n2.inc`), module API (`coreapi.h`), modules, injector, build scripts |
| `tools/` | launch and test-setup scripts, the earlier UE4SS Lua probe |
| `mod/` | the earlier AngelScript attempt (legacy, abandoned; still used by `tools\setup_test.ps1`) |
| `helper/` | the earlier UDP bridge (legacy, abandoned; only used by `tools\play_two.ps1`) |
| `docs/` | findings log and the coop-anything style write-up |

## Acknowledgements

- **[CouchLink](https://github.com/cranebeaker/couchlink)** was a big help in testing: it starts one copy of the game per player on its own monitor with its own input, which made the whole two-players-on-one-PC development loop possible. Thanks to its author.
- The general approach follows the coop-anything write-up in [docs/coop-anything-writeup.md](docs/coop-anything-writeup.md).

## License

Code in this repository is MIT licensed ([LICENSE](LICENSE)). Gothic and Gothic 1 Remake belong to their owners; this project contains none of their assets.
