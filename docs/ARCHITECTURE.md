# Architecture

## Overview

Two game copies run the same native **core DLL** (`native/coopdll.c` + `native/n2.inc`), injected into the game process. The core finds the game's objects, runs a hook in the game's tick, and talks to the other copy over UDP. Features live in **modules** (small DLLs built from `native/*_module.c`) that the core loads and reloads while the game runs.

```
 copy A (host)                                  copy B (guest)
 core + modules  <----------- UDP ----------->  core + modules
   hero  puppet of B                              hero  puppet of A
```

Each copy shows the friend as a **puppet** (a spawned skeletal mesh driven bone by bone) and mirrors the world state the friend changed.

## Core (`native/n2.inc`, `native/coopdll.c`)

- Resolves the hero and the engine's object array, calls game functions through `ProcessEvent` (`call_fn`), reads/writes memory safely.
- Network: one non-blocking UDP socket. Settings come from `native/build/cfg_<pid>.txt` (`<localPort> <peerHost> <peerPort> <id>`, test harness) or from `connect.txt` written by the menu button (`host <port>` / `join <ip> <port>`; the host locks onto the first friend who knocks).
- Join gate: the host announces `G|1` or `G|0`; nothing but gate packets is processed until the host is open (the host opens after the start conversation with Diego, computed by the story module).
- Safety patches: a function-name cache (the engine leaked memory), a null-argument guard patch for an audio predicate, the tick optimizer opt-out for the puppet.
- Module loader (`modloader.inc`): copies a module DLL to a per-process file, calls `mod_init(CoreAPI*)` and `mod_update(CoreAPI*)` every tick, reloads when the build changes.

## CoreAPI (`native/coreapi.h`, version 12)

What a module may use: `call_fn`, `find_object`, `obj_alive`, `obj_class`, `find_prop_offset`, `list_members`, `read_mem`/`write_mem`, `log`, the remote state (`have_remote`, `remote_blob*`), the blob channels (`set_local_blob2/3`), `observe_fn`/`obs_pop` (see below) and `set_coop_gate`. Add new functions at the end of the struct and raise `COREAPI_VERSION`.

## Modules

| Module | Job |
|---|---|
| `pose_module.c` | bone-by-bone pose of the friend on the puppet (blob channel `B|`), hides the puppet while the friend sends nothing |
| `anim_module.c` | animation clips of the puppet (locomotion states) |
| `equip_module.c` | the friend's weapons/armor on the puppet |
| `npc_module.c` | NPC ownership by proximity, bone-driven bodies of defeated NPCs, montage replay (`N|`) |
| `story_module.c` | story flags, chapter, quests, items, chests, loot, cutscenes, chapter banner, join gate (`S|`) |
| `menu_module.c` | the "Co-op" button of the main menu (10 languages) and its click |
| `probe_module.c` | recon commands for developers (`tools\dev.ps1 probe`) |

## Channels (UDP, first bytes)

`P|` pose/position, `B|` pose blob (chunked), `N|` NPC state, `S|` story/quests/items/chests/loot/cutscene/banner, `G|` join gate beacon. Chunked blobs are reassembled in a ring. The `S|` channel uses acknowledged delta lists with dedupe ids and echo-suppression lists (a change that came from the friend is not sent back).

## Observing game functions

`observe_fn(class, function)` swaps the function pointer of a `UFunction` so calls are recorded; `obs_pop` returns them with their parameters (read from the frame locals when the call came through `ProcessEvent`). **Calls made from AngelScript bytecode arrive without parameters** and some real trigger paths are not seen at all (the chapter banner trigger is one); in such cases compare state (the chapter number) instead of observing the call.

## Ownership rule for NPCs

The copy nearest to an NPC simulates it; the other copy holds a mirror. Damage and looting on a mirror are the weak points (see STATUS.md).

## Adding a synchronised feature (recipe)

1. Find the game state in a running copy with `probe` (`findall`, `members`, `get`).
2. Decide who owns it and what the minimal event is (a number, an id, a list).
3. Read it in your module (cheap, no per-frame scans of all objects), send on `S|` as an acknowledged delta, apply on the other copy with a guard against echoing it back.
4. Add a scripted test in `tools\tests\` and describe live checks in STATUS.md.
