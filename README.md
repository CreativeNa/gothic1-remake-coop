# Gothic 1 Remake co-op (experiment)

An unofficial, non-commercial experiment to bring co-op to Gothic 1 Remake. **This project is not made, endorsed or supported by THQ Nordic or Alkimia Interactive.** "Gothic" and the names of the game are trademarks of their owners.

*По-русски:* неофициальный некоммерческий эксперимент по кооперативу для Gothic 1 Remake. Проект не создан, не одобрен и не поддерживается THQ Nordic и Alkimia Interactive.

## State

The project is on hold. Earlier prototypes (with their technical notes, test reports and tools) were taken out of this public repository. What stays here is the part that follows the official modding rules.

## What is here

- [`mod/G1RCoop`](mod/G1RCoop): a small script mod for the official [G1R Modkit](https://github.com/AlkimiaInteractive/G1R-Modkit) (AngelScript, no code injection). It adds the configuration of a second player to the game mode and an in-game tool panel for trying the multiplayer functions that the game ships (they currently crash in the released game, so this is only a probe).
- [`LICENSE`](LICENSE)

## Rules this repository follows

- No game files, game assets, screenshots of the game or extracted data are published here.
- No tools that change the game's program code are published here.
- The mod only uses what the G1R Modkit provides; see its licence for what is allowed.

## Install the probe mod

Copy `mod/G1RCoop` to `<game folder>/G1R/Mods/G1RCoop` and start the game (add the launch option `-ingametools` to get the "Coop" panel in the top menu bar). Back up your saves first.
