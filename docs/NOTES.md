# Gothic 1 Remake co-op: notes

Layout: `mod/G1RCoop` (AngelScript mod), `tools/` (setup, launch, UE4SS probe). Game copy goes to `game-test/` (made by `tools/setup_test.ps1`; the original install is never touched).

## Facts from the modkit sources (read-only, not yet run)
- Engine UE 5.4.3 + Hazelight AngelScript. Mods: `G1R/Mods/<name>/GothicMod.json` + `Script/`. `-Mods=`, `-console`, `-as-development-mode`.
- Scripts already use `HasAuthority`, `bReplicates`, `Replicated`, `NetMulticast`, `OnRep_`; damage uses PlayerController/IsLocalController. `m_IsReplicated = true` on `UMainPlayerPersistentDataSubsystemFinal`.
- Quests/dialogs are bound to the unique name `HERO`; AI uses `GetPlayerCharacter(0)` / `AIScript::LocalPlayer`.
- Companions are AI follow states (`UAIState_FollowCharacter`), not player-controlled. `GA_Spell_Control` applies effects only; the actual possession is native.

## Spike checklist (needs the game installed)
1. `setup_test.ps1`, then `play.ps1`. Check `-as-development-mode` log: does `[G1RCoop] PlayerStartLessons` print, with HasAuthority=true?
2. In game: F6 prints NetDriver/PlayerControllers. Try console `open <map>?listen`, then on a second instance `open 127.0.0.1`.
3. Record: does the client connect? Does a second PlayerController/hero spawn? Which actors replicate? Is a second instance allowed (Steam/EOS single-instance)?
4. Decide: engine netcode (next: puppet via Control-like possession, owner-per-NPC, quest sync via HERO) or custom UE4SS transport.

Everything in `mod/` and `tools/` is untested.

