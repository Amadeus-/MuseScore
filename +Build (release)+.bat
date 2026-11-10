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

cd /d "C:\Dev\+MuseScore+"

if "%~1"=="" (
  call "C:\Dev\+MuseScore+\ninja_build.bat" -t release
) else (
  call "C:\Dev\+MuseScore+\ninja_build.bat" %*
)

echo.
pause
