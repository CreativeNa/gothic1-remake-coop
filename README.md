# Gothic 1 Remake Coop

A work-in-progress co-op mod for **Gothic 1 Remake** (Unreal Engine 5.4, Hazelight AngelScript). A native DLL is injected into two running game instances; each player sees the other as a puppet of the hero, driven by the other player's real skeleton pose, and the two games stay in sync over UDP.

> Fan project. Not affiliated with Alkimia Interactive, THQ Nordic or the coop-anything project. No game files are included; you need your own copy of the game.
>
> **Status: early prototype.** Two windows on one PC work. Connecting by IP (`tools\join.ps1`) is implemented but not yet tested between two real PCs. Shared NPCs and quests do not exist yet. See [STATUS.md](STATUS.md).

*По-русски:* кооператив-прототип для Gothic 1 Remake. В два запущенных экземпляра игры внедряется DLL; каждый игрок видит другого как куклу героя с его настоящей позой скелета. Пока работает только два окна на одном ПК; игра по интернету, общие NPC и квесты ещё не сделаны.

## What works today

- Second player shown as a copy of the hero, position and rotation synced (20 Hz UDP, loopback).
- Pose of the whole skeleton (422 bones) copied to the puppet: walking, running, sprinting, crouching, jumping, sword draw and sheathe, attacks, eating. The puppet's equipment (quiver, bow, sword, food in hand) is mirrored.
- Hot-reloadable modules, so the pose/equipment code can be rebuilt without restarting the game.

## What does not work yet

See [STATUS.md](STATUS.md) for the full list. The main gaps: IP transport untested between different PCs, no shared world (NPCs, combat, quests, saves), FPS dips during fights, occasional crashes, and no facial animation on the puppet.

## How it works

1. `native/inject.exe` loads `coopdll_N.dll` into each game process.
2. The core DLL finds engine functions by fixed addresses (read from a UE4SS log for one exact game build), hooks the game thread (`UGameEngine::Tick`) and calls UFunctions through `ProcessEvent` using parameter layouts read from the engine's own reflection data.
3. Modules in `modules\*.dll` (`anim`, `equip`, `pose`) are loaded through the core's API (`native/coreapi.h`) and reloaded when their file changes.
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
native\build_module.bat [anim|equip|pose]   -> native\build\modules\*.dll  (hot-reloaded within ~1 s)
native\build.bat                   -> inject.exe
tools\play_net.ps1 -Dll coopdll_<suffix>.dll   starts two windows and injects the DLL
```

Set up a **copy** of the game in a separate folder (`tools\setup_test.ps1`) so the real installation is never touched. Debug switches (files in `native\build\`): `pose_off.txt`, `pose_mode.txt`, `pose_skip.txt`, `equip_off.txt`, `try_clip.txt`.

## Repository layout

| Path | What |
|---|---|
| `native/` | core DLL (`coopdll.c`, `n2.inc`), module API (`coreapi.h`), modules, injector, build scripts |
| `tools/` | launch and test-setup scripts, the earlier UE4SS Lua probe |
| `mod/` | the earlier AngelScript attempt (legacy, abandoned; still used by `tools\setup_test.ps1`) |
| `helper/` | the earlier UDP bridge (legacy, abandoned; only used by `tools\play_two.ps1`) |
| `docs/` | findings log and the coop-anything style write-up |

## License

Code in this repository is MIT licensed ([LICENSE](LICENSE)). Gothic and Gothic 1 Remake belong to their owners; this project contains none of their assets.
