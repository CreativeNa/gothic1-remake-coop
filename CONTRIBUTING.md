# Contributing

Thanks for looking at this project. It is an experimental co-op for Gothic 1 Remake (Unreal Engine 5.4, AngelScript) made of a native DLL injected into two game copies. Read [STATUS.md](STATUS.md) first: it lists what works and the open problems, most of which are good first tasks. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) explains how the code is organised.

## Ground rules

- Work only on a **separate test copy** of the game (`game-test`), never on the real Steam install. Start both copies through the harness so the copies get their own settings.
- Everything is only tested with scripts and short live sessions between two copies on **one PC**. State what you tested and how in your pull request; do not claim more.
- No personal data (names, paths with user names, addresses, keys) in code, logs or docs you commit.
- Keep the style of the surrounding code (C, comments that say *why*, small modules).

## Build

Windows, Visual Studio build tools (`cl`). From the repository root:

```powershell
.\tools\dev.ps1 build            # all modules (anim, equip, pose, npc, story, menu, probe)
.\tools\dev.ps1 build story      # one module
.\tools\dev.ps1 core 61          # the core DLL as native\build\coopdll_61.dll (the game must be restarted to load it)
```

Modules are reloaded by the core while a game is running (a new build is picked up within seconds). The core and the menu module are not: restart the game.

## Run two copies

```powershell
.\tools\harness_start.ps1 -Lite          # two copies side by side, autoload of the newest save
.\tools\harness_start.ps1 -Lite -Gate    # with the join gate (the guest waits until Diego has left)
.\tools\harness.ps1 stop                 # stop the copies
```

`-Lite` lowers the texture pool and the resolution: two full copies need about 15-17 GB of RAM and some cutscenes add 1-4 GB each. `tools\watchdog.ps1` (started by the harness) stops the games if memory or CPU run out, so the PC stays usable. Never start the game with `-nosound` (the audio library crashes).

Put an empty `native\build\autoload_skip_<seat>.txt` to stop one copy from loading a save and stay in the main menu.

## Two copies with CouchLink (alternative to the harness)

[CouchLink](https://github.com/cranebeaker/couchlink) starts one copy per player, each on its own monitor with its own input. The recipe is in the repository: `tools\couchlink\gothic1r-coop-test.json`.

1. Copy the recipe to `%LOCALAPPDATA%\Couchlinkecipes\` and check the game path (`$GAME`) and the Steam app id in it.
2. Start the "Gothic 1 Remake co-op (test copy)" recipe with 2 players. For every seat CouchLink starts the helper `tools\couchlink\couchlink_seat.vbs` (-> `couchlink_seat.ps1`), which finds the game process by its `-CoopSeat=<n>` argument, writes the network config and injects the newest `nativeuild\coopdll_*.dll`. Seat 0 is the host (UDP 40001 -> 40002), seat 1 the guest, both over 127.0.0.1.
3. Optional: `tools\couchlink\perf_cmds.example.txt` -> `nativeuild\perf_cmds.txt` (console commands run in every copy once its hero exists), and `nativeuildutoload.txt` to press "Continue" in the main menu automatically.
4. The helper writes its log to `nativeuild\couchlink_seat<n>.log`; a DLL injected too early never hooks (the game needs ~20-30 s after its process exists), hence the settle time.

The harness (`tools\harness_start.ps1`) does the same without CouchLink and is what the scripted tests use. A real second PC does not need either: use `tools\join.ps1` or the Co-op button of the main menu.

## Look at the game and test

```powershell
.\tools\dev.ps1 probe "@0 findall StoryG1R"     # ask a running copy (seat 0 or 1) about objects, functions, properties
.\tools\tests\run_sync.ps1 story                # scripted two-copy tests: story, items, chests, loot, bodyloot
```

Useful `probe` commands: `findall`, `members`, `class`, `get`, `set`, `call`, `fninfo`, `chestlist`, `bodylist`, `tp`, `lookat`. The log of a run is `native\build\coopdll.log` (`tools\logq.ps1` filters it). Test reports go to `docs\reports\`.

Test aids (files in `native\build\`, all local, not in git): `gate_off.txt` (the harness creates it: no join gate), `menu_off.txt` (switch the menu module off), `menu_lang.txt` (force a menu language), `chapter_reset_h.txt` / `chapter_reset_g.txt` (put the chapter back to 0 on the host / guest copy), `scan_fast.txt`, `story_off.txt`.

## Pull requests

- One topic per pull request, with a short description of the problem, the cause and what you tested (scripts and/or live, which copies).
- Update [STATUS.md](STATUS.md) (move the item out of the open problems or add a new one) and [docs/CHANGELOG.md](docs/CHANGELOG.md).
- Do not commit binaries, game files, save games or logs.
