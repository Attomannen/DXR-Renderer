@echo off
echo ========================================================
echo Building NVIDIA Real-Time Denoisers (NRD) SDK
echo ========================================================

set "CMAKE_PATH=C:\Program Files\CMake\bin\cmake.exe"

if not exist "%CMAKE_PATH%" (
    echo [ERROR] CMake is not installed at "%CMAKE_PATH%". 
    echo Please install CMake and ensure it is in Program Files.
    pause
    exit /B 1
)

REM NRD is a git submodule pinned to v4.17.3; fetch it on a fresh clone.
git submodule update --init ThirdParty/NRD-4.17.3
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Could not fetch the NRD submodule.
    pause
    exit /B %ERRORLEVEL%
)

cd ThirdParty\NRD-4.17.3

echo.
echo [1/3] Deploying Submodules and Generating CMake Files...
call 1-Deploy.bat
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] 1-Deploy.bat failed.
    pause
    exit /B %ERRORLEVEL%
)

REM NRI must not be built with Agility SDK support: the engine runs on the OS
REM D3D12 runtime, and an Agility-built NRI crashes inside D3D12Core.
"%CMAKE_PATH%" -S . -B _Build -DNRI_ENABLE_AGILITY_SDK_SUPPORT=OFF
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake reconfigure failed.
    pause
    exit /B %ERRORLEVEL%
)

echo.
echo [2/3] Building NRD and NRI (This may take 10-15 minutes)...
"%CMAKE_PATH%" --build _Build --config Release -j %NUMBER_OF_PROCESSORS%
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Release build failed.
    pause
    exit /B %ERRORLEVEL%
)
"%CMAKE_PATH%" --build _Build --config Debug -j %NUMBER_OF_PROCESSORS%
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Debug build failed.
    pause
    exit /B %ERRORLEVEL%
)

echo.
echo [3/3] Preparing the SDK folder (_NRD_SDK)...
call 3-PrepareSDK.bat
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] 3-PrepareSDK.bat failed.
    pause
    exit /B %ERRORLEVEL%
)

REM Deploy the runtime DLLs next to the executables.
copy /Y "_Bin\Release\NRD.dll" "..\..\..\Bin\" >nul
copy /Y "_Bin\Release\NRI.dll" "..\..\..\Bin\" >nul

echo.
echo ========================================================
echo SUCCESS! _NRD_SDK has been generated!
echo You can now return to the editor.
echo ========================================================
pause
