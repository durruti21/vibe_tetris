@echo off
setlocal
pushd "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Could not find vswhere.exe. Install Visual Studio Build Tools with the C++ workload.
    popd
    exit /b 1
)

set "VSPATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if not defined VSPATH (
    echo Visual Studio C++ build tools were not found.
    popd
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo Failed to initialize the MSVC environment.
    popd
    exit /b 1
)

cl /nologo /W4 /EHsc /std:c++17 /O2 /DUNICODE /D_UNICODE audio.cpp game.cpp ^
   multiplayer.cpp net.cpp main.cpp ^
   /link /SUBSYSTEM:WINDOWS /OUT:tetris.exe winmm.lib ws2_32.lib
if errorlevel 1 (
    echo.
    echo Build failed.
    popd
    exit /b 1
)

echo.
echo Built tetris.exe
popd
endlocal
