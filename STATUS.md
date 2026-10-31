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
- **Arrows (parked, 2026-10-30):** the bow now shows on the puppet (it needs the hero as spawn owner). The nocked arrow in the hand sits wrongly (the hero's `arrow` socket hangs on an undriven IK bone; mapped to `weapon_r` it points down). The friend's shot is detected and an arrow visual is flown by `equip_module.c` (`shot_tick`, test trigger: create `nativeuild\shoot_test.txt`), positions are applied, but the arrow was not seen on screen (class is a real `ArrowVisual` projectile; maybe hidden until the game starts it). Next: check visibility, drop or fix the nocked arrow, send aim pitch.
- Arrow count on the puppet's quiver can only go down.
- **Resolved (2026-10-30):** equipment / quick-slot bar missing in the second copy was caused by GPU memory pressure (two copies on an 8 GB card); with memory freed everything shows. If it returns, lower texture quality for both copies in the Couchlink recipe.

## Shared world (2026-10-31)

- Host (seat 0) runs the NPC AI and sends id, position, yaw, speed and the active clip of the NPCs near either player (second blob channel, `npc_module.c`). The guest places the mirrored NPCs, plays the host's clip on them (not the additive `FA_*` clips) and disables their collision, so the host alone decides what happens to an NPC.
- Defeated NPCs (`GothicCharacter::IsDefeated`) are bone-driven on the guest: a `PoseableMeshComponent` with the NPC's mesh asset, the original mesh hidden with tick / physics / collision off, items (sword, bow, quiver, pouch) re-attached to it. Bones are applied exactly in one pass (compensating the parent cache of `SetBoneTransformByName`); bones that only exist in the close-up level of detail (face, ribs, scapula ...) stay in the reference pose. Bone-driving sitting or standing NPCs was dropped: their meshes were pulled into spikes once a player came near.
- Verified with the automated test (`tools\test_ko.ps1`, pictures in `docs\img\`): the host hero knocks Ratford out with real game input actions and the guest shows him lying in the same place with his bow and quiver on him; sitting NPCs (Ratford, Drax at the fire) match; repeated quick loads with an NPC down no longer crash (with the UE4SS loader of the test copy disabled: `dwmapi.dll.off`).
- Not done / not verified: the transition frames of falling and getting up (the camera of the test was unreliable), the sockets of the NPC's weapon (the host's mace is in the hand, the guest's stays holstered), health / death shared, the guest's own hits on NPCs (they are ignored, the host decides), arrows in flight.

## Not started

- Real two-PC test of the direct-IP transport (`tools/join.ps1` exists, verified on loopback only), Steam relay, and bone-pose compression for the internet. Postponed until the one-PC sync is finished.
- Shared world: NPC positions and health, shared combat, quests, dialogue, inventory, saves.
- Robustness: hero respawn on level change, late join.
- Game updates: every update changes the RVAs and needs a new recon pass.

## Next steps

1. Test direct IP between two PCs, then pose compression.
2. Puppet polish (face, arrows, more food items).
3. NPC / enemy mirroring for shared fights.
