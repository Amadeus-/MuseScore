@echo off
REM ============================================================
REM  Launch the locally-built MuseScore 5 dev binary.
REM  Prompts for which already-built config to run (Release default;
REM  Debug and RelWithDebInfo also offered). Release and RelWithDebInfo
REM  both live in build.release; only Debug uses build.debug.
REM  The build does not windeployqt, so the exe needs Qt's bin and the
REM  bundled C-lib DLLs (<build dir>\_deps\*\bin) on PATH.
REM  Usage:
REM    "+Run (dev)+.bat"                     chooser, then launch
REM    "+Run (dev)+.bat" debug               run that config (no prompt)
REM    "+Run (dev)+.bat" release score.mscz  config token first, then any
REM                                          extra args pass through to MuseScore
REM    "+Run (dev)+.bat" score.mscz          no token -> chooser, arg passes through
REM ============================================================
setlocal
set "QT_KIT=C:\Dev\Qt\6.11.2\msvc2022_64"
set "MSROOT=C:\Dev\+MuseScore+"

REM --- Consume an optional leading config token (release|debug|relwithdebinfo);
REM     anything else is treated as a MuseScore arg. The build scripts pass the
REM     config they just built.
set "BTTOKEN="
if /i "%~1"=="release" set "BTTOKEN=release"
if /i "%~1"=="debug" set "BTTOKEN=debug"
if /i "%~1"=="relwithdebinfo" set "BTTOKEN=relwithdebinfo"
if defined BTTOKEN shift

REM Collect remaining args to forward to MuseScore (%* ignores shift, so rebuild).
set "RUNARGS="
:collectargs
if "%~1"=="" goto :argsdone
set "RUNARGS=%RUNARGS% %1"
shift
goto :collectargs
:argsdone

if defined BTTOKEN goto :resolve
echo Which build config to run?
echo   1. Release        [default - just press ENTER]
echo   2. Debug
echo   3. RelWithDebInfo
set /p "BT=Choose [1/2/3]: "
if "%BT%"=="" set "BT=1"
if "%BT%"=="1" set "BTTOKEN=release"
if "%BT%"=="2" set "BTTOKEN=debug"
if "%BT%"=="3" set "BTTOKEN=relwithdebinfo"

:resolve
if /i "%BTTOKEN%"=="release" (
  set "BTDIR=build.release"
  set "BTNAME=Release"
)
if /i "%BTTOKEN%"=="debug" (
  set "BTDIR=build.debug"
  set "BTNAME=Debug"
)
if /i "%BTTOKEN%"=="relwithdebinfo" (
  set "BTDIR=build.release"
  set "BTNAME=RelWithDebInfo"
)
if not defined BTDIR (
  echo Invalid choice "%BT%%BTTOKEN%" - defaulting to Release.
  set "BTDIR=build.release"
  set "BTNAME=Release"
)

set "EXE=%MSROOT%\%BTDIR%\Reprise.exe"
set "D=%MSROOT%\%BTDIR%\_deps"

if not exist "%EXE%" (
  echo *** Not built yet: %EXE%
  echo *** Build the %BTNAME% config first with:  "+Build (release)+.bat"
  echo.
  pause
  exit /b 1
)

set "PATH=%QT_KIT%\bin;%D%\ogg\bin;%D%\opus\bin;%D%\vorbis\bin;%D%\zlib\bin;%D%\flac\bin;%D%\libpng\bin;%D%\libsndfile\bin;%D%\freetype\bin;%D%\harfbuzz\bin;%D%\fdk-aac\bin;%PATH%"

echo === Launching %BTNAME% ===
start "" "%EXE%" %RUNARGS%

echo.
echo Launched Reprise (it runs in its own window). You can close this console.
echo.
pause
