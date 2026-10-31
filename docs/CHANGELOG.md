# Changelog

Detailed log of what was built, why, what broke on the way and how it was fixed. Newest first. Dates are 2026-10-30 / 2026-10-31 unless noted. "Seat 0" is the host copy of the game, "seat 1" the guest copy; both run the same DLL.

Everything below was developed and verified with **two copies of the game on one PC** (a separate test copy of the game folder, never the real installation). Real two-PC play over the internet has **not** been tested yet.

> **Important note on testing.** Most of the changes described here were **not tested in real play situations**. Everything was developed and checked only with two copies of the game on **one PC**, mostly through automated scripts and synthetic actions (teleports, destroying an actor instead of a real pickup, direct state writes), not by people playing together. Real two-PC / internet play, long sessions and many ordinary game situations (dialogues, trading, normal looting, longer fights, save / load cycles) have not been exercised. Treat every feature as an experimental first version that **needs further work and real-world testing**.

> **Важно о тестировании.** Большая часть изменений **не проверялась в реальных игровых ситуациях**. Всё разрабатывалось и проверялось только на двух копиях игры на **одном компьютере**, в основном автоматическими скриптами и искусственными действиями (телепорты, удаление объекта вместо настоящего подбора, прямая запись состояний), а не живой игрой вдвоём. Игра по сети между двумя ПК, долгие сессии и обычные игровые ситуации не проверялись. Каждая функция — экспериментальная первая версия, которая **требует доработки и проверки в реальных условиях**.

## Contents

