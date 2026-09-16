mkdir Bin\
copy Dependencies\dll\* Bin\
call "Premake/premake5" --file=Source/GameEditor/workspace.lua vs2022
pause
