@echo off
REM ============================================================
REM  Reprise - NON-INTERACTIVE build wrapper (INTERNAL / agent use)
REM  Same toolchain environment as "+Build (release)+.bat" (aqt Qt
REM  6.11.2 kit + VS-bundled cmake/ninja first + Git bash + MSYS2
REM  stripped from PATH) but with NO deploy/launch/pause prompts,
REM  so an automated/headless agent can build without hanging.
REM
REM  WHY: building via raw ninja_build.bat from a plain shell lets a
REM  clean CMake reconfigure grab MSYS2 clang64's Vulkan/fmt and
REM  poison CMakeCache (see Guide ci-and-release.md 4.1). This wrapper
REM  guarantees the VS-first toolchain + the MSYS2 strip every time.
REM  AGENTS: call THIS, not raw ninja_build.bat.
REM
REM  This is NOT a user-facing "+Name+" helper - Andy runs the
REM  +Build / +Run / +Deploy / +Update scripts; this one is for
REM  internal automated builds only.
REM
REM  Usage:  "reprise-agent-build.bat"            Release (default)
REM          "reprise-agent-build.bat" -t debug   pass a target through
REM ============================================================
setlocal
set "QT_KIT=C:\Dev\Qt\6.11.2\msvc2022_64"
if not exist "%QT_KIT%\lib\cmake\Qt6\qt.toolchain.cmake" (
  echo *** Qt 6.11.2 kit not found at %QT_KIT%
  exit /b 1
)
set "QT_DIR=%QT_KIT%"
set "CMAKE_PREFIX_PATH=%QT_KIT%"
set "VS_CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "VS_NINJA=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
set "VS_INSTALLER=C:\Program Files (x86)\Microsoft Visual Studio\Installer"
set "GIT_BIN=C:\Program Files\Git\bin"
set "PATH=%VS_CMAKE%;%VS_NINJA%;%VS_INSTALLER%;%QT_KIT%\bin;%GIT_BIN%;%PATH%"

REM Strip MSYS2 (clang64/mingw/usr) from PATH - a clean CMake reconfigure
REM otherwise lets find_package grab msys64's Vulkan/fmt and poisons the
REM MSVC compile (see Guide ci-and-release.md 4.1). Full-path PowerShell
REM (no PATH dependency); for /f avoids set /p's ~1KB length cap.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -Command "($env:PATH -split ';' | Where-Object { $_ -and ($_ -notmatch 'msys64') }) -join ';'" > "%TEMP%\reprise_clean_path.txt"
for /f "usebackq delims=" %%p in ("%TEMP%\reprise_clean_path.txt") do set "PATH=%%p"

cd /d "C:\Dev\+MuseScore+"

if "%~1"=="" (
  call "C:\Dev\+MuseScore+\ninja_build.bat" -t release
) else (
  call "C:\Dev\+MuseScore+\ninja_build.bat" %*
)
set "RC=%errorlevel%"
echo.
echo === reprise-agent-build finished, exit %RC% ===
exit /b %RC%
