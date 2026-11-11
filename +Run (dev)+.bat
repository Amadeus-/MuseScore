@echo off
REM ============================================================
REM  Launch the locally-built MuseScore 5 dev binary.
REM  The build does not windeployqt, so the exe needs Qt's bin
REM  and the bundled C-lib DLLs (build.release\_deps\*\bin) on PATH.
REM  Any extra args are passed through to MuseScore.
REM ============================================================
setlocal
set "QT_KIT=C:\Dev\Qt\6.11.2\msvc2022_64"
set "EXE=C:\Dev\+MuseScore+\build.release\Reprise.exe"
set "D=C:\Dev\+MuseScore+\build.release\_deps"

if not exist "%EXE%" (
  echo *** Not built yet: %EXE%
  echo *** Build it first with:  "+Build (release)+.bat"
  echo.
  pause
  exit /b 1
)

set "PATH=%QT_KIT%\bin;%D%\ogg\bin;%D%\opus\bin;%D%\vorbis\bin;%D%\zlib\bin;%D%\flac\bin;%D%\libpng\bin;%D%\libsndfile\bin;%D%\freetype\bin;%D%\harfbuzz\bin;%D%\fdk-aac\bin;%PATH%"

start "" "%EXE%" %*

echo.
echo Launched Reprise (it runs in its own window). You can close this console.
echo.
pause
