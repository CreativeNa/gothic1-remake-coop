-- UE4SS probe mod for the co-op spike. UNTESTED.
-- F6: print net mode / net driver / players.   F7: host (open <map>?listen).   F8: join 127.0.0.1.
local function run_console(cmd)
    ExecuteInGameThread(function()
        local pc = FindFirstOf("PlayerController")
        if not pc:IsValid() then print("[G1RCoop] no PlayerController") return end
        local ksl = StaticFindObject("/Script/Engine.Default__KismetSystemLibrary")
        ksl:ExecuteConsoleCommand(pc, cmd, pc)
        print("[G1RCoop] console: " .. cmd)
    end)
end

RegisterKeyBind(Key.F6, function()
    ExecuteInGameThread(function()
        local world = FindFirstOf("World")
        if world:IsValid() then
            print("[G1RCoop] world: " .. world:GetFullName())
            print("[G1RCoop] NetDriver valid: " .. tostring(world.NetDriver:IsValid()))
        end
        local pcs = FindAllOf("PlayerController") or {}
        print("[G1RCoop] PlayerControllers: " .. #pcs)
        for i, pc in ipairs(pcs) do print("  " .. i .. ": " .. pc:GetFullName()) end
    end)
end)

RegisterKeyBind(Key.F7, function()
    ExecuteInGameThread(function()
        local world = FindFirstOf("World")
        local gs = StaticFindObject("/Script/Engine.Default__GameplayStatics")
        print("[G1RCoop] OpenLevel MenuMap ?listen")
        gs:OpenLevel(world, FName("/Game/Maps/Multiplayer/MenuMap"), true, "listen")
    end)
end)

RegisterKeyBind(Key.F8, function() run_console("open 127.0.0.1") end)
print("[G1RCoop] probe loaded")

-- F9: list loaded objects whose full name hints at networking/multiplayer.
RegisterKeyBind(Key.F9, function()
    ExecuteInGameThread(function()
        local seen, n = {}, 0
        ForEachUObject(function(obj)
            local ok, name = pcall(function() return obj:GetFullName() end)
            if ok and name and (name:find("Multiplayer") or name:find("OnlineSubsystem") or name:find("NetDriver")
                or name:find("SteamSockets") or name:find("OnlineSession") or name:find("Lobby")) then
                if n < 120 and not seen[name] then seen[name] = true; n = n + 1; print("[G1RCoop] obj: " .. name) end
            end
        end)
        print("[G1RCoop] F9 done, matches listed: " .. n)
    end)
end)

-- F10: dump members of the game's multiplayer classes and every object with "Multiplayer" in its name.
local function dump_class(out, path)
    local c = StaticFindObject(path)
    if not c or not c:IsValid() then out:write("MISSING " .. path .. "\n") return end
    local sup = c:GetSuperStruct()
    out:write("== " .. path .. "  super=" .. ((sup and sup:IsValid()) and sup:GetFullName() or "?") .. "\n")
    pcall(function() c:ForEachProperty(function(p) out:write("  P " .. p:GetFName():ToString() .. " : " .. p:GetClass():GetFName():ToString() .. "\n") end) end)
    pcall(function() c:ForEachFunction(function(f) out:write("  F " .. f:GetFName():ToString() .. "\n") end) end)
end

RegisterKeyBind(Key.F10, function()
    ExecuteInGameThread(function()
        local dir = "Mods/G1RCoopProbe/"
        local out = io.open(dir .. "dump_mp.txt", "w")
        if not out then print("[G1RCoop] cannot open dump file") return end
        for _, path in ipairs({
            "/Script/G1R.MultiplayerGameMode", "/Script/G1R.MultiplayerGameSession",
            "/Script/G1R.OnlineSessionResult", "/Script/G1R.OnlineSessionFindResult",
            "/Script/OnlineSubsystemSteam.SteamNetDriver", "/Script/Engine.GameModeBase", "/Script/Engine.GameInstance",
        }) do dump_class(out, path) end
        out:write("\n== objects containing 'Multiplayer' (not /Script/OnlineSubsystem*)\n")
        local seen = {}
        ForEachUObject(function(obj)
            local ok, name = pcall(function() return obj:GetFullName() end)
            if ok and name and name:find("Multiplayer") and not name:find("OnlineSubsystem") and not seen[name] then
                seen[name] = true; out:write(name .. "\n")
            end
        end)
        out:close()
        print("[G1RCoop] F10 dump written")
    end)
end)

-- F11: who is the game mode, and what are the multiplayer function signatures?
RegisterKeyBind(Key.F11, function()
    ExecuteInGameThread(function()
        local out = io.open("Mods/G1RCoopProbe/mp_sigs.txt", "w")
        local gm = FindFirstOf("GameModeBase")
        out:write("GameModeBase instance: " .. ((gm and gm:IsValid()) and gm:GetFullName() or "none") .. "\n")
        local mp = FindFirstOf("MultiplayerGameMode")
        out:write("MultiplayerGameMode instance: " .. ((mp and mp:IsValid()) and mp:GetFullName() or "none") .. "\n")
        for _, fn in ipairs({"CreateSession","FindSessions","JoinSession","StartGame","EndSession","GetAvailableSessions","HasLobbyMap","GetGameMapSoftPtr"}) do
            local f = StaticFindObject("/Script/G1R.MultiplayerGameMode:" .. fn)
            out:write("fn " .. fn .. ":\n")
            if f and f:IsValid() then
                pcall(function() f:ForEachProperty(function(p) out:write("   " .. p:GetFName():ToString() .. " : " .. p:GetClass():GetFName():ToString() .. "\n") end) end)
            end
        end
        local gi = FindFirstOf("GameInstance")
        out:write("GameInstance: " .. ((gi and gi:IsValid()) and gi:GetFullName() or "none") .. "\n")
        out:close()
        print("[G1RCoop] F11 written")
    end)
end)

-- Multiplayer menu-flow probes (the game's own MultiplayerGameMode on the menu map).
local function mp() local g = FindFirstOf("MultiplayerGameMode"); if g and g:IsValid() then return g end end
local function safe(f) local ok, v = pcall(f); if ok then return tostring(v) else return "ERR(" .. tostring(v):sub(1, 60) .. ")" end end
local function status(tag)
    local g = mp(); if not g then print("[G1RCoop] " .. tag .. ": no MultiplayerGameMode") return end
    print("[G1RCoop] " .. tag .. ": HasSession=" .. safe(function() return g:HasCurrentSession() end)
        .. " HasLobbyMap=" .. safe(function() return g:HasLobbyMap() end)
        .. " MaxPlayers=" .. safe(function() return g.MaxNumPlayers end)
        .. " Lobby=" .. safe(function() return g.LobbyMapName:ToString() end)
        .. " Game=" .. safe(function() return g.GameMapName:ToString() end)
        .. " Main=" .. safe(function() return g.MainMapName:ToString() end)
        .. " SessionId=" .. safe(function() local r = g:GetCurrentSessionIdStr(); return type(r) == "string" and r or r:ToString() end))
end
RegisterKeyBind(Key.F12, function() ExecuteInGameThread(function() status("before"); local g = mp(); if g then print("[G1RCoop] CreateSession(LAN)"); g:CreateSession(true) end end) end)
RegisterKeyBind(Key.F5, function() ExecuteInGameThread(function() local g = mp(); if g then g:FindSessions() end; status("find-started") end) end)
RegisterKeyBind(Key.F4, function() ExecuteInGameThread(function() status("status"); local g = mp(); if g then local r = g:GetAvailableSessions(); print("[G1RCoop] available sessions: " .. #r); for i = 1, #r do local s = r[i]:get(); print("  " .. i .. ": " .. s.SessionId:ToString() .. " " .. s.serverName:ToString()) end end end) end)
RegisterKeyBind(Key.F3, function() ExecuteInGameThread(function() local g = mp(); if g then print("[G1RCoop] StartGame"); g:StartGame() end end) end)

-- ===== Path 2 (own transport): puppet experiments. Load a save first. =====
local function hero()
    local pcs = FindAllOf("PlayerController") or {}
    for _, pc in ipairs(pcs) do
        local ok, pawn = pcall(function() return pc.Pawn end)
        if ok and pawn and pawn:IsValid() then return pc, pawn end
    end
end

-- F1: describe the hero pawn (class chain, location, rotation, components).
_G.G1RCoop_hero = function()
    ExecuteInGameThread(function()
        local out = io.open("Mods/G1RCoopProbe/hero.txt", "w")
        local NL = string.char(10)
        local function try(label, fn)
            local ok, v = pcall(fn)
            out:write(label .. ": " .. (ok and tostring(v) or ("ERR " .. tostring(v):sub(1, 120))) .. NL)
            out:flush()
            return ok and v or nil
        end
        local pc, pawn = hero()
        if not pawn then out:write("no pawn" .. NL) out:close() print("[G1RCoop] F1: no pawn (load a save first)") return end
        try("PC", function() return pc:GetFullName() end)
        try("Pawn", function() return pawn:GetFullName() end)
        try("classchain", function()
            local t, c = {}, pawn:GetClass()
            while c and c:IsValid() do t[#t + 1] = c:GetFullName(); c = c:GetSuperStruct() end
            return table.concat(t, NL .. "    <- ")
        end)
        try("RootComponent", function() return pawn.RootComponent:GetFullName() end)
        try("RelativeLocation", function() local l = pawn.RootComponent.RelativeLocation; return string.format("%.1f %.1f %.1f", l.X, l.Y, l.Z) end)
        try("K2_GetActorLocation", function() local l = pawn:K2_GetActorLocation(); return string.format("%.1f %.1f %.1f", l.X, l.Y, l.Z) end)
        try("K2_GetActorRotation", function() local r = pawn:K2_GetActorRotation(); return string.format("%.1f %.1f %.1f", r.Pitch, r.Yaw, r.Roll) end)
        try("Mesh", function() return pawn.Mesh:GetFullName() end)
        try("components", function()
            local comps = pawn:K2_GetComponentsByClass(StaticFindObject("/Script/Engine.SceneComponent"))
            local t = {}
            for i = 1, #comps do t[#t + 1] = comps[i]:get():GetFullName() end
            return #comps .. NL .. "    " .. table.concat(t, NL .. "    ")
        end)
        out:close()
        print("[G1RCoop] F1 hero.txt written")
    end)
end
RegisterKeyBind(Key.F1, _G.G1RCoop_hero)

-- F2: try to spawn a bare copy of the hero's class 300uu in front of it (puppet feasibility).
_G.G1RCoop_spawn = function()
    ExecuteInGameThread(function()
        local pc, pawn = hero()
        if not pawn then print("[G1RCoop] F2: no pawn") return end
        local loc, rot = pawn:K2_GetActorLocation(), pawn:K2_GetActorRotation()
        local fwd = pawn:GetActorForwardVector()
        local at = { X = loc.X + fwd.X * 300, Y = loc.Y + fwd.Y * 300, Z = loc.Z }
        local world = pawn:GetWorld()
        local puppet = world:SpawnActor(pawn:GetClass(), at, rot)
        print("[G1RCoop] F2 spawn: " .. ((puppet and puppet:IsValid()) and puppet:GetFullName() or "FAILED"))
    end)
end
RegisterKeyBind(Key.F2, _G.G1RCoop_spawn)

-- Command file: Mods/G1RCoopProbe/cmd_<ID>.txt (ID from env G1RCOOP_ID, default "A").
-- First line "hero" / "spawn" / "net" (start net layer) / "netstop", or "lua" followed by Lua code to run on the game thread.
-- Helpers inside eval: hero(), out(...) appends to eval_out_<ID>.txt. Dev tool for the local test copy only.
local ID = os.getenv("G1RCOOP_ID") or "A"
local NL = string.char(10)
local DIR = "Mods/G1RCoopProbe/"
-- All Lua work runs on the game thread; the async timer only schedules it (UE4SS Lua is not thread safe).
local function loop_gt(ms, fn) LoopAsync(ms, function() ExecuteInGameThread(fn) return false end) end

local function eval_lua(code)
    local f = io.open(DIR .. "eval_out_" .. ID .. ".txt", "w")
    _G.out = function(...) local t = {} for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end f:write(table.concat(t, " ") .. NL) f:flush() end
    _G.hero = hero
    local fn, err = load(code, "eval", "t", _G)
    if not fn then f:write("COMPILE ERR " .. tostring(err) .. NL) f:close() return end
    local ok, e = pcall(fn)
    if not ok then f:write("RUN ERR " .. tostring(e) .. NL) end
    f:close()
end

-- ===== Net layer: send my hero state, show the remote player as a ghost cube. =====
local net = { on = false, seq = 0, ghost = nil, lastSeq = -1, rx = 0 }

local function read_state_file(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local s = f:read("*l")
    f:close()
    return s
end

local function make_ghost(world, p)
    local cls = StaticFindObject("/Script/Engine.StaticMeshActor")
    local g = world:SpawnActor(cls, { X = p.x, Y = p.y, Z = p.z }, { Pitch = 0, Yaw = 0, Roll = 0 })
    if not g or not g:IsValid() then return nil end
    local comp = g.StaticMeshComponent
    pcall(function() comp.Mobility = 2 end)
    local mesh = StaticFindObject("/Engine/BasicShapes/Cube.Cube")
    if not (mesh and mesh:IsValid()) then pcall(function() mesh = LoadAsset("/Engine/BasicShapes/Cube.Cube") end) end
    pcall(function() comp.StaticMesh = mesh end)
    pcall(function() comp.RelativeScale3D = { X = 0.6, Y = 0.6, Z = 1.8 } end)
    return g
end

local function net_tick()
    local _, pawn = hero()
    if not pawn then return end
    local l = pawn.RootComponent.RelativeLocation
    local r = pawn.RootComponent.RelativeRotation
    net.seq = net.seq + 1
    local line = string.format("P|%s|%.2f|%.2f|%.2f|%.1f|%d", ID, l.X, l.Y, l.Z, r.Yaw, net.seq)
    local f = io.open(DIR .. "net/out_" .. ID .. ".txt", "w")
    if f then f:write(line) f:close() end

    local s = read_state_file(DIR .. "net/in_" .. ID .. ".txt")
    if not s then return end
    local pid, x, y, z, yaw, seq = s:match("^P|([^|]+)|([^|]+)|([^|]+)|([^|]+)|([^|]+)|([^|]+)$")
    if not pid or tonumber(seq) == net.lastSeq then return end
    net.lastSeq = tonumber(seq); net.rx = net.rx + 1
    local p = { x = tonumber(x), y = tonumber(y), z = tonumber(z), yaw = tonumber(yaw) }
    if not (net.ghost and net.ghost:IsValid()) then net.ghost = make_ghost(pawn:GetWorld(), p) end
    if net.ghost and net.ghost:IsValid() then
        local ok = pcall(function()
            net.ghost.RootComponent.RelativeLocation = { X = p.x, Y = p.y, Z = p.z + 90 }
            net.ghost.RootComponent.RelativeRotation = { Pitch = 0, Yaw = p.yaw, Roll = 0 }
        end)
        if net.rx % 40 == 1 then print(string.format("[G1RCoop] rx %d from %s at %.0f,%.0f,%.0f ok=%s", net.rx, pid, p.x, p.y, p.z, tostring(ok))) end
    end
end

local function net_start()
    if net.on then return end
    net.on = true
    print("[G1RCoop] net start id=" .. ID)
    loop_gt(50, function()
        if not net.on then return end
        local ok, e = pcall(net_tick)
        if not ok then net.errs = (net.errs or 0) + 1; if net.errs <= 5 then print("[G1RCoop] net_tick error: " .. tostring(e):sub(1, 150)) end end
    end)
end

loop_gt(1000, function()
    local f = io.open(DIR .. "cmd_" .. ID .. ".txt", "r")
    if f then
        local all = f:read("*a"); f:close()
        os.remove(DIR .. "cmd_" .. ID .. ".txt")
        local first, rest = all:match("^%s*(%S+)%s*(.*)$")
        print("[G1RCoop] cmd[" .. ID .. "]: " .. tostring(first))
        if first == "hero" then _G.G1RCoop_hero()
        elseif first == "spawn" then _G.G1RCoop_spawn()
        elseif first == "net" then net_start()
        elseif first == "netstop" then net.on = false
        elseif first == "lua" then ExecuteInGameThread(function() eval_lua(rest) end) end
    end
    return false
end)
print("[G1RCoop] probe ready, id=" .. ID)
