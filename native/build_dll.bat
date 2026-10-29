@echo off
rem usage: build_dll.bat <suffix>  -> build\coopdll_<suffix>.dll (a loaded dll cannot be overwritten)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0build"
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /LD ..\coopdll.c /Fe:coopdll_%1.dll || exit /b 1
