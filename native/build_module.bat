@echo off
rem Rebuilds hot-reloadable modules into build\modules\. Usage: build_module.bat [anim|equip|pose|npc|probe]  (default: all). The running game reloads them within ~1 second.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist "%~dp0build\modules" mkdir "%~dp0build\modules"
cd /d "%~dp0build\modules"
set WHAT=%1
if "%WHAT%"=="" set WHAT=all
if "%WHAT%"=="anim" goto anim
if "%WHAT%"=="equip" goto equip
if "%WHAT%"=="pose" goto pose
if "%WHAT%"=="npc" goto npc
if "%WHAT%"=="menu" goto menu
if "%WHAT%"=="story" goto story
if "%WHAT%"=="probe" goto probe
:anim
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /LD "%~dp0anim_module.c" /Fe:anim.dll /Fo:anim_module.obj || exit /b 1
if "%WHAT%"=="anim" exit /b 0
:equip
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /LD "%~dp0equip_module.c" /Fe:equip.dll /Fo:equip_module.obj || exit /b 1
if "%WHAT%"=="equip" exit /b 0
:pose
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /LD "%~dp0pose_module.c" /Fe:pose.dll /Fo:pose_module.obj || exit /b 1
if "%WHAT%"=="pose" exit /b 0
:npc
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /LD "%~dp0npc_module.c" /Fe:npc.dll /Fo:npc_module.obj || exit /b 1
if "%WHAT%"=="npc" exit /b 0
:story
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /LD "%~dp0story_module.c" /Fe:story.dll /Fo:story_module.obj || exit /b 1
if "%WHAT%"=="story" exit /b 0
:menu
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /utf-8 /LD "%~dp0menu_module.c" /Fe:menu.dll /Fo:menu_module.obj || exit /b 1
if "%WHAT%"=="menu" exit /b 0
:probe
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /LD "%~dp0probe_module.c" /Fe:probe.dll /Fo:probe_module.obj || exit /b 1
