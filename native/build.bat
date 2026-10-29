@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist "%~dp0build" mkdir "%~dp0build"
cd /d "%~dp0build"
cl /nologo /O2 /W3 /LD ..\coopdll.c /Fe:coopdll.dll || exit /b 1
cl /nologo /O2 /W3 ..\inject.c /Fe:inject.exe || exit /b 1
