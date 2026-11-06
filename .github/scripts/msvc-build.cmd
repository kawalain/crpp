@echo off
rem Configures and builds with MSVC and Ninja: msvc-build.cmd <build dir> ["-Dname=value" ...]
rem Keep the quotes: without them cmd splits an argument at its equals sign.
rem vcvars puts cl and Ninja on PATH; vswhere finds it on a runner and on a machine with
rem only the Build Tools. /MT makes the exe a single file needing no redistributable.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (
  echo No Visual Studio with the C++ tools was found.
  exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "BUILD=%~1"
shift
set "OPTIONS="
:collect
if "%~1"=="" goto configure
set OPTIONS=%OPTIONS% %1
shift
goto collect
:configure
cmake -S . -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded %OPTIONS% || exit /b 1
cmake --build "%BUILD%" || exit /b 1
