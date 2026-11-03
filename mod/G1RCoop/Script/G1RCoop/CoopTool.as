// Spike step 3: an in-game tool panel (official modkit feature, see Documentation/ingame-tools). It shows what the game mode reports and has
// buttons for the session functions of AMultiplayerGameMode, so the shipped co-op can be tried without reading logs.
// Enable the in-game tools in the mod manager or start the game with the command-line flag -ingametools; the panel is in the top menu bar under "Coop".
class UIngameTool_CoopSpike : UIngameTool
{
    default Category = "Coop";
    default MenuPath = "Coop.Spike";
    default Title = "Coop spike";

    UFUNCTION(BlueprintOverride)
    void Initialize()
    {
    }

    UFUNCTION(BlueprintOverride)
    void DrawImGuiWindow()
    {
        AGameModeBase gm = Gameplay::GetGameMode();
        if (gm == nullptr)
        {
            ImGui::Text("no game mode");
            return;
        }
        ImGui::Text(f"game mode: {gm.GetName()}");
        AMultiplayerGameMode mp = Cast<AMultiplayerGameMode>(gm);
        if (mp == nullptr)
        {
            ImGui::Text("this is not a multiplayer game mode");
            return;
        }
        ImGui::Text(f"max players {mp.MaxNumPlayers}, lobby map {mp.HasLobbyMap()}, session {mp.HasCurrentSession()}");
        if (ImGui::Button("CreateSession (LAN)"))
        {
            mp.CreateSession(true);
        }
        if (ImGui::Button("CreateSession (online)"))
        {
            mp.CreateSession(false);
        }
        if (ImGui::Button("FindSessions"))
        {
            mp.FindSessions();
        }
        // Direct connect through the engine's own travel (no online subsystem): the host reopens the current map as a listen server,
        // the guest joins by IP. EXPERIMENTAL: this reloads the map on the host.
        APlayerController pc = Gameplay::GetPlayerController(0);
        if (pc != nullptr)
        {
            if (ImGui::Button("HOST: open ?listen"))
            {
                System::ExecuteConsoleCommand("open ?listen", pc);
            }
            if (ImGui::Button("JOIN: open 127.0.0.1"))
            {
                System::ExecuteConsoleCommand("open 127.0.0.1", pc);
            }
        }
    }
}