1. [Story, quests, world items, chests](#1-story-quests-world-items-chests)
2. [Shared NPCs and per-NPC ownership](#2-shared-npcs-and-per-npc-ownership)
3. [Knocked-out and dead NPCs (bone-driven bodies)](#3-knocked-out-and-dead-npcs-bone-driven-bodies)
4. [Network layer](#4-network-layer)
5. [Automated tests and tools](#5-automated-tests-and-tools)
6. [Startup speed and auto-continue](#6-startup-speed-and-auto-continue)
7. [Puppet: bow, arrows, equipment](#7-puppet-bow-arrows-equipment)
8. [Crashes found and what was done](#8-crashes-found-and-what-was-done)
9. [Mistakes made and reverted](#9-mistakes-made-and-reverted)
10. [Not done / not verified](#10-not-done--not-verified)
11. [Credits](#11-credits)

---

## 1. Story, quests, world items, chests

New hot-reloadable module `native/story_module.c`, new third data channel in the core (`S|` datagrams), `COREAPI_VERSION` 9 -> 10 (`set_local_blob3`, `remote_blob3`, `remote_blob3_len`, `remote_blob3_seq`). The core now loads six modules (`anim`, `equip`, `pose`, `npc`, `story`, `probe`; `MAX_MODS` raised to 8). Because the module list grew, this needed a new core DLL (`coopdll_51`) and a game restart once; after that the module itself is hot-reloadable.

### 1.1 Story flags

- The game keeps its whole story in one object, `StoryG1R` (a subclass of `GameStory`). Its own properties are one **contiguous block of memory** (offsets 912..2788, about 520 four-byte words: integers such as `Drax_GotBeer`, and 8-byte in-game-time values), plus the current chapter in the parent class (`GameStory::Chapter`).
- Every second the module reads the block and compares it with "what both copies last agreed on". Words that differ become **pending changes**.
- Pending changes are sent as `(word index, value)` pairs. They stay in the list and are re-sent every second **until the other copy acknowledges** the blob (every blob carries its own sequence number and the last sequence number it received). A lost datagram therefore costs one second, not a lost change.
- The receiver writes the values into its own story object and records them as agreed, so they are not echoed back.
- **First contact:** the host starts with "everything differs", i.e. it sends the full block and the full quest table; the guest takes it over (the host leads the world, like everything else).
- **Conflict** (both copies changed the same word within a second): the host wins.
- Verified in both directions (test below): a flag set on the host appears on the guest and vice versa.

### 1.2 Quest states

- `QuestSubsystem.AllQuestInstances` holds 679 quest objects. Each has a one-byte `State`. Found values: 0 none, 1 available, 2 running, 3 failed, 4 succeeded (checked through `IsRunning`, `IsAvailable`, `HasSucceeded`, `HasFailed`, `IsCompleted` on sample quests of each state).
- Quests are matched between the copies by a hash of the object name (`Instance_Quest_...`), so the order of the arrays does not matter.
- Same delta mechanism as for the story words. A *live* change (a few quests) is applied through the game's own functions (`StartQuest`, `SucceedQuest`, `MakeQuestAvailable`) so journal entries, notifications and rewards happen for the local hero too; if the function leaves a different state, the byte is written directly. A *bulk* change (first sync, more than 20 quests) is written directly so the guest is not flooded with 80 "quest started" notifications.
- Verified: starting a quest on the host puts it into the running state on the guest; setting it finished on the guest finishes it on the host; resetting it resets it.
- `SucceedQuest` on a quest that was just started can refuse to change the state (quest logic of its own). The sync itself is consistent in that case (both copies show the same state); the automated test therefore sets the finished state directly.

### 1.3 Items lying in the world

- Items lying in the level (`BP_ItFo_Plants_*_World_C`, `BP_ItAI_BrushWood_World_C`, pouches, ...) are `InteractiveObjectActor`s with `m_WorldItemDefinition`. The module tracks the ones within 3500 units of its own hero (a sliced scan of the object table, 30000 objects per second, so a full pass takes about 15 s).
- An item that gets **destroyed** (`bActorIsBeingDestroyed`) while it was within 2500 units of its hero is a **pickup**; its id goes into the pending list and the other copy destroys the matching actor. If the other copy has not loaded that actor yet (streaming), the id is remembered and the actor is destroyed the moment it appears.
- **Identity across the two copies:** placed items have a 16-byte `m_PersistentId`. Items the level spawns at runtime (plants, wood) have an all-zero id, so their key is a hash of the class name and the position rounded to 25 units (deterministic in both copies). Keys of chests are told apart from item keys by flipping a byte.
- **False pickups were the hard part.** The game removes items by itself in groups when the hero teleports or a sub-level is unloaded. Rules that made the detection reliable: ignore everything for 15 s after the hero moved more than 1500 units in one second (teleport / load); if three or more tracked items vanish in the same second, treat it as streaming; a pickup is one item at a time. Items already destroyed and waiting for garbage collection are skipped when scanning.
- **Core bug found on the way:** `obj_alive(ptr, name)` caches a verified name *per FName index*, but objects that differ only by the number suffix of their name (`BrushWood_World_C_2147347916` vs `..._2147348295`) share the index, so the function reported living objects as dead (and this produced phantom pickups and "removed on arrival" noise). The story module therefore checks identity on the raw FName (index + number) instead. Other modules still use the old call; fixing it in the core is a todo.
- Each hero keeps their own inventory (intended). Only the *world* is shared.

### 1.4 Chests

- A chest actor (`Interactive_Chest_*`, `IO_*_Chest_*`) keeps its contents in a `DataModule_Container` inside its `DataModuleComponent`; the module's `m_Inventory` struct starts with two `TArray`s of items.
- The module counts the items (sum of both array lengths) of tracked chests. When a chest that had items becomes empty next to its hero, the id is sent and the other copy empties the same chest by giving both item lists length 0.
- The game's own `Server_Debug_EmptyInventory` was tried first: it faults (null read, caught by the DLL's exception guard) because the copy has no server authority, so it is not used.
- **Only "emptied completely" is synced.** Taking one item out of several is not.
- Verified in both directions on two different chests (test below).

### 1.5 Ambient birds removed (crash mitigation)

The game crashed three times with `EXCEPTION_ACCESS_VIOLATION reading 0x200` at `G1R-Win64-Shipping.exe+0x5be3977`. The stack runs through `fmodstudio.dll`, and the objects found on the stack each time were ambient birds (`Pidgeon_bp_C`, `Crow_bp_C`, `AmbientLifeWarningFlock`, a `GothicAudioComponent` on a crow). Our own code was not running at the moment of the crash. As a mitigation the story module destroys these actors in both copies (`native/build/ambient_keep.txt` switches this off). **It is not yet verified that this removes the crash.**

### 1.6 Probe commands added

- `ioscan [classSub] [max]`: live actors that have a persistent id, count per class, and for the first few the id and position (skips destroyed ones).
- `contscan [max]`: container modules with their raw inventory bytes (used to find the layout above).
- `emptychest <ptr> [dry]`: prints the item counts of a chest, empties it (test helper), `dry` only reads.
- Earlier this stretch: `heroinfo`, `npcinfo`, `npchp`, `gedamage`, `tp`, `tpto`, `lookat`, `inject`, `attached`, `@N` prefix to address one seat.

## 2. Shared NPCs and per-NPC ownership

Module `native/npc_module.c`.

- **Principle:** every NPC is simulated by exactly one copy, its *owner*; the other copy mirrors it. The owner is the copy whose hero is clearly nearer (hysteresis of 600 units so that ownership does not flap). Everything starts with seat 0. Both copies run the same code and send only the records of NPCs they own.
- Record per NPC: stable id (FNV hash of the instance name), position, yaw, speed, flags (defeated, dead, weapon state), health (in a spare field), animation hint. Sent on the second channel (`N|`), chunked and reassembled.
- The mirror has collision off, is placed every frame, gets its health applied every 400 ms and its weapon sockets synced every 700 ms (melee weapon / bow, holster <-> hand follows the owner).
- Both players can fight: whoever is nearer owns the NPC and takes the hits; health and death follow the owner on the mirror.
- The AI controller of a mirrored NPC stays possessed. Unpossessing it crashed the game's perception code (an earlier attempt), so `Controller` is left alone.
- World change (loading a save) resets all state; bone-driven bodies are created only when the new world is 8 s old.
- Actors whose calls fault (the engine streamed them out while their pointer still looked valid) are forgotten instead of being touched again; this removed a storm of thousands of caught exceptions.
- Classification of NPC classes uses a hashed cache that is large enough for the thousands of classes the game has (an earlier, too small table found no NPCs at all).

## 3. Knocked-out and dead NPCs (bone-driven bodies)

Problem: a knocked-out NPC lay correctly on the host but was shown standing, T-posed or twisted on the other copy; his sword, bow and quiver floated above him; textures were "torn" when a player came close.

What was tried and rejected: playing the knock-out animation clip on the mirror (`PlayAnimation` single-node gives a T-pose for the additive `FA_*` clips and crashed in `GothicAnimationComponent`); `SetLeaderPoseComponent` (crashed the game).

What works now:

- For **defeated or dead** NPCs only, the mirror gets a `PoseableMeshComponent` with the NPC's own skinned asset, attached to the original mesh (which is hidden, with tick, physics and collision off). The owner sends the **real component-space bone transforms** (sparse list of bone scales included), and the mirror applies them exactly: `T_i * T_parent^-1 * C_parent` (UE multiplication order), parents resolved with `GetParentBone`; `ik_*` bones and bones that only matter in close-ups are left in the reference pose.
- Items (sword, bow, quiver, ...) are re-parented from the original mesh to the poseable mesh, so they lie with the body.
- LOD is forced, and `BFGTickOptimizerSystem::SetTickOptimizationRuntime_Enabled(actor, false)` is called for these actors; without it the game's own tick optimizer crashed (`0xd50`).
- Standing and sitting NPCs stay on the normal path (position + the owner's real animation); the bone-driven path made sitting NPCs' heads and arms stretch, so it is not used for them.
- Switches in `native/build/`: `npc_off.txt`, `npc_nobones.txt`, `npc_skip.txt` (names to leave alone), `npc_unpossess.txt` (old behaviour, for experiments).
- Crashes that came with it: loading a save while a clip played (fixed by the world-change reset), the `0xd50` tick-optimizer crash (fixed as above), crashes with the UE4SS loader in the stack (UE4SS is disabled in the test copy by renaming `dwmapi.dll`).

## 4. Network layer

- UDP, loopback in tests. Packet types: `P|` (position, rotation, speed, action, crouch, ...), `B|` (full-skeleton pose of the puppet), `N|` (NPC channel), `S|` (story / quests / items / chests).
- Large blobs are chunked (1400-byte payloads, header: tag, `|`, chunk index, chunk count, blob sequence byte).
- **Robust reassembly (`BlobRing`):** up to four blobs in flight per channel, chunks may arrive in any order, duplicates are ignored, an older blob is never delivered after a newer one, an incomplete blob is dropped for a newer one. The sequence byte is per channel.
- **Network impairment proxy:** `tools/tests/netproxy.py` sits between the two copies (`native/build/net_proxy.txt` makes both send to the proxy ports) and applies packet loss, delay, jitter (reordering) and duplicates.
  - 10 % loss, 150 ms delay: suite passes.
  - 25 % loss, 300 ms delay with jitter: suite passes.
  - Reports: `docs/reports/net_loss10_delay150.md`, `docs/reports/net_loss25_delay300.md`.
- Per-channel cost log: every 10 s the core logs the average / maximum cost of each module per frame.

## 5. Automated tests and tools

All of these run against two live game copies, no human input.

| Tool | Purpose |
|---|---|
| `tools/harness_start.ps1` / `harness.ps1 stop` | start / stop both copies (seat 0 host, seat 1 guest) without CouchLink; autoload takes them into the newest save |
| `tools/winshot.ps1`, `tools/wininput.ps1`, `tools/realclick.ps1` | window screenshot (works for covered windows via `PrintWindow`), input to a window, bring a window to the front (the game reads input only while it has focus) |
| `tools/test_ko.ps1` | a hero fights an NPC with real input actions until it is knocked out; both windows are photographed |
| `tools/tests/run_all.ps1` | the full suite, 20 checks: boot, NPC sync by numbers, knock-out, ownership takeover, health and death, weapon state, two quick loads, fps, crashes; report in `docs/reports/last_run.md`; optional `-Net` scenarios with the impairment proxy |
| `tools/tests/test_owner_fuzz.ps1` | ownership fuzz test: heroes are teleported to random NPCs many times (absolute coordinates just above the NPC, rescue of a hero that fell through unloaded ground); afterwards both copies must agree on every owner, no ownership change in a quiet period, positions and health match, nothing in our code faulted |
| `tools/tests/test_story.ps1` | story flag and quest state sync, both directions (6 checks) |
| `tools/tests/test_items.ps1` | world item pickup sync, both directions (8 checks); the test destroys an item next to the hero, which is what a pickup does to the actor |
| `tools/tests/test_chests.ps1` | chest emptied-state sync, both directions (6 checks) |
| `tools/diff_npcs.ps1` | host vs guest NPC positions, defeated flags and health as numbers (skips duplicates, actors at the world origin, NPCs not loaded in one copy) |
| `tools/logq.ps1` | query the log of the newest run: `-Summary`, `-Crashes`, `-Seat N`, `-Tag T`, `-Grep` |
| `tools/dev.ps1 probe "<cmd>"` | run commands inside the live game(s): `findall`, `class`, `members`, `get`, `set`, `call`, `ioscan`, ... |

Things the tests taught (and which are now handled in the scripts): a vacuous "positions are equal" check passed while nothing was synced (a "guest receives and mirrors" check was added); the camera inside the puppet's leg; heroes teleported into rocks or falling through unloaded ground; a hash/table overflow that hid NPCs; PowerShell quirks (`$home` is read-only, text passed through tools loses one level of backslashes, unquoted paths with spaces).

Known test weakness: the story, items and chest tests assume the heroes are near their start position and that the world around is loaded. Run back to back, each leaves the heroes somewhere else and the next test can fail for that reason. Run them after a fresh start or add a "heroes back to the start" step.

## 6. Startup speed and auto-continue

- Injection waits shortened: the second seat no longer waits extra; a 13 s settle time is the minimum (at 8 s the engine has not created its objects yet and the DLL never hooks). The hook retry now refreshes the object count.
- **Auto-continue:** `native/build/autoload.txt` (`wait=`, fractional seconds allowed, default 3.5) makes each copy press "Continue" in the main menu and skip the "press any key" screen, so a restarted game is back in the newest save without a click.
- The `g_nmods` counter must equal the number of modules (forgetting it once left the probe module, and with it auto-continue, unloaded).

## 7. Puppet: bow, arrows, equipment

- The bow is shown on the puppet (spawned with the hero as owner; spawning without an owner crashed in `FinishSpawningActor`).
- The nocked arrow is mapped from the hero's `arrow` socket to `weapon_r` on the puppet (the original socket hangs on an undriven IK bone).
- A fake arrow flight (`shot_tick` / `fl_launch`, test trigger `shoot_test.txt`) exists, but the arrow is not visible in flight. Postponed.
- The missing equipment / quick-slot bar in the second copy turned out to be GPU memory pressure (two copies on an 8 GB card).

## 8. Crashes found and what was done

| Crash | Cause | Status |
|---|---|---|
| `0xd50` near the puppet / mirrored NPCs | game's `BFGTickOptimizerSystem` ticks a registered actor | fixed: optimizer disabled for those actors |
| Perception code after `UnPossess` of an NPC controller | AI needs a controller | fixed: controller stays possessed |
| `SetLeaderPoseComponent` | engine crash | removed |
| `PlayAnimation` single-node on NPC mesh | T-pose and `GothicAnimationComponent` crash | removed |
| Quick-load while a clip plays | stale actors after world change | fixed: world-change reset, 8 s delay |
| Actors streamed out while pointers looked valid | thousands of caught faults | fixed: forget faulting actors |
| `0x200` at `exe+0x5be3977` (fmod) | ambient bird audio emitters on the stack, game-internal | mitigated by removing the birds, unverified |
| `Server_Debug_EmptyInventory` null read | no server authority | caught by the guard, function no longer used |

## 9. Mistakes made and reverted

Kept here on purpose so they are not repeated.

- A "performance profile" that changed `sg.*` quality groups at runtime degraded the picture and changed the saved settings file (it lives in the per-user game config, shared with the real installation). Removed. If the in-game quality looks lower than before, check the graphics menu.
- A frame-rate cap for the inactive window hurt the active one. Removed completely.
- Too many things at once ("1 fix becomes 5 fixes") led to a dead end on 2026-10-31; the work was restarted with automated tests first, which is why the test tooling above is so large.
- A "god mode" for local testing exists only as an untracked local file (`native/local_god.inc`, `native/build/god.txt`, both git-ignored) and did not demonstrably work.

## 10. Not done / not verified

- **Real two-PC test over the internet / LAN.** Everything so far ran on one PC through loopback.
- Visually verified: journal entries, notifications and rewards on the guest when a quest changes; both heroes getting a reward only once; a dialogue running on one copy while the other player stands nearby.
- Partial looting of chests, loot of bodies, items dropped by a player, trading.
- Arrows in flight; facial animation of the puppet.
- Transition frames of falling and getting up for NPCs.
- Whether the bird removal really prevents the `0x200` crash; a fuzz run with the NPC module off to see if that crash is the game's own.
- A long soak test (30-60 minutes) for leaks; a full suite run after the last module changes (last full run was before the story / item / chest modules).
- Memory: two copies of the game use nearly all of 32 GB of RAM; the last long test run froze the PC. Close other programs, or run one copy per PC (the real use case).
- Game updates change the fixed addresses; a new reconnaissance pass is needed after each update.

## 11. Credits

- **[CouchLink](https://github.com/cranebeaker/couchlink)** made the two-copies-on-one-PC testing practical: one game instance per seat on its own monitor with its own input. The recipe in `tools/couchlink/` starts the seats and the helper that injects the DLL; the whole co-op development loop (and most of the testing above) relied on it. Thank you to its author.
- The coop-anything write-up in `docs/coop-anything-writeup.md` describes the general approach this project borrowed.
- Gothic and Gothic 1 Remake belong to their owners. This is a fan project and contains none of their assets.

## Addendum (2026-10-31, later): test tooling and chest recon

- `tools/tests/lib.ps1` + `tools/tests/run_sync.ps1`: the story, item and chest tests can now run back to back. The first test after a fresh start records both heroes' positions (`native/build/test_spawn.txt`, deleted by `harness_start.ps1`), every test puts the heroes back before and after. `run_sync.ps1 -Start` restarts both copies first. Last full run: story 6/6 and chests 6/6; the item test had 7/8 and was then changed (it now checks that the *specific* destroyed item is reported and waits up to 30 s for the other copy) and **has not been re-run since the change**.
- `tools/harness_start.ps1 -Seats 0` starts only the host copy: enough for probing the game and half the memory. Two copies together fill almost all of 32 GB RAM; a PC freeze and a "cannot create processes" state were seen during long test runs, so keep runs short and stop the games afterwards.
- New probe commands: `contdump`, `peek <addr> [bytes]` (raw memory with object names), `structinfo <ScriptStruct>` (members of a USTRUCT).
- **Chest layout findings (for partial looting, not implemented):** a container's `m_Inventory` (at offset 64 of `DataModule_Container`) is a `ReplicatedInventoryMap`: a fast-array-serializer header, `m_Keys` (TArray at +16, 16-byte keys) and `m_Values` (280-byte struct that contains sparse-array / hash-set internals and an array of `ItemVirtualData` entries: `m_Id`, `m_InventoryType`, `m_SlotData` = `ItemSlot{m_ItemDefinition class pointer, m_ItemCount}`, `m_Payload` 136 bytes, `m_TimeLastAdded`). The item arrays are not plain arrays of items, so reading single items from memory needs a full decode of that map. The game's own `DataModule_Container` functions come in `Server_*` / `Multicast_*` pairs (`Server_MoveItemFromActorToOtherActor`, `Server_AddAllItemsFromContainer`, `Server_PickWorldItem`, `Multicast_*`); the core has no ProcessEvent observer (it only swaps the `GameEngine` tick vtable slot), so those calls cannot yet be intercepted and replayed on the other copy.
- **Partial chest looting and loot of bodies are therefore still not synced.** Two ways forward: a ProcessEvent observer in the core (also the right tool for dialogues and trading), or a full decode of the inventory map.

## Addendum 2: function observer (core API v11, `coopdll_52`)

- **Idea:** the native body of a `UFunction` is a pointer inside the UFunction object (offset `+0xd8` in this build, found by scanning a known function for a pointer into the exe image). The core swaps it for a small thunk that records the call (object + the parameter buffer, taken from `FFrame::Locals`, which is valid when the call came through `ProcessEvent`) and then calls the original body. No code is patched. Up to 24 functions; a function whose body address is shared with another one (script / blueprint thunk) is refused. API: `observe_fn(outer, fn)` returns a slot, `obs_pop(...)` drains the recorded calls on the game thread.
- The story module is now an "always" module (it also runs with a single copy) and observes the seven `DataModule_Container` functions that move items: `Server_MoveItemTo`, `Server_MoveItemFromActorToOtherActor`, `Server_AddAllItemsFromContainer`, `Server_GetItemsFromContainerInv`, `Server_PickWorldItem`, `Server_ConsumeItem`, `Server_AddNewItem`. Every call is logged as `S obs <function> on <object>: <parameter bytes>` followed by the names of the objects the parameters point to.
- **Verified:** all seven hooks install, the game keeps running, and a call made through the probe (`Server_ConsumeItem` with a dummy id) shows up in the log with its exact parameters.
- **Not done:** (1) a real loot by a player was not observed yet (injected input did not open the chest UI: the chest was probably not targeted); play once with the log open and read the `S obs` lines to learn which of the seven functions the UI really calls and whether the parameters arrive in a buffer; (2) the replay on the other copy (which item ids, how to remove them from the chest without giving them to the other hero) is not written; (3) `Server_ConsumeItem` and `Server_EmptyInventory`-style calls made from the probe fault without server authority, so a replay must probably write the container map directly or call the `Multicast_*` variants.
- Probe commands added: `fninfo <Outer> <Function>` (UFunction, native body RVA, parameters), `structinfo`, `peek`, `contdump`.

## Addendum 3: what the game calls when a player takes items from a chest (observed live)

Taking an item (click on the slot or "Take all") makes the game call, on the **chest's** `DataModule_Container` module, once per item stack:
1. `Server_MoveItemFromActorToOtherActor(Actor = chest, OtherActor = hero, ItemPos = n, InventorySlot = 1, Amount = -1, targetInventorySlot = 0)`
2. `Server_ConsumeItem(InventoryType = 1, ID = n, ItemAmount = stack size)`

The parameters arrive in the `ProcessEvent` parameter buffer, so the observer sees them. "Take all" is simply this pair repeated for ids n = 3, 2, 1, ... The ids of a chest's items are small numbers that count from 0 (the same state loaded in both copies gives the same ids).

**Replay (written, NOT verified):** the story module records every `Server_ConsumeItem` the game makes on a tracked chest's module and sends `(chest key, type, id, amount)` on the story channel (acknowledged and de-duplicated by a loot id); the other copy calls `Server_ConsumeItem` with the same parameters on the same chest and does not echo it back (a short suppression list). `tools/tests/test_loot.ps1` exercises the pipeline with an invented item id; in the one run seen so far the take was noticed on the first copy but the replay on the second copy reported `failed` (the call returned 0), and the run then hung and was stopped by hand. Not known: whether the call fails because the invented id does not exist in that chest or because it needs authority; whether the chest then really shows one item less. The test should use real ids (read them from a take made by hand: the `S obs` lines show them).

A chest UI that opened with empty panels and "Label" placeholders was seen in one copy during that run; the most likely cause is test interference (invented item ids, teleports while the window was open), not verified.

## Addendum 4: memory and CPU overload during two-copy test runs (fixed in part)

- **Symptom:** several times the PC went to ~100 % CPU and ~99 % RAM and froze during long runs with two game copies.
- **Cause found:** the story module cached only 300 classes for its object scan while the game has thousands, so for most objects the class name and property offsets were recomputed every second, allocating memory; a copy grew from 9 GB to 14.5 GB as soon as the second copy connected and the scan started. Fixed: all classes are cached in a 32768-entry hash table (`cls_has_item`). Idle one copy: flat at ~9 GB working set. Two copies after the fix: ~6-8 GB of RAM stays available (was 0.7 GB). A slow growth (about 0.4 GB per minute for both copies together) is still there and not explained.
- **A second cause is the game's own streaming:** a test that teleports both heroes to a far place makes both copies load new world cells (several GB each); the item test hit the safety limit again that way.
- **Safety net:** `tools/watchdog.ps1` (started by `harness_start.ps1`): game processes at BelowNormal priority; stops the games and the test scripts when available RAM (free + reclaimable cache) < 800 MB, CPU >= 97 % for 25 s, or the run lasts longer than 25 min; reason in `native/build/watchdog.log`.
- `harness_start.ps1 -Lite`: command-line-only settings (`r.Streaming.PoolSize 200`, `r.MipMapLODBias 3`, `r.ScreenPercentage 50`, ...), about 1.5 GB less per copy; nothing is saved to the user's settings. `-Seats 0` starts only the host copy. The second copy is started only after the first is in the game (two loads at once need ~13 GB each); the second copy gets `-NoLoadGap` so that autoload does not wait for the first copy a second time. **Do not start the game with `-nosound`: `fmodstudio.dll` crashes a few seconds after the start.**
- `scan_fast.txt` in `native/build/` makes the item scan 80000 objects per second instead of 30000 (tests only).
- **Still open:** the item and chest tests teleport heroes far away; with two copies that can exhaust memory. They should pick items and chests that are already loaded near the heroes instead of teleporting.

## Addendum 5: known difference between the two copies (NPC state)

A mirrored NPC keeps its own AI running (its controller must stay possessed). The copy that owns the NPC sends position, yaw, speed, health, weapon state; what the NPC does with its hands is not sent. So one copy can show an NPC walking while the other shows the same NPC carrying a crate (its local routine). Possible fixes: send the playing montage and the carried item; quiet the mirror's AI; or let the host simulate all AI and the guest only display.

## Addendum 6: the real memory leak (core), NPC animation sync, scan cost

- **Core leak found and fixed (`coopdll_54`).** `FName::ToString` allocates the string in the engine's allocator and the old `fname_str` never freed it (and the function does not reuse a string passed in, so reusing one `FString` did not help either). Every `obj_name` / `fname_to_str` call leaked ~100 bytes; the object scans of the modules make millions of calls. Measured by switching modules off one by one: all modules on +1.2 GB/min for two copies, story off +0.74, story+npc off +0.27, everything off +0.03 GB/min. Fix: every distinct FName (comparison index + number, 8 bytes) is converted once and kept in a hash table with a text arena (2M slots, 96 MB); repeated calls allocate nothing and are faster. With the fix the two copies stay flat (19.3 -> 19.05 GB over 3 minutes, all modules on).
- The function-pointer cache of the core was limited to 64 entries; with five modules it overflowed and later `call_fn` calls silently failed. Now 512.
- **Story module scan cost:** the object scan ran 30000 objects once per second (200-800 ms hitches). It is now time-budgeted per frame (1 ms; 6 ms with `scan_fast.txt`, which tests create and must delete), and far items are not re-examined for 12 s. The class cache is a 32768-entry hash table (the old 300-entry cache recomputed names for most objects).
- **A stale `native/build/npc_off.txt`** (left over from an interrupted experiment) had switched the NPC module off in every run for a long time; removed. Reminder: the switch files in `native/build/` (`npc_off.txt`, `story_off.txt`, `pose_off.txt`, `scan_fast.txt`, ...) must not be left behind.
- **NPC animation sync (npc module).** A mirrored NPC kept running its own AI, so it showed other idle actions than the owner (one copy: guard with crossed arms, the other: another idle montage; one copy carries a crate, the other does not). Now the owner sends the name and package of its body montage; the mirror plays the same montage through its own `AnimInstance` (`Montage_Play`, other montages stopped) and every 700 ms checks that exactly that montage is playing, stopping its own body montages when the owner plays none. Face / voice montages (run-time `AnimMontage_<n>`) are left alone. All montages of an AnimInstance are read from its `MontageInstances` array (at `+0x88`, found with the new probe command `miscan`), because `GetCurrentActiveMontage` often returns a facial montage that hides the body one. Verified with the probe command `npcanim` (prints each NPC's body montages): 10 of 10 NPCs near both heroes show the same montage in both copies. Not verified by eye: smoothness of the blend, NPCs that carry items (the carried item state is not synced; only the montage is), montages that need parameters from the owner's AI.
- New probe commands: `npcanim`, `miscan`.
- Sync test run after the changes: story 6/6, items 8/8, loot 10/10, chests 4/6 (guest -> host emptying not noticed in that run; not investigated).

## Addendum 7 (end of session 2026-10-31)
- Chest test: uses the new one-call probe `chestlist` and waits until both copies track the chest; the last full run of the four sync tests was interrupted before it finished (the watchdog's 25 minute limit stopped the games earlier; the limit is now 60 min). Last known results: story 6/6, items 8/8, loot 10/10, chests 4/6 (flaky: depends on which chest is picked and whether it is loaded in both copies).
- Not verified yet: the NPC montage sync by eye in the game; the loot replay with a take made by hand in the UI after the leak fix.
