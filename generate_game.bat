mkdir Bin\
copy Dependencies\dll\* Bin\
call "Premake/premake5" --file=Source/GameMain/workspace.lua vs2022
pause
