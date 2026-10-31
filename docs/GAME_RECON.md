# Game recon notes

What was found out about Gothic 1 Remake's internals (UE 5.4, Hazelight AngelScript). Names are as the engine reports them; they are only verified in the game version this project was tested with.

## Story and quests
- `StoryG1R` object: one flat block of ints/time values (`block @912, 519 words` in the tested build) plus `Chapter` (property of the parent `GameStory`, offset 80 in the object). `Diego_After_Gamestart` becomes non-zero when the start conversation with Diego is over (used for the join gate).
- Quests: `Instance_Quest_*` objects with state bytes (`IsRunning`, `IsAvailable`, `HasSucceeded`, `HasFailed`, `HasBeenStarted`, `IsCompleted`); written back through the state bytes.
- Chapter banner: `GameStory::RequestChapterHUDNotification(NewChapter)`. Calling it shows the banner. The game's real trigger does not go through the observed UFunction: detect the chapter number change instead (0 -> 1 is the first chapter). A save that already has chapter 1 never shows the banner again.

## Items, chests, loot
- Chests and bodies hold their items in a `DataModule_Container`; taking an item calls `Server_ConsumeItem(type, item id, amount)`. The core replays it on the other copy. Putting items **in** is not synchronised yet.
- World items have a persistent id, or are keyed by class + position. Pouch-like items are excluded. Ambient birds are removed on the mirror (file `ambient_keep.txt` keeps them).

## Cutscenes
- `GothicCinematicManagerSubsystem`: `ForceStartCinematic`, `Server_OnForceStartCinematic(class)`, `Server_OnSkipCutsceneRequest`, `StopCinematicServer`. Replaying start/skip/stop on the other copy synchronises cutscenes. `StopCinematicServer` faults when no cinematic runs: guard it. The Extro cutscene is the credits (last). Heavy cutscenes (e.g. FireMages) add several GB of RAM.

## Main menu
- Widget `W_MainMenu_C` (parent `MainMenuWidget`); buttons are `W_MainMenuButton2_C` with `Text_Label` and `Button Text`. A button is created with `WidgetBlueprintLibrary.Create`, inserted into the vertical box that holds "Load game" and moved with `PanelWidget.AddChild/RemoveChild/GetChildAt`. The text is set with `KismetTextLibrary.Conv_StringToText` + `TextBlock.SetText`. After a trip into a game and back an older menu object can still exist: use the newest one. Click: poll `ButtonWidget::IsPressed`.
- Game languages (localization keys): english, german, french, spanish, schinese, italian, japanese, polish, brazilian, russian; the engine language comes from `KismetInternationalizationLibrary.GetCurrentLanguage`.

## Engine traps
- The core un-hides the puppet *actor* every tick (a guard against the game hiding spawned characters): hide the pose *component* instead.
- The audio predicate crashes on a null argument (patched at a fixed exe offset, valid for the tested exe only).
- `-nosound` crashes the audio library; do not use it.
- A function-name conversion leaked memory until it was cached.
- Scanning every object takes tens of milliseconds: never do it per frame.
- Hot reload does not work for the main menu: restart the game.

## Memory
Two full copies need ~15-17 GB of RAM (`-Lite` lowers textures and resolution). Loading a save takes ~13 GB per copy for about a minute: start the second copy after the first is in the game.
