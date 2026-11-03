// Spike step 2 (official modkit way, no code injection): the main story game mode of the base game already derives from
// AG1RGameModeCooperative (Script/Story/Support/G1R/G1RGameMode.as) but sets only m_MainPlayerConfig. The scripts ship the player configs
// UPlayerConfigTwo_G1R .. UPlayerConfigFive_G1R (Script/Player/PlayerConfig.as). This hook adds the second player.
// (A BeginPlay with Print showed nothing on screen in the shipped game: use the in-game tool panel in CoopTool.as instead.)
UCLASS(meta=(ReplaceParentClass))
class AG1RMainStoryGameMode_CoopSpike : AG1RMainStoryGameMode
{
    default m_SecondaryPlayerConfig.Add(UPlayerConfigTwo_G1R);
}
