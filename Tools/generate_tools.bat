@echo off
REM Generates the TextureCooker solution into Build\Solutions.
REM Run from anywhere; paths below are relative to this script.
pushd "%~dp0.."
if not exist "Run\" mkdir "Run"
copy /Y "ThirdParty\Dependencies\dll\*" "Run\" >nul
call "Premake\premake5" --file=Source/TextureCooker/workspace.lua vs2022
popd
pause
