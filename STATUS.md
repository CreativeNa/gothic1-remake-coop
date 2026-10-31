# Status

Last updated: 2026-10-30

## Done

- Native DLL injection, engine access by fixed RVAs, game-thread hook, ProcessEvent calls.
- Puppet of the other player (hero mesh), position and rotation sync over UDP (localhost).
- Full-skeleton pose sync (422 bones, chunked UDP datagrams), forced LOD0, face / armor / IK helper bones left in reference pose.
- Equipment mirrored on the puppet (quiver with arrow count, bow, sword, food in hand).
- Hot-reloadable modules (`anim`, `equip`, `pose`) with a small API.
- Connecting by IP: host names or addresses, packets accepted only from the configured friend (`tools/join.ps1`). Tested on one PC only.
- CouchLink recipe for two copies on one PC (`tools/couchlink/`); the two players see each other and animations keep working.

## Known problems

- **Skin stretching / "spike"** on the puppet, mainly seen with the sword drawn. Several causes were found and fixed (bones returning identity at the origin, only part of the bones sent, parent-chain skipping, NaN guard). Not proven fully gone; re-check with the sword drawn from both windows after every pose change.
- `SetBoneTransformByName` works only in component space; children are converted through the parent's cached pose, so fast motion can leave small errors.
- No facial animation; dark patches on the puppet's face in some lighting.
- FPS dips during fights (one engine call per bone).
- **Crash `0xd50`: root cause found, fix in place, needs a long soak test.** The game's own `BFGTickOptimizerSystem` (with `BFGInterpolationSystem`) tracks the puppet actor (`IsActorRegisteredAndActive` = 1) and faults inside its tick (`exe+0x363dfff`, `cmp [rcx+0xd50],0` with a null tracked entry) when the player runs away from it ("hit, then run away"). Bisecting showed: no puppet = no crash; puppet actor alone (no modules) = no crash; a bare `PoseableMeshComponent` or one with only the mesh set = no crash; attached/bone-driven = crash. The pose module now opts the puppet out of the optimizer with `SetTickOptimizationRuntime_Enabled(Actor, false)` (re-checked every 3 s). Details of the old investigation:
- Crash (access violation reading `0xd50`) a few seconds after swinging the sword near the puppet. The AV recorder pins it inside the game itself: `G1R-Win64-Shipping.exe+0x363dfff` is `cmp qword ptr [rcx+0xd50], 0` at the start of a method called with `this == NULL`; no coopdll frames on the stack. Prime suspect: the local sword hits the other player's owner-less item visual or the puppet mesh. Mitigation tried: collision fully disabled on item actors and on the poseable mesh (not yet verified). Previous mitigations: no destroy/respawn of the sword visual (re-attach instead), clip layer paused while the pose module drives the puppet.
- Equipment attachments to the puppet were once floating next to the body; re-check.
- **Arrows (parked, 2026-10-30):** the bow now shows on the puppet (it needs the hero as spawn owner). The nocked arrow in the hand sits wrongly (the hero's `arrow` socket hangs on an undriven IK bone; mapped to `weapon_r` it points down). The friend's shot is detected and an arrow visual is flown by `equip_module.c` (`shot_tick`, test trigger: create `native\build\shoot_test.txt`), positions are applied, but the arrow was not seen on screen (class is a real `ArrowVisual` projectile; maybe hidden until the game starts it). Next: check visibility, drop or fix the nocked arrow, send aim pitch.
- Arrow count on the puppet's quiver can only go down.
- **Resolved (2026-10-30):** equipment / quick-slot bar missing in the second copy was caused by GPU memory pressure (two copies on an 8 GB card); with memory freed everything shows. If it returns, lower texture quality for both copies in the Couchlink recipe.

## Shared world (2026-10-31)

- **Per-NPC ownership** (`npc_module.c`): the copy whose hero is clearly nearer to an NPC (600 units nearer) simulates it (AI, hits, health, knock-out); the other copy mirrors it. Everything starts with seat 0. Both copies run the same code: each sends what it simulates (position, yaw, speed, bones of defeated NPCs) on the second blob channel and mirrors what the other one simulates. So **both players can fight NPCs**: the nearer one hits, the other sees the result.
- Mirrored NPCs have their collision off, and are placed every frame; defeated ones are bone-driven (a `PoseableMeshComponent` with the NPC's mesh asset, original mesh hidden with tick / physics / collision off, items re-attached, exact one-pass bone application, close-up-only helper bones left in the reference pose). Bone-driving non-defeated NPCs and `PlayAnimation` clips were dropped (stretched skin, a crash in the game's animation component).
- Verified by `tools\tests\run_all.ps1` (18 checks, about 8 minutes, no human): boot, NPC positions host vs guest by numbers, the host knocks Ratford out, the guest takes the NPC over when its hero is nearer and knocks it out, the host mirrors it bone-driven at the same place, two quick loads with an NPC down, fps, no exception in our code.
- Health numbers (and death) of an NPC and the weapon in its hand (melee / bow, holster <-> hand) follow the owner on the mirror (`health:` checks of the suite).
- Not done: loot / inventory of a dead NPC is left to each world's own death logic, NPCs that exist in only one copy because of streaming when the players are far apart (the owner has them loaded: ownership by proximity covers this), arrows in flight, two real PCs over the internet.

## Not started

- Real two-PC test of the direct-IP transport (`tools/join.ps1` exists, verified on loopback only), Steam relay, and bone-pose compression for the internet. Postponed until the one-PC sync is finished.
- Shared world: NPC positions and health, shared combat, quests, dialogue, inventory, saves.
- Robustness: hero respawn on level change, late join.
- Game updates: every update changes the RVAs and needs a new recon pass.

## Next steps

1. Test direct IP between two PCs, then pose compression.
2. Puppet polish (face, arrows, more food items).
3. NPC / enemy mirroring for shared fights.
