@echo off
REM Build the live-preview host (Win32 + DirectX11). No cmake required.
REM Auto-detects any Visual Studio install (2019/2022/2026, any edition) via vswhere.
setlocal

REM allow override: set VS_VCVARS to your vcvars64.bat to skip detection
if defined VS_VCVARS (
  set "VCVARS=%VS_VCVARS%"
  goto :have_vcvars
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe not found. Install Visual Studio with the
  echo "Desktop development with C++" workload, or set VS_VCVARS to your vcvars64.bat.
  exit /b 1
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo ERROR: no VS install with C++ tools found. Add the
  echo "Desktop development with C++" workload in the VS Installer.
  exit /b 1
)

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
:have_vcvars
if not exist "%VCVARS%" (
  echo ERROR: vcvars64.bat not found at: %VCVARS%
  exit /b 1
)
echo Using VS: %VCVARS%
call "%VCVARS%" >nul

set ROOT=%~dp0
set IMGUI=%ROOT%vendor\imgui
set OUT=%ROOT%build
if not exist "%OUT%" mkdir "%OUT%"

echo Compiling...
cl /nologo /std:c++17 /EHsc /O2 /MD /DNDEBUG ^
  /I "%IMGUI%" /I "%IMGUI%\backends" /I "%IMGUI%\misc\cpp" /I "%ROOT%vendor" ^
  "%ROOT%host\main.cpp" ^
  "%ROOT%host\UiRenderer.cpp" ^
  "%IMGUI%\imgui.cpp" ^
  "%IMGUI%\imgui_draw.cpp" ^
  "%IMGUI%\imgui_tables.cpp" ^
  "%IMGUI%\imgui_widgets.cpp" ^
  "%IMGUI%\imgui_demo.cpp" ^
  "%IMGUI%\backends\imgui_impl_win32.cpp" ^
  "%IMGUI%\backends\imgui_impl_dx11.cpp" ^
  "%IMGUI%\misc\cpp\imgui_stdlib.cpp" ^
  /Fe"%OUT%\imgui_tool.exe" /Fo"%OUT%\\" ^
  /link d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib dwmapi.lib

if %errorlevel% neq 0 (
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK -^> %OUT%\imgui_tool.exe
endlocal
