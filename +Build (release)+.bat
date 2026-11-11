@echo off
REM ============================================================
REM  MuseScore 5 - Release build wrapper (fork-local dev helper)
REM  Points CMake at the aqt-installed Qt 6.11.2 kit, then runs
REM  the stock ninja_build.bat (which sets up MSVC via vcvars64).
REM
REM  Usage:
REM    "+Build (release)+.bat"            Release build (default)
REM    "+Build (release)+.bat" -t debug   pass any target through
REM    "+Build (release)+.bat" -t relwithdebinfo
REM ============================================================
setlocal
set "QT_KIT=C:\Dev\Qt\6.11.2\msvc2022_64"
if not exist "%QT_KIT%\lib\cmake\Qt6\qt.toolchain.cmake" (
  echo *** Qt 6.11.2 kit not found at %QT_KIT%
  echo *** Install it with:
  echo ***   aqt install-qt windows desktop 6.11.2 win64_msvc2022_64 -m qt5compat qtnetworkauth qtshadertools qtwebsockets -O C:\Dev\Qt
  echo.
  pause
  exit /b 1
)
set "QT_DIR=%QT_KIT%"
set "CMAKE_PREFIX_PATH=%QT_KIT%"
REM Prefer the MSVC-compatible cmake/ninja bundled with Visual Studio over any
REM MSYS2 clang64 cmake/ninja on PATH (those mis-invoke rc.exe -> fatal RC1107).
set "VS_CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "VS_NINJA=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
REM Include the VS Installer dir so the vcvars setup can find vswhere.exe
REM (otherwise it prints a harmless "'vswhere.exe' is not recognized" line).
set "VS_INSTALLER=C:\Program Files (x86)\Microsoft Visual Studio\Installer"
REM ninja_build.bat runs "bash ./ninja_build.sh"; Git for Windows only puts git
REM (not bash) on the system PATH, so a cmd.exe double-click can't find bash.
REM Add Git\bin (which has bash.exe). NOT Git\usr\bin -- its link.exe would
REM shadow the MSVC linker.
set "GIT_BIN=C:\Program Files\Git\bin"
set "PATH=%VS_CMAKE%;%VS_NINJA%;%VS_INSTALLER%;%QT_KIT%\bin;%GIT_BIN%;%PATH%"

REM Strip MSYS2 (clang64/mingw/usr) from PATH -- a clean CMake reconfigure
REM otherwise lets find_package grab msys64's Vulkan/fmt and bakes
REM -IC:\msys64\clang64\include into the MSVC compile, poisoning vcruntime.h
REM (see Guide ci-and-release.md 4.1). PowerShell by full path (no PATH
REM dependency); for /f avoids set /p's ~1KB length cap.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -Command "($env:PATH -split ';' | Where-Object { $_ -and ($_ -notmatch 'msys64') }) -join ';'" > "%TEMP%\reprise_clean_path.txt"
for /f "usebackq delims=" %%p in ("%TEMP%\reprise_clean_path.txt") do set "PATH=%%p"

cd /d "C:\Dev\+MuseScore+"

if "%~1"=="" (
  call "C:\Dev\+MuseScore+\ninja_build.bat" -t release
) else (
  call "C:\Dev\+MuseScore+\ninja_build.bat" %*
)

if errorlevel 1 (
  echo.
  echo *** Build FAILED - see the errors above. ***
  echo.
  pause
  goto :eof
)

echo.
echo === Build OK. ===
echo.
set /p "DODEPLOY=Deploy now (make the exe double-clickable)? [Y/n]: "
if "%DODEPLOY%"=="" set "DODEPLOY=Y"
if /i not "%DODEPLOY%"=="Y" goto :nodeploy
echo.
call "C:\Dev\+MuseScore+\+Deploy (standalone)+.bat"
:nodeploy

echo.
set /p "DORUN=Launch Reprise now? [y/N]: "
if /i not "%DORUN%"=="Y" goto :norun
echo.
call "C:\Dev\+MuseScore+\+Run (dev)+.bat"
goto :eof
:norun

echo.
pause
goto :eof