## Decision (path 2)
Engine listen crashes on every attempt (access violation at 0x1d8 with the game's driver, 0x80 with IpNetDriver forced; CreateSession also 0x1d8). Going with an own transport via UE4SS:
1. Puppet: second hero shown from received state (F1 dumps the hero, F2 tries a bare spawn of the hero class).
2. Transport: Lua has no sockets -> C++ UE4SS mod (UDP localhost for tests, then Steam relay, see coop-anything networking-steam-spacewar.md).
3. Then: owner-per-NPC, combat/death, quest flags (quests bind to HERO), late-join snapshot, two-client bot tests.

## Native DLL path (works for reading) - verified 2026-10-29 on exe size 171704320, image base 0x7FF696F10000
Injected with native/build/inject.exe <pid> <dll>; the DLL (native/coopdll.c) logs to native/build/coopdll.log. RVAs (from UE4SS.log): GUObjectArray 0x9B9D440, FName::ToString 0x11CE2A0, FName(wchar_t*) 0x11AFA80, StaticConstructObject_Internal 0x13D0460, ProcessEvent 0x13A2920, GNatives 0x9B9BED0, GMalloc 0x9AAE568.
- FUObjectArray.ObjObjects at +0x10 (chunk table; items 24 bytes, 65536 per chunk). 453202 objects at menu.
- UObject: class +0x10, FName +0x18 (UObject header 0x28). UStruct: SuperStruct +0x40, ChildProperties +0x50. FField: Next +0x18, FName +0x20; FProperty Offset_Internal at name+0x24 (= +0x44).
- Hero = PlayerCharacterBP_C (an instance already exists on the menu map). AActor::RootComponent +416 (0x1A0); Character: Mesh +792, CharacterMovement +800, CapsuleComponent +808; USceneComponent RelativeLocation +296 (0x128, doubles), RelativeRotation +320.
- Read position confirmed live: 117112.9, -84591.2, -5055.5, yaw 51.1.
Lua UE4SS findings: struct-valued UFunction calls and TArray element access fail ("Array failed invariants"); Lua timers must only schedule ExecuteInGameThread; EngineTick hook gets removed after a hook error; Ctrl+R does not restore it.

## Milestone: position sync between two windows (2026-10-29) - VERIFIED by the user on screen
Native DLL (native/coopdll.c + n2.inc) injected into both instances (tools/play_net.ps1). The game-thread hook is vtable slot 95 of UGameEngine (Tick). Calls go through ProcessEvent (RVA 0x13A2920) with parameter layouts read from the UFunction property chain (doubles for FVector, FHitResult 248 bytes, FTransform 96 bytes).
- Verified: K2_GetActorLocation via ProcessEvent == direct memory read; BeginDeferredActorSpawnFromClass + FinishSpawningActor spawn a StaticMeshActor; SetStaticMesh / SetMobility(Movable) / SetWorldScale3D / K2_SetActorLocation work; the cube is visible.
- UDP 20 Hz, loopback A:40001 <-> B:40002, text packet "P|id|x|y|z|yaw|seq"; each window shows the other player as a cube following their hero. ~2200 packets exchanged without errors.
- Not done: remote rotation/animation, a real character puppet (a bare spawned PlayerCharacterBP_C was hidden/invisible earlier via Lua; untested natively), Steam transport (only localhost UDP), NPC/quest sync, second player's interactions. RVAs hold only for exe size 171704320.

## Open problems: bone-driven puppet (modules/pose, 2026-10-30)
The puppet copies the hero's real skeleton instead of guessed clips (native/pose_module.c). It works (stances, crouch, sword draw, jump match 1:1) but is NOT finished. Known problems:
- Skin stretching / a long brown "spike" from head to ground on the puppet, seen mostly with the sword drawn. Flickered, changed shape between builds. Causes found and fixed so far: face bones returning identity-at-origin (hero mesh gives no pose for 79+ bones: face_*, armor, ribs, hood, ik_* ... -> receiver skips them), only 120 of 422 bones sent (fixed, all sent), unchanged-parent skip (now follows the parent chain). Not proven what removed the spike for good; one screenshot with the sword drawn looked correct. Re-check after every pose change with the sword drawn, from both windows.
- SetBoneTransformByName only works in component space (modes 1/2 in build\pose_mode.txt give a pile of bones / invisible puppet; mode 3 = two passes). Children are converted through the parent's cached pose, so fast motion can still leave small errors.
- Dark patches on the puppet's face and no facial animation (face bones are skipped = reference pose).
- Cost: every bone is one ProcessEvent call (422 reads per send + changed bones per receive); FPS dips appear in fights. Ideas: send only changed bones, lower rate for fingers/face, quantize more.
- Crashes (access violation reading 0xd50 / 0x108, no engine log available) happened while swinging the sword and moving. Suspected: clip playback on the hidden mesh (now paused while the pose module hides it), then spawn failures of item visuals (retry throttled). Not confirmed.
- Items (quiver, bow, sword) attach to the poseable mesh sockets; they were once detached/floating; re-check.
Debug switches (build\): pose_off.txt (back to clips), pose_mode.txt (0..3), pose_skip.txt ("min=N;substr;..." bones the receiver ignores), equip_off.txt (no items on the puppet), try_clip.txt (play a clip by name; "!" prefix freezes at the end). Screenshots of both monitors can be taken from PowerShell (CopyFromScreen) for checking visually.


## Shared world: recon (2026-10-30)

- NPCs and monsters are `AIAgentCharacter` > `GothicCharacter` > `ClientAuthoritativeCharacter` > `Character` (humans: `AIAgentCharacter_Human_Base_C`, animals e.g. `AIAgentCharacter_Scavenger_Young_C`). Probe command `npcs <radius> <max>` lists them with distance and class chain.
- Instance names are stable and spawn-point based (`Character_NC_ORG_Ratford_818-WP_ExF_RATFORD_SPAWN`, `Character_ScavengerYoung-WP_ExF_HUT_SCAVENGER_SPAWN_01-1`), so two copies that loaded the same save can match an NPC by name.
- An NPC is driven by a `GothicAIController` (`Pawn.Controller`). `Controller.UnPossess()` leaves the pawn standing with no AI; `Actor.K2_SetActorLocation` then moves it and it stays there. So the guest can freeze the AI of mirrored NPCs and place them from the host's data.
- Plan (host-authoritative): seat 0 / the host runs the AI and sends id, position, rotation, speed, health of NPCs near either player over a second blob channel; the guest unpossesses those NPCs and applies the data (later the pose blob, death, and the guest's hits sent to the host).

## Known game crash: fmod, G1R-Win64-Shipping.exe+0x5be3977 (2026-10-31)
Access violation reading 0x200, stack through fmodstudio.dll, "our code at that moment: call=none". Seen by the user and twice in the ownership fuzz test (rapid hero teleports around the camp).
Not yet checked whether it also happens with the NPC module off (todo: fuzz with native/build/npc_off.txt). Treat as a game bug until shown otherwise.
