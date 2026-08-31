@echo off
if not exist build mkdir build
clang++ -std=c++23 -g -gcodeview -o build\game.exe main.cpp ^
	-Ithird_party\SDL3-3.4.14\include ^
	third_party\SDL3-3.4.14\lib\x64\SDL3.lib ^
	-Wl,/subsystem:windows ^
	-Wl,/subsystem:console

copy /Y third_party\SDL3-3.4.14\lib\x64\SDL3.dll build\ >nul
