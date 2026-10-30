# Gothic 1 Remake (2026)

| | |
|---|---|
| Engine | Unreal Engine 5.4.3, Hazelight AngelScript |
| Versions tested | Steam build with `G1R-Win64-Shipping.exe` of 171704320 bytes |
| Projects | this repository (public prototype, MIT) |
| QA level | none (manual testing by the author on two windows of one PC) |
| Last updated | 2026-10-30 |

Format follows `templates/game.md` of the coop-anything project.

## Where things stand

Two players can see each other as puppets of the hero in two game windows on one PC, with the other player's full skeleton pose and equipment. Internet play, shared NPCs and quests are not implemented.

## Existing work

- G1R-Modkit (Alkimia Interactive, GitHub `AlkimiaInteractive/G1R-Modkit`): official modding kit, AngelScript, no networking. The modkit sources were newer than the shipped game and did not compile against it.
- No public co-op attempt for this game was found at the time of writing.

## How the game works (for co-op)

- UE 5.4.3 with a Hazelight AngelScript layer; scripts ship as a precompiled cache. Compiling modkit scripts against the shipped game fails ("unknown super type"), so script mods were abandoned.
- The engine's own listen server / session creation crashes on every attempt (access violation at `0x1d8`, with an IP net driver at `0x80`), so the game's netcode is not usable.
- UE4SS Lua can read the world but struct-valued calls and TArray access fail, and its engine tick hook is removed after an error. A custom UE4SS C++ SDK build was not usable either.
- Working approach: a native DLL injected into the process. Engine addresses (GUObjectArray, FName, ProcessEvent, ...) come from the UE4SS log of the exact exe build; calls go through `ProcessEvent` with parameter layouts read from reflection data.
- The hero is `PlayerCharacterBP_C`, its mesh is a Mutable-generated skeletal mesh with 422 bones. Many helper bones (face, armor, IK) report identity at the origin because the game does not animate them on the hero.
- Movement is driven by Motion Matching, so stances are not single animation clips; copying the bone pose is the reliable way to reproduce them.

## What we changed

Nothing in the game files. Everything is runtime code injected into the process (see README, "How it works").

| Problem | Root cause | Fix | Test |
|---|---|---|---|
| Spawned hero copy hidden every frame | the game hides extra `PlayerCharacterBP_C` | puppet is a plain `SkeletalMeshActor` with the hero's mesh | seen on screen |
| Puppet in T-pose, guessed clips never matched stances | Motion Matching, no stance clips | `PoseableMeshComponent` fed with the hero's bone transforms | seen on screen, sword draw / crouch / jump |
| Face and helper bones pulled to the feet | `GetSocketTransform` returns identity at the origin for bones the game does not animate | receiver skips such bones (reference pose) | spike gone in one sword-drawn screenshot |
| Lag and stutter | per-frame property lookup (allocating name strings), scan of 450k objects | property cache, scan only the hero mesh's children | FPS back to normal in the author's test |

## QA

Manual only. No automated suite yet.

## How to play

Direct connect by IP exists (`tools/join.ps1`) but has not been tested between two PCs yet. For local testing and the join steps see "Build and test" in the README.

## Known issues

See [STATUS.md](../STATUS.md).

## Next steps

Transport over the internet (direct IP first), pose compression, then NPC / enemy mirroring.
