# Status

Last updated: 2026-10-30

## Done

- Native DLL injection, engine access by fixed RVAs, game-thread hook, ProcessEvent calls.
- Puppet of the other player (hero mesh), position and rotation sync over UDP (localhost).
- Full-skeleton pose sync (422 bones, chunked UDP datagrams), forced LOD0, face / armor / IK helper bones left in reference pose.
- Equipment mirrored on the puppet (quiver with arrow count, bow, sword, food in hand).
- Hot-reloadable modules (`anim`, `equip`, `pose`) with a small API.

## Known problems

- **Skin stretching / "spike"** on the puppet, mainly seen with the sword drawn. Several causes were found and fixed (bones returning identity at the origin, only part of the bones sent, parent-chain skipping, NaN guard). Not proven fully gone; re-check with the sword drawn from both windows after every pose change.
- `SetBoneTransformByName` works only in component space; children are converted through the parent's cached pose, so fast motion can leave small errors.
- No facial animation; dark patches on the puppet's face in some lighting.
- FPS dips during fights (one engine call per bone).
- Occasional crashes (access violation at `0xd50` / `0x108`) while swinging the sword and moving; no engine log is written, cause unconfirmed.
- Equipment attachments to the puppet were once floating next to the body; re-check.
- Arrow count on the puppet's quiver can only go down.

## Not started

- Transport between different PCs (direct UDP by IP, Steam relay) and bone-pose compression for the internet.
- Shared world: NPC positions and health, shared combat, quests, dialogue, inventory, saves.
- Robustness: hero respawn on level change, late join.
- Game updates: every update changes the RVAs and needs a new recon pass.

## Next steps

1. Direct IP transport and pose compression, so two PCs can meet.
2. Puppet polish (face, arrows, more food items).
3. NPC / enemy mirroring for shared fights.
